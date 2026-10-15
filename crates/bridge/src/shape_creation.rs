//! Shape-tool output adapters. Geometry and rasterization come from the existing shape command.
use crate::Bridge;
use photocraft_doc::{LayerContent, LayerId};
use photocraft_engine::{EngineError, Session};
use serde_json::{Value, json};

impl Bridge {
    pub(crate) fn create_shape_output(&mut self, params: &Value) -> Result<Value, String> {
        let mode = params["creationMode"].as_str().ok_or("creationMode must be shape|path|pixels")?;
        if mode == "shape" {
            return self.session.execute("shape.create", params.clone()).map_err(|e| e.to_string());
        }
        if !matches!(mode, "path" | "pixels") {
            return Err("creationMode must be shape|path|pixels".into());
        }
        let state = self.session.active().ok_or("no document")?;
        let active = state.active_layer;
        if mode == "pixels" {
            let id = active.ok_or("select a pixel layer")?;
            let locks = state.doc.effective_locks(id);
            if locks.all || locks.pixels {
                return Err("pixel layer is locked".into());
            }
            if !matches!(state.doc.layer(id).map(|l| &l.content), Some(LayerContent::Raster(_))) {
                return Err("Pixels mode needs a pixel layer".into());
            }
            if state.doc.quick_mask.is_some() || state.channel_view.target != photocraft_engine::channel_cmds::ChannelTarget::Composite {
                return Err("Pixels mode needs the composite pixel target".into());
            }
        }
        let mut scratch = Session::new();
        scratch.tools = self.session.tools.clone();
        scratch.add_document((*state.doc).clone(), None);
        let mut p = params.clone();
        if let Some(p) = p.as_object_mut() {
            p.remove("addTo");
            p.remove("creationMode");
        }
        let shape = scratch.execute("shape.create", p).map_err(|e| e.to_string())?;
        if mode == "path" {
            let result = self.session.execute("path.set", json!({"name":"work","path":shape["path"]})).map_err(|e| e.to_string())?;
            return Ok(json!({"creationMode":"path","path":result,"target":"work"}));
        }
        let id = active.ok_or("select a pixel layer")?;
        let generated = LayerId(shape["layer"].as_u64().ok_or("shape has no layer")?);
        let source = scratch
            .active()
            .and_then(|s| s.doc.layer(generated))
            .and_then(|l| match &l.content {
                LayerContent::Shape(s) => s.cache.as_ref(),
                _ => None,
            })
            .ok_or("shape was not rendered")?
            .clone();
        self.session
            .edit("Draw Shape", move |doc, _| {
                let locks = doc.effective_locks(id);
                let bounds = source.content_bounds().intersect(&doc.bounds());
                let selection = doc.selection.clone();
                let layer = doc.layer_mut(id).ok_or(EngineError::NoLayer(id))?;
                let destination = layer.surface_mut().ok_or_else(|| EngineError::Other("Pixels mode needs a pixel layer".into()))?;
                if source.format() != destination.format() {
                    return Err(EngineError::Other("shape and target formats differ".into()));
                }
                let format = source.format();
                let n = source.channels();
                for tile in bounds.tiles() {
                    let r = tile.rect().intersect(&bounds);
                    let src = source.read_region(r);
                    let mut dst = destination.read_region(r);
                    let coverage = selection.as_ref().map(|s| s.read_region(r));
                    for (i, (s, d)) in src.chunks_exact(n).zip(dst.chunks_exact_mut(n)).enumerate() {
                        let sa = (if format.alpha { s[n - 1] } else { 1. }) * coverage.as_ref().map_or(1., |m| m[i]);
                        let da = if format.alpha { d[n - 1] } else { 1. };
                        if locks.transparency && da <= 0. {
                            continue;
                        }
                        let alpha = sa + da * (1. - sa);
                        for c in 0..format.mode.color_channels() {
                            d[c] = if locks.transparency {
                                s[c] * sa + d[c] * (1. - sa)
                            } else if alpha > 0. {
                                (s[c] * sa + d[c] * da * (1. - sa)) / alpha
                            } else {
                                0.
                            };
                        }
                        if format.alpha && !locks.transparency {
                            d[n - 1] = alpha;
                        }
                    }
                    destination.write_region(r, &dst);
                }
                Ok(())
            })
            .map_err(|e| e.to_string())?;
        Ok(json!({"layer":id.0,"creationMode":"pixels"}))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn path_and_pixels_use_the_shape_geometry_without_extra_layers_or_undo_steps() {
        for depth in [8, 16, 32] {
            let mut b = Bridge::new();
            b.execute("file.new", &json!({"width":64,"height":48,"depth":depth,"background":"transparent"}).to_string()).unwrap();
            let count = b.snapshot().unwrap().layers.len();
            let history = b.history_len();
            b.execute("shape.create", r##"{"kind":"ellipse","rect":[10,10,30,20],"creationMode":"path"}"##).unwrap();
            assert_eq!(b.snapshot().unwrap().layers.len(), count);
            assert!(b.snapshot().unwrap().work_path.is_some());
            assert_eq!(b.history_len(), history + 1);
            b.undo();
            assert!(b.snapshot().unwrap().work_path.is_none());
            b.execute("shape.create", r##"{"kind":"rect","rect":[10,10,30,20],"fill":"#ff0000","creationMode":"pixels"}"##).unwrap();
            assert_eq!(b.snapshot().unwrap().layers.len(), count);
            let sample = crate::sample(&b.snapshot().unwrap(), 20, 20);
            assert!(sample[0] > 0.99 && sample[1] < 0.01 && sample[3] > 0.99);
            assert_eq!(b.history_len(), history + 1);
            b.undo();
            assert!(crate::sample(&b.snapshot().unwrap(), 20, 20)[3] < 0.01);
            for mode in [json!("bad"), json!(42), Value::Null] {
                assert!(b.execute("shape.create", &json!({"creationMode":mode}).to_string()).is_err());
            }
        }
    }
    #[test]
    fn pixel_shapes_respect_selection_and_transparency_and_pixel_locks() {
        let mut b = Bridge::new();
        b.execute("file.new", r#"{"width":32,"height":24,"background":"transparent"}"#).unwrap();
        b.execute("select.rect", r#"{"x":0,"y":0,"width":16,"height":24}"#).unwrap();
        let shape = r##"{"kind":"rect","rect":[0,0,32,24],"fill":"#ff0000","creationMode":"pixels"}"##;
        b.execute("shape.create", shape).unwrap();
        assert!(crate::sample(&b.snapshot().unwrap(), 24, 12)[3] < 0.01);
        b.execute("select.deselect", "{}").unwrap();
        b.execute("layer.lockLayers", r#"{"transparency":true}"#).unwrap();
        b.execute("shape.create", shape).unwrap();
        assert!(crate::sample(&b.snapshot().unwrap(), 24, 12)[3] < 0.01);
        b.execute("layer.lockLayers", r#"{"pixels":true}"#).unwrap();
        let history = b.history_len();
        assert!(b.execute("shape.create", shape).is_err());
        assert_eq!(b.history_len(), history);
    }
}
