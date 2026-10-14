//! Read-only views for native tool panels. All mutations still use the command registry.
use crate::{Bridge, write_bgra};
use photocraft_doc::{LayerContent, LayerId};
use photocraft_geom::Rect;
use serde_json::{Value, json};

impl Bridge {
    pub fn clone_preview(&self, params: &str, rect: Rect, out: &mut [u8], stride: usize) -> Result<(), String> {
        let params: Value = serde_json::from_str(params).map_err(|e| e.to_string())?;
        let at = params.get("at").and_then(Value::as_array).ok_or("no preview point")?;
        let first = (at.first().and_then(Value::as_f64).ok_or("invalid point")?, at.get(1).and_then(Value::as_f64).ok_or("invalid point")?);
        let (map, _) = photocraft_engine::presets::clone_source::resolve(&self.session, &params, first, "clone.preview").map_err(|e| e.to_string())?;
        let sample = params.get("sampleLayer").and_then(Value::as_str).unwrap_or("current");
        let mut buffer = photocraft_engine::retouch_cmds::clone_preview(&self.session, rect, &map, sample).map_err(|e| e.to_string())?;
        let blend = params.get("blend").and_then(Value::as_str).unwrap_or("normal");
        if blend != "normal" {
            let doc = self.snapshot().ok_or("no document")?;
            let below = photocraft_compose::render(&doc, rect);
            for (src, dst) in buffer.px.iter_mut().zip(below.px) {
                *src = photocraft_engine::presets::clone_source::overlay_blend(blend, *src, dst);
            }
        }
        write_bgra(&buffer.px, rect.width() as usize, rect.height() as usize, out, stride)
    }

    pub fn adjustment_auto(&self, id: u64) -> Result<String, String> {
        let source = self.snapshot().ok_or("no document")?;
        let mut doc = (*source).clone();
        let layer = doc.layer_mut(LayerId(id)).ok_or("no layer")?;
        let curves = matches!(layer.content, LayerContent::Adjustment(photocraft_doc::Adjustment::Curves { .. }));
        if !curves && !matches!(layer.content, LayerContent::Adjustment(photocraft_doc::Adjustment::Levels { .. })) {
            return Err("Auto requires Levels or Curves".into());
        }
        fn below(layers: &mut [photocraft_doc::Layer], id: LayerId, above: &mut bool) {
            for layer in layers {
                if *above {
                    layer.visible = false;
                } else if layer.id == id {
                    layer.visible = false;
                    *above = true;
                } else if let LayerContent::Group(g) = &mut layer.content {
                    below(&mut g.children, id, above);
                }
            }
        }
        below(&mut doc.layers, LayerId(id), &mut false);
        let buffer = photocraft_compose::thumbnail_buffer(&doc, 256);
        let mut hist = [[0u32; 256]; 4];
        let mut count = 0u32;
        for pixel in buffer.px.iter().filter(|p| p[3] > 0.01) {
            let mut native = [0.0; 16];
            photocraft_raster::from_rgba_into(&doc.pixel_format(), *pixel, &mut native);
            for (h, v) in hist.iter_mut().zip(native.iter().take(doc.mode.color_channels())) {
                let index = (v.clamp(0.0, 1.0) * 255.0).round() as usize;
                if let Some(bin) = h.get_mut(index) {
                    *bin += 1;
                }
            }
            count += 1;
        }
        if count == 0 {
            return Err("no visible pixels to analyze".into());
        }
        let mut out = json!({"layer":id});
        let keys: &[&str] = match doc.mode {
            photocraft_color::ColorMode::Grayscale => &["gray"],
            photocraft_color::ColorMode::Cmyk => &["cyan", "magenta", "yellow", "black"],
            photocraft_color::ColorMode::Lab => &["lightness", "a", "b"],
            _ => &["red", "green", "blue"],
        };
        for (key, h) in keys.iter().zip(hist) {
            let clip = count / 200;
            let mut lo = 0;
            let mut hi = 255;
            let mut n = 0;
            for (i, v) in h.iter().enumerate() {
                n += v;
                if n > clip {
                    lo = i;
                    break;
                }
            }
            n = 0;
            for (i, v) in h.iter().enumerate().rev() {
                n += v;
                if n > clip {
                    hi = i;
                    break;
                }
            }
            if hi <= lo + 2 {
                lo = 0;
                hi = 255;
            }
            out[*key] = if curves { json!([[lo, 0], [hi, 255]]) } else { json!({"inBlack":lo,"inWhite":hi,"gamma":1.0}) };
        }
        Ok(out.to_string())
    }

    /// Caret stops and selection quadrilaterals from the exact engine layout, in canvas pixels.
    pub fn text_geometry(&self, id: u64) -> Result<String, String> {
        let doc = self.snapshot().ok_or("no document")?;
        let layer = doc.layer(LayerId(id)).ok_or("no layer")?;
        let LayerContent::Text(text) = &layer.content else { return Err("not a type layer".into()) };
        if text.text.chars().count() > 32768 {
            return Err("text overlay exceeds its budget".into());
        }
        let layout = photocraft_text::shared().lock().unwrap_or_else(|e| e.into_inner()).layout(text, doc.resolution_dpi);
        let warp = text.warp.as_ref().and_then(|w| layout.line_bounds().and_then(|b| photocraft_text::warp::Warp::new(w, b)));
        let map = |x: f32, y: f32| {
            let (x, y) = warp.as_ref().map_or((x as f64, y as f64), |w| w.apply(x as f64, y as f64));
            let p = text.transform.apply(photocraft_geom::Point::new(x, y));
            json!([p.x, p.y])
        };
        let stops = text.text.char_indices().map(|(i, _)| i).chain(std::iter::once(text.text.len()));
        let carets: Vec<Value> = stops
            .map(|i| {
                let [a, b] = layout.caret_segment(i);
                json!([map(a.0, a.1), map(b.0, b.1)])
            })
            .collect();
        let clusters:Vec<Value>=layout.clusters.iter().filter_map(|c|{
            let line=layout.lines.get(c.line)?;
            let points=[(c.x,line.baseline-line.ascent),(c.x+c.advance,line.baseline-line.ascent),(c.x+c.advance,line.baseline+line.descent),(c.x,line.baseline+line.descent)];
            let poly:Vec<Value>=points.into_iter().map(|(x,y)|{let (x,y)=layout.to_text(x,y);map(x,y)}).collect();
            Some(json!({"start":photocraft_text::char_index(&text.text,c.range.start),"end":photocraft_text::char_index(&text.text,c.range.end),"poly":poly}))
        }).collect();
        let mut at = 0usize;
        let colors:Vec<Value>=text.char_runs().iter().map(|run|{let start=at;at+=run.len;let rgb=run.style.color.to_rgb();json!({"start":photocraft_text::char_index(&text.text,start),"end":photocraft_text::char_index(&text.text,at),"color":[rgb[0],rgb[1],rgb[2],run.style.color.alpha]})}).collect();
        Ok(json!({"carets":carets,"clusters":clusters,"colors":colors}).to_string())
    }
}

pub fn font_preview(family: &str, width: u32, height: u32, out: &mut [u8], stride: usize) -> Result<(), String> {
    if family.len() > 512 || !(32..=512).contains(&width) || !(16..=128).contains(&height) {
        return Err("invalid font preview size".into());
    }
    let layer = photocraft_doc::TextLayer {
        text: "Aa Бб 123".into(),
        font_family: family.into(),
        size_pt: 18.0,
        color: photocraft_color::Color::WHITE,
        transform: photocraft_geom::Affine::translate(0.0, 24.0),
        ..Default::default()
    };
    let (_, rendered) = photocraft_text::shared().lock().unwrap_or_else(|e| e.into_inner()).render(&layer, 72.0, photocraft_color::PixelFormat::RGBA8);
    let mut pixels = vec![[0.0; 4]; width as usize * height as usize];
    rendered.surface.read_rgba_into(Rect::new(0, 0, width as i32, height as i32), &mut pixels);
    write_bgra(&pixels, width as usize, height as usize, out, stride)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn live_clone_uses_the_retouch_renderer_and_commits_or_cancels_once() {
        for depth in [8, 16, 32] {
            let mut b = Bridge::new();
            b.execute("file.new", &json!({"width":100,"height":80,"depth":depth}).to_string()).unwrap();
            b.execute("tools.setColors", r##"{"foreground":"#ff0000"}"##).unwrap();
            b.execute("paint.stroke", r#"{"points":[[20,20,1],[25,20,1]],"size":20,"hardness":1}"#).unwrap();
            b.execute("cloneSource.set", r#"{"source":[20,20]}"#).unwrap();
            let history = b.history_len();
            b.stroke_begin("paint.cloneStamp", r#"{"points":[[70,40,1]],"size":10,"hardness":100,"opacity":100}"#).unwrap();
            assert!(crate::sample(&b.snapshot().unwrap(), 70, 40)[0] > 0.9);
            b.stroke_push(&[[75.0, 40.0, 1.0, 16.0]]).unwrap();
            let result: Value = serde_json::from_str(&b.stroke_end(true).unwrap()).unwrap();
            assert!(result["fromLive"].as_bool().unwrap());
            assert_eq!(b.history_len(), history + 1);
            b.stroke_begin("paint.cloneStamp", r#"{"points":[[60,60,1]],"size":10}"#).unwrap();
            b.stroke_end(false).unwrap();
            assert_eq!(b.history_len(), history + 1);
            b.stroke_begin("paint.healingBrush", r#"{"points":[[60,60,1]],"size":10}"#).unwrap();
            b.stroke_end(true).unwrap();
            assert_eq!(b.history_len(), history + 2);
        }
    }
    #[test]
    fn geometry_uses_scalar_indices_and_the_real_transform_without_history() {
        let mut b = Bridge::new();
        b.execute("file.new", r#"{"width":200,"height":160}"#).unwrap();
        let result: Value = serde_json::from_str(&b.execute("type.create", r#"{"x":20,"y":60,"text":"A🙂Б\ncd","font":"Inter","size":20}"#).unwrap()).unwrap();
        let id = result["layer"].as_u64().unwrap();
        let history = b.history_len();
        let geometry: Value = serde_json::from_str(&b.text_geometry(id).unwrap()).unwrap();
        assert_eq!(geometry["carets"].as_array().unwrap().len(), 7);
        assert!(geometry["carets"][0][0][0].as_f64().unwrap() >= 19.0);
        assert!(geometry["clusters"].as_array().unwrap().iter().any(|c| c["end"] == 3));
        assert_eq!(b.history_len(), history);
        assert!(b.text_geometry(u64::MAX).is_err());
        let state = b.session.active_mut().unwrap();
        let layer = std::sync::Arc::make_mut(&mut state.doc).layer_mut(LayerId(id)).unwrap();
        if let LayerContent::Text(t) = &mut layer.content {
            t.text = "a".repeat(32769);
        }
        assert!(b.text_geometry(id).is_err());
    }
    #[test]
    fn actual_font_and_tip_previews_are_bounded_and_distinct() {
        let mut a = vec![0; 192 * 32 * 4];
        let mut b = a.clone();
        font_preview("Inter", 192, 32, &mut a, 192 * 4).unwrap();
        font_preview("JetBrains Mono", 192, 32, &mut b, 192 * 4).unwrap();
        assert!(a.chunks_exact(4).any(|p| p[3] > 0));
        assert_ne!(a, b);
        assert!(font_preview("Inter", u32::MAX, 32, &mut a, 192 * 4).is_err());
        assert!(font_preview("Inter", 192, 32, &mut a[..3], 192 * 4).is_err());
        let settings = photocraft_paint::brush::BrushSettings::default();
        let mut tip = vec![0; 48 * 48 * 4];
        crate::brush_tip(&serde_json::to_string(&settings).unwrap(), 48, &mut tip, 48 * 4).unwrap();
        assert!(tip.chunks_exact(4).any(|p| p[3] > 0));
        assert!(crate::brush_tip("{}", u32::MAX, &mut tip, 48 * 4).is_err());
    }
    #[test]
    fn auto_uses_only_pixels_below_the_adjustment_and_preserves_history() {
        for mode in ["rgb", "gray", "cmyk", "lab"] {
            let mut b = Bridge::new();
            b.execute("file.new", &json!({"width":32,"height":16,"mode":mode,"background":"transparent"}).to_string()).unwrap();
            b.execute("shape.create", r##"{"kind":"rect","rect":[0,0,16,16],"fill":"#404040"}"##).unwrap();
            b.execute("shape.create", r##"{"kind":"rect","rect":[16,0,16,16],"fill":"#c0c0c0"}"##).unwrap();
            let result: Value = serde_json::from_str(&b.execute("layer.newAdjustmentLayer.levels", "{}").unwrap()).unwrap();
            let id = result["layer"].as_u64().unwrap();
            let before = b.adjustment_auto(id).unwrap();
            b.execute("shape.create", r##"{"kind":"rect","rect":[0,0,32,16],"fill":"#ff0000"}"##).unwrap();
            let history = b.history_len();
            assert_eq!(b.adjustment_auto(id).unwrap(), before, "above layer affected Auto in {mode}");
            assert_eq!(b.history_len(), history);
            b.execute("layer.setAdjustment", &before).unwrap();
        }
    }
    #[test]
    fn disabled_mask_preview_and_gray_view_preserve_the_source() {
        let mut b = Bridge::new();
        b.execute("file.new", r#"{"width":16,"height":16}"#).unwrap();
        let id = b.session.active().unwrap().active_layer.unwrap().0;
        b.execute("layer.layerMask.hideAll", "{}").unwrap();
        b.execute("layer.layerMask.enabled", r#"{"enabled":false}"#).unwrap();
        let source = b.snapshot().unwrap();
        let history = b.history_len();
        let mut pixels = vec![255; 16 * 16 * 4];
        crate::mask_thumb(&source, id, 0, 16, &mut pixels, 64).unwrap();
        assert_eq!(&pixels[..4], &[0, 0, 0, 255]);
        b.execute("view.layerMask", r#"{"mode":"gray"}"#).unwrap();
        assert_eq!(crate::sample(&b.display_snapshot().unwrap(), 8, 8), [0.0, 0.0, 0.0, 1.0]);
        assert_eq!(crate::sample(&b.snapshot().unwrap(), 8, 8), crate::sample(&source, 8, 8));
        assert_eq!(b.history_len(), history);
    }
    #[test]
    fn pencil_curve_inspector_fits_points_without_mutating_the_map() {
        let mut b = Bridge::new();
        b.execute("file.new", r#"{"width":16,"height":16}"#).unwrap();
        let map: Vec<u32> = (0..256).rev().collect();
        b.execute("layer.newAdjustmentLayer.curves", &json!({"pencil":true,"points":map}).to_string()).unwrap();
        let history = b.history_len();
        let info: Value = serde_json::from_str(&b.document_info()).unwrap();
        let layer = &info["layers"][0];
        assert_eq!(layer["adjustment"]["points"].as_array().unwrap().len(), 256);
        assert_eq!(layer["adjustment"]["pencil"], true);
        let points = layer["curvePoints"]["points"].as_array().unwrap();
        assert!(points.len() >= 2 && points.len() <= 19);
        assert_eq!(points.first().unwrap(), &json!([0.0, 255.0]));
        assert_eq!(points.last().unwrap(), &json!([255.0, 0.0]));
        assert_eq!(b.history_len(), history);
    }
}
