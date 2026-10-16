//! Photoshop gradient appearance → dense linear stops. The model lives in `photocraft-color`
//! (the document resolves a gradient fill's ramp with it when it is drawn); this module keeps the
//! import's paths.

pub use photocraft_color::gradient::{Method, bake};
