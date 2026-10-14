use photocraft_engine::Session;
use photocraft_geom::Rect;
use serde_json::{Value, json};

fn session(mode: &str, depth: u32) -> Session {
    let mut s = Session::new();
    s.execute("file.new", json!({"width":40,"height":32,"background":"transparent","mode":mode,"depth":depth})).unwrap();
    s.execute("edit.fill", json!({"color":"#447799"})).unwrap();
    s
}
fn composite(s: &Session) -> Vec<[f32; 4]> {
    let d = &s.active().unwrap().doc;
    photocraft_compose::render(d, d.bounds()).px
}
fn execute(s: &mut Session, p: Value) -> Value {
    s.execute("image.cropContentAware", p).unwrap()
}

#[test]
fn extends_canvas_in_every_mode_and_depth_preserving_layers_and_one_history_step() {
    for mode in ["rgb", "gray", "cmyk", "lab"] {
        for depth in [8, 16, 32] {
            let mut s = session(mode, depth);
            let original = s.active().unwrap().doc.clone();
            let history = s.active().unwrap().history.past_len();
            let reference = composite(&s)[41];
            let result = execute(&mut s, json!({"x":-4,"y":-6,"width":48,"height":44,"fillLayerName":"Edge texture"}));
            assert!(result["filled"].as_u64().unwrap() > 0);
            let d = s.active().unwrap();
            assert_eq!(d.history.past_len(), history + 1);
            assert_eq!(d.doc.layers.len(), 2);
            assert_eq!(d.doc.layers[0].id, original.layers[0].id);
            assert_eq!(d.doc.layers[1].name, "Edge texture");
            assert_eq!(d.doc.pixel_format(), original.pixel_format());
            let edge = composite(&s)[0];
            for (a, b) in edge.into_iter().zip(reference) {
                assert!((a - b).abs() < 0.02, "{mode}/{depth}: {edge:?} vs {reference:?}");
            }
            s.undo();
            assert_eq!(composite(&s), photocraft_compose::render(&original, original.bounds()).px);
            assert_eq!(s.active().unwrap().doc.layers.len(), 1);
            s.redo();
            assert_eq!(s.active().unwrap().doc.layers.len(), 2);
        }
    }
}
#[test]
fn rotated_and_resampled_crop_fills_corners_but_does_not_fill_intentional_transparency() {
    for depth in [8, 16, 32] {
        let mut s = session("rgb", depth);
        s.execute("select.rect", json!({"x":17,"y":13,"width":6,"height":6})).unwrap();
        s.execute("edit.clear", json!({})).unwrap();
        s.execute("select.deselect", json!({})).unwrap();
        let result = execute(&mut s, json!({"x":0,"y":0,"width":40,"height":32,"angle":18,"targetWidth":80,"targetHeight":64,"resolution":144}));
        assert_eq!((result["documentWidth"].as_u64(), result["documentHeight"].as_u64()), (Some(80), Some(64)));
        assert_eq!(s.active().unwrap().doc.resolution_dpi, 144.0);
        assert!(result["filled"].as_u64().unwrap() > 0);
        let pixels = composite(&s);
        assert!(pixels[0][3] > 0.99, "outside corner must be completed");
        assert!(pixels[32 * 80 + 40][3] < 0.01, "original transparent hole must remain transparent");
    }
}
#[test]
fn no_extension_does_not_add_a_fill_layer_and_pixels_are_retained_without_delete() {
    let mut s = session("rgb", 8);
    let original = s.active().unwrap().doc.clone();
    let result = execute(&mut s, json!({"x":8,"y":8,"width":16,"height":16,"deleteCroppedPixels":false}));
    assert_eq!(result["filled"], 0);
    assert!(result["fillLayer"].is_null());
    assert_eq!(s.active().unwrap().doc.layers.len(), 1);
    assert_eq!(s.active().unwrap().doc.layers[0].surface().unwrap().rgba(-7, -7), original.layers[0].surface().unwrap().rgba(1, 1));
}
#[test]
fn invalid_or_empty_source_rolls_back_document_history_and_selection() {
    let mut s = session("rgb", 8);
    let before = s.active().unwrap().doc.clone();
    let past = s.active().unwrap().history.past_len();
    for p in [
        json!({"x":0,"y":0,"width":300000,"height":300000}),
        json!({"x":i64::MAX,"y":0,"width":10,"height":10}),
        json!({"x":0,"y":0,"width":10,"height":10,"angle":"bad"}),
        json!({"x":0,"y":0,"width":10,"height":10,"targetWidth":u64::MAX,"targetHeight":32}),
        json!({"x":1000,"y":1000,"width":10,"height":10}),
        json!({"x":-4,"y":0,"width":44,"height":32,"resolution":0}),
    ] {
        assert!(s.execute("image.cropContentAware", p).is_err());
        assert_eq!(s.active().unwrap().doc.size, before.size);
        assert_eq!(s.active().unwrap().history.past_len(), past);
        assert_eq!(composite(&s), photocraft_compose::render(&before, before.bounds()).px);
    }
    let mut empty = Session::new();
    empty.execute("file.new", json!({"width":20,"height":20,"background":"transparent"})).unwrap();
    assert!(empty.execute("image.cropContentAware", json!({"x":-2,"y":-2,"width":24,"height":24})).is_err());
    assert_eq!(empty.active().unwrap().doc.bounds(), Rect::new(0, 0, 20, 20));
}
