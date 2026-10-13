//! The engine as a native shell sees it.
//!
//! The VetroView shell is C++: it owns the window, the docks, the glass and every animation, and
//! asks this crate for everything a document is. The seam is deliberately the one the egui shell
//! already uses — commands by id with JSON params in, JSON views and pixels out — so the shell
//! never learns an engine type and the engine never learns there is a shell.
//!
//! This module is the safe half: a [`Bridge`] value holding a [`Session`]. `ffi.rs` turns it into
//! `extern "C"` functions and is the only place that touches raw pointers.
#![deny(clippy::unwrap_used, clippy::expect_used, clippy::panic, clippy::unimplemented, clippy::todo, clippy::unreachable)]

use std::sync::Arc;

use photocraft_doc::{Document, LayerId};
use photocraft_engine::Session;
use photocraft_engine::brush_cmds::{LiveStroke, parse_points};
use photocraft_geom::Rect;
use serde_json::{Value, json};

mod ffi;
#[cfg(windows)]
#[doc(hidden)]
pub mod gpu;

/// Photoshop's menu structure, in order, with separators. The very file the egui shell builds its
/// menus from, included rather than copied, so both shells always show the same tree.
#[allow(dead_code)]
#[path = "../../ui-egui/src/menu_catalog.rs"]
mod menu_catalog;

/// The interface translations the egui shell ships, included rather than copied (like the menu
/// catalog), so both shells speak the same Russian: `context<TAB>source<TAB>translation` rows.
pub fn ui_strings(code: &str) -> Option<&'static str> {
    match code {
        "ru" => Some(include_str!("../../ui-egui/src/i18n/ru.tsv")),
        _ => None,
    }
}

/// A stroke being drawn: rendered live onto a copy of the document, committed on release.
struct Live {
    stroke: LiveStroke,
    cmd: String,
    params: Value,
    points: Vec<Value>,
}

/// One engine session and the stroke in flight.
#[derive(Default)]
pub struct Bridge {
    session: Session,
    live: Option<Live>,
    /// The pixels each recent step touched: (document, revision, damage; None = everything),
    /// oldest first. The engine keeps only the latest step's damage, but the shell may run
    /// several steps between two frames (a live preview undoes the last one, then applies the
    /// next): the canvas must redraw all of them, or the old pixels stay where they were.
    log: std::collections::VecDeque<(photocraft_doc::DocId, u64, Option<Rect>)>,
}

/// Steps the damage log remembers.
const LOG_STEPS: usize = 256;

/// What a render wrote: its size in pixels.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Rendered {
    pub width: u32,
    pub height: u32,
}

fn parse(params: &str) -> Result<Value, String> {
    if params.trim().is_empty() {
        return Ok(json!({}));
    }
    serde_json::from_str(params).map_err(|e| format!("params are not JSON: {e}"))
}

impl Bridge {
    pub fn new() -> Self {
        Self::default()
    }

    /// Run a command by id. The result is the command's own JSON result.
    pub fn execute(&mut self, id: &str, params: &str) -> Result<String, String> {
        let params = parse(params)?;
        // A command never runs over a stroke still being drawn: it would act on the document
        // without the stroke and the stroke would then commit onto a document that moved.
        self.live = None;
        self.session.clear_live_stroke();
        let value = self.session.execute(id, params).map_err(|e| e.to_string());
        self.note();
        Ok(value?.to_string())
    }

    /// Record the active document's latest step in the damage log (once per revision).
    fn note(&mut self) {
        let Some(st) = self.session.active() else { return };
        let entry = (st.doc.id, st.revision, st.last_damage);
        if self.log.back().is_some_and(|e| e.0 == entry.0 && e.1 == entry.1) {
            return;
        }
        self.log.push_back(entry);
        while self.log.len() > LOG_STEPS {
            self.log.pop_front();
        }
    }

    /// The pixels every step after revision `since` touched in the active document, together.
    /// `None`: assume the whole document (a step the log doesn't know, or one that can't be
    /// bounded).
    pub fn damage_since(&self, since: u64) -> Option<Rect> {
        let st = self.session.active()?;
        let (id, now) = (st.doc.id, st.revision);
        if now <= since {
            return Some(Rect::EMPTY);
        }
        if now - since > LOG_STEPS as u64 {
            return None;
        }
        let mut all = Rect::EMPTY;
        for rev in since + 1..=now {
            let step = self.log.iter().rev().find(|e| e.0 == id && e.1 == rev)?;
            all = all.union(&step.2?);
        }
        Some(all)
    }

    pub fn is_enabled(&self, id: &str) -> bool {
        self.session.is_enabled(id)
    }

    /// Enablement of many commands at once, for a whole menu: ids separated by newlines in,
    /// one `'1'` or `'0'` per id out.
    pub fn enabled_many(&self, ids: &str) -> String {
        ids.split('\n').filter(|s| !s.is_empty()).map(|id| if self.session.is_enabled(id.trim()) { '1' } else { '0' }).collect()
    }

    pub fn disabled_reason(&self, id: &str) -> Option<String> {
        self.session.disabled_reason(id)
    }

    /// The menu tree: `[[path…], label, shortcut|null, id, live]` rows in Photoshop's order, with
    /// `"---"` separators. `live` says the engine runs the id; the rest are view commands the shell
    /// answers itself, or not built yet.
    pub fn catalog() -> String {
        let known: std::collections::HashSet<&str> = photocraft_engine::command_specs().iter().map(|s| s.id).collect();
        let rows: Vec<Value> = menu_catalog::CATALOG.iter().map(|(path, label, shortcut, id)| json!([path, label, shortcut, id, known.contains(id)])).collect();
        Value::Array(rows).to_string()
    }

    /// Every command: id, label, menu path, shortcut and the params description.
    pub fn commands() -> String {
        let rows: Vec<Value> = photocraft_engine::command_specs()
            .iter()
            .map(|s| json!({"id": s.id, "label": s.label, "menu": s.menu, "shortcut": s.shortcut, "params": s.params}))
            .collect();
        Value::Array(rows).to_string()
    }

    /// Open documents, the active one, and the colour pair.
    pub fn session_info(&self) -> String {
        let mut v = photocraft_engine::inspect::session(&self.session);
        let b = &self.session.tools.brush;
        v["brush"] = json!({"size": b.size, "hardness": b.hardness, "opacity": b.opacity, "flow": b.flow, "spacing": b.spacing});
        v.to_string()
    }

    /// The active document: layers (top first), history, selection, size, mode.
    pub fn document_info(&self) -> String {
        match self.session.active() {
            Some(d) => photocraft_engine::inspect::document(d).to_string(),
            None => "null".into(),
        }
    }

    /// The active document's revision and the area its latest change touched (`None`: assume the
    /// whole document). A live stroke reports the revision it started on.
    pub fn revision(&self) -> Option<(u64, Option<Rect>)> {
        self.session.active().map(|d| (d.revision, d.last_damage))
    }

    /// What the canvas shows: the stroke in flight when there is one, else the active document.
    /// Cheap (an `Arc`), and safe to render from another thread.
    pub fn snapshot(&self) -> Option<Arc<Document>> {
        if let Some(live) = &self.live {
            return Some(live.stroke.doc.clone());
        }
        self.session.active().map(|d| d.doc.clone())
    }

    /// The active document's marks on the canvas, as JSON: Count groups with their markers, notes
    /// and the Ruler line (`null` with no document). The shell draws them over the picture.
    pub fn marks(&self) -> String {
        let Some(d) = self.session.active() else { return "null".into() };
        let doc = &d.doc;
        let hex = |c: &photocraft_color::Color| {
            let [r, g, b, _] = c.to_rgba8();
            format!("#{r:02x}{g:02x}{b:02x}")
        };
        let m = &doc.measurement;
        json!({
            "counts": m.count_groups.iter().map(|g| json!({
                "name": g.name, "color": hex(&g.color), "visible": g.visible,
                "markerSize": g.marker_size, "labelSize": g.label_size, "points": g.points,
            })).collect::<Vec<_>>(),
            "activeCount": m.active_count_group,
            "notes": doc.notes.iter().map(|n| json!({
                "position": n.position, "text": n.text, "author": n.author, "color": hex(&n.color), "open": n.open,
            })).collect::<Vec<_>>(),
            "ruler": m.ruler.map(|r| json!({"start": r.start, "end": r.end, "protractor": r.protractor})),
        })
        .to_string()
    }

    /// Steps in the active document's history (its entries up to the current state).
    pub fn history_len(&self) -> usize {
        self.session.active().map_or(0, |d| d.history.entries().len())
    }

    /// Does the canvas store `doc` sRGB-encoded? Linear composites (EXR/HDR, linear profiles) are,
    /// so their shadows keep their precision in 8 bits — the engine's display rule, which the
    /// colour-management table then expects.
    pub fn canvas_encodes_srgb(&self, doc: &Document) -> bool {
        self.session.color.canvas_display(doc).is_ok_and(|d| d.encode_srgb)
    }

    /// The display the window is on: its ICC profile (None: the OS gave none) and frame in
    /// pixels. Becomes the main display, so Monitor Profile `auto` resolves to it.
    pub fn set_display(&mut self, id: u32, name: &str, icc: Option<Vec<u8>>, frame: [f64; 4]) -> Option<String> {
        let display = photocraft_engine::display_color::Display {
            id,
            name: name.to_string(),
            frame,
            profile_name: None,
            icc: icc.filter(|b| !b.is_empty()).map(Arc::new),
        };
        let warning = self.session.color.set_displays(Ok(vec![display]));
        self.session.color.main_display = Some(id);
        warning
    }

    /// Changes whenever how the active document reaches the screen changes (its profile, the
    /// monitor profile, Proof Colors): the shell rebuilds its colour table then.
    pub fn display_key(&self) -> u64 {
        self.session.active().map_or(0, |d| self.session.color.display_signature(&d.doc))
    }

    /// The canvas → monitor table for the active document: `size`³ RGBA8 entries, red fastest.
    /// `None` when values go to the screen unchanged.
    pub fn canvas_lut(&self, size: usize) -> Result<Option<Vec<u8>>, String> {
        let Some(d) = self.session.active() else { return Ok(None) };
        self.session.color.canvas_lut(&d.doc, size.clamp(2, 65)).map_err(|e| e.to_string())
    }

    /// Start a live stroke of `cmd` (`paint.stroke` or `paint.pencil`) from params holding its
    /// first points. Returns the area drawn.
    pub fn stroke_begin(&mut self, cmd: &str, params: &str) -> Result<Rect, String> {
        let params = parse(params)?;
        let points = params.get("points").and_then(Value::as_array).cloned().unwrap_or_default();
        let stroke = LiveStroke::begin_with(&self.session, cmd, &params).map_err(|e| e.to_string())?;
        let damage = stroke.bounds();
        self.live = Some(Live { stroke, cmd: cmd.into(), params, points });
        Ok(damage)
    }

    /// More points for the stroke in flight: `[x, y, pressure, timeMs]` each. Returns the area that
    /// changed.
    pub fn stroke_push(&mut self, points: &[[f64; 4]]) -> Result<Rect, String> {
        let Some(live) = self.live.as_mut() else { return Err("no stroke in progress".into()) };
        let json_points: Vec<Value> = points.iter().map(|p| json!([p[0], p[1], p[2], 0, 0, 0, p[3]])).collect();
        // Parsed by the engine's own reader, so the points the commit parses are exactly these.
        let parsed = parse_points(&json!({ "points": json_points }), &live.cmd).map_err(|e| e.to_string())?;
        let damage = live.stroke.push(&parsed).map_err(|e| e.to_string())?;
        live.points.extend(json_points);
        Ok(damage)
    }

    /// Finish the stroke: commit it as one history step (`commit`), or drop it.
    pub fn stroke_end(&mut self, commit: bool) -> Result<String, String> {
        let Some(live) = self.live.take() else { return Err("no stroke in progress".into()) };
        if !commit {
            return Ok("null".into());
        }
        let mut params = live.params.clone();
        params["points"] = Value::Array(live.points);
        params["seed"] = json!(live.stroke.seed);
        // The commit takes the pixels already drawn instead of rendering the stroke again.
        self.session.offer_live_stroke(live.stroke).map_err(|e| e.to_string())?;
        let result = self.session.execute(&live.cmd, params);
        self.note();
        self.session.clear_live_stroke();
        result.map(|v| v.to_string()).map_err(|e| e.to_string())
    }

    pub fn stroke_active(&self) -> bool {
        self.live.is_some()
    }

    pub fn undo(&mut self) -> bool {
        self.live = None;
        let done = self.session.undo();
        self.note();
        done
    }

    pub fn redo(&mut self) -> bool {
        self.live = None;
        let done = self.session.redo();
        self.note();
        done
    }

    pub fn set_active(&mut self, index: usize) -> bool {
        self.live = None;
        self.session.set_active(index)
    }

    /// Select a layer without a history step.
    pub fn select_layer(&mut self, id: u64) -> Result<(), String> {
        let r = self.session.select_layer(LayerId(id)).map_err(|e| e.to_string());
        self.note();
        r
    }

    /// File › Open: any format `photocraft-io` reads, `.pcraft` natively. The document becomes
    /// active. Returns the import warnings as a JSON array.
    pub fn open_path(&mut self, path: &str) -> Result<String, String> {
        let p = std::path::Path::new(path);
        let (document, warnings, read_only) = if p.is_dir() {
            (photocraft_format::load_path(p).map_err(|e| e.to_string())?, Vec::new(), false)
        } else {
            let bytes = photocraft_format::read_file(p).map_err(|e| format!("{path}: {e}"))?;
            let name = p.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
            if p.extension().is_some_and(|x| x.eq_ignore_ascii_case(photocraft_format::EXTENSION)) {
                (photocraft_format::load_from_bytes(&bytes).map_err(|e| e.to_string())?, Vec::new(), false)
            } else {
                let r = photocraft_io::import(&name, &bytes).map_err(|e| e.to_string())?;
                (r.document, r.warnings, r.source_read_only)
            }
        };
        self.live = None;
        self.session.add_document(document, Some(path.to_string()));
        if let Some(st) = self.session.active_mut() {
            st.source_read_only = read_only;
            if read_only {
                st.path = None;
            }
        }
        Ok(Value::Array(warnings.into_iter().map(Value::String).collect()).to_string())
    }

    /// File › Save / Save As (`mark_saved`) or a copy and export (not). The format comes from the
    /// extension; writes are atomic, so a failed save never destroys the previous file.
    pub fn save_path(&mut self, path: &str, mark_saved: bool) -> Result<String, String> {
        self.live = None;
        let st = self.session.active().ok_or("no document open")?;
        let p = std::path::Path::new(path);
        let ext = p.extension().map(|e| e.to_string_lossy().to_ascii_lowercase()).ok_or_else(|| format!("cannot tell the format of `{path}`"))?;
        let mut warnings = Vec::new();
        if ext == photocraft_format::EXTENSION {
            let mut named = (*st.doc).clone();
            if let Some(name) = p.file_name() {
                named.name = name.to_string_lossy().into_owned();
            }
            let previews = photocraft_format::SaveOptions {
                thumbnail: Some(photocraft_compose::thumbnail(&named, 256)),
                composite: Some(photocraft_compose::thumbnail(&named, 1024)),
            };
            photocraft_format::PcraftWriter::new().save_path(&named, p, &previews).map_err(|e| e.to_string())?;
        } else {
            let r = photocraft_io::export(&st.doc, &ext, &photocraft_io::ExportOptions::default()).map_err(|e| e.to_string())?;
            photocraft_format::atomic_write(p, &r.bytes).map_err(|e| e.to_string())?;
            warnings = r.warnings;
        }
        if mark_saved && let Some(st) = self.session.active_mut() {
            st.path = Some(path.to_string());
            st.saved_revision = st.revision;
            if let Some(name) = p.file_name() {
                std::sync::Arc::make_mut(&mut st.doc).name = name.to_string_lossy().into_owned();
            }
        }
        Ok(Value::Array(warnings.into_iter().map(Value::String).collect()).to_string())
    }

    /// Close the active document (unsaved changes are the shell's question to ask first).
    pub fn close_active(&mut self) -> bool {
        self.live = None;
        match self.session.active_index() {
            Some(i) => self.session.close(i).is_some(),
            None => false,
        }
    }
}

/// Composite `rect` of `doc` (document pixels; may reach past the canvas) area-averaged down to at
/// most `width`×`height`, into premultiplied BGRA8 rows of `stride` bytes — Direct2D's native
/// format. A rect smaller than the output is rendered 1:1 and the shell scales it up. `encode`:
/// store sRGB-encoded values ([`Bridge::canvas_encodes_srgb`]).
#[allow(clippy::too_many_arguments)]
pub fn render_view(doc: &Document, rect: Rect, width: u32, height: u32, out: &mut [u8], stride: usize, encode: bool) -> Result<Rendered, String> {
    if rect.is_empty() || width == 0 || height == 0 {
        return Ok(Rendered::default());
    }
    let (w, h) = (width.min(rect.width()), height.min(rect.height()));
    let mut buffer = if w == rect.width() && h == rect.height() {
        photocraft_compose::render(doc, rect)
    } else {
        photocraft_compose::render_reduced_rect(doc, rect, w, h)
    };
    if encode {
        encode_srgb(&mut buffer.px);
    }
    let (bw, bh) = (buffer.rect.width(), buffer.rect.height());
    write_bgra(&buffer.px, bw as usize, bh as usize, out, stride)?;
    Ok(Rendered { width: bw, height: bh })
}

/// Linear values → the sRGB curve, in place (colour channels only).
fn encode_srgb(px: &mut [[f32; 4]]) {
    for p in px {
        for v in &mut p[..3] {
            *v = photocraft_color::convert::linear_to_srgb(if v.is_nan() { 0.0 } else { v.clamp(0.0, 1.0) });
        }
    }
}

/// The part of the whole-document reduction to `width`×`height` that `damage` changed, written at
/// its place in `out` (the full reduced image). Returns the output rect that was rewritten.
pub fn render_damage(doc: &Document, width: u32, height: u32, damage: Rect, out: &mut [u8], stride: usize) -> Result<Rect, String> {
    let buffer = photocraft_compose::render_reduced_damage(doc, width, height, damage);
    let r = buffer.rect;
    if r.is_empty() {
        return Ok(r);
    }
    let (bw, bh) = (r.width() as usize, r.height() as usize);
    let x0 = usize::try_from(r.x0).map_err(|_| "damage left of the image")?;
    let y0 = usize::try_from(r.y0).map_err(|_| "damage above the image")?;
    for (row, src) in buffer.px.chunks_exact(bw.max(1)).enumerate().take(bh) {
        let start = (y0 + row) * stride + x0 * 4;
        let dst = out.get_mut(start..start + bw * 4).ok_or("damage outside the image")?;
        pack_row(src, dst);
    }
    Ok(r)
}

/// A square thumbnail of one layer's own pixels laid out on the canvas, as the Layers panel shows it.
pub fn layer_thumb(doc: &Document, layer: u64, side: u32, out: &mut [u8], stride: usize) -> Result<bool, String> {
    let Some(l) = doc.layer(LayerId(layer)) else { return Ok(false) };
    let Some(s) = l.surface() else { return Ok(false) };
    let side = side.clamp(1, 512) as usize;
    let (w, h) = (doc.size.width.max(1) as f32, doc.size.height.max(1) as f32);
    let scale = w.max(h) / side as f32;
    let (ox, oy) = ((side as f32 - w / scale) / 2.0, (side as f32 - h / scale) / 2.0);
    let mut px = vec![[0.0f32; 4]; side * side];
    let mut sample = [0.0f32; 8];
    let n = s.channels().min(sample.len());
    let format = s.format();
    for ty in 0..side {
        for tx in 0..side {
            let dx = (tx as f32 - ox + 0.5) * scale;
            let dy = (ty as f32 - oy + 0.5) * scale;
            if dx < 0.0 || dy < 0.0 || dx >= w || dy >= h {
                continue;
            }
            s.read_pixel(dx as i32, dy as i32, &mut sample[..n]);
            if let Some(p) = px.get_mut(ty * side + tx) {
                *p = photocraft_raster::to_rgba(&format, &sample[..n]);
            }
        }
    }
    write_bgra(&px, side, side, out, stride)?;
    Ok(true)
}

/// The composite colour at one document pixel, straight RGBA 0..1.
pub fn sample(doc: &Document, x: i32, y: i32) -> [f32; 4] {
    let b = photocraft_compose::render(doc, Rect::new(x, y, x.saturating_add(1), y.saturating_add(1)));
    b.px.first().copied().unwrap_or([0.0; 4])
}

fn write_bgra(px: &[[f32; 4]], w: usize, h: usize, out: &mut [u8], stride: usize) -> Result<(), String> {
    if w == 0 || h == 0 {
        return Ok(());
    }
    if stride < w * 4 || out.len() < stride * (h - 1) + w * 4 {
        return Err(format!("output buffer too small for {w}×{h}"));
    }
    for (row, src) in px.chunks_exact(w).enumerate().take(h) {
        let start = row * stride;
        let dst = out.get_mut(start..start + w * 4).ok_or("output buffer too small")?;
        pack_row(src, dst);
    }
    Ok(())
}

/// Straight float RGBA → premultiplied BGRA8.
fn pack_row(src: &[[f32; 4]], dst: &mut [u8]) {
    for (p, o) in src.iter().zip(dst.chunks_exact_mut(4)) {
        let a = if p[3].is_nan() { 0.0 } else { p[3].clamp(0.0, 1.0) };
        let q = |v: f32| ((if v.is_nan() { 0.0 } else { v.clamp(0.0, 1.0) }) * a * 255.0 + 0.5) as u8;
        o[0] = q(p[2]);
        o[1] = q(p[1]);
        o[2] = q(p[0]);
        o[3] = (a * 255.0 + 0.5) as u8;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn bridge_with_doc() -> Bridge {
        let mut b = Bridge::new();
        b.execute("file.new", r##"{"width":64,"height":48,"background":"white"}"##).unwrap();
        b
    }

    #[test]
    fn catalog_lists_the_file_menu_first_and_marks_engine_commands_live() {
        let v: Value = serde_json::from_str(&Bridge::catalog()).unwrap();
        let first = &v[0];
        assert_eq!(first[0][0], "File");
        assert_eq!(first[3], "file.new");
        assert_eq!(first[4], true);
    }

    #[test]
    fn renders_a_white_document_premultiplied() {
        let b = bridge_with_doc();
        let doc = b.snapshot().unwrap();
        let mut out = vec![0u8; 32 * 24 * 4];
        let r = render_view(&doc, doc.bounds(), 32, 24, &mut out, 32 * 4, false).unwrap();
        assert_eq!((r.width, r.height), (32, 24));
        assert_eq!(&out[..4], &[255, 255, 255, 255]);
    }

    #[test]
    fn a_live_stroke_commits_as_one_history_step() {
        let mut b = bridge_with_doc();
        b.execute("tools.setColors", r##"{"foreground":"#ff0000"}"##).unwrap();
        b.stroke_begin("paint.stroke", r##"{"points":[[10,10,1]],"size":8,"hardness":1}"##).unwrap();
        b.stroke_push(&[[30.0, 20.0, 1.0, 16.0], [50.0, 30.0, 1.0, 32.0]]).unwrap();
        let before: Value = serde_json::from_str(&b.document_info()).unwrap();
        b.stroke_end(true).unwrap();
        let after: Value = serde_json::from_str(&b.document_info()).unwrap();
        assert_eq!(after["history"].as_array().unwrap().len(), before["history"].as_array().unwrap().len() + 1);
        let doc = b.snapshot().unwrap();
        let px = sample(&doc, 30, 20);
        assert!(px[0] > 0.9 && px[1] < 0.1, "{px:?}");
    }

    #[test]
    fn bad_params_are_an_error_not_a_crash() {
        let mut b = Bridge::new();
        assert!(b.execute("file.new", "{not json").is_err());
        assert!(b.execute("no.such.command", "{}").is_err());
        assert!(b.stroke_push(&[[0.0, 0.0, 1.0, 0.0]]).is_err());
    }

    #[test]
    fn saves_and_reopens_png_and_pcraft() {
        let dir = std::env::temp_dir().join(format!("photocraft-bridge-test-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let mut b = bridge_with_doc();
        for name in ["out.png", "out.pcraft"] {
            let path = dir.join(name).to_string_lossy().into_owned();
            b.save_path(&path, true).unwrap();
            b.open_path(&path).unwrap();
            let info: Value = serde_json::from_str(&b.document_info()).unwrap();
            assert_eq!((info["width"].as_u64(), info["height"].as_u64()), (Some(64), Some(48)), "{name}");
        }
        assert!(b.open_path(&dir.join("missing.png").to_string_lossy()).is_err());
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn enabled_many_answers_per_id() {
        let b = bridge_with_doc();
        assert_eq!(b.enabled_many("file.new\nno.such.command\n"), "10");
    }

    #[test]
    fn marks_list_count_markers_notes_and_the_ruler() {
        let mut b = Bridge::new();
        assert_eq!(b.marks(), "null");
        b.execute("file.new", r#"{"width":64,"height":48}"#).unwrap();
        b.execute("count.add", r#"{"x":10,"y":12}"#).unwrap();
        b.execute("count.add", r#"{"x":20,"y":22}"#).unwrap();
        b.execute("notes.add", r#"{"x":5,"y":6,"text":"hi"}"#).unwrap();
        let m: Value = serde_json::from_str(&b.marks()).unwrap();
        assert_eq!(m["counts"][0]["points"], json!([[10.0, 12.0], [20.0, 22.0]]));
        assert_eq!(m["notes"][0]["text"], "hi");
        assert_eq!(m["notes"][0]["position"], json!([5.0, 6.0]));
        assert!(m["ruler"].is_null());
    }

    #[test]
    fn damage_since_covers_every_step_a_preview_took() {
        let mut b = Bridge::new();
        b.execute("file.new", r#"{"width":200,"height":100}"#).unwrap();
        let (start, _) = b.revision().unwrap();
        // Paint the left, take it back, paint the right: the last step alone covers only the right.
        b.execute("paint.stroke", r##"{"points":[[20,50]],"size":10,"color":"#ff0000"}"##).unwrap();
        assert!(b.undo());
        b.execute("paint.stroke", r##"{"points":[[180,50]],"size":10,"color":"#ff0000"}"##).unwrap();
        let (_, last) = b.revision().unwrap();
        let last = last.unwrap();
        assert!(last.x0 > 100, "the latest step is only the right stroke: {last:?}");
        let all = b.damage_since(start).unwrap();
        assert!(all.x0 < 30 && all.x1 > 170, "both strokes: {all:?}");
        assert_eq!(b.damage_since(b.revision().unwrap().0), Some(Rect::EMPTY));
        assert_eq!(b.damage_since(start.saturating_sub(1000)), None, "steps the log never saw");
    }

    #[test]
    fn ui_strings_are_the_egui_catalog_rows() {
        let ru = ui_strings("ru").unwrap();
        assert!(ru.lines().any(|l| l.split('\t').nth(1) == Some("Layers")));
        assert!(ui_strings("en").is_none());
    }

    #[test]
    fn history_len_counts_steps_and_settings_leave_it_alone() {
        let mut b = bridge_with_doc();
        let before = b.history_len();
        b.execute("layer.new.layer", "{}").unwrap();
        assert_eq!(b.history_len(), before + 1);
        // A setting is no edit: a preview of it must find nothing to take back.
        let _ = b.execute("edit.colorSettings", r#"{"monitorProfile":"srgb"}"#);
        assert_eq!(b.history_len(), before + 1);
        assert!(b.undo());
        assert_eq!(b.history_len(), before);
    }

    #[test]
    fn an_srgb_document_on_an_srgb_monitor_needs_no_table_and_a_p3_monitor_does() {
        let mut b = bridge_with_doc();
        let doc = b.snapshot().unwrap();
        assert!(!b.canvas_encodes_srgb(&doc));
        b.execute("edit.colorSettings", r#"{"monitorProfile":"srgb"}"#).unwrap();
        assert!(b.canvas_lut(17).unwrap().is_none());
        let key = b.display_key();
        b.execute("edit.colorSettings", r#"{"monitorProfile":"display-p3"}"#).unwrap();
        assert_ne!(b.display_key(), key);
        let lut = b.canvas_lut(17).unwrap().expect("sRGB on a P3 monitor is not the identity");
        assert_eq!(lut.len(), 17 * 17 * 17 * 4);
        // Pure red, red fastest: entry 16 is (r=1, g=0, b=0); a P3 monitor shows sRGB red less saturated.
        assert!(lut[16 * 4] < 255 && lut[16 * 4 + 1] > 0);
    }

    #[test]
    fn white_is_white_encoded_or_not() {
        let b = bridge_with_doc();
        let doc = b.snapshot().unwrap();
        let mut plain = vec![0u8; 4 * 4 * 4];
        let mut encoded = plain.clone();
        render_view(&doc, Rect::new(0, 0, 4, 4), 4, 4, &mut plain, 16, false).unwrap();
        render_view(&doc, Rect::new(0, 0, 4, 4), 4, 4, &mut encoded, 16, true).unwrap();
        assert_eq!(plain, encoded);
    }
}
