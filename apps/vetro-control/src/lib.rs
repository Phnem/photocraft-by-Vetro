//! L7 transport for the native shell. No document/session is owned here: every request is
//! handed back to Vetro's UI thread, which serializes work on its existing engine worker.
#![deny(unsafe_code)]
#![deny(clippy::unwrap_used, clippy::expect_used, clippy::panic, clippy::unimplemented, clippy::todo, clippy::unreachable)]
mod ffi;
use photocraft_automation::{
    budgets::write_reply,
    security::{self, ConnectionLimiter, LineRead},
};
use serde_json::{Value, json};
use std::{
    collections::BTreeMap,
    io::{BufReader, Write},
    net::{Shutdown, TcpListener, TcpStream},
    sync::{
        Arc, Mutex, PoisonError,
        atomic::{AtomicBool, AtomicU8, AtomicU64, Ordering},
        mpsc::{self, Receiver, SyncSender},
    },
    thread::JoinHandle,
    time::{Duration, Instant},
};
const DEADLINE: Duration = Duration::from_secs(60);
fn lock<T>(m: &Mutex<T>) -> std::sync::MutexGuard<'_, T> {
    m.lock().unwrap_or_else(PoisonError::into_inner)
}
struct Pending {
    number: u64,
    request: Value,
    reply: SyncSender<Value>,
    phase: AtomicU8,
    created: Instant,
}
struct Core {
    stopped: AtomicBool,
    queue: Mutex<Receiver<Arc<Pending>>>,
    active: Mutex<BTreeMap<u64, Arc<Pending>>>,
    streams: Mutex<BTreeMap<u64, TcpStream>>,
    serial: AtomicU64,
}
pub struct Control {
    core: Arc<Core>,
    listener: Option<JoinHandle<()>>,
    pub port: u16,
    files: photocraft_automation::AuthorizedWorkspace,
    read_root: Option<std::path::PathBuf>,
    write_root: Option<std::path::PathBuf>,
}
impl Control {
    pub fn start(config: &Value) -> Result<Self, String> {
        let root = |name: &str| -> Result<Option<std::path::PathBuf>, String> {
            config
                .get(name)
                .and_then(Value::as_str)
                .filter(|s| !s.is_empty())
                .map(|s| std::path::Path::new(s).canonicalize().map_err(|e| e.to_string()))
                .transpose()
        };
        let read_root = root("readRoot")?;
        let write_root = root("writeRoot")?;
        let files = photocraft_automation::AuthorizedWorkspace::new(read_root.as_deref(), write_root.as_deref()).map_err(|e| e.to_string())?;
        let port = config.get("port").and_then(Value::as_u64).filter(|v| *v <= 65535).ok_or("port must be 0–65535")? as u16;
        let file = config.get("tokenFile").and_then(Value::as_str).filter(|s| !s.is_empty()).ok_or("tokenFile is required")?;
        let token = security::server_token(None, Some(std::path::Path::new(file))).map_err(|e| e.to_string())?;
        let listener = TcpListener::bind(("127.0.0.1", port)).map_err(|e| e.to_string())?;
        let port = listener.local_addr().map_err(|e| e.to_string())?.port();
        listener.set_nonblocking(true).map_err(|e| e.to_string())?;
        let (tx, rx) = mpsc::sync_channel(32);
        let core = Arc::new(Core {
            stopped: AtomicBool::new(false),
            queue: Mutex::new(rx),
            active: Mutex::new(BTreeMap::new()),
            streams: Mutex::new(BTreeMap::new()),
            serial: AtomicU64::new(1),
        });
        let shared = core.clone();
        let listener = std::thread::Builder::new()
            .name("vetro-control".into())
            .spawn(move || {
                let limiter = ConnectionLimiter::new(security::MAX_CONNECTIONS);
                let token = Arc::new(token);
                while !shared.stopped.load(Ordering::Acquire) {
                    match listener.accept() {
                        Ok((stream, _)) => {
                            let Some(permit) = limiter.try_acquire() else {
                                continue;
                            };
                            let tx = tx.clone();
                            let token = token.clone();
                            let core = shared.clone();
                            let number = core.serial.fetch_add(1, Ordering::Relaxed);
                            if let Ok(copy) = stream.try_clone() {
                                lock(&core.streams).insert(number, copy);
                            } else {
                                continue;
                            }
                            let cleanup = core.clone();
                            if std::thread::Builder::new()
                                .name("vetro-client".into())
                                .spawn(move || {
                                    let _permit = permit;
                                    serve(stream, &token, &tx, &core);
                                    lock(&core.streams).remove(&number);
                                })
                                .is_err()
                            {
                                lock(&cleanup.streams).remove(&number);
                            }
                        }
                        Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => std::thread::sleep(Duration::from_millis(10)),
                        Err(_) => break,
                    }
                }
            })
            .map_err(|e| e.to_string())?;
        Ok(Self { core, listener: Some(listener), port, files, read_root, write_root })
    }
    pub fn poll(&self) -> Option<Value> {
        if self.core.stopped.load(Ordering::Acquire) {
            return None;
        }
        let mut active = lock(&self.core.active);
        if active.len() >= security::MAX_CONNECTIONS {
            return None;
        }
        while let Ok(p) = lock(&self.core.queue).try_recv() {
            if p.created.elapsed() >= DEADLINE {
                p.phase.store(2, Ordering::Release);
                continue;
            }
            if p.phase.compare_exchange(0, 1, Ordering::AcqRel, Ordering::Acquire).is_err() {
                continue;
            }
            let mut request = p.request.clone();
            request["_ticket"] = json!(p.number);
            active.insert(p.number, p);
            return Some(request);
        }
        None
    }
    pub fn reply(&self, ticket: u64, value: Value) {
        if let Some(p) = lock(&self.core.active).remove(&ticket) {
            let _ = p.reply.try_send(value);
        }
    }
    pub fn stop(&mut self) {
        self.core.stopped.store(true, Ordering::Release);
        for stream in lock(&self.core.streams).values() {
            let _ = stream.shutdown(Shutdown::Both);
        }
        if let Some(t) = self.listener.take() {
            let _ = t.join();
        }
    }
    pub fn read_file(&self, path: &str) -> Result<Vec<u8>, String> {
        if std::path::Path::new(path).extension().is_some_and(|s| s.eq_ignore_ascii_case("pcraft"))
            && let Some(doc) = self.files.open_pcraft(path).map_err(|e| e.to_string())?
        {
            return photocraft_format::save_to_bytes(&doc, &Default::default()).map_err(|e| e.to_string());
        }
        self.files.read(path).map_err(|e| e.to_string())
    }
    pub fn write_file(&self, path: &str, bytes: &[u8]) -> Result<(), String> {
        self.files.write(path, bytes).map_err(|e| e.to_string())
    }
    /// Metadata only; all actual IO goes through the directory capabilities above.
    pub fn file_path(&self, path: &str, write: bool) -> Result<String, String> {
        let root = if write { self.write_root.as_ref() } else { self.read_root.as_ref() }.ok_or("filesystem authority is absent")?;
        let p = std::path::Path::new(path);
        Ok(if p.is_absolute() { p.to_path_buf() } else { root.join(p) }.to_string_lossy().into_owned())
    }
}
impl Drop for Control {
    fn drop(&mut self) {
        self.stop();
    }
}
fn serve(mut stream: TcpStream, token: &str, tx: &SyncSender<Arc<Pending>>, core: &Core) {
    // Winsock accept inherits the listener's nonblocking mode. Framed client IO is blocking
    // with the shared read/write timeout; otherwise an idle first read drops the connection.
    if stream.set_nonblocking(false).is_err() {
        return;
    }
    if security::configure_stream(&stream).is_err() {
        return;
    }
    let Ok(read) = stream.try_clone() else {
        return;
    };
    let mut reader = BufReader::new(read);
    let mut line = String::new();
    let mut authenticated = false;
    while !core.stopped.load(Ordering::Acquire) {
        if !matches!(security::read_bounded_line(&mut reader, &mut line), Ok(LineRead::Line)) {
            break;
        }
        if line.trim().is_empty() {
            continue;
        }
        if !authenticated {
            let (r, ok) = security::authentication_reply(&line, token);
            if write_reply(&mut stream, &r).is_err() || stream.flush().is_err() || !ok {
                break;
            }
            authenticated = true;
            continue;
        }
        if let Some(r) = security::repeat_auth_reply(&line) {
            if write_reply(&mut stream, &r).is_err() {
                break;
            }
            continue;
        }
        let request: Value = match serde_json::from_str::<Value>(&line) {
            Ok(v) if v.is_object() => v,
            Ok(_) => {
                if write_reply(&mut stream, &json!({"id":null,"ok":false,"error":"request must be an object"})).is_err() {
                    break;
                }
                continue;
            }
            Err(_) => {
                if write_reply(&mut stream, &json!({"id":null,"ok":false,"error":"bad JSON"})).is_err() {
                    break;
                }
                continue;
            }
        };
        let id = request.get("id").cloned().unwrap_or(Value::Null);
        let (rtx, rx) = mpsc::sync_channel(1);
        let p =
            Arc::new(Pending { number: core.serial.fetch_add(1, Ordering::Relaxed), request, reply: rtx, phase: AtomicU8::new(0), created: Instant::now() });
        let mut result = if tx.try_send(p.clone()).is_err() {
            json!({"ok":false,"error":"control queue is full; request was not run"})
        } else {
            rx.recv_timeout(DEADLINE).unwrap_or_else(|_|{
            let cancelled=matches!(p.phase.compare_exchange(0,2,Ordering::AcqRel,Ordering::Acquire),Ok(_)|Err(2));json!({"ok":false,"error":if cancelled{"reply deadline exceeded; request was not run"}else{"reply deadline exceeded; operation may have completed"}})
        })
        };
        result["id"] = id;
        if write_reply(&mut stream, &result).is_err() || stream.flush().is_err() {
            break;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::BufRead;
    fn read(r: &mut BufReader<TcpStream>) -> Value {
        let mut s = String::new();
        r.read_line(&mut s).unwrap();
        serde_json::from_str(&s).unwrap()
    }
    #[test]
    fn authenticated_request_runs_on_the_host_and_stop_closes_connections() {
        let file = std::env::temp_dir().join(format!("vetro-token-{}", std::process::id()));
        let mut server = Control::start(&json!({"port":0,"tokenFile":file})).unwrap();
        let token = std::fs::read_to_string(&file).unwrap();
        let mut stream = TcpStream::connect(("127.0.0.1", server.port)).unwrap();
        stream.set_read_timeout(Some(Duration::from_secs(5))).unwrap();
        let mut r = BufReader::new(stream.try_clone().unwrap());
        writeln!(stream, "{}", json!({"id":1,"method":"auth","params":{"token":token.trim()}})).unwrap();
        assert_eq!(read(&mut r)["ok"], true);
        writeln!(stream, "[]").unwrap();
        assert_eq!(read(&mut r)["ok"], false);
        writeln!(stream, "{}", json!({"id":2,"method":"ui.inspect"})).unwrap();
        let request = loop {
            if let Some(p) = server.poll() {
                break p;
            }
            std::thread::sleep(Duration::from_millis(2));
        };
        assert_eq!(request["method"], "ui.inspect");
        server.reply(request["_ticket"].as_u64().unwrap(), json!({"ok":true,"result":{"session":"same"}}));
        let result = read(&mut r);
        assert_eq!(result["id"], 2);
        assert_eq!(result["result"]["session"], "same");
        server.stop();
        let mut line = String::new();
        assert!(matches!(r.read_line(&mut line), Ok(0) | Err(_)));
        let _ = std::fs::remove_file(file);
    }
    #[test]
    fn wrong_auth_never_reaches_the_ui() {
        let file = std::env::temp_dir().join(format!("vetro-bad-token-{}", std::process::id()));
        let mut server = Control::start(&json!({"port":0,"tokenFile":file})).unwrap();
        let mut stream = TcpStream::connect(("127.0.0.1", server.port)).unwrap();
        let mut reader = BufReader::new(stream.try_clone().unwrap());
        writeln!(stream, "{}", json!({"id":1,"method":"engine.execute","params":{}})).unwrap();
        assert_eq!(read(&mut reader)["ok"], false);
        assert!(server.poll().is_none());
        server.stop();
        let _ = std::fs::remove_file(file);
    }
    #[test]
    fn expired_or_cancelled_requests_never_dispatch() {
        let (tx, rx) = mpsc::sync_channel(2);
        let (reply, _) = mpsc::sync_channel(1);
        let expired = Arc::new(Pending {
            number: 1,
            request: json!({"method":"engine.execute"}),
            reply: reply.clone(),
            phase: AtomicU8::new(0),
            created: Instant::now() - DEADLINE,
        });
        let cancelled = Arc::new(Pending { number: 2, request: json!({"method":"engine.execute"}), reply, phase: AtomicU8::new(2), created: Instant::now() });
        tx.send(expired.clone()).unwrap();
        tx.send(cancelled).unwrap();
        let server = Control {
            core: Arc::new(Core {
                stopped: AtomicBool::new(false),
                queue: Mutex::new(rx),
                active: Mutex::new(BTreeMap::new()),
                streams: Mutex::new(BTreeMap::new()),
                serial: AtomicU64::new(3),
            }),
            listener: None,
            port: 0,
            files: photocraft_automation::AuthorizedWorkspace::new(None, None).unwrap(),
            read_root: None,
            write_root: None,
        };
        assert!(server.poll().is_none());
        assert_eq!(expired.phase.load(Ordering::Acquire), 2);
        assert!(lock(&server.core.active).is_empty());
    }
}
