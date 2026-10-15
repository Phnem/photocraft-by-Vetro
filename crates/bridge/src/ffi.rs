//! `extern "C"` surface of [`crate::Bridge`], mirrored by `vetro/shell/src/bridge.h`.
//!
//! Conventions, so the C++ side never has to guess:
//!
//! * Every function catches panics: nothing unwinds into C++. A caught panic is an error.
//! * Strings in are NUL-terminated UTF-8; a null pointer reads as an empty string.
//! * Strings out are allocated here and freed with `pc_string_free`, never with `free`.
//! * Functions returning `int` return 1 on success and 0 on failure; the reason is then in
//!   `pc_last_error` (per thread).
//! * Pixel buffers are premultiplied BGRA8 with an explicit length and row stride; a write that
//!   would not fit is refused, never truncated past the end.
#![allow(unsafe_code)]

use std::cell::RefCell;
use std::ffi::{CStr, CString, c_char, c_int};
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::sync::{Arc, Mutex, MutexGuard, PoisonError};

use photocraft_doc::Document;
use photocraft_geom::Rect;

use crate::Bridge;

/// The session behind a lock: the shell runs engine work on a worker thread while its UI thread
/// keeps asking for views, and the lock makes that safe whatever the shell gets wrong (at worst
/// the UI waits).
pub struct PcBridge(Mutex<Bridge>);

/// A document as it was when taken: renders from it don't hold the session. `encode`: the canvas
/// stores it sRGB-encoded.
pub struct PcSnapshot {
    doc: Arc<Document>,
    source: Arc<Document>,
    encode: bool,
    source_encode: bool,
    channels: Option<crate::channels::Display>,
}

thread_local! {
    static LAST_ERROR: RefCell<String> = const { RefCell::new(String::new()) };
}

fn set_error(message: impl Into<String>) {
    LAST_ERROR.with(|e| *e.borrow_mut() = message.into());
}

/// Run `f`, turning a panic into an error result.
fn guard<T>(fallback: T, f: impl FnOnce() -> T) -> T {
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(v) => v,
        Err(payload) => {
            let what = payload
                .downcast_ref::<&str>()
                .map(|s| (*s).to_string())
                .or_else(|| payload.downcast_ref::<String>().cloned())
                .unwrap_or_else(|| "unknown panic".into());
            set_error(format!("internal error: {what}"));
            fallback
        }
    }
}

fn status(r: Result<(), String>) -> c_int {
    match r {
        Ok(()) => 1,
        Err(e) => {
            set_error(e);
            0
        }
    }
}

fn out_string(s: String) -> *mut c_char {
    let clean = if s.contains('\0') { s.replace('\0', " ") } else { s };
    CString::new(clean).map_or(std::ptr::null_mut(), CString::into_raw)
}

/// # Safety
/// `p` is null or a NUL-terminated string valid for the call.
unsafe fn in_str<'a>(p: *const c_char) -> std::borrow::Cow<'a, str> {
    if p.is_null() {
        return std::borrow::Cow::Borrowed("");
    }
    // SAFETY: the caller promises `p` is a live NUL-terminated string.
    unsafe { CStr::from_ptr(p) }.to_string_lossy()
}

/// # Safety
/// `b` is null or a pointer from `pc_new` not yet freed (any thread: the session is locked).
unsafe fn bridge<'a>(b: *mut PcBridge) -> Option<MutexGuard<'a, Bridge>> {
    // SAFETY: as promised by the caller.
    let handle = unsafe { b.as_ref() }?;
    Some(handle.0.lock().unwrap_or_else(PoisonError::into_inner))
}

/// # Safety
/// `s` is null or a pointer from `pc_snapshot` not yet freed.
unsafe fn snap<'a>(s: *const PcSnapshot) -> Option<&'a PcSnapshot> {
    // SAFETY: as promised by the caller.
    unsafe { s.as_ref() }
}

/// # Safety
/// `p` is null or points at `len` writable bytes.
unsafe fn out_bytes<'a>(p: *mut u8, len: usize) -> Option<&'a mut [u8]> {
    if p.is_null() || len == 0 {
        return None;
    }
    // SAFETY: the caller promises `len` writable bytes at `p`.
    Some(unsafe { std::slice::from_raw_parts_mut(p, len) })
}

/// # Safety
/// `p` is null or points at four writable `i32`.
unsafe fn write_rect(p: *mut i32, r: Rect) {
    if p.is_null() {
        return;
    }
    // SAFETY: the caller promises four writable values.
    let out = unsafe { std::slice::from_raw_parts_mut(p, 4) };
    out.copy_from_slice(&[r.x0, r.y0, r.x1, r.y1]);
}

// ------------------------------------------------------------------------- lifetime --

#[unsafe(no_mangle)]
pub extern "C" fn pc_new() -> *mut PcBridge {
    guard(std::ptr::null_mut(), || Box::into_raw(Box::new(PcBridge(Mutex::new(Bridge::new())))))
}

/// # Safety
/// `b` is null or from `pc_new`, freed once.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_free(b: *mut PcBridge) {
    if !b.is_null() {
        // SAFETY: from `Box::into_raw` in `pc_new`, freed once.
        guard((), || drop(unsafe { Box::from_raw(b) }));
    }
}

/// # Safety
/// `s` is null or a string returned by this library, freed once.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_string_free(s: *mut c_char) {
    if !s.is_null() {
        // SAFETY: from `CString::into_raw` in `out_string`, freed once.
        drop(unsafe { CString::from_raw(s) });
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn pc_last_error() -> *mut c_char {
    out_string(LAST_ERROR.with(|e| e.borrow().clone()))
}

// ------------------------------------------------------------------------- commands --

/// The clipboard boundary uses straight (unpremultiplied) RGBA8, unlike rendered BGRA8.
/// # Safety
/// Module conventions; width and height are null or writable u32 pointers.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_clipboard_size(b: *mut PcBridge, width: *mut u32, height: *mut u32) -> c_int {
    guard(0, || {
        // SAFETY: module conventions.
        let Some(b) = (unsafe { bridge(b) }) else { return 0 };
        let Some((w, h)) = b.clipboard_size() else { return 0 };
        // SAFETY: the caller promises writable output pointers when non-null.
        unsafe {
            if !width.is_null() {
                *width = w;
            }
            if !height.is_null() {
                *height = h;
            }
        }
        1
    })
}

/// # Safety
/// Module conventions; pixels points to exactly len writable bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_clipboard_read(b: *mut PcBridge, pixels: *mut u8, len: usize) -> c_int {
    guard(0, || {
        if len > crate::clipboard::MAX_CLIPBOARD_BYTES {
            set_error("clipboard buffer is too large");
            return 0;
        }
        // SAFETY: module conventions, length bounded above.
        let (Some(b), Some(out)) = (unsafe { bridge(b) }, unsafe { out_bytes(pixels, len) }) else {
            set_error("no clipboard buffer");
            return 0;
        };
        status(b.clipboard_read(out))
    })
}

/// # Safety
/// Module conventions; pixels points to len readable bytes of straight RGBA8.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_clipboard_set(b: *mut PcBridge, width: u32, height: u32, pixels: *const u8, len: usize) -> c_int {
    guard(0, || {
        if crate::clipboard::image_len(width, height).ok() != Some(len) || pixels.is_null() {
            set_error("invalid clipboard image buffer");
            return 0;
        }
        // SAFETY: module conventions; dimensions and length validated above.
        let Some(mut b) = (unsafe { bridge(b) }) else {
            set_error("no bridge");
            return 0;
        };
        // SAFETY: caller promises len readable bytes, bounded above.
        let pixels = unsafe { std::slice::from_raw_parts(pixels, len) };
        status(b.clipboard_set(width, height, pixels))
    })
}

/// # Safety
/// Module conventions.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_clipboard_clear(b: *mut PcBridge) {
    guard((), || {
        // SAFETY: module conventions.
        if let Some(mut b) = unsafe { bridge(b) } {
            b.clipboard_clear();
        }
    });
}

/// Run a command. On success `*result` is its JSON result; on failure the error message.
///
/// # Safety
/// Pointers as described in the module docs; `result` is null or writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_execute(b: *mut PcBridge, id: *const c_char, params: *const c_char, result: *mut *mut c_char) -> c_int {
    guard(0, || {
        // SAFETY: module conventions.
        let (Some(mut b), id, params) = (unsafe { bridge(b) }, unsafe { in_str(id) }, unsafe { in_str(params) }) else {
            set_error("no bridge");
            return 0;
        };
        let (ok, text) = match b.execute(&id, &params) {
            Ok(v) => (1, v),
            Err(e) => {
                set_error(e.clone());
                (0, e)
            }
        };
        if !result.is_null() {
            // SAFETY: the caller promises `result` is writable.
            unsafe { *result = out_string(text) };
        }
        ok
    })
}

/// # Safety
/// Module conventions.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_enabled(b: *mut PcBridge, id: *const c_char) -> c_int {
    guard(0, || {
        // SAFETY: module conventions.
        let (Some(b), id) = (unsafe { bridge(b) }, unsafe { in_str(id) }) else { return 0 };
        c_int::from(b.is_enabled(&id))
    })
}

/// # Safety
/// Module conventions.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_enabled_many(b: *mut PcBridge, ids: *const c_char) -> *mut c_char {
    guard(std::ptr::null_mut(), || {
        // SAFETY: module conventions.
        let (Some(b), ids) = (unsafe { bridge(b) }, unsafe { in_str(ids) }) else { return std::ptr::null_mut() };
        out_string(b.enabled_many(&ids))
    })
}

/// Why `id` can't run now; null when it can.
///
/// # Safety
/// Module conventions.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_disabled_reason(b: *mut PcBridge, id: *const c_char) -> *mut c_char {
    guard(std::ptr::null_mut(), || {
        // SAFETY: module conventions.
        let (Some(b), id) = (unsafe { bridge(b) }, unsafe { in_str(id) }) else { return std::ptr::null_mut() };
        b.disabled_reason(&id).map_or(std::ptr::null_mut(), out_string)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn pc_catalog() -> *mut c_char {
    guard(std::ptr::null_mut(), || out_string(Bridge::catalog()))
}

#[unsafe(no_mangle)]
pub extern "C" fn pc_commands() -> *mut c_char {
    guard(std::ptr::null_mut(), || out_string(Bridge::commands()))
}

/// # Safety
/// Module conventions.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_session_info(b: *mut PcBridge) -> *mut c_char {
    guard(std::ptr::null_mut(), || {
        // SAFETY: module conventions.
        unsafe { bridge(b) }.map_or(std::ptr::null_mut(), |b| out_string(b.session_info()))
    })
}

/// # Safety
/// Module conventions.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_document_info(b: *mut PcBridge) -> *mut c_char {
    guard(std::ptr::null_mut(), || {
        // SAFETY: module conventions.
        unsafe { bridge(b) }.map_or(std::ptr::null_mut(), |b| out_string(b.document_info()))
    })
}

/// Preference values and the engine's field metadata for native dialogs.
/// # Safety
/// Module conventions.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_preferences_info(b: *mut PcBridge) -> *mut c_char {
    guard(std::ptr::null_mut(), || {
        // SAFETY: module conventions.
        let Some(b) = (unsafe { bridge(b) }) else { return std::ptr::null_mut() };
        out_string(b.preferences_info())
    })
}
/// Restore the frontend's saved preferences and presets.
/// # Safety
/// Module conventions.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_preferences_restore(b: *mut PcBridge, serialized: *const c_char) -> c_int {
    guard(0, || {
        // SAFETY: module conventions.
        let (Some(mut b), serialized) = (unsafe { bridge(b) }, unsafe { in_str(serialized) }) else { return 0 };
        status(b.restore_preferences(&serialized))
    })
}

/// The interface translation catalog for a language code ("ru"), as TSV text; null when there is
/// none (English is the source). Free with `pc_string_free`.
///
/// # Safety
/// `code` is null or a NUL-terminated string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_ui_strings(code: *const c_char) -> *mut c_char {
    guard(std::ptr::null_mut(), || {
        // SAFETY: as promised by the caller.
        let code = unsafe { in_str(code) };
        crate::ui_strings(&code).map_or(std::ptr::null_mut(), |s| out_string(s.to_string()))
    })
}

/// The active document's marks (Count markers, notes, the Ruler line) as JSON; free with
/// `pc_string_free`. "null" with no document.
///
/// # Safety
/// `b` is null or a live handle.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_marks(b: *mut PcBridge) -> *mut c_char {
    guard(std::ptr::null_mut(), || {
        // SAFETY: module conventions.
        unsafe { bridge(b) }.map_or(std::ptr::null_mut(), |b| out_string(b.marks()))
    })
}

/// The active document's revision. Returns 0 with no document, 1 when the whole document may have
/// changed, 2 when `damage` (x0, y0, x1, y1 in document pixels) holds the changed area.
///
/// # Safety
/// Module conventions; `revision` null or writable, `damage` null or four writable values.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_revision(b: *mut PcBridge, revision: *mut u64, damage: *mut i32) -> c_int {
    guard(0, || {
        // SAFETY: module conventions.
        let Some(b) = (unsafe { bridge(b) }) else { return 0 };
        let Some((rev, dmg)) = b.revision() else { return 0 };
        if !revision.is_null() {
            // SAFETY: the caller promises `revision` is writable.
            unsafe { *revision = rev };
        }
        match dmg {
            Some(r) => {
                // SAFETY: the caller promises four writable values.
                unsafe { write_rect(damage, r) };
                2
            }
            None => 1,
        }
    })
}

/// The pixels every step after revision `since` touched, together: 2 with `damage` (x0, y0, x1,
/// y1) written, 1 when the whole document must be assumed, 0 with no document.
///
/// # Safety
/// Module conventions; `damage` points at four writable values.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_damage_since(b: *mut PcBridge, since: u64, damage: *mut i32) -> c_int {
    guard(0, || {
        // SAFETY: module conventions.
        let Some(b) = (unsafe { bridge(b) }) else { return 0 };
        if b.revision().is_none() {
            return 0;
        }
        match b.damage_since(since) {
            Some(r) => {
                // SAFETY: the caller promises four writable values.
                unsafe { write_rect(damage, r) };
                2
            }
            None => 1,
        }
    })
}

/// # Safety
/// Module conventions.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_undo(b: *mut PcBridge) -> c_int {
    // SAFETY: module conventions.
    guard(0, || unsafe { bridge(b) }.map_or(0, |mut b| c_int::from(b.undo())))
}

/// # Safety
/// Module conventions.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_redo(b: *mut PcBridge) -> c_int {
    // SAFETY: module conventions.
    guard(0, || unsafe { bridge(b) }.map_or(0, |mut b| c_int::from(b.redo())))
}

/// # Safety
/// Module conventions.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_set_active(b: *mut PcBridge, index: usize) -> c_int {
    // SAFETY: module conventions.
    guard(0, || unsafe { bridge(b) }.map_or(0, |mut b| c_int::from(b.set_active(index))))
}

/// # Safety
/// Module conventions.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_select_layer(b: *mut PcBridge, layer: u64) -> c_int {
    // SAFETY: module conventions.
    guard(0, || status(unsafe { bridge(b) }.ok_or_else(|| "no bridge".to_string()).and_then(|mut b| b.select_layer(layer))))
}

// ---------------------------------------------------------------------------- files --

/// Open a file (UTF-8 path). `*result` as in `pc_execute`: import warnings, or the error.
///
/// # Safety
/// Module conventions; `result` null or writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_open(b: *mut PcBridge, path: *const c_char, result: *mut *mut c_char) -> c_int {
    guard(0, || {
        // SAFETY: module conventions.
        let (Some(mut b), path) = (unsafe { bridge(b) }, unsafe { in_str(path) }) else { return 0 };
        let (ok, text) = match b.open_path(&path) {
            Ok(v) => (1, v),
            Err(e) => {
                set_error(e.clone());
                (0, e)
            }
        };
        if !result.is_null() {
            // SAFETY: the caller promises `result` is writable.
            unsafe { *result = out_string(text) };
        }
        ok
    })
}

/// Save the active document to a UTF-8 path; `mark_saved` 0 writes a copy (Export, Save a Copy).
///
/// # Safety
/// Module conventions; `result` null or writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_save(b: *mut PcBridge, path: *const c_char, mark_saved: c_int, result: *mut *mut c_char) -> c_int {
    guard(0, || {
        // SAFETY: module conventions.
        let (Some(mut b), path) = (unsafe { bridge(b) }, unsafe { in_str(path) }) else { return 0 };
        let (ok, text) = match b.save_path(&path, mark_saved != 0) {
            Ok(v) => (1, v),
            Err(e) => {
                set_error(e.clone());
                (0, e)
            }
        };
        if !result.is_null() {
            // SAFETY: the caller promises `result` is writable.
            unsafe { *result = out_string(text) };
        }
        ok
    })
}

/// # Safety
/// Module conventions.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_close_active(b: *mut PcBridge) -> c_int {
    // SAFETY: module conventions.
    guard(0, || unsafe { bridge(b) }.map_or(0, |mut b| c_int::from(b.close_active())))
}

// -------------------------------------------------------------------------- strokes --

/// Start a live stroke (`cmd` is `paint.stroke` or `paint.pencil`; `params` hold its first points).
///
/// # Safety
/// Module conventions; `damage` null or four writable values.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_stroke_begin(b: *mut PcBridge, cmd: *const c_char, params: *const c_char, damage: *mut i32) -> c_int {
    guard(0, || {
        // SAFETY: module conventions.
        let (Some(mut b), cmd, params) = (unsafe { bridge(b) }, unsafe { in_str(cmd) }, unsafe { in_str(params) }) else { return 0 };
        match b.stroke_begin(&cmd, &params) {
            Ok(r) => {
                // SAFETY: as promised by the caller.
                unsafe { write_rect(damage, r) };
                1
            }
            Err(e) => status(Err(e)),
        }
    })
}

/// More points: `count` groups of four doubles (x, y, pressure, time in ms).
///
/// # Safety
/// Module conventions; `points` holds `count * 4` doubles; `damage` null or four writable values.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_stroke_push(b: *mut PcBridge, points: *const f64, count: usize, damage: *mut i32) -> c_int {
    guard(0, || {
        // SAFETY: module conventions.
        let Some(mut b) = (unsafe { bridge(b) }) else { return 0 };
        if points.is_null() || count == 0 {
            return 1;
        }
        let Some(n) = count.checked_mul(4) else { return status(Err("too many points".into())) };
        // SAFETY: the caller promises `count * 4` doubles at `points`.
        let flat = unsafe { std::slice::from_raw_parts(points, n) };
        let pts: Vec<[f64; 4]> = flat.chunks_exact(4).map(|c| [c[0], c[1], c[2], c[3]]).collect();
        match b.stroke_push(&pts) {
            Ok(r) => {
                // SAFETY: as promised by the caller.
                unsafe { write_rect(damage, r) };
                1
            }
            Err(e) => status(Err(e)),
        }
    })
}

/// Finish the stroke: commit (`commit` non-zero) or drop it. `*result` as in `pc_execute`.
///
/// # Safety
/// Module conventions; `result` null or writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_stroke_end(b: *mut PcBridge, commit: c_int, result: *mut *mut c_char) -> c_int {
    guard(0, || {
        // SAFETY: module conventions.
        let Some(mut b) = (unsafe { bridge(b) }) else { return 0 };
        let (ok, text) = match b.stroke_end(commit != 0) {
            Ok(v) => (1, v),
            Err(e) => {
                set_error(e.clone());
                (0, e)
            }
        };
        if !result.is_null() {
            // SAFETY: the caller promises `result` is writable.
            unsafe { *result = out_string(text) };
        }
        ok
    })
}

// ------------------------------------------------------------------------ snapshots --

/// The document the canvas shows (the stroke in flight included); null with no document.
///
/// # Safety
/// Module conventions.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_snapshot(b: *mut PcBridge) -> *mut PcSnapshot {
    guard(std::ptr::null_mut(), || {
        // SAFETY: module conventions.
        let Some(b) = (unsafe { bridge(b) }) else { return std::ptr::null_mut() };
        b.snapshot().zip(b.display_snapshot()).map_or(std::ptr::null_mut(), |(source, doc)| {
            let encode = b.canvas_encodes_srgb(&doc);
            let source_encode = b.canvas_encodes_srgb(&source);
            let channels = b.session.active().filter(|st| !st.channel_view.is_plain(&source)).map(|st| crate::channels::Display {
                view: st.channel_view.clone(),
                active: st.active_layer,
                in_color: b.session.prefs().interface.show_channels_in_color,
            });
            Box::into_raw(Box::new(PcSnapshot { doc, source, encode, source_encode, channels }))
        })
    })
}

/// # Safety
/// `s` is null or from `pc_snapshot`, freed once. Any thread.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_snapshot_free(s: *mut PcSnapshot) {
    if !s.is_null() {
        // SAFETY: from `Box::into_raw` in `pc_snapshot`, freed once.
        guard((), || drop(unsafe { Box::from_raw(s) }));
    }
}

/// # Safety
/// `s` from `pc_snapshot`; `width`/`height` null or writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_snapshot_size(s: *const PcSnapshot, width: *mut u32, height: *mut u32) -> c_int {
    guard(0, || {
        // SAFETY: module conventions.
        let Some(s) = (unsafe { snap(s) }) else { return 0 };
        // SAFETY: the caller promises writable outputs.
        unsafe {
            if !width.is_null() {
                *width = s.doc.size.width;
            }
            if !height.is_null() {
                *height = s.doc.size.height;
            }
        }
        1
    })
}

/// Composite document rect (x0, y0)–(x1, y1) into at most `width`×`height` pixels of `out`;
/// `*out_w`/`*out_h` get the size written (smaller when the rect is rendered 1:1).
/// Thread-safe for distinct snapshots and buffers.
///
/// # Safety
/// `s` from `pc_snapshot`; `out` holds `len` writable bytes; `out_w`/`out_h` null or writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_render_view(
    s: *const PcSnapshot,
    x0: i32,
    y0: i32,
    x1: i32,
    y1: i32,
    width: u32,
    height: u32,
    out: *mut u8,
    len: usize,
    stride: usize,
    out_w: *mut u32,
    out_h: *mut u32,
) -> c_int {
    guard(0, || {
        // SAFETY: module conventions.
        let (Some(s), Some(buf)) = (unsafe { snap(s) }, unsafe { out_bytes(out, len) }) else { return status(Err("no snapshot or buffer".into())) };
        let rect = Rect::new(x0, y0, x1.max(x0), y1.max(y0));
        let result = if let Some(view) = &s.channels {
            crate::channels::render(&s.source, view, rect, width, height, buf, stride, s.source_encode)
        } else {
            crate::render_view(&s.doc, rect, width, height, buf, stride, s.encode)
        };
        match result {
            Ok(r) => {
                // SAFETY: the caller promises writable outputs.
                unsafe {
                    if !out_w.is_null() {
                        *out_w = r.width;
                    }
                    if !out_h.is_null() {
                        *out_h = r.height;
                    }
                }
                1
            }
            Err(e) => status(Err(e)),
        }
    })
}

/// Re-render the part of the whole-document `width`×`height` reduction that `damage` (document
/// pixels x0, y0, x1, y1) changed, in place in `out`; `out_rect` gets the output pixels rewritten.
///
/// # Safety
/// As `pc_render_view`; `damage` holds four values; `out_rect` null or four writable values.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_render_damage(
    s: *const PcSnapshot,
    width: u32,
    height: u32,
    damage: *const i32,
    out: *mut u8,
    len: usize,
    stride: usize,
    out_rect: *mut i32,
) -> c_int {
    guard(0, || {
        // SAFETY: module conventions.
        let (Some(s), Some(buf)) = (unsafe { snap(s) }, unsafe { out_bytes(out, len) }) else { return status(Err("no snapshot or buffer".into())) };
        if damage.is_null() {
            return status(Err("no damage rect".into()));
        }
        // SAFETY: the caller promises four values at `damage`.
        let d = unsafe { std::slice::from_raw_parts(damage, 4) };
        if let Some(view) = &s.channels {
            return match crate::channels::render(&s.source, view, s.source.bounds(), width, height, buf, stride, s.source_encode) {
                Ok(size) => {
                    // SAFETY: module conventions; the output rectangle has four writable values.
                    unsafe { write_rect(out_rect, Rect::new(0, 0, size.width as i32, size.height as i32)) };
                    1
                }
                Err(e) => status(Err(e)),
            };
        }
        match crate::render_damage(&s.doc, width, height, Rect::new(d[0], d[1], d[2].max(d[0]), d[3].max(d[1])), buf, stride) {
            Ok(r) => {
                // SAFETY: as promised by the caller.
                unsafe { write_rect(out_rect, r) };
                1
            }
            Err(e) => status(Err(e)),
        }
    })
}

/// A `side`×`side` thumbnail of a layer's own pixels. Returns 0 for layers without pixels.
///
/// # Safety
/// As `pc_render_view`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_layer_thumb(s: *const PcSnapshot, layer: u64, side: u32, out: *mut u8, len: usize, stride: usize) -> c_int {
    guard(0, || {
        // SAFETY: module conventions.
        let (Some(s), Some(buf)) = (unsafe { snap(s) }, unsafe { out_bytes(out, len) }) else { return 0 };
        match crate::layer_thumb(&s.source, layer, side, buf, stride) {
            Ok(drawn) => c_int::from(drawn),
            Err(e) => status(Err(e)),
        }
    })
}

/// Read selection coverage without rendering or changing the document.
///
/// # Safety
/// `s` follows the `pc_snapshot` ownership conventions.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_selection_contains(s: *const PcSnapshot, x: i32, y: i32) -> c_int {
    guard(0, || {
        // SAFETY: module conventions.
        let Some(s) = (unsafe { snap(s) }) else { return 0 };
        i32::from(s.doc.bounds().contains(x, y) && s.doc.selection.as_ref().is_some_and(|m| m.sample_channel(x, y, 0) > 0.0))
    })
}

/// Frozen alpha-contour mesh for Puppet Warp; no session mutation.
/// # Safety
/// `s` follows snapshot ownership conventions; `params` is null or NUL-terminated UTF-8.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_puppet_geometry(s: *const PcSnapshot, params: *const c_char) -> *mut c_char {
    guard(std::ptr::null_mut(), || {
        // SAFETY: module pointer/string conventions.
        let Some(s) = (unsafe { snap(s) }) else { return std::ptr::null_mut() };
        // SAFETY: module string conventions.
        let p = unsafe { in_str(params) };
        let result = serde_json::from_str(&p).map_err(|e| e.to_string()).and_then(|p| crate::workspace_geometry::puppet(&s.source, &p));
        match result {
            Ok(v) => out_string(v.to_string()),
            Err(e) => {
                set_error(e);
                std::ptr::null_mut()
            }
        }
    })
}

/// Capture snap geometry before a gesture. Module pointer/string conventions apply.
///
/// # Safety
/// `b` is a live bridge; `params` is a null-terminated UTF-8 string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_snap_targets(b: *mut PcBridge, params: *const c_char) -> *mut c_char {
    guard(std::ptr::null_mut(), || {
        // SAFETY: module conventions.
        let Some(b) = (unsafe { bridge(b) }) else { return std::ptr::null_mut() };
        // SAFETY: module conventions.
        match b.snap_targets(&unsafe { in_str(params) }) {
            Ok(value) => out_string(value),
            Err(error) => {
                set_error(error);
                std::ptr::null_mut()
            }
        }
    })
}

/// # Safety
/// `params` is null or a valid NUL-terminated UTF-8 string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_color_convert(params: *const c_char) -> *mut c_char {
    guard(std::ptr::null_mut(), || {
        // SAFETY: module conventions.
        match crate::panels::color_convert(&unsafe { in_str(params) }) {
            Ok(v) => out_string(v),
            Err(e) => {
                set_error(e);
                std::ptr::null_mut()
            }
        }
    })
}

/// # Safety
/// `b` and `params` follow the module conventions.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_export_gradients(b: *mut PcBridge, params: *const c_char) -> *mut c_char {
    guard(std::ptr::null_mut(), || {
        // SAFETY: module conventions.
        let Some(b) = (unsafe { bridge(b) }) else { return std::ptr::null_mut() };
        // SAFETY: module conventions.
        match b.export_gradients(&unsafe { in_str(params) }) {
            Ok(v) => out_string(v),
            Err(e) => {
                set_error(e);
                std::ptr::null_mut()
            }
        }
    })
}

/// # Safety
/// `s` is a live snapshot.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_histogram(s: *const PcSnapshot) -> *mut c_char {
    guard(std::ptr::null_mut(), || {
        // SAFETY: module conventions.
        let Some(s) = (unsafe { snap(s) }) else { return std::ptr::null_mut() };
        match crate::panels::histogram(&s.source, s.source_encode) {
            Ok(v) => out_string(v),
            Err(e) => {
                set_error(e);
                std::ptr::null_mut()
            }
        }
    })
}

/// # Safety
/// `b`, `params` and output buffer follow module ownership/length conventions.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_panel_thumb(b: *mut PcBridge, params: *const c_char, side: u32, out: *mut u8, len: usize, stride: usize) -> c_int {
    guard(0, || {
        // SAFETY: module conventions.
        let (Some(b), Some(out)) = (unsafe { bridge(b) }, unsafe { out_bytes(out, len) }) else { return 0 };
        // SAFETY: module conventions.
        status(b.panel_thumb(&unsafe { in_str(params) }, side, out, stride))
    })
}

/// The composite colour at a document pixel, straight RGBA 0..1.
///
/// # Safety
/// Module conventions: UTF-8 input and a writable output buffer of `len` bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_brush_tip(settings: *const c_char, side: u32, out: *mut u8, len: usize, stride: usize) -> c_int {
    guard(0, || {
        // SAFETY: module conventions.
        let Some(buf) = (unsafe { out_bytes(out, len) }) else { return 0 };
        // SAFETY: module conventions.
        status(crate::brush_tip(&unsafe { in_str(settings) }, side, buf, stride))
    })
}

/// A mask thumbnail (kind 0 pixel, 1 vector, 2 filter).
/// # Safety
/// Module conventions, as `pc_layer_thumb`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_mask_thumb(s: *const PcSnapshot, layer: u64, kind: u32, side: u32, out: *mut u8, len: usize, stride: usize) -> c_int {
    guard(0, || {
        // SAFETY: module conventions.
        let (Some(s), Some(buf)) = (unsafe { snap(s) }, unsafe { out_bytes(out, len) }) else { return 0 };
        status(crate::mask_thumb(&s.source, layer, kind, side, buf, stride))
    })
}

/// Read-only native text geometry.
/// # Safety
/// Module conventions. Free the returned string with `pc_string_free`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_text_geometry(b: *mut PcBridge, layer: u64) -> *mut c_char {
    guard(std::ptr::null_mut(), || {
        // SAFETY: module conventions.
        let Some(b) = (unsafe { bridge(b) }) else { return std::ptr::null_mut() };
        match b.text_geometry(layer) {
            Ok(s) => out_string(s),
            Err(e) => {
                set_error(e);
                std::ptr::null_mut()
            }
        }
    })
}
/// Actual engine font preview (including bundled fonts).
/// # Safety
/// Module conventions, UTF-8 family, writable output of len bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_font_preview(family: *const c_char, width: u32, height: u32, out: *mut u8, len: usize, stride: usize) -> c_int {
    guard(0, || {
        // SAFETY: module conventions.
        let Some(out) = (unsafe { out_bytes(out, len) }) else { return 0 };
        // SAFETY: module conventions.
        status(crate::interactive::font_preview(&unsafe { in_str(family) }, width, height, out, stride))
    })
}
/// Read-only Auto Levels/Curves parameters.
/// # Safety
/// Module conventions.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_adjustment_auto(b: *mut PcBridge, layer: u64) -> *mut c_char {
    guard(std::ptr::null_mut(), || {
        // SAFETY: module conventions.
        let Some(b) = (unsafe { bridge(b) }) else { return std::ptr::null_mut() };
        match b.adjustment_auto(layer) {
            Ok(s) => out_string(s),
            Err(e) => {
                set_error(e);
                std::ptr::null_mut()
            }
        }
    })
}
/// Read-only Clone Source image overlay.
/// # Safety
/// Module conventions, params UTF-8, out writable for len bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_clone_preview(
    b: *mut PcBridge,
    params: *const c_char,
    x0: i32,
    y0: i32,
    x1: i32,
    y1: i32,
    out: *mut u8,
    len: usize,
    stride: usize,
) -> c_int {
    guard(0, || {
        // SAFETY: module conventions.
        let (Some(b), Some(out)) = (unsafe { bridge(b) }, unsafe { out_bytes(out, len) }) else { return 0 };
        // SAFETY: module conventions.
        status(b.clone_preview(&unsafe { in_str(params) }, Rect::new(x0, y0, x1, y1), out, stride))
    })
}

/// The composite colour at a document pixel, straight RGBA 0..1.
///
/// # Safety
/// `s` from `pc_snapshot`; `rgba` holds four writable floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_sample(s: *const PcSnapshot, x: i32, y: i32, rgba: *mut f32) -> c_int {
    guard(0, || {
        // SAFETY: module conventions.
        let Some(s) = (unsafe { snap(s) }) else { return 0 };
        if rgba.is_null() {
            return 0;
        }
        let c = crate::sample(&s.doc, x, y);
        // SAFETY: the caller promises four writable floats.
        unsafe { std::slice::from_raw_parts_mut(rgba, 4) }.copy_from_slice(&c);
        1
    })
}

// ------------------------------------------------------------------------ the display --

/// The display the window is on: `id` (any stable number), its name (UTF-8), its ICC profile
/// (`icc`/`len`; null or 0: none) and frame in pixels. Becomes the main display.
///
/// # Safety
/// Module conventions; `icc` null or `len` readable bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_set_display(b: *mut PcBridge, id: u32, name: *const c_char, icc: *const u8, len: usize, x: f64, y: f64, w: f64, h: f64) -> c_int {
    guard(0, || {
        // SAFETY: module conventions.
        let (Some(mut b), name) = (unsafe { bridge(b) }, unsafe { in_str(name) }) else { return 0 };
        let profile = if icc.is_null() || len == 0 {
            None
        } else {
            // SAFETY: the caller promises `len` readable bytes at `icc`.
            Some(unsafe { std::slice::from_raw_parts(icc, len) }.to_vec())
        };
        if let Some(w) = b.set_display(id, &name, profile, [x, y, w, h]) {
            set_error(w);
        }
        1
    })
}

/// Changes whenever the active document's way to the screen changes (rebuild the colour table).
///
/// # Safety
/// Module conventions.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_display_key(b: *mut PcBridge) -> u64 {
    // SAFETY: module conventions.
    guard(0, || unsafe { bridge(b) }.map_or(0, |b| b.display_key()))
}

/// The canvas → monitor table of the active document: `size`³ RGBA8 entries, red fastest, into
/// `out` (`len` bytes). Returns 2 when written, 1 when no table is needed (values go to the
/// screen unchanged), 0 on error.
///
/// # Safety
/// Module conventions; `out` holds `len` writable bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_canvas_lut(b: *mut PcBridge, size: u32, out: *mut u8, len: usize) -> c_int {
    guard(0, || {
        // SAFETY: module conventions.
        let (Some(b), Some(buf)) = (unsafe { bridge(b) }, unsafe { out_bytes(out, len) }) else { return status(Err("no bridge or buffer".into())) };
        match b.canvas_lut(size as usize) {
            Ok(None) => 1,
            Ok(Some(lut)) => match buf.get_mut(..lut.len()) {
                Some(dst) => {
                    dst.copy_from_slice(&lut);
                    2
                }
                None => status(Err(format!("the table needs {} bytes", lut.len()))),
            },
            Err(e) => status(Err(e)),
        }
    })
}

// ----------------------------------------------------------------------- GPU canvas --

/// The GPU canvas (Windows): the engine's wgpu compositor on Direct3D 12. Used from one thread
/// (the shell's render thread).
#[cfg(windows)]
pub struct PcGpu(crate::gpu::Gpu);
#[cfg(not(windows))]
pub struct PcGpu;

/// A GPU canvas on the adapter with this LUID (the shell's Direct3D 11 adapter); null when that
/// isn't possible (`pc_last_error` says why) — the shell then keeps the CPU canvas.
#[unsafe(no_mangle)]
pub extern "C" fn pc_gpu_new(luid_low: u32, luid_high: i32) -> *mut PcGpu {
    guard(std::ptr::null_mut(), || {
        #[cfg(windows)]
        {
            match crate::gpu::Gpu::new((luid_low, luid_high)) {
                Ok(g) => Box::into_raw(Box::new(PcGpu(g))),
                Err(e) => {
                    set_error(e);
                    std::ptr::null_mut()
                }
            }
        }
        #[cfg(not(windows))]
        {
            let _ = (luid_low, luid_high);
            set_error("the GPU canvas is Windows-only");
            std::ptr::null_mut()
        }
    })
}

/// # Safety
/// `g` is null or from `pc_gpu_new`, freed once.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_gpu_free(g: *mut PcGpu) {
    if !g.is_null() {
        // SAFETY: from `Box::into_raw` in `pc_gpu_new`, freed once.
        guard((), || drop(unsafe { Box::from_raw(g) }));
    }
}

/// A new `width`×`height` canvas texture (premultiplied BGRA8); `*handle` gets its NT handle,
/// which the caller opens (`OpenSharedResource1`) and closes.
///
/// # Safety
/// `g` from `pc_gpu_new`; `handle` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_gpu_target(g: *mut PcGpu, width: u32, height: u32, handle: *mut *mut std::ffi::c_void) -> c_int {
    guard(0, || {
        // SAFETY: as promised by the caller.
        let Some(g) = (unsafe { g.as_mut() }) else { return status(Err("no GPU canvas".into())) };
        if handle.is_null() {
            return status(Err("no handle output".into()));
        }
        #[cfg(windows)]
        {
            match g.0.create_target(width, height) {
                Ok(h) => {
                    // SAFETY: the caller promises `handle` is writable.
                    unsafe { *handle = h };
                    1
                }
                Err(e) => status(Err(e)),
            }
        }
        #[cfg(not(windows))]
        {
            let _ = (g, width, height);
            status(Err("the GPU canvas is Windows-only".into()))
        }
    })
}

/// Composite document rect (x0, y0)–(x1, y1) of the snapshot into the canvas texture, waiting
/// for the GPU. Returns 1 when done, 2 when the GPU compositor doesn't cover this document (the
/// reason in `pc_last_error`: use the CPU canvas), 0 on error.
///
/// # Safety
/// `g` from `pc_gpu_new`, `s` from `pc_snapshot`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_gpu_render(g: *mut PcGpu, s: *const PcSnapshot, x0: i32, y0: i32, x1: i32, y1: i32) -> c_int {
    guard(0, || {
        // SAFETY: as promised by the caller.
        let (Some(g), Some(s)) = (unsafe { g.as_mut() }, unsafe { snap(s) }) else { return status(Err("no GPU canvas or snapshot".into())) };
        if s.channels.is_some() {
            set_error("channel preview uses the CPU renderer");
            return 2;
        }
        #[cfg(windows)]
        {
            match g.0.render(&s.doc, Rect::new(x0, y0, x1.max(x0), y1.max(y0)), s.encode) {
                Ok(crate::gpu::Outcome::Done) => 1,
                Ok(crate::gpu::Outcome::Unsupported(why)) => {
                    set_error(why);
                    2
                }
                Err(e) => status(Err(e)),
            }
        }
        #[cfg(not(windows))]
        {
            let _ = (g, s, x0, y0, x1, y1);
            status(Err("the GPU canvas is Windows-only".into()))
        }
    })
}

/// Steps in the active document's history: the shell compares it around a preview, so it only
/// takes back what the preview itself added.
///
/// # Safety
/// Module conventions.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pc_history_len(b: *mut PcBridge) -> u64 {
    // SAFETY: module conventions.
    guard(0, || unsafe { bridge(b) }.map_or(0, |b| b.history_len() as u64))
}
