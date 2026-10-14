//! Bounded, read-only native panel views. Editing stays in the existing engine commands.
use crate::{Bridge, encode_srgb, write_bgra};
use photocraft_color::convert::{cmyk_to_rgb, lab_to_srgb, rgb_to_cmyk, srgb_to_lab};
use photocraft_doc::{Document, Layer, LayerContent};
use photocraft_engine::presets::swatches::{hex, hsb_to_rgb};
use photocraft_geom::Rect;
use serde_json::{Value, json};

/// Picker units: RGB 0..255, HSB degrees/percent, Lab D50, CMYK percent.
pub fn color_convert(params: &str) -> Result<String, String> {
    let p: Value = serde_json::from_str(params).map_err(|e| e.to_string())?;
    let values = p["values"].as_array().ok_or("values must be an array")?;
    let n = |i: usize| -> Result<f32, String> {
        values.get(i).and_then(Value::as_f64).filter(|v| v.is_finite() && v.abs() < 1e6).map(|v| v as f32).ok_or("invalid colour component".into())
    };
    let rgb = match p["model"].as_str().unwrap_or("rgb") {
        "rgb" => [n(0)? / 255., n(1)? / 255., n(2)? / 255.],
        "hsb" => hsb_to_rgb([n(0)?, n(1)? / 100., n(2)? / 100.]),
        "lab" => lab_to_srgb([n(0)?, n(1)?, n(2)?]),
        "cmyk" => cmyk_to_rgb([n(0)? / 100., n(1)? / 100., n(2)? / 100., n(3)? / 100.]),
        _ => return Err("unknown colour model".into()),
    }
    .map(|v| v.clamp(0., 1.));
    let rgb = if p["webOnly"].as_bool().unwrap_or(false) { rgb.map(|v| (v * 5.).round() / 5.) } else { rgb };
    let max = rgb.into_iter().fold(0f32, f32::max);
    let min = rgb.into_iter().fold(1f32, f32::min);
    let d = max - min;
    let hue = if d < 1e-7 {
        0.
    } else if max == rgb[0] {
        60. * ((rgb[1] - rgb[2]) / d).rem_euclid(6.)
    } else if max == rgb[1] {
        60. * ((rgb[2] - rgb[0]) / d + 2.)
    } else {
        60. * ((rgb[0] - rgb[1]) / d + 4.)
    };
    let hsb = [hue, if max == 0. { 0. } else { d / max }, max];
    let cmyk = rgb_to_cmyk(rgb);
    let printable = cmyk_to_rgb(cmyk).map(|v| v.clamp(0., 1.));
    let lab = srgb_to_lab(rgb);
    let back = srgb_to_lab(printable);
    let delta = lab.iter().zip(back).map(|(a, b)| (a - b).powi(2)).sum::<f32>().sqrt();
    Ok(json!({"rgb":rgb.map(|v|v*255.),"rgba":[rgb[0],rgb[1],rgb[2],1.],"hsb":[hsb[0],hsb[1]*100.,hsb[2]*100.],
        "lab":lab,"cmyk":cmyk.map(|v|v*100.),"hex":hex(rgb),"outOfGamut":delta>2.,"printable":hex(printable)})
    .to_string())
}

/// Exact composite histogram; tile-sized buffers avoid a second whole-image allocation.
pub fn histogram(doc: &Document, encode: bool) -> Result<String, String> {
    let names = photocraft_engine::channel_cmds::color_names(doc.mode);
    let mut bins = vec![vec![0u64; 256]; names.len() + 1];
    let profile = photocraft_compose::cmyk_space(doc);
    let mut samples = 0u64;
    photocraft_color::convert::with_cmyk_space(profile.as_ref(), || {
        for y in (0..doc.size.height as i32).step_by(128) {
            for x in (0..doc.size.width as i32).step_by(128) {
                let r = Rect::new(x, y, (x + 128).min(doc.size.width as i32), (y + 128).min(doc.size.height as i32));
                let mut buffer = photocraft_compose::render(doc, r);
                if encode {
                    encode_srgb(&mut buffer.px);
                }
                for p in buffer.px {
                    if p[3] <= 0. || p.iter().any(|v| !v.is_finite()) {
                        continue;
                    }
                    samples += 1;
                    let luminance = 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
                    let values = match doc.mode {
                        photocraft_color::ColorMode::Cmyk => rgb_to_cmyk([p[0], p[1], p[2]]).to_vec(),
                        photocraft_color::ColorMode::Lab => {
                            let lab = srgb_to_lab([p[0], p[1], p[2]]);
                            vec![lab[0] / 100., (lab[1] + 128.) / 255., (lab[2] + 128.) / 255.]
                        }
                        photocraft_color::ColorMode::Grayscale => vec![luminance],
                        _ => vec![p[0], p[1], p[2]],
                    };
                    for (i, v) in values.into_iter().chain(std::iter::once(luminance)).enumerate() {
                        bins[i][(v.clamp(0., 1.) * 255.).round() as usize] += 1;
                    }
                }
            }
        }
    });
    Ok(json!({"channels":bins,"channelNames":names,"mode":photocraft_engine::channel_cmds::composite_name(doc.mode),"samples":samples,"sampled":false})
        .to_string())
}

impl Bridge {
    /// The upstream brush accepts history indices. A scratch session exposes the named snapshot
    /// at index zero, runs that same brush, then commits its result as one real undoable edit.
    pub(crate) fn paint_snapshot(&mut self, params: &Value) -> Result<Value, String> {
        let id = params["snapshot"].as_u64().ok_or("invalid snapshot id")?;
        let st = self.session.active().ok_or("no document")?;
        let source = st.snapshots.iter().find(|v| v.id == id).ok_or("snapshot no longer exists")?.doc.clone();
        let current = st.doc.clone();
        let active = st.active_layer;
        let view = st.channel_view.clone();
        let mut scratch = photocraft_engine::Session::new();
        scratch.add_document((*source).clone(), None);
        scratch.tools = self.session.tools.clone();
        scratch.patterns = self.session.patterns.clone();
        scratch
            .edit("Current", |doc, target| {
                *doc = (*current).clone();
                *target = active;
                Ok(())
            })
            .map_err(|e| e.to_string())?;
        if let Some(st) = scratch.active_mut() {
            st.channel_view = view;
        }
        let mut p = params.clone();
        p["state"] = json!(0);
        if let Some(p) = p.as_object_mut() {
            p.remove("snapshot");
        }
        let result = scratch.execute("paint.historyBrush", p).map_err(|e| e.to_string())?;
        let painted = scratch.active().ok_or("scratch document missing")?.doc.clone();
        self.session
            .edit("History Brush", |doc, _| {
                *doc = (*painted).clone();
                Ok(())
            })
            .map_err(|e| e.to_string())?;
        Ok(result)
    }
    /// Export presets through the existing Photoshop GRD writer.
    pub fn export_gradients(&self, params: &str) -> Result<String, String> {
        use photocraft_engine::presets::gradients::StopColor;
        use photocraft_psd::grd::{GrdColor, GrdGradient, GrdStop};
        let p: Value = serde_json::from_str(params).map_err(|e| e.to_string())?;
        let path = p["path"].as_str().filter(|v| !v.is_empty()).ok_or("missing export path")?;
        let group = p["group"].as_str();
        let gradients: Vec<_> = self
            .session
            .presets
            .gradients
            .iter()
            .filter(|g| group.is_none_or(|n| g.name == n))
            .flat_map(|g| g.items.iter())
            .map(|g| GrdGradient {
                name: g.name.clone(),
                noise: false,
                stops: g
                    .stops
                    .iter()
                    .map(|(t, c)| GrdStop {
                        location: *t,
                        midpoint: 0.5,
                        color: match c {
                            StopColor::Foreground => GrdColor::Foreground,
                            StopColor::Background => GrdColor::Background,
                            StopColor::Rgb(c) => GrdColor::Rgb(*c),
                        },
                    })
                    .collect(),
                opacity: g.opacity.iter().map(|(t, v)| (*t, *v, 0.5)).collect(),
            })
            .collect();
        if gradients.is_empty() {
            return Err("no gradients in the selected group".into());
        }
        let bytes = photocraft_psd::grd::write(&gradients);
        photocraft_format::atomic_write(std::path::Path::new(path), &bytes).map_err(|e| e.to_string())?;
        Ok(json!({"count":gradients.len(),"bytes":bytes.len()}).to_string())
    }
    pub fn panel_thumb(&self, params: &str, side: u32, out: &mut [u8], stride: usize) -> Result<(), String> {
        if !(1..=256).contains(&side) {
            return Err("thumbnail side must be 1..256".into());
        }
        let p: Value = serde_json::from_str(params).map_err(|e| e.to_string())?;
        let st = self.session.active();
        let mut doc = st.map(|s| (*s.doc).clone()).unwrap_or_else(|| {
            Document::new("Preview", photocraft_geom::Size::new(side, side), photocraft_color::ColorMode::Rgb, photocraft_color::SampleType::U8)
        });
        let mut component = None;
        if p["kind"] == "pattern" {
            let id = p["id"].as_str().ok_or("missing pattern id")?;
            let pat = self.session.patterns.items.iter().find(|v| v.id == id).ok_or("pattern missing")?;
            doc.size.width = pat.width;
            doc.size.height = pat.height;
            doc.layers = vec![Layer::new("Pattern", LayerContent::Raster(pat.surface.clone()))];
        } else if let Some(i) = p["alpha"].as_u64() {
            let ch = doc.channels.get(i as usize).ok_or("channel missing")?;
            doc.layers = vec![Layer::new("Channel", LayerContent::Raster(ch.surface.clone()))];
        } else {
            component = p["component"].as_u64();
        }
        let mut buffer = photocraft_compose::thumbnail_buffer(&doc, side);
        if self.canvas_encodes_srgb(&doc) {
            encode_srgb(&mut buffer.px);
        }
        if let Some(k) = component {
            for px in &mut buffer.px {
                let a = px[3].clamp(0., 1.);
                let rgb = [px[0] * a + 1. - a, px[1] * a + 1. - a, px[2] * a + 1. - a];
                let k = k as usize;
                let v = match doc.mode {
                    photocraft_color::ColorMode::Cmyk => 1. - rgb_to_cmyk(rgb)[k.min(3)],
                    photocraft_color::ColorMode::Lab => {
                        let l = srgb_to_lab(rgb);
                        if k == 0 { l[0] / 100. } else { (l[k.min(2)] + 128.) / 255. }
                    }
                    _ => rgb[k.min(2)],
                };
                *px = [v, v, v, 1.];
            }
        }
        let w = buffer.rect.width() as usize;
        let h = buffer.rect.height() as usize;
        let mut square = vec![[0.; 4]; side as usize * side as usize];
        let ox = (side as usize - w) / 2;
        let oy = (side as usize - h) / 2;
        for y in 0..h {
            square[(y + oy) * side as usize + ox..(y + oy) * side as usize + ox + w].copy_from_slice(&buffer.px[y * w..(y + 1) * w]);
        }
        write_bgra(&square, side as usize, side as usize, out, stride)
    }
}

#[cfg(test)]
#[allow(clippy::unwrap_used)]
mod tests {
    use super::*;
    #[test]
    fn snapshot_brush_is_one_real_undo_step_and_grd_roundtrips() {
        let mut b = Bridge::new();
        b.execute("file.new", r##"{"width":32,"height":32,"background":"#ff0000"}"##).unwrap();
        let snap: Value = serde_json::from_str(&b.execute("history.newSnapshot", "{}").unwrap()).unwrap();
        b.execute("edit.fill", r##"{"color":"#0000ff"}"##).unwrap();
        let before = b.history_len();
        b.execute("paint.historyBrush", &json!({"snapshot":snap["snapshot"],"points":[[16,16,1]],"size":12,"hardness":100}).to_string()).unwrap();
        assert_eq!(b.history_len(), before + 1);
        let doc = b.snapshot().unwrap();
        let p = photocraft_compose::render(&doc, Rect::new(16, 16, 17, 17)).px[0];
        assert!(p[0] > 0.9 && p[2] < 0.1);
        assert!(b.undo());
        assert!(b.redo());
        assert!(b.execute("paint.historyBrush", r#"{"snapshot":9999}"#).is_err());
        let path = std::env::temp_dir().join(format!("vetro-gradients-{}.grd", std::process::id()));
        b.export_gradients(&json!({"path":path}).to_string()).unwrap();
        let data = std::fs::read(&path).unwrap();
        assert!(!photocraft_psd::grd::parse(&data).unwrap().is_empty());
        std::fs::remove_file(path).unwrap();
        assert!(b.export_gradients("{}").is_err());
    }
    #[test]
    fn picker_models_web_palette_and_invalid_input() {
        for model in [r#"{"model":"rgb","values":[255,0,0]}"#, r#"{"model":"hsb","values":[0,100,100]}"#] {
            let v: Value = serde_json::from_str(&color_convert(model).unwrap()).unwrap();
            assert_eq!(v["hex"], "#ff0000");
            assert!(v["outOfGamut"].as_bool().unwrap());
        }
        let v: Value = serde_json::from_str(&color_convert(r#"{"values":[123,133,12],"webOnly":true}"#).unwrap()).unwrap();
        assert_eq!(v["hex"], "#669900");
        for p in ["{}", r#"{"model":"bad","values":[0,0,0]}"#, r#"{"values":[null,1,2]}"#] {
            assert!(color_convert(p).is_err());
        }
    }
    #[test]
    fn histogram_counts_pixels_and_thumbnail_is_bounded() {
        let mut b = Bridge::new();
        b.execute("file.new", r##"{"width":32,"height":16,"background":"#ff0000"}"##).unwrap();
        let doc = b.snapshot().unwrap();
        let v: Value = serde_json::from_str(&histogram(&doc, false).unwrap()).unwrap();
        assert_eq!(v["samples"], 512);
        assert_eq!(v["channels"][0][255], 512);
        assert!(b.panel_thumb("{}", 257, &mut [], 0).is_err());
        let mut px = vec![0; 64 * 64 * 4];
        b.panel_thumb("{}", 64, &mut px, 256).unwrap();
        assert!(px.iter().any(|v| *v != 0));
    }
}
