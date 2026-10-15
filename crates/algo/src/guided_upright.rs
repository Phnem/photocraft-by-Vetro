//! Projective rectification from up to two horizontal and two vertical image guides.
//! Vanishing points determine the image of the line at infinity. Its removal is followed
//! by an affine change of basis that makes the guide families horizontal and vertical.
//! With only one family, the missing vanishing point is assumed to be at infinity in the
//! perpendicular direction. The image centre and its local area scale are preserved.
use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Guide {
    pub from: [f64; 2],
    pub to: [f64; 2],
    pub orientation: Orientation,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum Orientation {
    Horizontal,
    Vertical,
}
#[derive(Debug)]
pub struct Correction {
    pub matrix: [[f64; 3]; 3],
    pub method: &'static str,
}

fn cross(a: [f64; 3], b: [f64; 3]) -> [f64; 3] {
    [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
}
fn unit(v: [f64; 2]) -> Option<[f64; 2]> {
    let n = v[0].hypot(v[1]);
    (n > 1e-9 && n.is_finite()).then(|| [v[0] / n, v[1] / n])
}
pub fn apply(m: &[[f64; 3]; 3], p: [f64; 2]) -> Option<[f64; 2]> {
    let d = m[2][0] * p[0] + m[2][1] * p[1] + m[2][2];
    if d.abs() < 1e-8 {
        return None;
    }
    let result = [(m[0][0] * p[0] + m[0][1] * p[1] + m[0][2]) / d, (m[1][0] * p[0] + m[1][1] * p[1] + m[1][2]) / d];
    result.iter().all(|v| v.is_finite()).then_some(result)
}
fn direction(guides: &[&Guide], m: &[[f64; 3]; 3], vertical: bool) -> Option<[f64; 2]> {
    let mut sum = [0.; 2];
    for g in guides {
        let a = apply(m, g.from)?;
        let b = apply(m, g.to)?;
        let mut d = unit([b[0] - a[0], b[1] - a[1]])?;
        if d[usize::from(vertical)] < 0. {
            d = [-d[0], -d[1]];
        }
        sum[0] += d[0];
        sum[1] += d[1];
    }
    unit(sum)
}
fn vanishing(guides: &[&Guide], fallback: [f64; 2]) -> Result<[f64; 3], String> {
    let (Some(a), Some(b)) = (guides.first(), guides.get(1)) else {
        return Ok([fallback[0], fallback[1], 0.]);
    };
    let line = |g: &Guide| cross([g.from[0], g.from[1], 1.], [g.to[0], g.to[1], 1.]);
    let p = cross(line(a), line(b));
    let n = p.iter().map(|v| v * v).sum::<f64>().sqrt();
    if n < 1e-9 || !n.is_finite() {
        return Err("guides in a family must be distinct lines".into());
    }
    Ok(p.map(|v| v / n))
}
pub fn rectify(guides: &[Guide], size: [f64; 2]) -> Result<Correction, String> {
    if guides.is_empty() || guides.len() > 4 {
        return Err("draw 1–4 guides, at most two per orientation".into());
    }
    if size.iter().any(|v| !v.is_finite() || *v < 1. || *v > 300_000.) {
        return Err("invalid image dimensions".into());
    }
    for g in guides {
        if g.from.iter().chain(&g.to).any(|v| !v.is_finite() || v.abs() > 300_000.) || (g.to[0] - g.from[0]).hypot(g.to[1] - g.from[1]) < 1. {
            return Err("guide endpoints must be finite and at least one pixel apart".into());
        }
    }
    let horizontal: Vec<_> = guides.iter().filter(|g| g.orientation == Orientation::Horizontal).collect();
    let vertical: Vec<_> = guides.iter().filter(|g| g.orientation == Orientation::Vertical).collect();
    if horizontal.len() > 2 || vertical.len() > 2 {
        return Err("at most two guides per orientation".into());
    }
    let identity = [[1., 0., 0.], [0., 1., 0.], [0., 0., 1.]];
    let h = direction(&horizontal, &identity, false);
    let v = direction(&vertical, &identity, true);
    let h = h.or_else(|| v.map(|v| [v[1], -v[0]])).ok_or("horizontal direction is undefined")?;
    let v = v.unwrap_or([-h[1], h[0]]);
    let projective = horizontal.len() == 2 || vertical.len() == 2;
    let mut m = identity;
    if projective {
        let horizon = cross(vanishing(&horizontal, h)?, vanishing(&vertical, v)?);
        if horizon[2].abs() < 1e-9 {
            return Err("the vanishing line crosses the image origin".into());
        }
        m[2] = [horizon[0] / horizon[2], horizon[1] / horizon[2], 1.];
    }
    let centre = [size[0] / 2., size[1] / 2.];
    let denominator = m[2][0] * centre[0] + m[2][1] * centre[1] + 1.;
    for p in [[0., 0.], [size[0], 0.], size, [0., size[1]]] {
        let d = m[2][0] * p[0] + m[2][1] * p[1] + 1.;
        if d * denominator <= 1e-6 {
            return Err("guides put a perspective horizon inside the image".into());
        }
    }
    let h = direction(&horizontal, &m, false);
    let v = direction(&vertical, &m, true);
    let h = h.or_else(|| v.map(|v| [v[1], -v[0]])).ok_or("invalid horizontal direction")?;
    let v = v.unwrap_or([-h[1], h[0]]);
    let det = h[0] * v[1] - h[1] * v[0];
    if det <= 0.05 {
        return Err("horizontal and vertical guides must define independent axes".into());
    }
    let scale = (det * denominator.abs().powi(3)).sqrt();
    let a = [[v[1] * scale / det, -v[0] * scale / det], [-h[1] * scale / det, h[0] * scale / det]];
    m[0] = [a[0][0], a[0][1], 0.];
    m[1] = [a[1][0], a[1][1], 0.];
    let mapped = apply(&m, centre).ok_or("invalid centre")?;
    let denominator_row = m[2];
    for (row, delta) in [(0, centre[0] - mapped[0]), (1, centre[1] - mapped[1])] {
        for (value, denominator) in m[row].iter_mut().zip(denominator_row) {
            *value += delta * denominator;
        }
    }
    Ok(Correction {
        matrix: m,
        method: if projective {
            "projective"
        } else if horizontal.is_empty() || vertical.is_empty() {
            "rotation"
        } else {
            "affine"
        },
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    fn guide(from: [f64; 2], to: [f64; 2], orientation: Orientation) -> Guide {
        Guide { from, to, orientation }
    }
    #[test]
    fn tilted_horizon_rotates_and_preserves_centre() {
        let c = rectify(&[guide([10., 20.], [190., 50.], Orientation::Horizontal)], [200., 160.]).unwrap();
        let a = apply(&c.matrix, [10., 20.]).unwrap();
        let b = apply(&c.matrix, [190., 50.]).unwrap();
        assert!((a[1] - b[1]).abs() < 1e-8);
        assert_eq!(c.method, "rotation");
        let p = apply(&c.matrix, [100., 80.]).unwrap();
        assert!((p[0] - 100.).abs() < 1e-8 && (p[1] - 80.).abs() < 1e-8);
    }
    #[test]
    fn four_guides_remove_both_vanishing_points() {
        let guides = [
            guide([30., 20.], [170., 40.], Orientation::Horizontal),
            guide([10., 130.], [190., 120.], Orientation::Horizontal),
            guide([30., 20.], [10., 130.], Orientation::Vertical),
            guide([170., 40.], [190., 120.], Orientation::Vertical),
        ];
        let c = rectify(&guides, [200., 160.]).unwrap();
        assert_eq!(c.method, "projective");
        for g in &guides {
            let a = apply(&c.matrix, g.from).unwrap();
            let b = apply(&c.matrix, g.to).unwrap();
            let axis = if g.orientation == Orientation::Horizontal { 1 } else { 0 };
            assert!((a[axis] - b[axis]).abs() < 1e-7);
        }
    }
    #[test]
    fn two_verticals_use_infinite_horizontal_prior() {
        let guides = [guide([30., 20.], [10., 130.], Orientation::Vertical), guide([170., 20.], [190., 130.], Orientation::Vertical)];
        let c = rectify(&guides, [200., 160.]).unwrap();
        for g in &guides {
            let a = apply(&c.matrix, g.from).unwrap();
            let b = apply(&c.matrix, g.to).unwrap();
            assert!((a[0] - b[0]).abs() < 1e-7);
        }
    }
    #[test]
    fn malformed_or_degenerate_guides_fail() {
        assert!(rectify(&[], [200., 160.]).is_err());
        assert!(rectify(&[guide([0., 0.], [0., 0.], Orientation::Horizontal)], [200., 160.]).is_err());
        assert!(rectify(&[guide([f64::NAN, 0.], [10., 10.], Orientation::Horizontal)], [200., 160.]).is_err());
        let g = guide([10., 10.], [50., 10.], Orientation::Horizontal);
        assert!(rectify(&[g.clone(), g], [200., 160.]).is_err());
    }
}
