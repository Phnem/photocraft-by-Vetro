#pragma once
// PhotoCraft by Vetro -- the shared vocabulary of the shell: motion, the Tokyo Glass tokens,
// geometry and input events. Everything visible reads its numbers from here.
#include <windows.h>
#include <d2d1_1.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

// ------------------------------------------------------------------------------ motion --
// Every visible value travels through a spring, so nothing in the interface teleports
// (Vetro Look design.md §10). Reduce Motion collapses springs to instant assignment.
extern bool reducedMotion;

struct Spring {
  float v = 0, target = 0, vel = 0, k = 420, c = 32;
  Spring() = default;
  Spring(float value, float stiffness, float damping) : v(value), target(value), k(stiffness), c(damping) {}
  void Reset(float value) { v = target = value; vel = 0; }
  void To(float value) { target = value; }
  void Nudge(float amount) { vel += amount; }
  void Tune(float stiffness, float damping) { k = stiffness; c = damping; }
  bool Moving() const { return std::fabs(v - target) > 0.0004f || std::fabs(vel) > 0.0025f; }
  bool Step(float dt) {
    if (!Moving()) { v = target; vel = 0; return false; }
    if (reducedMotion) { v = target; vel = 0; return true; }
    dt = std::min(dt, 0.05f);
    const float h = 1.f / 600.f;
    for (float t = 0; t < dt; t += h) {
      float step = std::min(h, dt - t);
      vel += ((target - v) * k - vel * c) * step;
      v += vel * step;
    }
    if (!Moving()) { v = target; vel = 0; }
    return true;
  }
};
// A spring for a value measured in pixels: same physics, larger rest thresholds.
struct PixelSpring : Spring {
  using Spring::Spring;
  bool Step(float dt) {
    if (std::fabs(v - target) < 0.05f && std::fabs(vel) < 0.5f) { v = target; vel = 0; return false; }
    return Spring::Step(dt);
  }
};

// The spring from a period and a damping ratio (mass 1): w = 2pi/T, k = w^2, c = 2*zeta*sqrt(k).
// This is how Vetro-collection's StagedSheetMotion states its springs.
struct SpringSpec { float k, c; };
inline SpringSpec FromPeriod(float periodSeconds, float zeta) {
  float w = 6.2831853f / periodSeconds;
  float k = w * w;
  return {k, 2 * zeta * std::sqrt(k)};
}
inline SpringSpec FromStiffness(float k, float zeta) { return {k, 2 * zeta * std::sqrt(k)}; }

namespace motion {
// Small controls: stiff and quick. Panels: softer and heavier. The view: critically damped.
constexpr float ButtonK = 1100, ButtonC = 52;
constexpr float HoverK = 520, HoverC = 40;
constexpr float PanelK = 340, PanelC = 31;
constexpr float DockK = 260, DockC = 27;
constexpr float ZoomK = 420, ZoomC = 41;
constexpr float SlideK = 700, SlideC = 46;  // sliding highlights (segmented, tabs, menu rows)
// Menu morph from its anchor (Vetro-collection TtmMenu, MorphPath.DIRECT): one spring for the
// shell, a faster critically damped track for the light.
inline SpringSpec MenuOpen() { return FromPeriod(0.40f, 0.78f); }
inline SpringSpec MenuClose() { return FromStiffness(900, 1.f); }
inline SpringSpec ContentIn() { return FromStiffness(420, 1.f); }
inline SpringSpec ContentOut() { return FromStiffness(1400, 1.f); }
constexpr float ContentRevealAt = 0.42f;  // content appears once the shell can hold it
constexpr float ContentMinScale = 0.94f;
constexpr float RowRevealStart = 0.45f, RowStagger = 0.07f, RowSpan = 0.22f, RowLift = 10.f;
inline float ContentScale(float shell) {
  float p = std::clamp(shell, 0.f, 1.f);
  if (p >= 1) return 1;
  float t = std::clamp((p - ContentRevealAt) / (1 - ContentRevealAt), 0.f, 1.f);
  return ContentMinScale + (1 - ContentMinScale) * t;
}
inline float RowReveal(float shell, int indexFromAnchor) {
  // The cascade is capped so the last row has arrived by the time the shell settles (0.97).
  float start = RowRevealStart + std::min(RowStagger * float(indexFromAnchor), .30f);
  return std::clamp((shell - start) / RowSpan, 0.f, 1.f);
}
}  // namespace motion

// ------------------------------------------------------------------------------ colour --
inline D2D1_COLOR_F Rgba(float r, float g, float b, float a = 1) { return D2D1::ColorF(r, g, b, a); }
inline D2D1_COLOR_F Fade(D2D1_COLOR_F c, float a) { c.a *= a; return c; }
inline D2D1_COLOR_F Mix(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t) {
  return D2D1::ColorF(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}

// Tokyo Glass (VetroView docs/TOKYO_GLASS.md): cream type, a vermilion accent, the original neutral
// graphite material. Photographs keep their own colours.
struct Palette {
  D2D1_COLOR_F tint = Rgba(.105f, .105f, .118f, .50f);       // dock glass over the live backdrop
  D2D1_COLOR_F tintDense = Rgba(.095f, .095f, .108f, .66f);  // popovers: one step denser
  D2D1_COLOR_F tintFallback = Rgba(.085f, .085f, .095f, .86f);
  D2D1_COLOR_F edge = Rgba(1, 1, 1, .12f);
  D2D1_COLOR_F lift = Rgba(1, 1, 1, .055f);
  D2D1_COLOR_F text = Rgba(.99f, .93f, .81f, 1.f);
  D2D1_COLOR_F dim = Rgba(.99f, .93f, .81f, .74f);
  D2D1_COLOR_F faint = Rgba(.99f, .93f, .81f, .50f);
  D2D1_COLOR_F icon = Rgba(.97f, .95f, .91f, .82f);
  D2D1_COLOR_F accent = Rgba(1.f, .25f, .18f, 1.f);
  D2D1_COLOR_F hover = Rgba(1, 1, 1, .085f);
  D2D1_COLOR_F press = Rgba(1, 1, 1, .15f);
  D2D1_COLOR_F selected = Rgba(1, 1, 1, .13f);
  D2D1_COLOR_F track = Rgba(1, 1, 1, .14f);
  D2D1_COLOR_F sunk = Rgba(0, 0, 0, .24f);
  D2D1_COLOR_F sep = Rgba(1, 1, 1, .08f);
  D2D1_COLOR_F focus = Rgba(1.f, .93f, .81f, .55f);
  D2D1_COLOR_F canvasTop = Rgba(.085f, .085f, .095f, 1.f);
  D2D1_COLOR_F canvasBottom = Rgba(.030f, .030f, .035f, 1.f);
};
extern Palette P;

// ------------------------------------------------------------------------------ tokens --
// 4-DIP grid. Radii shrink with the surface (design.md §3): big calm layers are softer.
namespace tok {
constexpr float Grid = 4;
constexpr float RadiusLarge = 30;    // large floating surfaces, dialogs
constexpr float RadiusDock = 22;     // semantic docks (Tokyo capsules)
constexpr float RadiusPopover = 18;  // menus, popovers
constexpr float RadiusControl = 11;  // buttons, rows, fields
constexpr float RadiusSmall = 7;
constexpr float Hit = 36;            // minimum interactive target
constexpr float IconSize = 20;
constexpr float DockPad = 6;
constexpr float Margin = 14;         // docks keep this from the window edge
constexpr float Gap = 12;
constexpr float BlurSigma = 24;      // backdrop blur, DIPs (design.md: 20..30 px)
constexpr float NoiseAlpha = 0.022f;
}  // namespace tok

// ---------------------------------------------------------------------------- geometry --
using Rect = D2D1_RECT_F;
inline Rect R(float l, float t, float r, float b) { return D2D1::RectF(l, t, r, b); }
inline Rect RWH(float x, float y, float w, float h) { return D2D1::RectF(x, y, x + w, y + h); }
inline float W(const Rect& r) { return r.right - r.left; }
inline float H(const Rect& r) { return r.bottom - r.top; }
inline float MidX(const Rect& r) { return (r.left + r.right) * .5f; }
inline float MidY(const Rect& r) { return (r.top + r.bottom) * .5f; }
inline bool Inside(const Rect& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
inline Rect Inset(const Rect& r, float dx, float dy) { return R(r.left + dx, r.top + dy, r.right - dx, r.bottom - dy); }
inline Rect Inset(const Rect& r, float d) { return Inset(r, d, d); }
inline Rect Offset(const Rect& r, float dx, float dy) { return R(r.left + dx, r.top + dy, r.right + dx, r.bottom + dy); }
inline Rect Lerp(const Rect& a, const Rect& b, float t) {
  return R(a.left + (b.left - a.left) * t, a.top + (b.top - a.top) * t, a.right + (b.right - a.right) * t, a.bottom + (b.bottom - a.bottom) * t);
}
inline Rect Union(const Rect& a, const Rect& b) {
  return R(std::min(a.left, b.left), std::min(a.top, b.top), std::max(a.right, b.right), std::max(a.bottom, b.bottom));
}
inline D2D1_ROUNDED_RECT RR(const Rect& r, float radius) {
  float m = std::max(0.f, std::min(W(r), H(r)) * .5f);
  radius = std::min(radius, m);
  return D2D1::RoundedRect(r, radius, radius);
}

// ------------------------------------------------------------------------------ events --
struct Mods {
  bool ctrl = false, shift = false, alt = false;
};
struct PointerEvent {
  float x = 0, y = 0;
  int button = 0;          // 0 left, 1 right, 2 middle
  float pressure = 1;      // pen pressure 0..1; 1 for a mouse
  bool pen = false;
  double timeMs = 0;
  int clicks = 1;          // 2 for a double click
  Mods mods;
};

// ------------------------------------------------------------------------------- text --
std::wstring Widen(const std::string& utf8);
std::string Narrow(const std::wstring& wide);
