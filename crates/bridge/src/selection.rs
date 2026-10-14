//! Native-shell adapter for the Wand's averaged seed. The engine's region and history code
//! remain authoritative; only a temporary sampling image receives the averaged seed colour.
use photocraft_algo::selection::{self, SelectionMode};
use photocraft_engine::mask_view_cmds::MaskViewMode;
use photocraft_engine::sample_cmds::{SAMPLE_SIZES, SampleLayers, sample_color, sample_rect};
use serde_json::{Value, json};

use crate::Bridge;

/// The largest document (pixels) an averaged Wand sample reads: every size the CPU path
/// composites (120 Mpx), so a larger sample size never fails on a document a point sample opens.
const MAX_SAMPLING_PIXELS: usize = 130_000_000;

impl Bridge {
    /// `gradient.fill.create` with a `name` (the shell's interface-language layer name, which the
    /// engine command has no parameter for): the same layer, inserted in one history step.
    pub(crate) fn named_gradient_fill(&mut self, p: &Value) -> Result<Value, String> {
        let name = p.get("name").and_then(Value::as_str).filter(|n| !n.is_empty() && n.len() <= 255).ok_or("invalid layer name")?;
        let mut layer = {
            let d = self.session.active().ok_or("no document open")?;
            photocraft_engine::gradient_fill_cmds::new_layer(&self.session, &d.doc, p).map_err(|e| e.to_string())?
        };
        layer.name = name.to_string();
        let id = self
            .session
            .edit("New Gradient Fill Layer", |doc, active| {
                let id = doc.insert_above(*active, layer);
                *active = Some(id);
                Ok(id)
            })
            .map_err(|e| e.to_string())?;
        Ok(json!({"layer": id.0}))
    }

    pub(crate) fn sampled_wand(&mut self, p: &Value) -> Result<Value, String> {
        let size = p
            .get("sampleSize")
            .and_then(Value::as_u64)
            .and_then(|n| u32::try_from(n).ok())
            .filter(|n| SAMPLE_SIZES.contains(n))
            .ok_or("invalid Wand sample size")?;
        if size == 1 {
            return self.session.execute("select.magicWand", p.clone()).map_err(|e| e.to_string());
        }
        let x = p.get("x").and_then(Value::as_f64).filter(|n| n.is_finite()).ok_or("invalid Wand x coordinate")?;
        let y = p.get("y").and_then(Value::as_f64).filter(|n| n.is_finite()).ok_or("invalid Wand y coordinate")?;
        let d = self.session.active().ok_or("no document open")?;
        let doc = &d.doc;
        let area = doc.bounds();
        let count =
            (area.width() as usize).checked_mul(area.height() as usize).filter(|n| *n <= MAX_SAMPLING_PIXELS).ok_or("Wand sampling image is too large")?;
        let view = &d.channel_view;
        let fmt = doc.pixel_format();
        let colors_off = view.visible_colors(fmt.channels().saturating_sub(usize::from(fmt.alpha))) == 0;
        let gray = match view.shown_layer_mask(doc) {
            Some((mask, MaskViewMode::Gray)) => Some(&mask.surface),
            _ if colors_off => doc
                .channels
                .iter()
                .enumerate()
                .find(|(i, _)| view.alpha_shown(*i))
                .map(|(_, c)| &c.surface)
                .or_else(|| doc.quick_mask.as_ref().filter(|_| !view.quick_mask_hidden).map(|q| &q.surface))
                .or_else(|| view.shown_layer_mask(doc).map(|(m, _)| &m.surface))
                .or_else(|| d.active_layer.and_then(|id| doc.layer(id)).and_then(|l| l.mask.as_ref()).map(|m| &m.surface)),
            _ => None,
        };
        let all = p.get("sampleAllLayers").and_then(Value::as_bool).unwrap_or(false);
        let source = gray.or_else(|| (!all).then(|| d.active_layer.and_then(|id| doc.layer(id)).and_then(|l| l.surface())).flatten());
        let mut image = Vec::new();
        image.try_reserve_exact(count).map_err(|_| "not enough memory for Wand sampling")?;
        let byte = |v: f32| (v.clamp(0.0, 1.0) * 255.0 + 0.5) as u8;
        if let Some(src) = source {
            image.resize(count, [0; 4]);
            src.read_rgba8_into(area, &mut image);
        } else {
            image.extend(photocraft_compose::render(doc, area).px.into_iter().map(|v| v.map(byte)));
        }
        let sampled = if gray.is_some() {
            let r = sample_rect(x, y, size, area).ok_or("Wand seed is outside the canvas")?;
            let mut sums = [0u64; 4];
            let mut n = 0u64;
            for yy in r.y0..r.y1 {
                for xx in r.x0..r.x1 {
                    let i = (yy - area.y0) as usize * area.width() as usize + (xx - area.x0) as usize;
                    if let Some(pixel) = image.get(i) {
                        for (sum, v) in sums.iter_mut().zip(pixel) {
                            *sum += u64::from(*v);
                        }
                        n += 1;
                    }
                }
            }
            if n == 0 {
                return Err("Wand seed is outside the canvas".into());
            }
            sums.map(|sum| ((sum + n / 2) / n) as u8)
        } else {
            sample_color(doc, d.active_layer, x, y, size, if all || source.is_none() { SampleLayers::All } else { SampleLayers::Current }).map(byte)
        };
        let (sx, sy) = (x.floor() as i32, y.floor() as i32);
        if !area.contains(sx, sy) {
            return Err("Wand seed is outside the canvas".into());
        }
        let i = (sy - area.y0) as usize * area.width() as usize + (sx - area.x0) as usize;
        let seed = image.get_mut(i).ok_or("incomplete Wand sampling image")?;
        *seed = sampled;
        let tolerance = p.get("tolerance").and_then(Value::as_f64).unwrap_or(32.0).clamp(0.0, 255.0) as f32;
        let contiguous = p.get("contiguous").and_then(Value::as_bool).unwrap_or(true);
        let antialias = p.get("antiAlias").and_then(Value::as_bool).unwrap_or(true);
        let region = selection::wand_region(&image, area, (sx, sy), tolerance, contiguous, antialias);
        let mode = SelectionMode::parse(p.get("mode").and_then(Value::as_str).unwrap_or("replace"));
        let selected = self
            .session
            .edit("Magic Wand", |doc, _| {
                doc.selection = selection::combine_region(doc.selection.as_ref(), region.as_ref(), mode);
                Ok(doc.selection.is_some())
            })
            .map_err(|e| e.to_string())?;
        Ok(json!({"selected": selected}))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn averaged_seed_changes_selection_and_never_source_pixels_at_every_depth() {
        for depth in [8, 16, 32] {
            let mut b = Bridge::new();
            b.execute("file.new", &json!({"width":10,"height":10,"depth":depth}).to_string()).unwrap();
            b.execute("edit.fill", r##"{"color":"#888888"}"##).unwrap();
            b.execute("select.rect", r#"{"x":1,"y":1,"width":1,"height":1}"#).unwrap();
            b.execute("edit.fill", r##"{"color":"#000000"}"##).unwrap();
            b.execute("select.rect", r#"{"x":7,"y":7,"width":2,"height":2}"#).unwrap();
            b.execute("edit.fill", r##"{"color":"#797979"}"##).unwrap();
            b.execute("select.deselect", "{}").unwrap();
            let original = b.session.active().unwrap().doc.layers[0].surface().unwrap().rgba(1, 1);
            let p = json!({"x":1,"y":1,"sampleSize":3,"tolerance":0,"contiguous":false,"antiAlias":false});
            b.execute("select.magicWand", &p.to_string()).unwrap();
            let doc = &b.session.active().unwrap().doc;
            assert_eq!(doc.layers[0].surface().unwrap().rgba(1, 1), original);
            assert!(doc.selection.as_ref().unwrap().read_region(photocraft_geom::Rect::new(7, 7, 8, 8))[0] > 0.9);
            assert!(b.undo());
            assert!(b.session.active().unwrap().doc.selection.is_none());
        }
    }
    #[test]
    fn named_gradient_fill_is_one_history_step_with_the_shell_name() {
        for depth in [8, 16, 32] {
            let mut b = Bridge::new();
            b.execute("file.new", &json!({"width":20,"height":10,"depth":depth}).to_string()).unwrap();
            let history = b.history_len();
            let p = json!({"from":[1,1],"to":[18,8],"name":"Градиент 1"});
            b.execute("gradient.fill.create", &p.to_string()).unwrap();
            assert_eq!(b.history_len(), history + 1);
            let doc = &b.session.active().unwrap().doc;
            assert_eq!(doc.layers.len(), 2);
            assert!(doc.layers.iter().any(|l| l.name == "Градиент 1"));
            assert!(b.undo());
            assert_eq!(b.session.active().unwrap().doc.layers.len(), 1);
            for bad in [json!({"from":[1,1],"to":[18,8],"name":""}), json!({"from":[1,1],"to":[18,8],"name":5}), json!({"name":"x"})] {
                assert!(b.execute("gradient.fill.create", &bad.to_string()).is_err());
            }
            assert_eq!(b.session.active().unwrap().doc.layers.len(), 1);
        }
    }

    #[test]
    fn translated_adjustments_and_fills_keep_one_history_step_and_redo_names() {
        for depth in [8, 16, 32] {
            let mut b = Bridge::new();
            b.execute("file.new", &json!({"width":20,"height":10,"depth":depth}).to_string()).unwrap();
            for (cmd, name) in [
                ("layer.newAdjustmentLayer.curves", "Кривые 1"),
                ("layer.newFillLayer.solidColor", "Цветовая заливка 1"),
                ("layer.newFillLayer.gradient", "Градиент 1"),
            ] {
                let history = b.history_len();
                let result: Value = serde_json::from_str(&b.execute(cmd, &json!({"name":name}).to_string()).unwrap()).unwrap();
                let id = photocraft_doc::LayerId(result["layer"].as_u64().unwrap());
                assert_eq!(b.history_len(), history + 1);
                assert_eq!(b.session.active().unwrap().doc.layer(id).unwrap().name, name);
                assert!(b.undo());
                assert!(b.session.active().unwrap().doc.layer(id).is_none());
                assert!(b.redo());
                assert_eq!(b.session.active().unwrap().doc.layer(id).unwrap().name, name);
                for invalid in [json!({"name":""}), json!({"name":42})] {
                    assert!(b.execute(cmd, &invalid.to_string()).is_err());
                    assert_eq!(b.history_len(), history + 1);
                }
            }
        }
    }

    #[test]
    fn snap_geometry_uses_engine_bounds_preferences_and_selected_layer_exclusion() {
        let mut b = Bridge::new();
        b.execute("file.new", r#"{"width":200,"height":100,"resolution":100}"#).unwrap();
        b.execute("shape.create", r#"{"kind":"rect","rect":[20,30,40,20]}"#).unwrap();
        b.execute("view.newGuide", r#"{"orientation":"vertical","position":90}"#).unwrap();
        b.execute("prefs.set", r#"{"values":{"guidesGridAndSlices.gridlineEvery":40,"guidesGridAndSlices.gridUnit":"pixels","guidesGridAndSlices.subdivisions":4,"unitsAndRulers.rulers":"cm"}}"#).unwrap();
        let history = b.history_len();
        let info: Value = serde_json::from_str(&b.document_info()).unwrap();
        assert_eq!(info["guides"]["vertical"], json!([90.0]));
        assert!((info["viewPreferences"]["unitPixels"][0].as_f64().unwrap() - 100.0 / 2.54).abs() < 1e-6);
        let targets: Value = serde_json::from_str(&b.snap_targets(r#"{"grid":true,"excludeSelected":true}"#).unwrap()).unwrap();
        assert_eq!(targets["grid"], json!([10.0, 10.0]));
        let x = targets["x"].as_array().unwrap();
        assert!(x.iter().any(|t| t["kind"] == "guide" && t["pos"] == 90.0));
        assert!(!x.iter().any(|t| t["kind"] == "layerEdge" && (t["pos"] == 20.0 || t["pos"] == 60.0)));
        let targets: Value = serde_json::from_str(&b.snap_targets("{}").unwrap()).unwrap();
        assert!(targets["x"].as_array().unwrap().iter().any(|t| t["kind"] == "layerEdge" && t["pos"] == 20.0));
        assert_eq!(b.history_len(), history);
    }

    #[test]
    fn invalid_sampling_is_an_error_without_history_or_pixel_changes() {
        let mut b = Bridge::new();
        b.execute("file.new", r#"{"width":4,"height":4}"#).unwrap();
        let history = b.history_len();
        for p in [json!({"sampleSize":0}), json!({"sampleSize":u64::MAX}), json!({"sampleSize":"3"}), json!({"sampleSize":3,"x":999,"y":999})] {
            assert!(b.execute("select.magicWand", &p.to_string()).is_err());
            assert_eq!(b.history_len(), history);
        }
    }
}
