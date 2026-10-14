// PhotoCraft by Vetro -- the interaction classes (see widgets.h).
#include "widgets.h"
#include "color_picker.h"
#include "i18n.h"

namespace {
// A label column that grows to fit its label (a translation may be longer than the English).
float LabelColumn(const std::wstring& label, float width) {
  return width > 0 ? std::max(width, Measure(label, F_Label) + 10) : width;
}
}  // namespace
#include "icons_phosphor.h"
#include "json.h"
#include <commdlg.h>
#include <cctype>
#include <cstdio>

// =============================================================================== root ==
namespace ui {
namespace {
HWND hwnd = nullptr;
Rect bounds{};
std::vector<Widget*> layers;
std::vector<Popover*> popovers;
Widget* hovered = nullptr;
Widget* captured = nullptr;
double hoverSince = 0;
LARGE_INTEGER freq{}, start{};
// Tooltip: appears after a calm hover, fades and lifts in.
Widget* tipFor = nullptr;
std::wstring tipText;
Rect tipAnchor{};
Spring tipAlpha{0, motion::HoverK, motion::HoverC};
// Modal scrim behind dialogs.
Spring scrim{0, motion::PanelK, motion::PanelC};
struct ToastItem {
  std::wstring text;
  bool error = false;
  double born = 0;
  Spring a{0, motion::PanelK, motion::PanelC};
};
std::vector<ToastItem> toasts;
std::function<bool(Widget*, const PointerEvent&)> gate;

bool InPopover(Widget* w) {
  for (; w; w = w->parent)
    if (dynamic_cast<Popover*>(w)) return true;
  return false;
}
Popover* TopModal() {
  for (auto it = popovers.rbegin(); it != popovers.rend(); ++it)
    if ((*it)->open && (*it)->modal) return *it;
  return nullptr;
}
Widget* HitAll(float x, float y) {
  for (auto it = popovers.rbegin(); it != popovers.rend(); ++it) {
    if (!(*it)->open) continue;
    if (Widget* h = (*it)->HitTest(x, y)) return h;
    if ((*it)->modal) return nullptr;  // nothing under a dialog answers
  }
  if (TopModal()) return nullptr;
  for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
    if (!(*it)->visible) continue;
    if (Widget* h = (*it)->HitTest(x, y)) return h;
  }
  return nullptr;
}
void SetHovered(Widget* w) {
  if (w == hovered) return;
  if (hovered) hovered->OnHover(false);
  hovered = w;
  hoverSince = NowMs();
  if (hovered) hovered->OnHover(true);
  if (tipFor && tipFor != w) { tipFor = nullptr; tipAlpha.To(0); }
}
}  // namespace

void Init(HWND window) {
  hwnd = window;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&start);
}
HWND Window() { return hwnd; }
void RequestFrame() { if (hwnd) InvalidateRect(hwnd, nullptr, FALSE); }
double NowMs() {
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return double(now.QuadPart - start.QuadPart) * 1000.0 / double(freq.QuadPart);
}
Rect Bounds() { return bounds; }
void SetBounds(const Rect& r) { bounds = r; }
void AddLayer(Widget* w) { layers.push_back(w); }

void OpenPopover(Popover* p) {
  auto it = std::find(popovers.begin(), popovers.end(), p);
  if (it != popovers.end()) popovers.erase(it);
  popovers.push_back(p);
  RequestFrame();
}
Popover* ActivePopover() { for (auto i = popovers.rbegin(); i != popovers.rend(); ++i) if ((*i)->open) return *i; return nullptr; }
void ClosePopovers() {
  for (auto* p : popovers)
    if (p->open && !p->modal && !p->pinned) p->Close();
}
void Forget(Popover* p) {
  auto it = std::find(popovers.begin(), popovers.end(), p);
  if (it != popovers.end()) popovers.erase(it);
  if (hovered && (hovered == p || InPopover(hovered))) hovered = nullptr;
  if (captured && (captured == p || InPopover(captured))) captured = nullptr;
  tipFor = nullptr;
}
bool AnyPopoverOpen() {
  for (auto* p : popovers)
    if (p->open) return true;
  return false;
}
bool IsCaptured(const Widget* w) { return captured == w; }
void Release() { captured = nullptr; }

void PointerMove(const PointerEvent& e) {
  for (auto* p : popovers)
    if (auto* m = dynamic_cast<Menu*>(p); m && m->open) m->PointerAt(e.x, e.y);
  if (captured) {
    captured->OnDrag(e);
    SetHovered(captured->HitTest(e.x, e.y) ? captured : nullptr);
  } else {
    SetHovered(HitAll(e.x, e.y));
  }
  RequestFrame();
}

void SetGate(std::function<bool(Widget*, const PointerEvent&)> g) { gate = std::move(g); }
Popover* PopoverOf(Widget* w) {
  for (; w; w = w->parent)
    if (auto* p = dynamic_cast<Popover*>(w)) return p;
  return nullptr;
}

void PointerDown(const PointerEvent& e) {
  tipFor = nullptr;
  tipAlpha.To(0);
  Widget* hit = HitAll(e.x, e.y);
  if (gate && !gate(hit, e)) {
    RequestFrame();
    return;
  }
  // A click outside open menus closes them and does nothing else (pinned tool panels stay, and
  // let the click through).
  if (!InPopover(hit)) {
    bool closable = false;
    for (auto* p : popovers) closable |= p->open && !p->modal && !p->pinned;
    if (closable) {
      bool hadModal = TopModal() != nullptr;
      ClosePopovers();
      if (!hadModal) {
        RequestFrame();
        return;
      }
    }
  }
  captured = hit;
  if (captured) captured->OnDown(e);
  RequestFrame();
}

void PointerUp(const PointerEvent& e) {
  Widget* c = captured;
  captured = nullptr;
  Widget* hit = HitAll(e.x, e.y);
  if (c) {
    bool inside = c->HitTest(e.x, e.y) != nullptr;
    // Press on a menu title, drag onto a row, release: the row is chosen.
    if (!inside && hit && InPopover(hit) && !InPopover(c)) {
      c->OnUp(e, false);
      hit->OnUp(e, true);
    } else {
      c->OnUp(e, inside);
    }
  }
  SetHovered(HitAll(e.x, e.y));
  RequestFrame();
}

bool Wheel(float delta, const PointerEvent& e) {
  for (Widget* w = HitAll(e.x, e.y); w; w = w->parent)
    if (w->OnWheel(delta, e)) { RequestFrame(); return true; }
  return false;
}

bool Key(UINT vk, Mods m) {
  for (auto it = popovers.rbegin(); it != popovers.rend(); ++it)
    if ((*it)->open) {
      (*it)->OnKey(vk, m);
      RequestFrame();
      return true;  // an open popover owns the keyboard
    }
  return false;
}
bool Char(wchar_t c) {
  for (auto it = popovers.rbegin(); it != popovers.rend(); ++it) if ((*it)->open) return (*it)->OnChar(c);
  return false;
}

void PointerLeave() { SetHovered(nullptr); }

bool Animate(float dt) {
  bool moving = false;
  for (auto* l : layers) moving |= l->Animate(dt);
  for (size_t i = 0; i < popovers.size(); i++) moving |= popovers[i]->Animate(dt);
  // Finished popovers leave the list; their owners keep the objects.
  popovers.erase(std::remove_if(popovers.begin(), popovers.end(), [](Popover* p) { return !p->Showing(); }), popovers.end());
  // Tooltips: only for a calm hover with no button held.
  bool wantsTip = hovered && !captured && !AnyPopoverOpen() && !hovered->Tooltip().empty();
  if (wantsTip && tipFor != hovered) {
    if (NowMs() - hoverSince > 650) {
      tipFor = hovered;
      tipText = hovered->Tooltip();
      tipAnchor = hovered->rect;
      tipAlpha.To(1);
    }
    moving = true;  // wait for it
  }
  moving |= tipAlpha.Step(dt);
  scrim.To(TopModal() ? 1.f : 0.f);
  moving |= scrim.Step(dt);
  double now = NowMs();
  for (auto& t : toasts) {
    t.a.To(now - t.born < 2600 ? 1.f : 0.f);
    t.a.Step(dt);
    moving = true;
  }
  toasts.erase(std::remove_if(toasts.begin(), toasts.end(), [&](const ToastItem& t) { return now - t.born > 2600 && t.a.v < .01f; }), toasts.end());
  return moving;
}

void Paint() {
  for (auto* l : layers)
    if (l->visible) l->Paint();
  if (scrim.v > .004f) FillRect(bounds, Rgba(0, 0, 0, .28f * scrim.v));
  for (auto* p : popovers) p->Paint();
  // Toasts: bottom centre, above the status capsule.
  float y = bounds.bottom - 70;
  for (auto it = toasts.rbegin(); it != toasts.rend(); ++it) {
    float w = Measure(it->text, F_Row) + (it->error ? 54.f : 40.f);
    float h = 38;
    float a = std::clamp(it->a.v, 0.f, 1.f);
    Rect r = RWH(MidX(bounds) - w / 2, y - h + (1 - a) * 12, w, h);
    Glass(r, h / 2, a, GlassKind::Popover);
    float tx = r.left + 20;
    if (it->error) { FillEllipse(tx + 3, MidY(r), 4, 4, Fade(P.accent, a)); tx += 14; }
    Text(it->text, R(tx, r.top, r.right - 16, r.bottom), F_Row, Fade(P.text, a));
    y -= h + 8;
  }
  if (tipAlpha.v > .01f && !tipText.empty()) {
    float w = Measure(tipText, F_Small) + 20, h = 26;
    float x = std::clamp(MidX(tipAnchor) - w / 2, bounds.left + 6, bounds.right - w - 6);
    float ty = tipAnchor.bottom + 8;
    if (ty + h > bounds.bottom - 6) ty = tipAnchor.top - h - 8;
    // Beside a vertical dock the tooltip goes to the side of the control.
    if (W(tipAnchor) < 60 && tipAnchor.left < 120) { x = tipAnchor.right + 12; ty = MidY(tipAnchor) - h / 2; }
    float a = std::clamp(tipAlpha.v, 0.f, 1.f);
    Rect r = RWH(x, ty + (1 - a) * 4, w, h);
    Glass(r, 9, a, GlassKind::Popover);
    Text(tipText, r, F_Small, Fade(P.text, a), Align::Center);
  }
}

void Toast(const std::wstring& message, bool error) {
  ToastItem t;
  t.text = message;
  t.error = error;
  t.born = NowMs();
  toasts.push_back(std::move(t));
  if (toasts.size() > 3) toasts.erase(toasts.begin());
  RequestFrame();
}
}  // namespace ui

// ============================================================================= widget ==
Widget::~Widget() {
  // Document refresh can replace rows during their click callback.
  if (ui::hovered == this) ui::hovered = nullptr;
  if (ui::captured == this) ui::captured = nullptr;
  if (ui::tipFor == this) { ui::tipFor = nullptr; ui::tipAlpha.To(0); }
}
bool Widget::Animate(float dt) {
  bool a = hover.Step(dt);
  bool b = press.Step(dt);
  return a || b;
}
Widget* Widget::HitTest(float x, float y) { return visible && Inside(rect, x, y) ? this : nullptr; }
void Widget::OnHover(bool inside) { hover.To(inside && enabled ? 1.f : 0.f); }
void Widget::OnUp(const PointerEvent&, bool) { press.To(0); }

void Container::Paint() {
  for (auto& c : children)
    if (c->visible) c->Paint();
}
bool Container::Animate(float dt) {
  bool moving = Widget::Animate(dt);
  for (auto& c : children) moving |= c->Animate(dt);
  return moving;
}
Widget* Container::HitTest(float x, float y) {
  if (!visible) return nullptr;
  for (auto it = children.rbegin(); it != children.rend(); ++it) {
    if (!(*it)->visible) continue;
    if (Widget* h = (*it)->HitTest(x, y); h && h->Interactive()) return h;
  }
  return Interactive() && Inside(rect, x, y) ? this : nullptr;
}

// ========================================================================= IconButton ==
bool IconButton::Animate(float dt) {
  bool moving = Widget::Animate(dt);
  moving |= selected.Step(dt);
  // Long press opens the flyout.
  if (ui::IsCaptured(this) && onLongPress && !longFired && downButton == 0) {
    if (ui::NowMs() - downAt > 380) {
      longFired = true;
      press.To(0);
      onLongPress();
    }
    moving = true;
  }
  return moving;
}
void IconButton::Paint() {
  float s = PressScale();
  float lift = -1.2f * hover.v * (1 - selected.v);
  Rect r = Offset(rect, 0, lift);
  float cx = MidX(r), cy = MidY(r);
  Rect plate = R(cx - W(r) / 2 * s, cy - H(r) / 2 * s, cx + W(r) / 2 * s, cy + H(r) / 2 * s);
  float a = enabled ? 1.f : .35f;
  FillRound(plate, radius, Fade(P.hover, std::max(hover.v * (1 - selected.v), 0.f)));
  FillRound(plate, radius, Fade(P.press, std::clamp(press.v, 0.f, 1.f)));
  if (selected.v > .01f) {
    FillRound(plate, radius, Fade(P.selected, selected.v));
    StrokeRound(Inset(plate, .5f), radius, Rgba(1, 1, 1, .10f * selected.v), 1);
  }
  D2D1_COLOR_F base = tint.a > 0 ? tint : P.icon;
  D2D1_COLOR_F c = Mix(base, Rgba(1, .98f, .95f, 1), std::clamp(std::max(hover.v * .6f, selected.v), 0.f, 1.f));
  IconAt(icon, cx, cy, iconSize * s, Fade(c, a), stroked);
  if (flyout) {
    float x = plate.right - 6, y = plate.bottom - 6;
    D2D1_COLOR_F m = Fade(Mix(P.faint, P.text, std::clamp(selected.v, 0.f, 1.f)), a);
    Line(x - 3.5f, y, x, y, m, 1.2f);
    Line(x, y - 3.5f, x, y, m, 1.2f);
  }
}
void IconButton::OnDown(const PointerEvent& e) {
  if (!enabled) return;
  press.To(1);
  downAt = ui::NowMs();
  longFired = false;
  downButton = e.button;
  if (e.button == 1 && onLongPress) {
    longFired = true;
    press.To(0);
    onLongPress();
  }
}
void IconButton::OnUp(const PointerEvent& e, bool inside) {
  press.To(0);
  press.Nudge(-6);  // the elastic return after compression
  if (enabled && inside && !longFired && e.button == 0) {
    if (e.clicks == 2 && onDoubleClick) onDoubleClick();
    else if (onClick) onClick();
  }
}

// ========================================================================= TextButton ==
void TextButton::Paint() {
  float s = PressScale();
  Rect r = rect;
  float cx = MidX(r), cy = MidY(r);
  Rect plate = R(cx - W(r) / 2 * s, cy - H(r) / 2 * s, cx + W(r) / 2 * s, cy + H(r) / 2 * s);
  float a = enabled ? 1.f : .38f;
  float radius = style == Quiet ? 9.f : H(r) / 2;
  if (style == Primary) {
    FillRound(plate, radius, Fade(Mix(Rgba(.95f, .89f, .78f), Rgba(1, .96f, .89f), hover.v), a));
    Text(label, plate, face, Fade(Rgba(.10f, .09f, .08f), a), Align::Center);
    return;
  }
  if (style == Secondary) FillRound(plate, radius, Fade(Rgba(1, 1, 1, .07f + .05f * hover.v), a));
  else FillRound(plate, radius, Fade(P.hover, hover.v));
  if (active) FillRound(plate, radius, P.selected);
  FillRound(plate, radius, Fade(P.press, std::clamp(press.v, 0.f, 1.f)));
  D2D1_COLOR_F c = style == Quiet ? Mix(P.dim, P.text, std::max(hover.v, active ? 1.f : 0.f)) : P.text;
  Text(label, plate, face, Fade(c, a), Align::Center);
}
void TextButton::OnDown(const PointerEvent&) {
  if (!enabled) return;
  press.To(1);
  if (onPressed) onPressed();
}
void TextButton::OnUp(const PointerEvent& e, bool inside) {
  press.To(0);
  press.Nudge(-6);
  if (enabled && inside && e.button == 0 && onClick) onClick();
}
void TextButton::OnHover(bool inside) {
  Widget::OnHover(inside);
  if (inside && onHoverEnter) onHoverEnter();
}

void Label::Paint() { Text(text, rect, face, colour.a > 0 ? colour : P.dim, align); }

namespace {
class NumberInput : public Popover {
 public:
  std::wstring text;
  std::function<void(double)> commit;
  double min = 0, max = 1;
  bool replace = true;
  D2D1_SIZE_F Natural() override { return {160, 52}; }
  void PaintContent() override {
    FillRound(Inset(target, 6), tok::RadiusControl, P.selected);
    Text(text + (fmod(ui::NowMs(), 1000) < 600 ? L"|" : L""), Inset(target, 12, 0), F_Value, P.text);
  }
  bool Animate(float dt) override { return Popover::Animate(dt) || open; }
  bool OnKey(UINT vk, Mods) override {
    if (vk == VK_ESCAPE) { Close(); return true; }
    if (vk == VK_BACK) { if (replace) text.clear(); else if (!text.empty()) text.pop_back(); replace = false; return true; }
    if (vk == VK_RETURN || vk == VK_TAB) {
      std::wstring number = text; std::replace(number.begin(), number.end(), L',', L'.');
      wchar_t* end = nullptr; double v = wcstod(number.c_str(), &end);
      if (end && end != number.c_str() && !*end && std::isfinite(v)) { Close(); if (commit) commit(std::clamp(v, min, max)); }
      return true;
    }
    return true;
  }
  bool OnChar(wchar_t c) override {
    if (c >= L'0' && c <= L'9' || c == L'-' || c == L'+' || c == L'.' || c == L',') {
      if (replace) { text.clear(); replace = false; }
      if (text.size() < 24) text += c;
    }
    return true;
  }
};
std::unique_ptr<NumberInput> numberInput;
class StringInput : public Popover {
 public:
  std::wstring text;
  bool replace = true;
  std::function<void(const std::wstring&)> commit;
  D2D1_SIZE_F Natural() override { return {320,48}; }
  void PaintContent() override { FillRound(Inset(target,6,6),8,P.hover); Text(text+L"│",Inset(target,12,0),F_Row,P.text); }
  bool OnKey(UINT vk,Mods m) override {
    if (m.ctrl && vk == 'A') { replace = true; return true; }
    if (vk == VK_ESCAPE) { Close(); return true; }
    if (vk == VK_BACK) { if (replace) text.clear(); else if (!text.empty()) text.pop_back(); replace = false; return true; }
    if (vk == VK_RETURN || vk == VK_TAB) { Close(); auto f = commit; auto value = text; if (f) f(value); return true; }
    return true;
  }
  bool OnChar(wchar_t c) override { if (c >= 32 && text.size() < 4096) { if (replace) { text.clear(); replace = false; } text += c; } return true; }
};
std::unique_ptr<StringInput> stringInput;
}
void TextField::Paint() { FillRound(rect,tok::RadiusControl,Fade(P.hover,.6f+hover.v)); Text(label+L"  "+value,Inset(rect,9,0),F_Label,enabled ? P.text : P.faint); }
void TextField::OnUp(const PointerEvent& e,bool inside) {
  Widget::OnUp(e,inside); if (!inside || !enabled || e.button != 0) return;
  stringInput = std::make_unique<StringInput>(); stringInput->text = value; auto callback = onChange;
  stringInput->commit = [this,callback,life=std::weak_ptr<int>(lifetime)](const std::wstring& v) { if(life.expired())return;value = v; if (callback) callback(v); };
  stringInput->Open(rect,Side::Below);
}
std::wstring NumberField::ValueText() const { wchar_t b[64]{}; swprintf(b, 64, L"%.*f%ls", decimals, value, i18n::Tr(unit).c_str()); return b; }
float NumberField::Natural() const { return std::max(82.f, Measure(label, F_Label) + Measure(L"-0000.0" + unit, F_Value) + 22); }
void NumberField::Paint() {
  FillRound(rect, tok::RadiusControl, Fade(P.hover, .6f + hover.v));
  Text(label, R(rect.left + 8, rect.top, rect.left + 8 + Measure(label, F_Label), rect.bottom), F_Label, P.dim);
  Text(ValueText(), R(rect.left + 12 + Measure(label, F_Label), rect.top, rect.right - 8, rect.bottom), F_Value, enabled ? P.text : Fade(P.dim,.5f), Align::Trailing);
}
void NumberField::OnDown(const PointerEvent& e) { if (!enabled) return; startX = e.x; startValue = value; scrubbed = false; press.To(1); }
void NumberField::OnDrag(const PointerEvent& e) {
  if (!enabled) return;
  if (std::abs(e.x - startX) < 3 && !scrubbed) return;
  scrubbed = true; Set(startValue + (e.x - startX) * step * (e.mods.shift ? 10 : e.mods.alt ? .1 : 1)); if (onChange) onChange(value);
}
void NumberField::OnUp(const PointerEvent& e, bool inside) {
  Widget::OnUp(e, inside); if (!enabled || e.button != 0) return;
  if (scrubbed) { if (onCommit) onCommit(value); return; }
  if (!inside) return;
  numberInput = std::make_unique<NumberInput>(); wchar_t numeric[64]{}; swprintf(numeric,64,L"%.*f",decimals,value); numberInput->text = numeric;
  numberInput->min = min; numberInput->max = max;
  auto change = onChange, done = onCommit;
  numberInput->commit = [this, change, done,life=std::weak_ptr<int>(lifetime)](double v) { if(life.expired())return;Set(v); if (change) change(v); if (done) done(v); };
  numberInput->Open(rect, Side::Below);
}
bool NumberField::OnWheel(float delta, const PointerEvent& e) {
  if (!enabled) return false;
  Set(value + delta * step * (e.mods.shift ? 10 : 1)); auto change = onChange, commit = onCommit; double v = value; if (change) change(v); if (commit) commit(v); return true;
}

// ============================================================================= Slider ==
double Slider::Norm(double v) const {
  if (max <= min) return 0;
  if (logScale) {
    if (min > 0) return std::log(v / min) / std::log(max / min);
    return std::log1p(v - min) / std::log1p(max - min);
  }
  return (v - min) / (max - min);
}
double Slider::FromNorm(double t) const {
  t = std::clamp(t, 0.0, 1.0);
  double v;
  if (logScale) v = min > 0 ? min * std::pow(max / min, t) : min + std::expm1(t * std::log1p(max - min));
  else v = min + (max - min) * t;
  double q = std::pow(10.0, decimals);
  return std::clamp(std::round(v * q) / q, min, max);
}
float Slider::ValueColumn() const {
  if (unit.empty() || unit == L"%") return 50;
  return std::max(50.f, Measure(L"0000" + i18n::Tr(unit), F_Value) + 10);
}
std::wstring Slider::ValueText() const {
  wchar_t buf[64];
  swprintf(buf, 64, L"%.*f%s", decimals, value, i18n::Tr(unit).c_str());
  return buf;
}
Rect Slider::Track() const {
  if (compact) return R(rect.left + LabelColumn(label, labelWidth), MidY(rect) - 2, rect.right - ValueColumn(), MidY(rect) + 2);
  return R(rect.left + 2, rect.bottom - 10, rect.right - 2, rect.bottom - 6);
}
bool Slider::Animate(float dt) {
  if (fill.v < -.5f) fill.Reset(float(Norm(value)));
  bool moving = Widget::Animate(dt);
  moving |= fill.Step(dt);
  moving |= grab.Step(dt);
  return moving;
}
void Slider::Paint() {
  float a = enabled ? 1.f : .4f;
  Rect t = Track();
  if (compact) {
    Text(label, R(rect.left, rect.top, rect.left + LabelColumn(label, labelWidth) - 8, rect.bottom), F_Label, Fade(P.dim, a));
    Text(ValueText(), R(t.right + 6, rect.top, rect.right, rect.bottom), F_Value, Fade(P.text, a), Align::Trailing);
  } else {
    Text(label, R(rect.left, rect.top, rect.right - 70, rect.bottom - 14), F_Label, Fade(P.dim, a));
    Text(ValueText(), R(rect.right - 90, rect.top, rect.right, rect.bottom - 14), F_Value, Fade(P.text, a), Align::Trailing);
  }
  float f = std::clamp(fill.v < -.5f ? float(Norm(value)) : fill.v, 0.f, 1.f);
  FillRound(t, 2, Fade(P.track, a * (1 + .4f * hover.v)));
  float x = t.left + W(t) * f;
  FillRound(R(t.left, t.top, x, t.bottom), 2, Fade(Rgba(.99f, .93f, .81f, .88f), a));
  float r = 6.f + 1.5f * hover.v + 2.f * grab.v;
  FillEllipse(x, MidY(t) + .8f, r + 1.5f, r + 1.5f, Rgba(0, 0, 0, .22f * a));
  FillEllipse(x, MidY(t), r, r, Fade(Rgba(.99f, .95f, .88f), a));
  if (grab.v > .01f) FillEllipse(x, MidY(t), 2.4f * grab.v, 2.4f * grab.v, Fade(P.accent, std::clamp(grab.v, 0.f, 1.f)));
}
void Slider::FromX(float px) {
  Rect t = Track();
  double v = FromNorm((px - t.left) / std::max(1.f, W(t)));
  if (v != value) {
    value = v;
    fill.To(float(Norm(value)));
    if (onChange) onChange(value);
  }
}
void Slider::OnDown(const PointerEvent& e) {
  if (!enabled) return;
  if (e.clicks >= 2) {
    Set(def);
    if (onChange) onChange(value);
    if (onCommit) onCommit(value);
    return;
  }
  dragging = true;
  grab.To(1);
  FromX(e.x);
}
void Slider::OnDrag(const PointerEvent& e) {
  if (dragging) FromX(e.x);
}
void Slider::OnUp(const PointerEvent& e, bool inside) {
  Widget::OnUp(e, inside);
  grab.To(0);
  if (dragging && onCommit) onCommit(value);
  dragging = false;
}
bool Slider::OnWheel(float delta, const PointerEvent& e) {
  if (!enabled) return false;
  double q = std::pow(10.0, decimals);
  double v;
  if (logScale) {
    v = FromNorm(Norm(value) + (delta > 0 ? 1 : -1) * (e.mods.shift ? .002 : .01));
  } else {
    double step = std::max((max - min) / 100.0, 1.0 / q);
    v = std::clamp(value + (delta > 0 ? 1 : -1) * step * (e.mods.shift ? .2 : 1), min, max);
    v = std::round(v * q) / q;
  }
  Set(v);
  if (onChange) onChange(value);
  if (onCommit) onCommit(value);
  return true;
}

// ============================================================================= Toggle ==
bool Toggle::Animate(float dt) {
  bool moving = Widget::Animate(dt);
  return knob.Step(dt) || moving;
}
void Toggle::Paint() {
  float a = enabled ? 1.f : .4f;
  Rect sw = R(rect.right - 38, MidY(rect) - 11, rect.right, MidY(rect) + 11);
  if (!label.empty()) Text(label, R(rect.left, rect.top, sw.left - 8, rect.bottom), F_Label, Fade(P.dim, a));
  float k = std::clamp(knob.v, 0.f, 1.f);
  FillRound(sw, 11, Fade(Mix(Rgba(1, 1, 1, .14f), Fade(P.accent, .92f), k), a));
  StrokeRound(sw, 11, Rgba(1, 1, 1, .08f * a), 1);
  float r = 8.f * PressScale();
  float x = sw.left + 11 + (W(sw) - 22) * knob.v;
  FillEllipse(x, MidY(sw) + .8f, r + 1, r + 1, Rgba(0, 0, 0, .18f * a));
  FillEllipse(x, MidY(sw), r, r, Fade(Rgba(1, .97f, .92f), a));
}
void Toggle::OnUp(const PointerEvent& e, bool inside) {
  Widget::OnUp(e, inside);
  if (!enabled || !inside) return;
  Set(!on);
  if (onChange) onChange(on);
}

// ========================================================================== Segmented ==
bool Segmented::Animate(float dt) {
  bool moving = Widget::Animate(dt);
  return slide.Step(dt) || moving;
}
void Segmented::Paint() {
  float a = enabled ? 1.f : .4f;
  size_t n = std::max<size_t>(1, options.size());
  FillRound(rect, H(rect) / 2, Fade(P.sunk, a));
  float w = W(rect) / float(n);
  Rect hi = RWH(rect.left + 2 + w * slide.v, rect.top + 2, w - 4, H(rect) - 4);
  FillRound(hi, H(hi) / 2, Fade(Rgba(1, 1, 1, .15f), a));
  StrokeRound(hi, H(hi) / 2, Fade(Rgba(1, 1, 1, .10f), a), 1);
  for (size_t i = 0; i < n; i++) {
    Rect cell = RWH(rect.left + w * float(i), rect.top, w, H(rect));
    float sel = std::clamp(1.f - std::fabs(slide.v - float(i)), 0.f, 1.f);
    D2D1_COLOR_F c = Fade(Mix(P.dim, P.text, sel), a);
    if (i < icons.size() && icons[i]) IconAt(icons[i], MidX(cell), MidY(cell), 16, c, iconsStroked);
    else if (i < options.size()) Text(options[i], cell, F_Small, c, Align::Center);
  }
}
void Segmented::OnUp(const PointerEvent& e, bool inside) {
  Widget::OnUp(e, inside);
  if (!enabled || !inside || options.empty()) return;
  int i = std::clamp(int((e.x - rect.left) / (W(rect) / float(options.size()))), 0, int(options.size()) - 1);
  if (i != index) {
    Set(i);
    if (onChange) onChange(i);
  }
}

// =============================================================================== Tabs ==
std::vector<Rect> Tabs::TabRects() const {
  std::vector<Rect> out;
  float x = rect.left;
  for (auto& l : labels) {
    float w = Measure(l, F_Button) + 22;
    out.push_back(RWH(x, rect.top, w, H(rect)));
    x += w + 2;
  }
  return out;
}
bool Tabs::Animate(float dt) {
  auto rs = TabRects();
  if (index >= 0 && size_t(index) < rs.size()) {
    float px = rs[size_t(index)].left - rect.left, pw = W(rs[size_t(index)]);
    if (plateX.v < -.5f) { plateX.Reset(px); plateW.Reset(pw); }
    plateX.To(px);
    plateW.To(pw);
  }
  bool moving = Widget::Animate(dt);
  moving |= plateX.Step(dt);
  moving |= plateW.Step(dt);
  return moving;
}
void Tabs::Paint() {
  auto rs = TabRects();
  if (plateX.v > -.5f) {
    Rect plate = RWH(rect.left + plateX.v, rect.top, plateW.v, H(rect));
    FillRound(plate, H(plate) / 2, Rgba(1, 1, 1, .11f));
  }
  for (size_t i = 0; i < rs.size(); i++) Text(labels[i], rs[i], F_Button, int(i) == index ? P.text : P.dim, Align::Center);
}
void Tabs::OnUp(const PointerEvent& e, bool inside) {
  Widget::OnUp(e, inside);
  if (!inside) return;
  auto rs = TabRects();
  for (size_t i = 0; i < rs.size(); i++)
    if (Inside(rs[i], e.x, e.y) && int(i) != index) {
      index = int(i);
      if (onChange) onChange(index);
    }
}

// ============================================================================ Popover ==
Popover::~Popover() { ui::Forget(this); }

void Popover::Open(const Rect& from, Side s) {
  anchor = from;
  side = s;
  D2D1_SIZE_F n = Natural();
  Rect win = ui::Bounds();
  constexpr float gap = 8, edge = 10;
  float x = from.left, y = from.bottom + gap;
  switch (s) {
    case Side::Below: x = from.left; y = from.bottom + gap; break;
    case Side::Above: x = from.left; y = from.top - gap - n.height; break;
    case Side::Right: x = from.right + gap; y = from.top - 6; break;
    case Side::Left: x = from.left - gap - n.width; y = from.top - 6; break;
    case Side::Center: x = MidX(win) - n.width / 2; y = MidY(win) - n.height / 2; break;
  }
  if (s == Side::Right && x + n.width > win.right - edge) x = from.left - gap - n.width;  // flip
  if (s == Side::Below && y + n.height > win.bottom - edge && from.top - gap - n.height > win.top + edge) y = from.top - gap - n.height;
  x = std::clamp(x, win.left + edge, std::max(win.left + edge, win.right - edge - n.width));
  y = std::clamp(y, win.top + edge, std::max(win.top + edge, win.bottom - edge - n.height));
  target = RWH(x, y, n.width, std::min(n.height, H(win) - 2 * edge));
  auto o = motion::MenuOpen();
  auto c = motion::ContentIn();
  shell.Tune(o.k, o.c);
  content.Tune(c.k, c.c);
  if (!Showing()) { shell.Reset(0); content.Reset(0); }
  shell.To(1);
  content.To(1);
  open = true;
  Arrange();
  ui::OpenPopover(this);
}
void Popover::Close() {
  if (!open) return;
  open = false;
  auto o = motion::MenuClose();
  auto c = motion::ContentOut();
  shell.Tune(o.k, o.c);
  content.Tune(c.k, c.c);
  shell.To(0);
  content.To(0);
  if (onClosed) onClosed();
  ui::RequestFrame();
}
Rect Popover::Bounds() const {
  // The direct path of the morph: from the anchor straight to the target, overshoot included --
  // clamping at 1 would hide the spring's flight and show only its return (StagedSheetMotion).
  float t = std::max(0.f, shell.v);
  Rect from = anchor;
  if (side == Side::Center) from = RWH(MidX(anchor) - 20, MidY(anchor) - 20, 40, 40);
  return Lerp(from, target, t);
}
float Popover::Radius() const {
  Rect b = Bounds();
  float half = std::min(W(b), H(b)) / 2;
  float origin = std::min(tok::RadiusControl, std::min(W(anchor), H(anchor)) / 2);
  return std::min(std::max(CornerRadius(), std::min(origin, half)), half);
}
D2D1_MATRIX_3X2_F Popover::ContentTransform() const {
  float s = motion::ContentScale(shell.v);
  // Content grows from the corner the surface grows from, so text stays pinned to its edge.
  D2D1_POINT_2F o{MidX(target), target.top};
  switch (side) {
    case Side::Below: o = {std::clamp(MidX(anchor), target.left, target.right), target.top}; break;
    case Side::Above: o = {std::clamp(MidX(anchor), target.left, target.right), target.bottom}; break;
    case Side::Right: o = {target.left, std::clamp(MidY(anchor), target.top, target.bottom)}; break;
    case Side::Left: o = {target.right, std::clamp(MidY(anchor), target.top, target.bottom)}; break;
    case Side::Center: o = {MidX(target), MidY(target)}; break;
  }
  return D2D1::Matrix3x2F::Scale(s, s, o);
}
D2D_POINT_2F Popover::ToContent(float x, float y) const {
  D2D1_MATRIX_3X2_F raw = ContentTransform();
  D2D1::Matrix3x2F m = *D2D1::Matrix3x2F::ReinterpretBaseType(&raw);
  if (!m.Invert()) return {x, y};
  return m.TransformPoint({x, y});
}
bool Popover::Animate(float dt) {
  bool moving = Container::Animate(dt);
  moving |= shell.Step(dt);
  moving |= content.Step(dt);
  return moving;
}
void Popover::Paint() {
  if (!Showing()) return;
  Rect b = Bounds();
  if (W(b) < 1 || H(b) < 1) return;
  float glassA = std::clamp(shell.v / .12f, 0.f, 1.f);
  float r = Radius();
  Glass(b, r, glassA, GlassKind::Popover);
  float reveal = std::clamp((shell.v - .25f) / .2f, 0.f, 1.f);
  float ca = std::clamp(content.v, 0.f, 1.f) * reveal;
  if (ca <= .004f) return;
  PushClip(b, r);
  PushTransform(ContentTransform());
  PushOpacity(ca);
  PaintContent();
  Container::Paint();
  PopOpacity();
  PopTransform();
  PopClip();
}
Widget* Popover::HitTest(float x, float y) {
  if (!open || !Inside(Bounds(), x, y)) return nullptr;
  auto p = ToContent(x, y);
  for (auto it = children.rbegin(); it != children.rend(); ++it)
    if ((*it)->visible)
      if (Widget* h = (*it)->HitTest(p.x, p.y); h && h->Interactive()) return h;
  return this;
}
bool Popover::OnKey(UINT vk, Mods) {
  if (vk == VK_ESCAPE) {
    Close();
    return true;
  }
  return false;
}

// =============================================================================== Menu ==
D2D1_SIZE_F Menu::Natural() {
  float w = minWidth, h = 12;
  for (auto& it : items) {
    if (it.separator) { h += 9; continue; }
    float row = 46 + Measure(it.label, F_Row) + (it.shortcut.empty() ? 0 : Measure(it.shortcut, F_Small) + 28) + (it.submenu.empty() ? 0 : 22) + 14;
    w = std::max(w, row);
    h += 30;
  }
  return {std::min(w, 460.f), h};
}
float Menu::RowTop(int i) const {
  float y = target.top + 6 - scroll;
  for (int k = 0; k < i && size_t(k) < items.size(); k++) y += RowH(k);
  return y;
}
int Menu::RowAt(float y) const {
  float top = target.top + 6 - scroll;
  for (size_t i = 0; i < items.size(); i++) {
    float h = RowH(int(i));
    if (y >= top && y < top + h) return int(i);
    top += h;
  }
  return -1;
}
bool Menu::Animate(float dt) {
  bool moving = Popover::Animate(dt);
  moving |= plateY.Step(dt);
  moving |= plateA.Step(dt);
  // A row held under the pointer for a moment opens its submenu.
  if (open && hot >= 0 && size_t(hot) < items.size() && !items[size_t(hot)].submenu.empty() && childIndex != hot) {
    if (ui::NowMs() - hotSince > 150) OpenChild(hot);
    else moving = true;
  }
  if (!open && child && child->open) child->CloseTree();
  return moving;
}
void Menu::Paint() { Popover::Paint(); }
void Menu::PaintContent() {
  int n = int(items.size());
  float rowsH = 0;
  for (int i = 0; i < n; i++) rowsH += RowH(i);
  bool scrolls = rowsH + 12 > H(target) + .5f;
  if (scrolls) PushClip(Inset(target, 0, 4));
  if (plateA.v > .01f && hot >= 0) {
    Rect plate = R(target.left + 6, plateY.v, target.right - 6, plateY.v + 30);
    FillRound(plate, 9, Fade(Rgba(1, 1, 1, .10f), std::clamp(plateA.v, 0.f, 1.f)));
  }
  for (int i = 0; i < n; i++) {
    const MenuItem& it = items[size_t(i)];
    float top = RowTop(i);
    if (top > target.bottom || top + RowH(i) < target.top) continue;
    // The cascade: rows nearest the anchor arrive first, each a little lifted (TtmMenu).
    int fromAnchor = side == Side::Above ? n - 1 - i : i;
    float reveal = open ? motion::RowReveal(shell.v, fromAnchor) : 1.f;
    float lift = (1 - reveal) * motion::RowLift * (side == Side::Above ? 1.f : -1.f);
    if (it.separator) {
      Line(target.left + 14, top + 4.5f + lift, target.right - 14, top + 4.5f + lift, Fade(P.sep, reveal), 1);
      continue;
    }
    Rect row = R(target.left + 6, top + lift, target.right - 6, top + 30 + lift);
    float a = (it.enabled ? 1.f : .36f) * reveal;
    bool isHot = i == hot && it.enabled;
    if (it.checked) IconAt(PhCheck, row.left + 15, MidY(row), 14, Fade(P.text, a));
    else if (it.icon) IconAt(it.icon, row.left + 15, MidY(row), 16, Fade(P.icon, a), it.iconStroked);
    float right = row.right - 10;
    if (!it.submenu.empty()) {
      IconAt(PhCaretRight, row.right - 14, MidY(row), 12, Fade(P.dim, a));
      right -= 18;
    }
    if (!it.shortcut.empty()) {
      float sw = Measure(it.shortcut, F_Small);
      Text(it.shortcut, R(right - sw, row.top, right, row.bottom), F_Small, Fade(P.faint, a), Align::Trailing);
      right -= sw + 16;
    }
    Text(it.label, R(row.left + 32, row.top, right, row.bottom), F_Row, Fade(isHot ? P.text : Mix(P.dim, P.text, .55f), a));
  }
  if (scrolls) {
    PopClip();
    float vis = H(target) / (rowsH + 12);
    float th = std::max(24.f, H(target) * vis);
    float ty = target.top + (H(target) - th) * (scroll / std::max(1.f, rowsH + 12 - H(target)));
    FillRound(R(target.right - 5, ty + 4, target.right - 2, ty + th - 4), 2, Rgba(1, 1, 1, .18f));
  }
}
Widget* Menu::HitTest(float x, float y) { return Popover::HitTest(x, y); }
void Menu::PointerAt(float x, float y) {
  if (!open) return;
  if (!Inside(Bounds(), x, y)) {
    // Leaving towards an open submenu keeps its row lit.
    if (child && child->open) return;
    if (hot != -1) { hot = -1; plateA.To(0); }
    return;
  }
  auto p = ToContent(x, y);
  int i = RowAt(p.y);
  if (i >= 0 && (items[size_t(i)].separator || !items[size_t(i)].enabled)) i = -1;
  if (i != hot) {
    hot = i;
    hotSince = ui::NowMs();
    if (i >= 0) {
      float top = RowTop(i);
      if (plateA.v < .05f) plateY.Reset(top);
      plateY.To(top);
      plateA.To(1);
    } else {
      plateA.To(0);
    }
    if (child && child->open && childIndex != i) {
      child->CloseTree();
      childIndex = -1;
    }
  }
}
void Menu::OnHover(bool inside) { Widget::OnHover(inside); }
void Menu::OnDrag(const PointerEvent& e) { PointerAt(e.x, e.y); }
void Menu::OnUp(const PointerEvent& e, bool inside) {
  Widget::OnUp(e, inside);
  if (!inside || !open) return;
  PointerAt(e.x, e.y);
  if (hot >= 0) Activate(hot);
}
bool Menu::OnWheel(float delta, const PointerEvent&) {
  float rowsH = 0;
  for (int i = 0; i < int(items.size()); i++) rowsH += RowH(i);
  float maxScroll = std::max(0.f, rowsH + 12 - H(target));
  scroll = std::clamp(scroll - delta * 0.6f, 0.f, maxScroll);
  return true;
}
void Menu::CloseTree() {
  if (child) child->CloseTree();
  childIndex = -1;
  Close();
}
void Menu::Activate(int i) {
  if (i < 0 || size_t(i) >= items.size()) return;
  MenuItem& it = items[size_t(i)];
  if (!it.enabled || it.separator) return;
  if (!it.submenu.empty()) {
    OpenChild(i);
    return;
  }
  auto action = it.action;
  Menu* root = this;
  while (root->owner && dynamic_cast<Menu*>(root->owner)) root = static_cast<Menu*>(root->owner);
  root->CloseTree();
  if (action) action();
}
void Menu::OpenChild(int i) {
  if (childIndex == i && child && child->open) return;
  if (child) child->CloseTree();
  child = std::make_unique<Menu>(items[size_t(i)].submenu);
  child->owner = this;
  childIndex = i;
  float top = RowTop(i);
  child->Open(R(target.left + 6, top, target.right - 6, top + 30), Side::Right);
}
bool Menu::OnKey(UINT vk, Mods m) {
  if (child && child->open) {
    if (vk == VK_LEFT || vk == VK_ESCAPE) { child->CloseTree(); childIndex = -1; return true; }
    return child->OnKey(vk, m);
  }
  auto step = [&](int dir) {
    int n = int(items.size());
    for (int k = 1; k <= n; k++) {
      int j = ((hot < 0 ? (dir > 0 ? -1 : n) : hot) + dir * k + n * 4) % n;
      if (!items[size_t(j)].separator && items[size_t(j)].enabled) {
        hot = j;
        float top = RowTop(j);
        if (plateA.v < .05f) plateY.Reset(top);
        plateY.To(top);
        plateA.To(1);
        return;
      }
    }
  };
  switch (vk) {
    case VK_DOWN: step(1); return true;
    case VK_UP: step(-1); return true;
    case VK_RETURN: case VK_SPACE: if (hot >= 0) Activate(hot); return true;
    case VK_RIGHT: if (hot >= 0 && !items[size_t(hot)].submenu.empty()) OpenChild(hot); return true;
    case VK_ESCAPE: case VK_LEFT: CloseTree(); return true;
  }
  return false;
}

// ========================================================================= SelectList ==
Rect SelectList::Field() const { return R(rect.left + LabelColumn(label, labelWidth), rect.top, rect.right, rect.bottom); }
bool SelectList::Animate(float dt) {
  chevron.To(menu && menu->open ? 1.f : 0.f);
  bool moving = Widget::Animate(dt);
  return chevron.Step(dt) || moving;
}
void SelectList::Paint() {
  float a = enabled ? 1.f : .4f;
  if (labelWidth > 0) Text(label, R(rect.left, rect.top, rect.left + LabelColumn(label, labelWidth) - 8, rect.bottom), F_Label, Fade(P.dim, a));
  Rect f = Field();
  FillRound(f, 10, Fade(Rgba(1, 1, 1, .065f + .045f * hover.v), a));
  StrokeRound(f, 10, Fade(Rgba(1, 1, 1, .07f), a), 1);
  std::wstring v = index >= 0 && size_t(index) < options.size() ? options[size_t(index)] : L"";
  Text(v, R(f.left + 12, f.top, f.right - 28, f.bottom), F_Row, Fade(P.text, a));
  Icon(PhCaretDown, RWH(f.right - 22, MidY(f) - 6, 12, 12), Fade(P.dim, a), false, 180.f * chevron.v);
}
void SelectList::OnUp(const PointerEvent& e, bool inside) {
  Widget::OnUp(e, inside);
  if (!enabled || !inside) return;
  std::vector<MenuItem> rows;
  for (size_t i = 0; i < options.size(); i++) {
    MenuItem m;
    m.label = options[i];
    m.checked = int(i) == index;
    m.action = [this, i] {
      index = int(i);
      if (onChange) onChange(index);
    };
    rows.push_back(std::move(m));
  }
  if (!menu) menu = std::make_unique<Menu>();
  menu->items = std::move(rows);
  menu->minWidth = W(Field());
  menu->Open(Field(), Side::Below);
}

// =============================================================================== Dock ==
void Dock::Place(const Rect& window, bool snap) {
  D2D1_SIZE_F n = Natural();
  Rect h = home ? home(window, n) : RWH(window.left + tok::Margin, window.top + tok::Margin, n.width, n.height);
  float tx = std::clamp(h.left + dragX, window.left + 4, std::max(window.left + 4, window.right - 4 - n.width));
  float ty = std::clamp(h.top + dragY, window.top + 4, std::max(window.top + 4, window.bottom - 4 - n.height));
  if (!placed || snap) {
    x.Reset(tx);
    y.Reset(ty);
    if (!placed) born = ui::NowMs();
    placed = true;
  }
  x.To(tx);
  y.To(ty);
  rect = RWH(x.v, y.v, n.width, n.height);
  Arrange();
}
bool Dock::Animate(float dt) {
  bool moving = Container::Animate(dt);
  if (ui::NowMs() - born >= revealDelay * 1000.0) reveal.To(1);
  else moving = true;
  moving |= reveal.Step(dt);
  moving |= x.Step(dt);
  moving |= y.Step(dt);
  rect = RWH(x.v, y.v, W(rect), H(rect));
  Arrange();
  return moving;
}
void Dock::Paint() {
  float a = std::clamp(reveal.v, 0.f, 1.f);
  if (a <= .004f) return;
  // Surface reveal (§10): opacity + a short lift + 0.96 -> 1. The surface leads, its contents follow.
  float s = .96f + .04f * reveal.v;
  float dy = (1 - reveal.v) * 10;
  PushTransform(D2D1::Matrix3x2F::Scale(s, s, D2D1::Point2F(MidX(rect), MidY(rect))) * D2D1::Matrix3x2F::Translation(0, dy));
  Glass(rect, radius, a);
  float ca = std::clamp((reveal.v - .3f) / .7f, 0.f, 1.f);
  PushOpacity(ca);
  PaintBody();
  Container::Paint();
  PopOpacity();
  PopTransform();
}
Widget* Dock::HitTest(float px, float py) {
  if (!visible || reveal.v < .5f) return nullptr;
  for (auto it = children.rbegin(); it != children.rend(); ++it)
    if ((*it)->visible)
      if (Widget* h = (*it)->HitTest(px, py); h && h->Interactive()) return h;
  return Inside(rect, px, py) ? this : nullptr;
}
void Dock::OnDown(const PointerEvent& e) {
  if (!draggable) return;
  if (e.clicks >= 2) {
    ResetPosition();
    return;
  }
  dragging = true;
  grabX = e.x;
  grabY = e.y;
  startDX = dragX;
  startDY = dragY;
}
void Dock::OnDrag(const PointerEvent& e) {
  if (!dragging) return;
  dragX = startDX + (e.x - grabX);
  dragY = startDY + (e.y - grabY);
  ui::RequestFrame();
}
void Dock::OnUp(const PointerEvent& e, bool inside) {
  Widget::OnUp(e, inside);
  if (!dragging) return;
  dragging = false;
  // Close to home, it settles home.
  if (std::hypot(dragX, dragY) < 28) dragX = dragY = 0;
}

// ========================================================================= ScrollArea ==
void ScrollArea::Clamp() {
  float maxScroll = std::max(0.f, contentHeight - H(rect));
  offset.To(std::clamp(offset.target, 0.f, maxScroll));
}
bool ScrollArea::Animate(float dt) {
  Clamp();
  bool moving = Container::Animate(dt);
  return offset.Step(dt) || moving;
}
void ScrollArea::Paint() {
  PushClip(rect);
  Container::Paint();
  PopClip();
  if (contentHeight > H(rect) + 1) {
    float vis = H(rect) / contentHeight;
    float th = std::max(20.f, H(rect) * vis);
    float ty = rect.top + (H(rect) - th) * (offset.v / std::max(1.f, contentHeight - H(rect)));
    FillRound(R(rect.right - 4, ty, rect.right - 1, ty + th), 2, Rgba(1, 1, 1, .16f + .1f * hover.v));
  }
}
Widget* ScrollArea::HitTest(float x, float y) {
  if (!visible || !Inside(rect, x, y)) return nullptr;
  for (auto it = children.rbegin(); it != children.rend(); ++it)
    if ((*it)->visible)
      if (Widget* h = (*it)->HitTest(x, y); h && h->Interactive()) return h;
  return this;
}
bool ScrollArea::OnWheel(float delta, const PointerEvent&) {
  if (contentHeight <= H(rect)) return false;
  offset.To(offset.target - delta * 0.5f);
  Clamp();
  return true;
}

// ======================================================================== ParamDialog ==
std::wstring HumanKey(const std::string& key) {
  std::wstring out;
  for (size_t i = 0; i < key.size(); i++) {
    unsigned char c = static_cast<unsigned char>(key[i]);
    if (i == 0) out += wchar_t(toupper(c));
    else if (isupper(c)) {
      if (!out.empty() && out.back() != L' ' && out.back() != L'-') out += L' ';
      out += wchar_t(tolower(c));
    }
    else out += wchar_t(c);
  }
  return out;
}

namespace {
int DecimalsOf(const std::string& n) {
  auto dot = n.find('.');
  return dot == std::string::npos ? 0 : int(std::min<size_t>(3, n.size() - dot - 1));
}
std::string Trim(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '?')) s.pop_back();
  while (!s.empty() && s.front() == ' ') s.erase(s.begin());
  return s;
}
}  // namespace

std::vector<ParamField> ParseParams(const std::string& doc) {
  std::vector<ParamField> out;
  size_t i = doc.find('{');
  if (i == std::string::npos) return out;
  i++;
  while (i < doc.size()) {
    // "key":
    while (i < doc.size() && doc[i] != '"' && doc[i] != '}') i++;
    if (i >= doc.size() || doc[i] == '}') break;
    size_t k0 = ++i;
    while (i < doc.size() && doc[i] != '"') i++;
    std::string key = doc.substr(k0, i - k0);
    i++;
    if (i >= doc.size() || doc[i] != ':') break;
    i++;
    // The value runs to the next top-level comma or the closing brace.
    std::string value;
    bool inString = false;
    int depth = 0;
    while (i < doc.size()) {
      char c = doc[i];
      if (c == '"') inString = !inString;
      if (!inString) {
        if (c == '[' || c == '{' || c == '(') depth++;
        if (c == ']' || c == ')') depth--;
        if (c == '}') { if (depth == 0) break; depth--; }
        if (c == ',' && depth == 0) break;
      }
      value += c;
      i++;
    }
    if (i < doc.size() && doc[i] == ',') i++;
    value = Trim(value);
    if (key == "seed" || key == "coalesce" || key == "target" || key == "layer") continue;
    ParamField f;
    f.key = key;
    f.label = HumanKey(key);
    if (value.rfind("bool", 0) == 0) {
      f.kind = ParamField::Flag;
      f.flag = value.find("=true") != std::string::npos;
      out.push_back(f);
      continue;
    }
    if (value.rfind("\"#rrggbb\"", 0) == 0) {
      // A colour: the command's own default stands until one is picked.
      f.kind = ParamField::Color;
      size_t eq = value.find("=\"#");
      if (eq != std::string::npos && value.size() >= eq + 9) {
        unsigned v = unsigned(strtoul(value.substr(eq + 3, 6).c_str(), nullptr, 16));
        f.color = Rgba(float((v >> 16) & 255) / 255.f, float((v >> 8) & 255) / 255.f, float(v & 255) / 255.f, 1);
      }
      out.push_back(f);
      continue;
    }
    if (value.rfind("deg", 0) == 0 || value.rfind("%", 0) == 0) {
      // An angle or a signed percentage.
      bool deg = value[0] == 'd';
      f.min = deg ? -180 : -100;
      f.max = deg ? 180 : 100;
      size_t eq = value.find('=');
      f.def = eq == std::string::npos ? 0 : std::clamp(strtod(value.c_str() + eq + 1, nullptr), f.min, f.max);
      f.decimals = 0;
      out.push_back(f);
      continue;
    }
    if ((key == "blend" && value.rfind("str", 0) == 0) || ((key == "blend" || key == "mode") && value.find("multiply") != std::string::npos)) {
      // A blend mode: the engine reads any spelling of Photoshop's names.
      static const char* const blends[] = {"normal", "dissolve", "darken", "multiply", "colorBurn", "linearBurn", "darkerColor", "lighten", "screen",
                                           "colorDodge", "linearDodge", "lighterColor", "overlay", "softLight", "hardLight", "vividLight", "linearLight",
                                           "pinLight", "hardMix", "difference", "exclusion", "subtract", "divide", "hue", "saturation", "color", "luminosity"};
      f.kind = ParamField::Choice;
      for (auto* b : blends) f.choices.push_back(b);
      size_t eq = value.rfind('=');
      std::string def = eq == std::string::npos ? "normal" : value.substr(eq + 1);
      def.erase(std::remove(def.begin(), def.end(), '"'), def.end());
      for (size_t c = 0; c < f.choices.size(); c++)
        if (_stricmp(f.choices[c].c_str(), def.c_str()) == 0) f.choice = int(c);
      out.push_back(f);
      continue;
    }
    if (!value.empty() && value[0] == '"') {
      size_t close = value.find('"', 1);
      if (close == std::string::npos) continue;
      std::string opts = value.substr(1, close - 1);
      if (opts.find('|') == std::string::npos || opts.find('#') != std::string::npos) continue;
      size_t a = 0;
      while (a <= opts.size()) {
        size_t b = opts.find('|', a);
        if (b == std::string::npos) b = opts.size();
        f.choices.push_back(opts.substr(a, b - a));
        a = b + 1;
      }
      std::string def;
      size_t eq = value.find('=', close);
      if (eq != std::string::npos) {
        def = value.substr(eq + 1);
        if (!def.empty() && def.front() == '"') {
          size_t endQuote = def.find('"', 1);
          def = def.substr(1, endQuote == std::string::npos ? std::string::npos : endQuote - 1);
        }
      }
      for (size_t c = 0; c < f.choices.size(); c++)
        if (f.choices[c] == def) f.choice = int(c);
      f.kind = ParamField::Choice;
      out.push_back(f);
      continue;
    }
    // A pixel count without a range: sizes and offsets.
    if (value.rfind("px", 0) == 0) {
      // A pixel amount: canvas sizes, signed offsets, or a radius/size/distance from zero.
      bool size = key == "width" || key == "height";
      bool offset = key == "horizontal" || key == "vertical";
      f.min = size ? 1 : offset ? -30000 : 0;
      f.max = size || offset ? 30000 : key == "radius" ? 30000 : 250;
      size_t eq = value.find('=');
      f.def = eq == std::string::npos ? (size ? 1 : 0) : strtod(value.c_str() + eq + 1, nullptr);
      f.decimals = 0;
      out.push_back(f);
      continue;
    }
    // min..max=default
    size_t dots = value.find("..");
    if (dots == std::string::npos) continue;
    std::string lo = Trim(value.substr(0, dots));
    std::string rest = value.substr(dots + 2);
    size_t eq = rest.find('=');
    std::string hi = eq == std::string::npos ? rest : rest.substr(0, eq);
    std::string def = eq == std::string::npos ? lo : rest.substr(eq + 1);
    hi = Trim(hi.substr(0, hi.find(' ')));
    def = Trim(def.substr(0, def.find(' ')));
    char* end = nullptr;
    f.min = strtod(lo.c_str(), &end);
    if (end == lo.c_str()) continue;
    f.max = strtod(hi.c_str(), &end);
    if (end == hi.c_str()) continue;
    f.def = strtod(def.c_str(), &end);
    if (end == def.c_str()) f.def = f.min;
    f.decimals = std::max({DecimalsOf(lo), DecimalsOf(hi), DecimalsOf(def)});
    if (f.max <= f.min) continue;
    f.def = std::clamp(f.def, f.min, f.max);
    out.push_back(f);
  }
  return out;
}

// ========================================================================== ParamForm ==
ParamForm::ParamForm(std::vector<ParamField> f, bool dense) : fields(std::move(f)), compact(dense) {
  for (size_t i = 0; i < fields.size(); i++) {
    ParamField& field = fields[i];
    auto changed = [this, i] {
      fields[i].touched = true;
      if (onChange) onChange();
    };
    switch (field.kind) {
      case ParamField::Number: {
        auto* s = Add<Slider>(field.label, field.min, field.max, field.def, field.decimals);
        s->logScale = field.max >= 200 && field.min >= 0 && field.max / std::max(1e-9, field.min + 1.0) >= 100;
        s->compact = compact;
        s->labelWidth = labelWidth;
        s->onChange = [this, i, changed](double v) { fields[i].def = v; changed(); };
        controls.push_back(s);
        break;
      }
      case ParamField::Choice: {
        std::vector<std::wstring> opts;
        size_t longest = 0;
        for (auto& c : field.choices) { opts.push_back(HumanKey(c)); longest = std::max(longest, c.size()); }
        if (opts.size() <= 3 && longest <= 9) {
          auto* s = Add<Segmented>(opts, field.choice);
          s->onChange = [this, i, changed](int v) { fields[i].choice = v; changed(); };
          controls.push_back(s);
        } else {
          auto* s = Add<SelectList>(opts, field.choice);
          s->label = field.label;
          s->labelWidth = labelWidth;
          s->onChange = [this, i, changed](int v) { fields[i].choice = v; changed(); };
          controls.push_back(s);
        }
        break;
      }
      case ParamField::Flag: {
        auto* s = Add<Toggle>(field.label, field.flag);
        s->onChange = [this, i, changed](bool v) { fields[i].flag = v; changed(); };
        controls.push_back(s);
        break;
      }
      case ParamField::Color: {
        auto* s = Add<ColorField>(field.label, field.color, field.colorSet);
        s->labelWidth = labelWidth;
        s->onChange = [this, i, changed](D2D1_COLOR_F c) { fields[i].color = c; fields[i].colorSet = true; changed(); };
        controls.push_back(s);
        break;
      }
    }
  }
}
float ParamForm::Height() const {
  float h = 0;
  for (size_t i = 0; i < fields.size(); i++) {
    switch (fields[i].kind) {
      case ParamField::Number: h += compact ? 34 : 50; break;
      case ParamField::Choice: h += dynamic_cast<Segmented*>(controls[i]) ? 52 : 40; break;
      case ParamField::Flag: h += 36; break;
      case ParamField::Color: h += 40; break;
    }
  }
  return h;
}
void ParamForm::Place(float x0, float top, float x1) {
  float y = top;
  for (size_t i = 0; i < controls.size(); i++) {
    Widget* c = controls[i];
    switch (fields[i].kind) {
      case ParamField::Number:
        if (compact) { c->rect = R(x0, y + 2, x1, y + 30); y += 34; }
        else { c->rect = R(x0, y, x1, y + 42); y += 50; }
        break;
      case ParamField::Choice:
        if (dynamic_cast<Segmented*>(c)) { c->rect = R(x0, y + 18, x1, y + 46); y += 52; }
        else { c->rect = R(x0, y, x1, y + 32); y += 40; }
        break;
      case ParamField::Flag: c->rect = R(x0, y, x1, y + 28); y += 36; break;
      case ParamField::Color: c->rect = R(x0, y, x1, y + 32); y += 40; break;
    }
  }
  rect = R(x0, top, x1, y);
}
void ParamForm::Paint() {
  for (size_t i = 0; i < controls.size(); i++)
    if (fields[i].kind == ParamField::Choice && dynamic_cast<Segmented*>(controls[i]))
      Text(fields[i].label, R(controls[i]->rect.left, controls[i]->rect.top - 18, controls[i]->rect.right, controls[i]->rect.top - 2), F_Label, P.dim);
  Container::Paint();
}
void ParamForm::Write(JsonObject& o) const {
  for (auto& f : fields) {
    if (touchedOnly && !f.touched) continue;
    switch (f.kind) {
      case ParamField::Number: {
        double q = std::pow(10.0, f.decimals);
        o.Num(f.key.c_str(), std::round(f.def * q) / q);
        break;
      }
      case ParamField::Choice:
        o.Str(f.key.c_str(), f.choice >= 0 && size_t(f.choice) < f.choices.size() ? f.choices[size_t(f.choice)] : "");
        break;
      case ParamField::Flag: o.Bool(f.key.c_str(), f.flag); break;
      case ParamField::Color:
        if (f.colorSet) {
          char buf[16];
          snprintf(buf, sizeof buf, "#%02x%02x%02x", int(f.color.r * 255 + .5f), int(f.color.g * 255 + .5f), int(f.color.b * 255 + .5f));
          o.Str(f.key.c_str(), buf);
        }
        break;
    }
  }
}

// ======================================================================== ParamDialog ==
ParamDialog::ParamDialog(std::wstring t, std::string cmd, std::vector<ParamField> f) : title(std::move(t)), command(std::move(cmd)) {
  modal = true;
  form = Add<ParamForm>(std::move(f));
  form->onChange = [this] { Changed(); };
  previewToggle = Add<Toggle>(L"Preview", true);
  previewToggle->onChange = [this](bool on) {
    if (on) Changed();
    else if (unpreview) unpreview();
  };
  Add<TextButton>(L"Cancel", TextButton::Secondary, [this] { Finish(false); });
  Add<TextButton>(L"Apply", TextButton::Primary, [this] { Finish(true); });
  changedAt = ui::NowMs();
  dirty = true;  // the first preview shows the defaults
}
D2D1_SIZE_F ParamDialog::Natural() {
  float h = 24 + 30 + 10 + form->Height();
  if (form->fields.empty()) h += 30;
  h += 12 + 36 + 20;
  return {360, h};
}
void ParamDialog::Arrange() {
  float x0 = target.left + 22, x1 = target.right - 22;
  form->Place(x0, target.top + 24 + 30 + 10, x1);
  float by = target.bottom - 20 - 36;
  previewToggle->rect = R(x0, by + 4, x0 + 112, by + 32);
  previewToggle->visible = preview != nullptr;  // settings apply on OK only
  auto* cancel = static_cast<TextButton*>(children[children.size() - 2].get());
  auto* apply = static_cast<TextButton*>(children.back().get());
  apply->rect = R(x1 - 96, by, x1, by + 36);
  cancel->rect = R(x1 - 96 - 8 - 92, by, x1 - 104, by + 36);
}
void ParamDialog::PaintContent() {
  Text(title, R(target.left + 22, target.top + 20, target.right - 22, target.top + 50), F_Title, P.text);
  float y = target.top + 24 + 30 + 10;
  if (form->fields.empty()) Text(L"This command has no settings.", R(target.left + 22, y, target.right - 22, y + 24), F_Label, P.dim);
  float sepY = target.bottom - 20 - 36 - 12;
  Line(target.left + 22, sepY, target.right - 22, sepY, P.sep, 1);
}
std::string ParamDialog::Params() const {
  JsonObject o;
  form->Write(o);
  return o.Text();
}
void ParamDialog::Changed() {
  dirty = true;
  changedAt = ui::NowMs();
}
bool ParamDialog::Animate(float dt) {
  bool moving = Popover::Animate(dt);
  // A live preview once the values rest for a moment and the last one is done, never on every
  // pixel of a drag.
  if (open && dirty && previewToggle && previewToggle->on && preview) {
    if (ui::NowMs() - changedAt > 160 && shell.v > .9f && !(previewBusy && previewBusy())) {
      preview(Params());
      dirty = false;
    } else {
      moving = true;
    }
  }
  return moving;
}
bool ParamDialog::OnKey(UINT vk, Mods) {
  if (vk == VK_ESCAPE) { Finish(false); return true; }
  if (vk == VK_RETURN) { Finish(true); return true; }
  return false;
}
void ParamDialog::Finish(bool ok) {
  if (finished) return;
  finished = true;
  if (finish) finish(ok, Params());
  Close();
}

// ======================================================================== CurveEditor ==
Rect CurveEditor::Plot() const { return Inset(rect, 8); }
D2D_POINT_2F CurveEditor::ToScreen(float x, float y) const {
  Rect p = Plot();
  return {p.left + x / 255.f * W(p), p.bottom - y / 255.f * H(p)};
}
bool CurveEditor::IsIdentity() const {
  for (auto& q : points)
    if (std::fabs(q.x - q.y) > .5f) return false;
  return true;
}
std::string CurveEditor::PointsJson() const {
  std::string out = "[";
  for (size_t i = 0; i < points.size(); i++) {
    char buf[48];
    snprintf(buf, sizeof buf, "%s[%d,%d]", i ? "," : "", int(std::lround(points[i].x)), int(std::lround(points[i].y)));
    out += buf;
  }
  return out + "]";
}
float CurveEditor::Eval(float x) const {
  // Natural cubic spline through the points, flat beyond the ends (as Photoshop's curve).
  size_t n = points.size();
  if (n == 0) return x;
  if (x <= points.front().x) return points.front().y;
  if (x >= points.back().x) return points.back().y;
  if (n == 2) {
    float t = (x - points[0].x) / std::max(1e-3f, points[1].x - points[0].x);
    return points[0].y + t * (points[1].y - points[0].y);
  }
  std::vector<float> h(n - 1), a(n, 0), l(n, 1), mu(n, 0), z(n, 0), c(n, 0);
  for (size_t i = 0; i + 1 < n; i++) h[i] = std::max(1e-3f, points[i + 1].x - points[i].x);
  for (size_t i = 1; i + 1 < n; i++)
    a[i] = 3.f / h[i] * (points[i + 1].y - points[i].y) - 3.f / h[i - 1] * (points[i].y - points[i - 1].y);
  for (size_t i = 1; i + 1 < n; i++) {
    l[i] = 2.f * (points[i + 1].x - points[i - 1].x) - h[i - 1] * mu[i - 1];
    mu[i] = h[i] / l[i];
    z[i] = (a[i] - h[i - 1] * z[i - 1]) / l[i];
  }
  for (size_t j = n - 1; j-- > 0;) c[j] = z[j] - mu[j] * c[j + 1];
  size_t i = 0;
  while (i + 2 < n && x > points[i + 1].x) i++;
  float b = (points[i + 1].y - points[i].y) / h[i] - h[i] * (c[i + 1] + 2.f * c[i]) / 3.f;
  float d = (c[i + 1] - c[i]) / (3.f * h[i]);
  float t = x - points[i].x;
  return std::clamp(points[i].y + b * t + c[i] * t * t + d * t * t * t, 0.f, 255.f);
}
void CurveEditor::Paint() {
  Rect p = Plot();
  FillRound(rect, 12, Rgba(0, 0, 0, .18f));
  for (int i = 1; i < 4; i++) {
    float gx = p.left + W(p) * float(i) / 4, gy = p.top + H(p) * float(i) / 4;
    Line(gx, p.top, gx, p.bottom, Rgba(1, 1, 1, .07f), 1);
    Line(p.left, gy, p.right, gy, Rgba(1, 1, 1, .07f), 1);
  }
  Line(p.left, p.bottom, p.right, p.top, Rgba(1, 1, 1, .12f), 1);
  StrokeRound(p, 2, Rgba(1, 1, 1, .12f), 1);
  D2D1_COLOR_F c = ink.a > 0 ? ink : P.text;
  std::vector<D2D_POINT_2F> line;
  for (int i = 0; i <= 128; i++) {
    float x = 255.f * float(i) / 128;
    line.push_back(ToScreen(x, Eval(x)));
  }
  Microsoft::WRL::ComPtr<ID2D1PathGeometry> g;
  GfxFactory()->CreatePathGeometry(&g);
  if (g) {
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    g->Open(&sink);
    sink->BeginFigure(line[0], D2D1_FIGURE_BEGIN_HOLLOW);
    for (size_t i = 1; i < line.size(); i++) sink->AddLine(line[i]);
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    PushClip(p);
    Dc()->DrawGeometry(g.Get(), Ink(c), 1.8f);
    PopClip();
  }
  for (size_t i = 0; i < points.size(); i++) {
    if (dropping && int(i) == active) continue;
    auto q = ToScreen(points[i].x, points[i].y);
    bool on = int(i) == active;
    FillRound(R(q.x - 4.5f, q.y - 4.5f, q.x + 4.5f, q.y + 4.5f), 2, on ? c : Rgba(.1f, .1f, .12f, .9f));
    StrokeRound(R(q.x - 4.5f, q.y - 4.5f, q.x + 4.5f, q.y + 4.5f), 2, c, 1.2f);
  }
  if (active >= 0 && size_t(active) < points.size() && !dropping) {
    wchar_t buf[48];
    swprintf(buf, 48, i18n::Tr(L"In %d   Out %d").c_str(), int(std::lround(points[size_t(active)].x)), int(std::lround(points[size_t(active)].y)));
    Text(buf, R(p.left + 6, p.top + 4, p.right - 6, p.top + 22), F_Small, P.dim);
  }
}
void CurveEditor::OnDown(const PointerEvent& e) {
  Rect p = Plot();
  active = -1;
  dropping = false;
  for (size_t i = 0; i < points.size(); i++) {
    auto q = ToScreen(points[i].x, points[i].y);
    if (std::hypot(q.x - e.x, q.y - e.y) <= 9) { active = int(i); return; }
  }
  if (!Inside(p, e.x, e.y)) return;
  // A new point where the click is, between its neighbours.
  float x = std::clamp((e.x - p.left) / W(p) * 255.f, 0.f, 255.f);
  float y = std::clamp((p.bottom - e.y) / H(p) * 255.f, 0.f, 255.f);
  if (points.size() >= 19) return;
  size_t at = 0;
  while (at < points.size() && points[at].x < x) at++;
  if ((at < points.size() && std::fabs(points[at].x - x) < 2) || (at > 0 && std::fabs(points[at - 1].x - x) < 2)) return;
  points.insert(points.begin() + long(at), D2D_POINT_2F{x, y});
  active = int(at);
  if (onChange) onChange();
}
void CurveEditor::OnDrag(const PointerEvent& e) {
  if (active < 0 || size_t(active) >= points.size()) return;
  Rect p = Plot();
  size_t i = size_t(active);
  float lo = i > 0 ? points[i - 1].x + 1 : 0, hi = i + 1 < points.size() ? points[i + 1].x - 1 : 255;
  float x = std::clamp((e.x - p.left) / W(p) * 255.f, lo, hi);
  float y = std::clamp((p.bottom - e.y) / H(p) * 255.f, 0.f, 255.f);
  // Dragged well off the grid: an inner point goes away on release.
  bool inner = i > 0 && i + 1 < points.size();
  dropping = inner && (e.x < p.left - 24 || e.x > p.right + 24 || e.y < p.top - 24 || e.y > p.bottom + 24);
  points[i] = {x, y};
  if (onChange) onChange();
}
void CurveEditor::OnUp(const PointerEvent& e, bool inside) {
  Widget::OnUp(e, inside);
  if (dropping && active > 0 && size_t(active) + 1 < points.size()) {
    points.erase(points.begin() + active);
    active = -1;
    if (onChange) onChange();
  }
  dropping = false;
}
bool CurveEditor::OnKey(UINT vk, Mods) {
  if ((vk == VK_DELETE || vk == VK_BACK) && active > 0 && size_t(active) + 1 < points.size()) {
    points.erase(points.begin() + active);
    active = -1;
    if (onChange) onChange();
    return true;
  }
  return false;
}

// ========================================================================= ColorField ==
void ColorField::Paint() {
  float lw = LabelColumn(label, labelWidth);
  Text(label, R(rect.left, rect.top, rect.left + lw, rect.bottom), F_Label, enabled ? P.dim : P.faint);
  Rect sw = R(rect.left + lw, rect.top + 3, rect.right, rect.bottom - 3);
  FillRound(sw, 9, Fade(P.hover, .6f + hover.v * .4f));
  Rect chip = R(sw.left + 5, sw.top + 5, sw.left + 5 + H(sw) - 10, sw.bottom - 5);
  if (set) {
    FillRound(chip, 6, color);
  } else {
    Checkerboard(chip, 4);
  }
  StrokeRound(chip, 6, Rgba(1, 1, 1, .35f), 1);
  wchar_t buf[24];
  if (set) swprintf(buf, 24, L"#%02X%02X%02X", int(color.r * 255 + .5f), int(color.g * 255 + .5f), int(color.b * 255 + .5f));
  else wcscpy_s(buf, L"Default");
  Text(buf, R(chip.right + 10, sw.top, sw.right - 8, sw.bottom), F_Value, set ? P.text : P.faint);
  StrokeRound(sw, 9, Rgba(1, .93f, .81f, .12f + .18f * hover.v), 1);
}
void ColorField::OnUp(const PointerEvent& e, bool inside) {
  Widget::OnUp(e, inside);
  if (!inside || !enabled) return;
  std::weak_ptr<int> alive=lifetime;
  OpenColorPicker(color,[this,alive](D2D1_COLOR_F c) {if(alive.expired())return;color=c;set=true;if(onChange)onChange(color);},rect);
}
