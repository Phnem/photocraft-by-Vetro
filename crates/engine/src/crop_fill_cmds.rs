//! Content-Aware Crop: crop existing layers, complete only newly exposed canvas, one undo step.
use photocraft_algo::content_aware::{FillOptions, fill};
use photocraft_doc::{Layer, LayerContent, Size};
use photocraft_geom::Rect;
use photocraft_raster::Surface;
use serde_json::{Value, json};

use crate::{EngineError, Result, Session, commands::CommandSpec};

pub const CMD: &str = "image.cropContentAware";
const MAX_PIXELS: u64 = 64 * 1024 * 1024;
const PROXY_PIXELS: f64 = 1_000_000.0;
fn bad(message: impl Into<String>) -> EngineError {
    EngineError::BadParams { cmd: CMD.into(), msg: message.into() }
}
fn has_doc(s: &Session) -> std::result::Result<(), String> {
    s.active().map(|_| ()).ok_or_else(|| "no document open".into())
}

/// Maps a pixel of the final (possibly resampled) crop back to the original canvas.
struct Coverage {
    old: Size,
    sx: f64,
    sy: f64,
    ox: f64,
    oy: f64,
    ncx: f64,
    ncy: f64,
    sn: f64,
    cs: f64,
}
impl Coverage {
    fn new(old: Size, frame: &Value, output: Size) -> Self {
        let x = frame.get("x").and_then(Value::as_f64).unwrap_or(0.0);
        let y = frame.get("y").and_then(Value::as_f64).unwrap_or(0.0);
        let w = frame.get("width").and_then(Value::as_f64).unwrap_or(f64::from(output.width));
        let h = frame.get("height").and_then(Value::as_f64).unwrap_or(f64::from(output.height));
        let angle = frame.get("angle").and_then(Value::as_f64).unwrap_or(0.0);
        let new = crate::mode_cmds::rotated_size(old, -angle);
        let (sn, cs) = angle.to_radians().sin_cos();
        let (ncx, ncy) = (f64::from(new.width) / 2.0, f64::from(new.height) / 2.0);
        let (dx, dy) = (x + w / 2.0 - f64::from(old.width) / 2.0, y + h / 2.0 - f64::from(old.height) / 2.0);
        Self {
            old,
            sx: w / f64::from(output.width.max(1)),
            sy: h / f64::from(output.height.max(1)),
            ox: (ncx + cs * dx + sn * dy - w / 2.0).round(),
            oy: (ncy - sn * dx + cs * dy - h / 2.0).round(),
            ncx,
            ncy,
            sn,
            cs,
        }
    }
    fn contains(&self, x: f64, y: f64) -> bool {
        let (dx, dy) = (x * self.sx + self.ox - self.ncx, y * self.sy + self.oy - self.ncy);
        let (px, py) = (f64::from(self.old.width) / 2.0 + self.cs * dx - self.sn * dy, f64::from(self.old.height) / 2.0 + self.sn * dx + self.cs * dy);
        px >= 0.0 && py >= 0.0 && px < f64::from(self.old.width) && py < f64::from(self.old.height)
    }
}
fn buffer<T: Clone>(length: usize, value: T) -> Result<Vec<T>> {
    let mut out = Vec::new();
    out.try_reserve_exact(length).map_err(|_| bad("not enough memory for Content-Aware Crop"))?;
    out.resize(length, value);
    Ok(out)
}
fn fill_exposed(s: &mut Session, old: Size, frame: &Value, name: &str) -> Result<(u64, Option<u64>)> {
    let doc = &s.active().ok_or(EngineError::NoDocument)?.doc;
    let area = doc.bounds();
    let fmt = doc.pixel_format();
    let n = fmt.channels();
    if !fmt.alpha || !(2..=8).contains(&n) {
        return Err(bad("unsupported pixel format for crop completion"));
    }
    let cc = n - 1;
    let (w, h) = (area.width() as usize, area.height() as usize);
    let coverage = Coverage::new(old, frame, doc.size);
    // A convex original canvas covers the entire crop if it covers all four pixel centres.
    if [(0.5, 0.5), (w as f64 - 0.5, 0.5), (0.5, h as f64 - 0.5), (w as f64 - 0.5, h as f64 - 0.5)].into_iter().all(|(x, y)| coverage.contains(x, y)) {
        return Ok((0, None));
    }
    let count = w.checked_mul(h).filter(|v| *v as u64 <= MAX_PIXELS).ok_or_else(|| bad("Content-Aware Crop is limited to 64 megapixels"))?;
    let k = ((count as f64 / PROXY_PIXELS).sqrt().ceil() as usize).max(1);
    let (pw, ph) = (w.div_ceil(k), h.div_ceil(k));
    let pixels = pw.checked_mul(ph).ok_or_else(|| bad("crop proxy is too large"))?;
    let samples = pixels.checked_mul(cc).ok_or_else(|| bad("crop proxy is too large"))?;
    let source_image = crate::file_cmds::flattened(doc, fmt);
    let mut image = buffer(samples, 0.0f32)?;
    let mut hole = buffer(pixels, false)?;
    let mut source = buffer(pixels, false)?;
    for (i, cell) in image.chunks_exact_mut(cc).enumerate() {
        let (x, y) = ((i % pw) * k, (i / pw) * k);
        let (x1, y1) = ((x + k).min(w), (y + k).min(h));
        let (cx, cy) = ((x + x1) / 2, (y + y1) / 2);
        for (c, value) in cell.iter_mut().enumerate() {
            *value = source_image.sample_channel(cx as i32, cy as i32, c);
        }
        let exposed =
            [(x as f64 + 0.5, y as f64 + 0.5), (x1 as f64 - 0.5, y as f64 + 0.5), (x as f64 + 0.5, y1 as f64 - 0.5), (x1 as f64 - 0.5, y1 as f64 - 0.5)]
                .into_iter()
                .any(|(x, y)| !coverage.contains(x, y));
        if let Some(v) = hole.get_mut(i) {
            *v = exposed;
        }
        if let Some(v) = source.get_mut(i) {
            *v = !exposed && source_image.sample_channel(cx as i32, cy as i32, cc) > 0.01;
        }
    }
    if !source.iter().any(|v| *v) {
        return Err(bad("Content-Aware Crop needs visible original pixels inside the frame to sample from"));
    }
    let filled = fill(pw, ph, cc, &image, &hole, &source, &FillOptions::default());
    if filled.len() != samples {
        return Err(bad("crop completion returned an incomplete image"));
    }
    let mut patch = Surface::new(fmt);
    let mut row = Vec::new();
    let mut total = 0u64;
    let read = |x: usize, y: usize, c: usize| filled.get((y * pw + x) * cc + c).copied().unwrap_or(0.0);
    for y in 0..h {
        let mut start = 0;
        for x in 0..=w {
            if x < w && !coverage.contains(x as f64 + 0.5, y as f64 + 0.5) {
                if row.is_empty() {
                    start = x;
                }
                let (fx, fy) =
                    (((x as f64 + 0.5) / k as f64 - 0.5).clamp(0.0, (pw - 1) as f64), ((y as f64 + 0.5) / k as f64 - 0.5).clamp(0.0, (ph - 1) as f64));
                let (ix, iy) = (fx as usize, fy as usize);
                let (tx, ty) = ((fx - ix as f64) as f32, (fy - iy as f64) as f32);
                row.try_reserve(n).map_err(|_| bad("not enough memory for crop fill row"))?;
                for c in 0..cc {
                    row.push(
                        (read(ix, iy, c) * (1.0 - tx) + read((ix + 1).min(pw - 1), iy, c) * tx) * (1.0 - ty)
                            + (read(ix, (iy + 1).min(ph - 1), c) * (1.0 - tx) + read((ix + 1).min(pw - 1), (iy + 1).min(ph - 1), c) * tx) * ty,
                    );
                }
                row.push(1.0);
                total += 1;
            } else if !row.is_empty() {
                patch.write_region(Rect::new(start as i32, y as i32, x as i32, y as i32 + 1), &row);
                row.clear();
            }
        }
    }
    let layer = Layer::new(name, LayerContent::Raster(patch));
    let id = s.edit("Content-Aware Crop Fill", |doc, active| {
        let id = doc.insert_above(None, layer);
        *active = Some(id);
        Ok(id)
    })?;
    Ok((total, Some(id.0)))
}
fn crop(s: &mut Session, p: &Value) -> Result<Value> {
    let d = s.active().ok_or(EngineError::NoDocument)?;
    let old = d.doc.size;
    let extent = |target: &str, frame: &str, fallback: u32| p.get(target).or_else(|| p.get(frame)).and_then(Value::as_u64).unwrap_or(u64::from(fallback));
    let (w, h) = (extent("targetWidth", "width", old.width), extent("targetHeight", "height", old.height));
    if w.checked_mul(h).is_none_or(|n| n > MAX_PIXELS) {
        return Err(bad("Content-Aware Crop is limited to 64 megapixels"));
    }
    let name = p.get("fillLayerName").and_then(Value::as_str).unwrap_or("Content-Aware Crop Fill");
    if name.len() > 4096 {
        return Err(bad("fill layer name is too long"));
    }
    crate::analysis_cmds::compound(s, "Content-Aware Crop", |s| {
        let mut frame = s.execute("image.crop", p.clone())?;
        let (filled, layer) = fill_exposed(s, old, &frame, name)?;
        if let Some(o) = frame.as_object_mut() {
            o.insert("filled".into(), json!(filled));
            o.insert("fillLayer".into(), json!(layer));
        }
        Ok(frame)
    })
}
pub fn specs() -> Vec<CommandSpec> {
    vec![CommandSpec {
        id: CMD,
        label: "Content-Aware Crop",
        menu: &[],
        shortcut: None,
        params: r#"{"x":px,"y":px,"width":px,"height":px,"angle":degrees=0,"deleteCroppedPixels":bool=true,"targetWidth":px?,"targetHeight":px?,"resolution":ppi?,"resample":"bicubic|bilinear|nearest|lanczos"="bicubic","fillLayerName":string?} → {x,y,width,height,documentWidth,documentHeight,resolution,filled,fillLayer} (one undo step; preserves original layers and transparency, adds a pixel layer only for canvas exposed by extension/rotation; bounded 64 MP, texture proxy <= 1.1 MP)"#,
        enabled: has_doc,
        run: crop,
        journal: true,
    }]
}
