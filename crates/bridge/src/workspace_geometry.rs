//! Read-only geometry for the native canvas editors, always against their frozen source.
use photocraft_doc::{Document, LayerId};
use serde_json::{Value, json};

pub fn puppet(doc: &Document, p: &Value) -> Result<Value, String> {
    let id = p.get("layer").and_then(Value::as_u64).ok_or("missing layer")?;
    let src = doc.layer(LayerId(id)).and_then(|l| l.surface()).ok_or("Puppet Warp needs a pixel layer")?;
    let warp = photocraft_engine::distort_cmds::puppet_params("edit.puppetWarp", p).map_err(|e| e.to_string())?;
    if warp.pins.len() > 256 {
        return Err("at most 256 pins".into());
    }
    let (solver, positions, _) = photocraft_algo::puppet::deform(src, src.content_bounds(), &warp, photocraft_algo::puppet::ITERATIONS);
    Ok(json!({"vertices": positions, "rest":solver.mesh.verts, "triangles":solver.mesh.tris}))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::Bridge;
    #[test]
    fn alpha_mesh_is_read_only_and_single_pin_translates_it() {
        let mut b = Bridge::new();
        b.execute("file.new", r#"{"width":80,"height":60,"background":"transparent"}"#).unwrap();
        b.execute("shape.create", r##"{"kind":"ellipse","rect":[10,10,30,30],"fill":"#ff0000","creationMode":"pixels"}"##).unwrap();
        let doc = b.snapshot().unwrap();
        let layer = b.session.active().unwrap().active_layer.unwrap().0;
        let result = puppet(&doc, &json!({"layer":layer,"expansion":0,"pins":[{"src":[25,25],"dst":[30,27]}]})).unwrap();
        assert!(!result["triangles"].as_array().unwrap().is_empty());
        for (at, rest) in result["vertices"].as_array().unwrap().iter().zip(result["rest"].as_array().unwrap()) {
            assert!((at[0].as_f64().unwrap() - rest[0].as_f64().unwrap() - 5.).abs() < 0.1);
        }
        assert!(puppet(&doc, &json!({"layer":0})).is_err());
        assert!(std::sync::Arc::ptr_eq(&doc, &b.snapshot().unwrap()));
    }
}
