//! Optional author-file oracle. The public PSD stays outside Git.
use photocraft_bridge::{Bridge, render_view};
use photocraft_geom::Rect;
use serde_json::json;

fn core(b: &Bridge) -> Vec<u8> {
    let mut pixels = vec![0; 16 * 16 * 4];
    render_view(&b.snapshot().unwrap(), Rect::new(86, 32, 102, 48), 16, 16, &mut pixels, 64, false).unwrap();
    pixels
}

#[test]
#[ignore = "requires VETRO_ISSUE_5524_FILE pointing to the author's public PSD"]
fn blur_and_smudge_affect_the_upper_patch_when_that_layer_is_targeted() {
    let path = std::env::var("VETRO_ISSUE_5524_FILE").expect("set VETRO_ISSUE_5524_FILE");
    let bytes = std::fs::read(&path).unwrap();
    assert_eq!(bytes.len(), 151814, "different source fixture");
    for command in ["paint.blur", "paint.smudge"] {
        let mut b = Bridge::new();
        b.open_path(&path).unwrap();
        let doc = b.snapshot().unwrap();
        assert_eq!((doc.size.width, doc.size.height), (1248, 198));
        assert_eq!(doc.layers.len(), 3);
        let (background, upper) = (doc.layers[0].id.0, doc.layers[1].id.0);
        let original = core(&b);
        let params = json!({"points":[[90,40,1],[100,40,1]],"size":48,"strength":100,"hardness":100}).to_string();
        b.execute("layer.select", &json!({"layer":background}).to_string()).unwrap();
        b.execute(command, &params).unwrap();
        let underneath = core(&b);
        assert_eq!(original, underneath, "the upper opaque patch should cover a lower-layer stroke");
        b.execute("layer.select", &json!({"layer":upper}).to_string()).unwrap();
        b.execute(command, &params).unwrap();
        assert_ne!(core(&b), underneath, "the patch must change when its own layer is painted ({command})");
    }
}
