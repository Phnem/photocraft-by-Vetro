//! Straight RGBA8 at the OS boundary; copies inside the engine retain their original depth.
use photocraft_color::PixelFormat;
use photocraft_engine::edit_cmds::Clip;
use photocraft_geom::Rect;
use photocraft_raster::Surface;

use crate::Bridge;

pub(crate) const MAX_CLIPBOARD_BYTES: usize = 256 * 1024 * 1024;

pub(crate) fn image_len(width: u32, height: u32) -> Result<usize, String> {
    if width == 0 || height == 0 || width > 32768 || height > 32768 {
        return Err("invalid clipboard image dimensions".into());
    }
    (width as usize)
        .checked_mul(height as usize)
        .and_then(|n| n.checked_mul(4))
        .filter(|n| *n <= MAX_CLIPBOARD_BYTES)
        .ok_or_else(|| "clipboard image is too large".into())
}

impl Bridge {
    pub fn clipboard_size(&self) -> Option<(u32, u32)> {
        let c = self.session.clipboard.as_ref()?;
        (!c.bounds.is_empty()).then(|| (c.bounds.width(), c.bounds.height()))
    }

    pub fn clipboard_read(&self, out: &mut [u8]) -> Result<(), String> {
        let c = self.session.clipboard.as_ref().ok_or("the clipboard is empty")?;
        let len = image_len(c.bounds.width(), c.bounds.height())?;
        if out.len() != len {
            return Err("clipboard buffer length does not match dimensions".into());
        }
        let mut rgba = vec![[0; 4]; len / 4];
        c.surface.read_rgba8_into(c.bounds, &mut rgba);
        for (dst, src) in out.chunks_exact_mut(4).zip(rgba) {
            dst.copy_from_slice(&src);
        }
        Ok(())
    }

    pub fn clipboard_set(&mut self, width: u32, height: u32, rgba: &[u8]) -> Result<(), String> {
        if rgba.len() != image_len(width, height)? {
            return Err("clipboard buffer length does not match dimensions".into());
        }
        let bounds = Rect::new(0, 0, width as i32, height as i32);
        let mut surface = Surface::from_interleaved(PixelFormat::RGBA8, bounds, rgba);
        surface.prune();
        self.session.clipboard = Some(Clip { surface, bounds, layers: None, resolution: None });
        Ok(())
    }

    pub fn clipboard_clear(&mut self) {
        self.session.clipboard = None;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn external_rgba_round_trip_and_paste_at_every_depth() {
        let rgba = [255, 0, 17, 128, 0, 255, 0, 255];
        for depth in [8, 16, 32] {
            let mut b = Bridge::new();
            b.execute("file.new", &json!({"width":10,"height":10,"depth":depth}).to_string()).unwrap();
            b.clipboard_set(2, 1, &rgba).unwrap();
            let mut out = [0; 8];
            b.clipboard_read(&mut out).unwrap();
            assert_eq!(out, rgba);
            b.execute("edit.paste", "{}").unwrap();
            assert_eq!(b.session.active().unwrap().doc.depth.bits(), depth);
            assert_eq!(b.session.active().unwrap().doc.layers.len(), 2);
            assert!(b.undo());
            assert_eq!(b.session.active().unwrap().doc.layers.len(), 1);
        }
    }

    #[test]
    fn clipboard_without_a_document_creates_one_and_new_from_clipboard_always_does() {
        let mut b = Bridge::new();
        b.clipboard_set(1, 1, &[1, 2, 3, 0]).unwrap();
        b.execute("edit.paste", "{}").unwrap();
        assert!(b.session.active().unwrap().is_dirty());
        b.execute("file.newFromClipboard", "{}").unwrap();
        assert_eq!(b.session.documents().len(), 2);
        b.clipboard_clear();
        assert!(b.clipboard_size().is_none());
        assert!(b.clipboard_read(&mut [0; 4]).is_err());
    }

    #[test]
    fn malformed_buffers_leave_existing_clipboard_intact() {
        let mut b = Bridge::new();
        b.clipboard_set(1, 1, &[0, 0, 0, 255]).unwrap();
        for (w, h, bytes) in [(0, 1, &[][..]), (u32::MAX, 2, &[][..]), (32768, 32768, &[][..]), (1, 1, &[0][..])] {
            assert!(b.clipboard_set(w, h, bytes).is_err());
        }
        assert_eq!(b.clipboard_size(), Some((1, 1)));
        assert!(b.clipboard_read(&mut [0; 3]).is_err());
    }
}
