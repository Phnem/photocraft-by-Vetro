//! Channel visibility over a rendered region. Work stays on the render thread and uses the
//! immutable snapshot, so toggling an eye never materializes a canvas-sized image on the UI thread.
use photocraft_color::ColorMode;
use photocraft_doc::{AlphaChannel, Document, LayerId};
use photocraft_engine::channel_cmds::{ChannelView, color_count};
use photocraft_engine::mask_view_cmds::MaskViewMode;
use photocraft_raster::{from_rgba, to_rgba};

#[derive(Clone)]
pub struct Display {
    pub view: ChannelView,
    pub active: Option<LayerId>,
    pub in_color: bool,
}

pub fn apply(doc: &Document, display: &Display, pixels: &mut [[f32; 4]], width: usize, origin: [f64; 2], step: [f64; 2]) {
    let view = &display.view;
    let colors = color_count(doc);
    let visible = view.visible_colors(colors);
    let layer_mask = view.shown_layer_mask(doc);
    let mask_gray = layer_mask.is_some_and(|(_, mode)| mode == MaskViewMode::Gray);
    let channels: Vec<&AlphaChannel> = doc.channels.iter().enumerate().filter(|(i, _)| view.alpha_shown(*i)).map(|(_, c)| c).collect();
    let quick = doc.quick_mask.as_ref().filter(|_| !view.quick_mask_hidden);
    let profile = photocraft_compose::cmyk_space(doc);
    photocraft_color::convert::with_cmyk_space(profile.as_ref(), || {
        for (i, p) in pixels.iter_mut().enumerate() {
            let x = (origin[0] + (i % width) as f64 * step[0]).floor() as i32;
            let y = (origin[1] + (i / width) as f64 * step[1]).floor() as i32;
            let gray = |s: &photocraft_raster::Surface| {
                let v = s.sample_channel(x, y, 0).clamp(0., 1.);
                [v, v, v, 1.]
            };
            if mask_gray {
                if let Some((m, _)) = layer_mask {
                    *p = gray(&m.surface);
                }
                continue;
            }
            let mut solo: Option<&AlphaChannel> = None;
            let mut solo_mask = false;
            if visible == 0 {
                solo = channels.first().copied().or(quick);
                if let Some(c) = solo {
                    *p = gray(&c.surface);
                } else if let Some(mask) = layer_mask.map(|(m, _)| m).or_else(|| display.active.and_then(|id| doc.layer(id)).and_then(|l| l.mask.as_ref())) {
                    *p = gray(&mask.surface);
                    solo_mask = true;
                }
            } else if visible < colors {
                let a = p[3].clamp(0., 1.);
                let rgba = [p[0] * a + 1. - a, p[1] * a + 1. - a, p[2] * a + 1. - a, 1.];
                let fmt = doc.pixel_format();
                let mut native = from_rgba(&fmt, rgba);
                if visible == 1 && !display.in_color {
                    let k = (0..colors).find(|k| view.color_visible(*k)).unwrap_or(0);
                    let value = native[k];
                    let g = if doc.mode == ColorMode::Cmyk { 1. - value } else { value };
                    *p = [g, g, g, 1.];
                } else {
                    for (k, v) in native.iter_mut().take(colors).enumerate() {
                        if !view.color_visible(k) {
                            *v = if doc.mode == ColorMode::Lab && (k > 0 || display.in_color) { 0.5 } else { 0. };
                        }
                    }
                    *p = to_rgba(&fmt, &native);
                    p[3] = 1.;
                }
            }
            // Straight RGBA -> premultiplied accumulator, then back after rubylith overlays.
            for k in 0..3 {
                p[k] *= p[3];
            }
            let tint = |p: &mut [f32; 4], value: f32, color: [f32; 3], opacity: f32| {
                let a = (value * opacity).clamp(0., 1.);
                for k in 0..3 {
                    p[k] = color[k] * a + p[k] * (1. - a);
                }
                p[3] = a + p[3] * (1. - a);
            };
            for channel in channels.iter().copied().chain(quick) {
                if solo.is_some_and(|s| std::ptr::eq(s, channel)) {
                    continue;
                }
                let value = channel.surface.sample_channel(x, y, 0).clamp(0., 1.);
                let (color, opacity) = channel.spot.map(|(c, s)| (c.to_rgb(), 0.5 + 0.5 * s)).unwrap_or((channel.color.to_rgb(), channel.opacity));
                tint(p, channel.overlay_coverage(value), color, opacity);
            }
            if !solo_mask && let Some((mask, MaskViewMode::Overlay)) = layer_mask {
                tint(p, 1. - mask.surface.sample_channel(x, y, 0).clamp(0., 1.), AlphaChannel::DEFAULT_COLOR.to_rgb(), 0.5);
            }
            if p[3] > 0. {
                for k in 0..3 {
                    p[k] /= p[3];
                }
            }
        }
    });
}

#[allow(clippy::too_many_arguments)]
pub fn render(
    doc: &Document,
    display: &Display,
    rect: photocraft_geom::Rect,
    width: u32,
    height: u32,
    out: &mut [u8],
    stride: usize,
    encode: bool,
) -> Result<crate::Rendered, String> {
    if rect.is_empty() || width == 0 || height == 0 {
        return Ok(crate::Rendered::default());
    }
    let w = width.min(rect.width());
    let h = height.min(rect.height());
    let mut buffer =
        if w == rect.width() && h == rect.height() { photocraft_compose::render(doc, rect) } else { photocraft_compose::render_reduced_rect(doc, rect, w, h) };
    let bw = buffer.rect.width();
    let bh = buffer.rect.height();
    apply(doc, display, &mut buffer.px, bw as usize, [rect.x0 as f64, rect.y0 as f64], [rect.width() as f64 / bw as f64, rect.height() as f64 / bh as f64]);
    if encode {
        crate::encode_srgb(&mut buffer.px);
    }
    crate::write_bgra(&buffer.px, bw as usize, bh as usize, out, stride)?;
    Ok(crate::Rendered { width: bw, height: bh })
}

#[cfg(test)]
#[allow(clippy::unwrap_used)]
mod tests {
    use super::*;
    #[test]
    fn eyes_targeting_and_alpha_overlay_do_not_change_source_pixels() {
        for depth in [8, 16, 32] {
            let mut b = crate::Bridge::new();
            b.execute("file.new", &format!(r##"{{"width":32,"height":32,"depth":{depth},"background":"#336699"}}"##)).unwrap();
            let before = b.snapshot().unwrap();
            let source = photocraft_compose::render(&before, before.bounds()).px;
            b.execute("channel.target", r#"{"channel":"red"}"#).unwrap();
            let st = b.session.active().unwrap();
            let display = Display { view: st.channel_view.clone(), active: st.active_layer, in_color: false };
            let mut px = source.clone();
            apply(&st.doc, &display, &mut px, 32, [0., 0.], [1., 1.]);
            assert!((px[0][0] - px[0][1]).abs() < 1e-6);
            b.execute("channel.new", r#"{"fill":"white"}"#).unwrap();
            b.execute("channel.target", r#"{"channel":0}"#).unwrap();
            let st = b.session.active().unwrap();
            let display = Display { view: st.channel_view.clone(), active: st.active_layer, in_color: false };
            let mut px = source.clone();
            apply(&st.doc, &display, &mut px, 32, [0., 0.], [1., 1.]);
            assert!(px[0][0] > 0.99 && px[0][1] > 0.99);
            assert_eq!(photocraft_compose::render(&st.doc, st.doc.bounds()).px, source);
        }
    }
}
