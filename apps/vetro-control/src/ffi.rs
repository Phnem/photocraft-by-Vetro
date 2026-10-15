//! The only raw-pointer boundary. Native callers own handles/strings and stop before freeing.
#![allow(unsafe_code)]
use crate::Control;
use serde_json::Value;
use std::{
    ffi::{CStr, CString, c_char},
    panic::{AssertUnwindSafe, catch_unwind},
};
thread_local! {static ERROR:std::cell::RefCell<String>=const{std::cell::RefCell::new(String::new())};}
fn error(e: impl ToString) {
    ERROR.with(|v| *v.borrow_mut() = e.to_string());
}
fn string(s: String) -> *mut c_char {
    CString::new(s.replace('\0', " ")).map_or(std::ptr::null_mut(), CString::into_raw)
}
fn guard<T>(fallback: T, f: impl FnOnce() -> T) -> T {
    catch_unwind(AssertUnwindSafe(f)).unwrap_or_else(|_| {
        error("internal control error");
        fallback
    })
}
// SAFETY: callers pass null or a live NUL-terminated UTF-8 string.
unsafe fn text(p: *const c_char) -> String {
    if p.is_null() { String::new() } else { unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned() }
}
/// # Safety
/// `config` is a live NUL-terminated UTF-8 JSON object.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vc_start(config: *const c_char) -> *mut Control {
    guard(std::ptr::null_mut(), || {
        // SAFETY: caller string conventions.
        let parsed = serde_json::from_str(&unsafe { text(config) }).map_err(|e| e.to_string()).and_then(|v| Control::start(&v));
        match parsed {
            Ok(v) => Box::into_raw(Box::new(v)),
            Err(e) => {
                error(e);
                std::ptr::null_mut()
            }
        }
    })
}
/// # Safety
/// `s` is null or a live handle from vc_start.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vc_poll(s: *mut Control) -> *mut c_char {
    guard(std::ptr::null_mut(), || {
        // SAFETY: caller handle conventions, handle remains live for this call.
        unsafe { s.as_ref() }.and_then(Control::poll).map_or(std::ptr::null_mut(), |v| string(v.to_string()))
    })
}
/// # Safety
/// `s` is live, `reply` follows string conventions.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vc_reply(s: *mut Control, ticket: u64, reply: *const c_char) {
    guard((), || {
        // SAFETY: caller handle/string conventions.
        if let Some(s) = unsafe { s.as_ref() } {
            match serde_json::from_str(&unsafe { text(reply) }) {
                Ok(v) => s.reply(ticket, v),
                Err(e) => error(e),
            }
        }
    })
}
/// # Safety
/// `s` is null or live, stop is called on the owning UI thread.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vc_stop(s: *mut Control) {
    guard((), || {
        // SAFETY: exclusive UI-thread call, no other mutable handle access.
        if let Some(s) = unsafe { s.as_mut() } {
            s.stop();
        }
    })
}
/// # Safety
/// `s` comes from vc_start, freed once after vc_stop and all in-flight native callbacks.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vc_free(s: *mut Control) {
    guard((), || {
        if !s.is_null() {
            // SAFETY: allocation originated in vc_start and is freed once.
            drop(unsafe { Box::from_raw(s) });
        }
    })
}
/// # Safety
/// `s` is null or live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vc_port(s: *mut Control) -> u16 {
    guard(0, || {
        // SAFETY: caller handle conventions.
        unsafe { s.as_ref() }.map_or(0, |s| s.port)
    })
}
#[unsafe(no_mangle)]
pub extern "C" fn vc_error() -> *mut c_char {
    guard(std::ptr::null_mut(), || ERROR.with(|s| string(s.borrow().clone())))
}
/// # Safety
/// `id`/`params` follow string conventions. Returns an empty string on permission success.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vc_authorize(id: *const c_char, params: *const c_char) -> *mut c_char {
    guard(std::ptr::null_mut(), || {
        // SAFETY: caller string conventions.
        let (id, params) = unsafe { (text(id), text(params)) };
        let parsed: Result<Value, _> = serde_json::from_str(&params);
        let result = parsed
            .map_err(|e| e.to_string())
            .and_then(|p| photocraft_automation::workspace::authorize_desktop_engine_command(&id, &p).map_err(|e| e.to_string()));
        string(result.err().unwrap_or_default())
    })
}
/// # Safety
/// `bytes` points to `len` readable PNG bytes. Returns bounded base64 PNG JSON.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vc_png(bytes: *const u8, len: usize) -> *mut c_char {
    guard(std::ptr::null_mut(), || {
        use base64::Engine;
        if bytes.is_null() || photocraft_automation::budgets::check_png(len).is_err() {
            error("screenshot exceeds PNG budget");
            return std::ptr::null_mut();
        }
        // SAFETY: caller provides len readable bytes, length bounded above.
        let png = unsafe { std::slice::from_raw_parts(bytes, len) };
        string(serde_json::json!({"png":base64::engine::general_purpose::STANDARD.encode(png)}).to_string())
    })
}
/// # Safety
/// `p` is null or a string allocated by this DLL, freed once.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vc_string_free(p: *mut c_char) {
    if !p.is_null() {
        // SAFETY: all returned strings are CString::into_raw, caller frees once.
        drop(unsafe { CString::from_raw(p) });
    }
}

/// Owned bytes read through the automation directory capability.
pub struct VcData(Vec<u8>);
/// # Safety
/// `s` is live, `path` follows string conventions.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vc_read_file(s: *mut Control, path: *const c_char) -> *mut VcData {
    guard(std::ptr::null_mut(), || {
        // SAFETY: caller handle and string conventions.
        let (s, path) = unsafe { (s.as_ref(), text(path)) };
        let result = s.ok_or("control unavailable".to_string()).and_then(|s| s.read_file(&path));
        match result {
            Ok(v) => Box::into_raw(Box::new(VcData(v))),
            Err(e) => {
                error(e);
                std::ptr::null_mut()
            }
        }
    })
}
/// # Safety
/// `s` is live, `path` follows string conventions, `bytes` holds `len` readable bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vc_write_file(s: *mut Control, path: *const c_char, bytes: *const u8, len: usize) -> *mut c_char {
    guard(std::ptr::null_mut(), || {
        if bytes.is_null() || len > 512 << 20 {
            return string("invalid file buffer".into());
        }
        // SAFETY: caller provides the bounded live byte slice and handle/string.
        let (s, path, bytes) = unsafe { (s.as_ref(), text(path), std::slice::from_raw_parts(bytes, len)) };
        let result = s.ok_or("control unavailable".to_string()).and_then(|s| s.write_file(&path, bytes));
        string(result.err().unwrap_or_default())
    })
}
/// Metadata only, called after a successful capability read/write.
/// # Safety
/// `s`/`path` follow handle/string conventions.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vc_file_path(s: *mut Control, path: *const c_char, write: i32) -> *mut c_char {
    guard(std::ptr::null_mut(), || {
        // SAFETY: caller conventions.
        let (s, path) = unsafe { (s.as_ref(), text(path)) };
        match s.ok_or("control unavailable".to_string()).and_then(|s| s.file_path(&path, write != 0)) {
            Ok(p) => string(p),
            Err(e) => {
                error(e);
                std::ptr::null_mut()
            }
        }
    })
}
/// # Safety
/// `d` is null or live. Returned bytes stay live until vc_data_free.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vc_data_bytes(d: *const VcData) -> *const u8 {
    // SAFETY: caller handle conventions.
    unsafe { d.as_ref() }.map_or(std::ptr::null(), |d| d.0.as_ptr())
}
/// # Safety
/// `d` is null or live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vc_data_size(d: *const VcData) -> usize {
    // SAFETY: caller handle conventions.
    unsafe { d.as_ref() }.map_or(0, |d| d.0.len())
}
/// # Safety
/// `d` was returned by vc_read_file and is freed once after its bytes are no longer used.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vc_data_free(d: *mut VcData) {
    if !d.is_null() {
        // SAFETY: caller owns the allocation from Box::into_raw above.
        drop(unsafe { Box::from_raw(d) });
    }
}
