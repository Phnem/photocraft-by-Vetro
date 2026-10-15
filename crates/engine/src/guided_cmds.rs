//! Guided Upright: guide-derived homography, rendered by the existing perspective warp.
use crate::{EngineError, Result, Session, commands::CommandSpec};
use photocraft_algo::guided_upright::{Guide, apply, rectify};
use serde_json::{Value, json};
pub const GUIDED: &str = "image.guidedUpright";
fn bad(msg: impl Into<String>) -> EngineError {
    EngineError::BadParams { cmd: GUIDED.into(), msg: msg.into() }
}
fn run(s: &mut Session, p: &Value) -> Result<Value> {
    let lines = p.get("lines").and_then(Value::as_array).filter(|a| !a.is_empty() && a.len() <= 4).ok_or_else(|| bad("pass 1–4 lines"))?;
    let guides: Vec<Guide> = serde_json::from_value(Value::Array(lines.clone())).map_err(|e| bad(e.to_string()))?;
    let st = s.active().ok_or(EngineError::NoDocument)?;
    let w = f64::from(st.doc.size.width);
    let h = f64::from(st.doc.size.height);
    let correction = rectify(&guides, [w, h]).map_err(bad)?;
    let src = [[0., 0.], [w, 0.], [w, h], [0., h]];
    let mut dst = [[0.; 2]; 4];
    for (d, p) in dst.iter_mut().zip(src) {
        *d = apply(&correction.matrix, p).ok_or_else(|| bad("invalid perspective"))?;
    }
    let (mut lo, mut hi) = ([f64::INFINITY; 2], [f64::NEG_INFINITY; 2]);
    for d in dst {
        for axis in 0..2 {
            lo[axis] = lo[axis].min(d[axis]);
            hi[axis] = hi[axis].max(d[axis]);
        }
    }
    if (hi[0] - lo[0]) * (hi[1] - lo[1]) > photocraft_algo::puppet::MAX_WARP_PIXELS || dst.iter().flatten().any(|v| v.abs() > 300_000.) {
        return Err(bad("correction would stretch the image too far"));
    }
    let mut params = json!({"planes":[{"src":src,"dst":dst}]});
    if let Some(layer) = p.get("layer") {
        params["layer"] = layer.clone();
    }
    if let Some(interpolation) = p.get("interpolation") {
        params["interpolation"] = interpolation.clone();
    }
    let result = s.execute(crate::distort_cmds::PERSPECTIVE, params)?;
    Ok(json!({"layer":result["layer"],"matrix":correction.matrix,"method":correction.method,"lines":guides.len()}))
}
pub fn specs() -> Vec<CommandSpec> {
    vec![CommandSpec {
        id: GUIDED,
        label: "Guided Upright",
        menu: &["Image", "Image Rotation"],
        shortcut: None,
        params: r#"{"lines":[{"from":[x,y],"to":[x,y],"orientation":"horizontal|vertical"}],"layer":id?} — 1–4 guides, at most two per orientation; one guide straightens the horizon; two converging guides correct perspective"#,
        enabled: |s| s.disabled_reason(crate::distort_cmds::PERSPECTIVE).map_or(Ok(()), Err),
        journal: true,
        run,
    }]
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn guided_warp_is_one_undo_and_depth_independent() {
        for depth in [8, 16, 32] {
            let mut s = Session::new();
            s.execute("file.new", json!({"width":100,"height":80,"depth":depth,"background":"white"})).unwrap();
            let before = s.active().unwrap().doc.clone();
            let steps = s.active().unwrap().history.entries().len();
            let r = s.execute(GUIDED, json!({"lines":[{"from":[10,30],"to":[90,40],"orientation":"horizontal"}]})).unwrap();
            assert_eq!(r["method"], "rotation");
            assert_eq!(s.active().unwrap().history.entries().len(), steps + 1);
            assert!(s.undo());
            assert_eq!(s.active().unwrap().doc.size, before.size);
            assert_eq!(
                s.active().unwrap().doc.layers[0].surface().unwrap().read_region(before.bounds()),
                before.layers[0].surface().unwrap().read_region(before.bounds())
            );
        }
    }
    #[test]
    fn invalid_guides_do_not_change_history() {
        let mut s = Session::new();
        s.execute("file.new", json!({"width":40,"height":40})).unwrap();
        let steps = s.active().unwrap().history.entries().len();
        for p in [json!({}), json!({"lines":[]}), json!({"lines":[{"from":[0,0],"to":[0,0],"orientation":"horizontal"}]}), json!({"lines":"bad"})] {
            assert!(s.execute(GUIDED, p).is_err());
            assert_eq!(s.active().unwrap().history.entries().len(), steps);
        }
    }
}
