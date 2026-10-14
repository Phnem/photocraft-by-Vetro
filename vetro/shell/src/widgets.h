#pragma once
// The interaction classes. Every control in the shell is an instance of one of these: a new panel
// is assembled from them, never drawn by hand in place. Each class owns its states (rest, hover,
// press, selected, disabled, focus -- design.md §8) and its motion.
//
//   Widget       base: rect, hover/press springs, tooltip, pointer and key hooks
//   Container    holds children, paints and hit-tests them in order
//   IconButton   icon in a transparent hit zone; a plate only on hover/press; selected; flyout mark
//   TextButton   short command; quiet (menu bar) and primary variants
//   Label        static text
//   Slider       label + value + track; drag, wheel, double-click resets; linear or log
//   Toggle       switch with a spring knob
//   Segmented    mutually exclusive short options with a sliding highlight
//   Tabs         panel tabs with a sliding plate
//   SelectList   drop-down list: shows the value, opens a Menu of choices
//   Popover      glass surface that morphs out of its anchor (Vetro-collection TtmMenu physics)
//   Menu         Popover of rows: shortcuts, checks, submenus, separators, keyboard navigation
//   Dock         floating glass surface; dragged anywhere on its background, springs into place
//   ScrollArea   vertical clip with spring scrolling
//   ParamForm    controls generated from a command's params schema (Number, Choice, Flag, Color)
//   ParamDialog  Popover around a ParamForm, with live preview, Cancel and Apply
//   CurveEditor  tone curve: points on a 0..255 grid, dragged, added by a click, dragged off to remove
//   ui::Toast    transient status capsules
#include "core.h"
#include "gfx.h"
#include "json.h"
#include <memory>
#include <vector>

class Widget;
class Popover;

// ------------------------------------------------------------------------------- root --
// The root of the interface: layers in z order, pointer capture, hover, tooltips, popovers.
namespace ui {
void Init(HWND window);
HWND Window();
void RequestFrame();
double NowMs();
Rect Bounds();  // the window in DIPs
void SetBounds(const Rect& r);

void AddLayer(Widget* w);  // painted and hit-tested in the order added (last on top)
void OpenPopover(Popover* p);
Popover* ActivePopover();  // top open dialog/menu, for focus and interaction inspection
void ClosePopovers();
void Forget(Popover* p);  // a popover is being destroyed
bool AnyPopoverOpen();
bool IsCaptured(const Widget* w);
void Release();

// Pointer and key input from the window.
void PointerMove(const PointerEvent& e);
void PointerDown(const PointerEvent& e);
void PointerUp(const PointerEvent& e);
bool Wheel(float delta, const PointerEvent& e);
bool Key(UINT vk, Mods m);
bool Char(wchar_t c);
void PointerLeave();

bool Animate(float dt);  // true while anything moves
void Paint();            // all layers, popovers, tooltip, toasts
void Toast(const std::wstring& message, bool error = false);

// A press first asks the gate (when set): false and the press goes nowhere. The shell uses it to
// hold input that would touch the document while the engine is busy.
void SetGate(std::function<bool(Widget* target, const PointerEvent& e)> gate);
void SetDeferredSink(std::function<void(std::function<void()>)> sink);
Popover* PopoverOf(Widget* w);  // the popover a widget lives in, if any
}  // namespace ui

// ------------------------------------------------------------------------------ widget --
class Widget {
 public:
  Rect rect{};
  bool visible = true, enabled = true;
  std::wstring tooltip;
  Spring hover{0, motion::HoverK, motion::HoverC};
  Spring press{0, motion::ButtonK, motion::ButtonC};
  Widget* parent = nullptr;
  std::shared_ptr<int> lifetime = std::make_shared<int>(0);

  virtual ~Widget();
  virtual void Layout() {}
  virtual void Paint() = 0;
  virtual bool Animate(float dt);
  virtual Widget* HitTest(float x, float y);
  virtual bool Interactive() const { return true; }
  virtual float NaturalWidth() const { return 0; }
  virtual void OnHover(bool inside);
  virtual void OnDown(const PointerEvent&) {}
  virtual void OnDrag(const PointerEvent&) {}
  virtual void OnUp(const PointerEvent&, bool inside);
  virtual bool OnWheel(float, const PointerEvent&) { return false; }
  virtual bool OnKey(UINT, Mods) { return false; }
  virtual bool OnChar(wchar_t) { return false; }
  virtual std::wstring Tooltip() const { return tooltip; }
  // Press compression (design.md §10): down to 0.96, back on an elastic spring.
  float PressScale() const { return 1.f - .04f * std::clamp(press.v, 0.f, 1.5f); }
};

class Container : public Widget {
 public:
  std::vector<std::unique_ptr<Widget>> children;
  template <class T, class... Args>
  T* Add(Args&&... args) {
    auto w = std::make_unique<T>(std::forward<Args>(args)...);
    T* raw = w.get();
    raw->parent = this;
    children.push_back(std::move(w));
    return raw;
  }
  void Clear() { children.clear(); }
  void Paint() override;
  bool Animate(float dt) override;
  Widget* HitTest(float x, float y) override;
  bool Interactive() const override { return false; }
};

// ------------------------------------------------------------------------- the controls --
class IconButton : public Widget {
 public:
  const wchar_t* icon = nullptr;
  bool stroked = false;  // custom icons are stroked on the Phosphor grid
  bool flyout = false;   // a small corner mark: the slot holds more tools
  float iconSize = tok::IconSize;
  float radius = tok::RadiusControl;
  D2D1_COLOR_F tint{};   // a=0: the palette's icon colour
  Spring selected{0, motion::SlideK, motion::SlideC};
  std::function<void()> onClick;
  std::function<void()> onDoubleClick;
  std::function<void()> onLongPress;  // also the right button
  IconButton(const wchar_t* ic, std::wstring tip, std::function<void()> click) : icon(ic), onClick(std::move(click)) { tooltip = std::move(tip); }
  void SetSelected(bool on) { selected.To(on ? 1.f : 0.f); }
  void Paint() override;
  bool Animate(float dt) override;
  void OnDown(const PointerEvent& e) override;
  void OnUp(const PointerEvent& e, bool inside) override;
 private:
  double downAt = 0;
  bool longFired = false;
  int downButton = 0;
};

class TextButton : public Widget {
 public:
  enum Style { Quiet, Secondary, Primary } style = Secondary;
  std::wstring label;
  Face face = F_Button;
  bool active = false;  // quiet buttons: the menu they opened is showing
  std::function<void()> onClick;
  std::function<void()> onPressed;     // fires on press (menu bar triggers open on press)
  std::function<void()> onHoverEnter;  // menu bar: slide to the neighbour's menu
  TextButton(std::wstring text, Style s, std::function<void()> click) : style(s), label(std::move(text)), onClick(std::move(click)) {}
  float Natural() const { return Measure(label, face) + (style == Quiet ? 20.f : 32.f); }
  void Paint() override;
  void OnUp(const PointerEvent& e, bool inside) override;
  void OnDown(const PointerEvent& e) override;
  void OnHover(bool inside) override;
};

class Label : public Widget {
 public:
  std::wstring text;
  Face face = F_Label;
  D2D1_COLOR_F colour{};
  Align align = Align::Leading;
  Label(std::wstring t, Face f, Align a = Align::Leading) : text(std::move(t)), face(f), align(a) {}
  void Paint() override;
  bool Interactive() const override { return false; }
};

// A compact numeric field: scrub, wheel or click to type an exact value.
class NumberField : public Widget {
 public:
  std::wstring label, unit;
  double value = 0, min = -100000, max = 100000, step = 1;
  int decimals = 1;
  std::function<void(double)> onChange, onCommit;
  NumberField(std::wstring text, double initial, double lo, double hi, int digits = 1, std::wstring suffix = L"")
      : label(std::move(text)), unit(std::move(suffix)), value(initial), min(lo), max(hi), decimals(digits) {}
  void Set(double v) { value = std::clamp(v, min, max); }
  std::wstring ValueText() const;
  float Natural() const;
  void Paint() override;
  void OnDown(const PointerEvent& e) override;
  void OnDrag(const PointerEvent& e) override;
  void OnUp(const PointerEvent& e, bool inside) override;
  bool OnWheel(float delta, const PointerEvent& e) override;
 private:
  float startX = 0;
  double startValue = 0;
  bool scrubbed = false;
};
// String fields open an exact text entry; useful for font search, names and settings.
class TextField : public Widget {
 public:
  std::wstring label, value;
  std::function<void(const std::wstring&)> onChange;
  TextField(std::wstring title, std::wstring initial = L"") : label(std::move(title)), value(std::move(initial)) {}
  void Paint() override;
  void OnUp(const PointerEvent&,bool inside) override;
  float NaturalWidth() const override { return 210; }
};

class Slider : public Widget {
 public:
  std::wstring label, unit;
  double min = 0, max = 100, value = 0, def = 0;
  int decimals = 0;
  bool logScale = false;
  bool compact = false;  // one row: label, track, value (the options dock)
  float labelWidth = 0;  // compact: fixed label column
  std::function<void(double)> onChange;  // live, while dragging
  std::function<void(double)> onCommit;  // released
  Slider(std::wstring l, double lo, double hi, double v, int dec = 0, std::wstring u = L"")
      : label(std::move(l)), unit(std::move(u)), min(lo), max(hi), value(v), def(v), decimals(dec) {}
  void Set(double v) { value = std::clamp(v, min, max); fill.To(float(Norm(value))); }
  double Norm(double v) const;
  double FromNorm(double t) const;
  std::wstring ValueText() const;
  float ValueColumn() const;  // compact: room for the value, wide enough for four digits and the unit
  Rect Track() const;
  void Paint() override;
  bool Animate(float dt) override;
  void OnDown(const PointerEvent& e) override;
  void OnDrag(const PointerEvent& e) override;
  void OnUp(const PointerEvent& e, bool inside) override;
  bool OnWheel(float delta, const PointerEvent& e) override;
 private:
  Spring fill{-1, motion::SlideK, motion::SlideC};
  Spring grab{0, motion::ButtonK, motion::ButtonC};
  bool dragging = false;
  void FromX(float x);
};

class Toggle : public Widget {
 public:
  std::wstring label;
  bool on = false;
  std::function<void(bool)> onChange;
  Toggle(std::wstring l, bool v) : label(std::move(l)), on(v) { knob.Reset(v ? 1.f : 0.f); }
  void Set(bool v) { on = v; knob.To(v ? 1.f : 0.f); }
  void Paint() override;
  bool Animate(float dt) override;
  void OnUp(const PointerEvent& e, bool inside) override;
 private:
  Spring knob{0, motion::SlideK, motion::SlideC};
};

class Segmented : public Widget {
 public:
  std::vector<std::wstring> options;
  std::vector<const wchar_t*> icons;  // optional: icon segments (stroked when iconsStroked)
  bool iconsStroked = false;
  int index = 0;
  std::function<void(int)> onChange;
  Segmented(std::vector<std::wstring> o, int i) : options(std::move(o)), index(i) { slide.Reset(float(i)); }
  void Set(int i) { index = i; slide.To(float(i)); }
  void Paint() override;
  bool Animate(float dt) override;
  void OnUp(const PointerEvent& e, bool inside) override;
 private:
  Spring slide{0, motion::SlideK, motion::SlideC};
};

class Tabs : public Widget {
 public:
  std::vector<std::wstring> labels;
  int index = 0;
  std::function<void(int)> onChange;
  Tabs(std::vector<std::wstring> l, int i) : labels(std::move(l)), index(i) {}
  void Paint() override;
  bool Animate(float dt) override;
  void OnUp(const PointerEvent& e, bool inside) override;
 private:
  PixelSpring plateX{-1, motion::SlideK, motion::SlideC}, plateW{0, motion::SlideK, motion::SlideC};
  std::vector<Rect> TabRects() const;
};

// --------------------------------------------------------------------------- popovers --
enum class Side { Below, Above, Right, Left, Center };

class Popover : public Container {
 public:
  Rect anchor{};            // where it grows from
  Side side = Side::Below;
  Rect target{};            // where it settles
  Popover* owner = nullptr; // a submenu's menu
  bool open = false;
  bool modal = false;       // a dialog: dims the interface behind it
  bool pinned = false;      // a tool panel: stays open while the canvas under it is used
  std::function<void()> onClosed;
  Spring shell{0, 1, 1}, content{0, 1, 1};

  ~Popover() override;
  void Open(const Rect& from, Side s);
  void Close();
  bool Showing() const { return open || shell.v > .004f || shell.Moving(); }
  Rect Bounds() const;
  float Radius() const;
  bool Animate(float dt) override;
  void Paint() override;
  Widget* HitTest(float x, float y) override;
  bool Interactive() const override { return true; }
  virtual D2D1_SIZE_F Natural() = 0;  // the settled size
  virtual void PaintContent() = 0;    // in target coordinates; the morph transform is applied
  virtual float CornerRadius() const { return tok::RadiusPopover; }
  virtual void Arrange() {}           // lay children out inside `target`
  bool OnKey(UINT vk, Mods m) override;
  D2D_POINT_2F ToContent(float x, float y) const;  // undo the morph for hit testing
 protected:
  D2D1_MATRIX_3X2_F ContentTransform() const;
};

struct MenuItem {
  std::wstring label, shortcut;
  const wchar_t* icon = nullptr;
  bool iconStroked = false;
  bool enabled = true, checked = false, separator = false;
  std::vector<MenuItem> submenu;
  std::function<void()> action;
  static MenuItem Separator() { MenuItem m; m.separator = true; return m; }
};

class Menu : public Popover {
 public:
  std::vector<MenuItem> items;
  float minWidth = 200;
  explicit Menu(std::vector<MenuItem> rows = {}) : items(std::move(rows)) {}
  D2D1_SIZE_F Natural() override;
  void PaintContent() override;
  bool Animate(float dt) override;
  void Paint() override;
  Widget* HitTest(float x, float y) override;
  void OnHover(bool inside) override;
  void OnDrag(const PointerEvent& e) override;
  void OnUp(const PointerEvent& e, bool inside) override;
  bool OnWheel(float delta, const PointerEvent& e) override;
  bool OnKey(UINT vk, Mods m) override;
  void PointerAt(float x, float y);  // hover tracking (also while the button is held)
  void CloseTree();                  // this menu and everything it opened
 private:
  int hot = -1;
  PixelSpring plateY{0, motion::SlideK, motion::SlideC};
  Spring plateA{0, motion::HoverK, motion::HoverC};
  float scroll = 0;
  std::unique_ptr<Menu> child;
  int childIndex = -1;
  double hotSince = 0;
  float RowTop(int i) const;
  float RowH(int i) const { return items[size_t(i)].separator ? 9.f : 30.f; }
  int RowAt(float y) const;
  void Activate(int i);
  void OpenChild(int i);
};

// A colour: label and swatch; a click opens the shared native picker. `set` stays false until
// the user picks one, so a command keeps its own default meanwhile.
class ColorField : public Widget {
 public:
  std::wstring label;
  D2D1_COLOR_F color{0, 0, 0, 1};
  bool set = false;
  float labelWidth = 120;
  std::function<void(D2D1_COLOR_F)> onChange;
  ColorField(std::wstring l, D2D1_COLOR_F c, bool isSet) : label(std::move(l)), color(c), set(isSet) {}
  void Paint() override;
  void OnUp(const PointerEvent& e, bool inside) override;
};

class SelectList : public Widget {
 public:
  std::vector<std::wstring> options;
  int index = 0;
  std::wstring label;  // optional caption to the left
  float labelWidth = 0;
  std::function<void(int)> onChange;
  SelectList(std::vector<std::wstring> o, int i) : options(std::move(o)), index(i) {}
  void Paint() override;
  void OnUp(const PointerEvent& e, bool inside) override;
  bool Animate(float dt) override;
 private:
  std::unique_ptr<Menu> menu;
  Spring chevron{0, motion::SlideK, motion::SlideC};
  Rect Field() const;
};

// ------------------------------------------------------------------------------ docks --
class Dock : public Container {
 public:
  std::function<Rect(const Rect& window, D2D1_SIZE_F size)> home;  // default placement
  float radius = tok::RadiusDock;
  bool draggable = true;
  Spring reveal{0, motion::DockK * .8f, motion::DockC};
  PixelSpring x{0, motion::DockK, motion::DockC}, y{0, motion::DockK, motion::DockC};
  float dragX = 0, dragY = 0;  // the user's displacement from home
  float revealDelay = 0;       // seconds: docks arrive one after another, parents before contents
  virtual D2D1_SIZE_F Natural() = 0;
  virtual void Arrange() {}  // children inside `rect`
  void Place(const Rect& window, bool snap);
  void Paint() override;
  virtual void PaintBody() {}
  bool Animate(float dt) override;
  Widget* HitTest(float x, float y) override;
  bool Interactive() const override { return true; }
  void OnDown(const PointerEvent& e) override;
  void OnDrag(const PointerEvent& e) override;
  void OnUp(const PointerEvent& e, bool inside) override;
  void ResetPosition() { dragX = dragY = 0; }
  Rect Target() const { return RWH(x.target, y.target, W(rect), H(rect)); }  // where it is going
 private:
  float grabX = 0, grabY = 0, startDX = 0, startDY = 0;
  bool dragging = false;
  bool placed = false;
  double born = 0;
};

class ScrollArea : public Container {
 public:
  float contentHeight = 0;
  PixelSpring offset{0, motion::PanelK, motion::PanelC};
  void Paint() override;
  Widget* HitTest(float x, float y) override;
  bool OnWheel(float delta, const PointerEvent& e) override;
  bool Animate(float dt) override;
  bool Interactive() const override { return true; }
  float Scroll() const { return offset.v; }
  void Clamp();
};

// ---------------------------------------------------------------- generated dialogs --
// A command's params doc, e.g. {"radius":0.1..1000=1,"distribution":"uniform|gaussian","mono":bool},
// read into fields. Anything the shell cannot present (json, ids, seeds) keeps its default.
struct ParamField {
  enum Kind { Number, Choice, Flag, Color } kind = Number;
  std::string key;
  std::wstring label;
  double min = 0, max = 1, def = 0;
  int decimals = 0;
  std::vector<std::string> choices;
  int choice = 0;
  bool flag = false;
  D2D1_COLOR_F color{0, 0, 0, 1};
  bool colorSet = false;  // Color: sent only once picked
  bool touched = false;   // changed by the user
};
std::vector<ParamField> ParseParams(const std::string& doc);
std::wstring HumanKey(const std::string& key);

// The controls of a params schema, laid out top to bottom. Owners place it, read its values, and
// hear about every change.
class ParamForm : public Container {
 public:
  std::vector<ParamField> fields;
  std::function<void()> onChange;
  float labelWidth = 120;
  bool compact = false;      // sliders on one row (long forms: Camera Raw, Layer Style)
  bool touchedOnly = false;  // Write only what the user changed (the command keeps its own values)
  explicit ParamForm(std::vector<ParamField> f, bool dense = false);
  float Height() const;                     // for the current fields
  void Place(float x0, float top, float x1);  // controls from `top` down; `rect` covers them
  void Paint() override;
  void Write(JsonObject& o) const;          // the values as params
 private:
  std::vector<Widget*> controls;
};

class ParamDialog : public Popover {
 public:
  std::wstring title;
  std::string command;
  ParamForm* form = nullptr;
  // The owner runs the engine side (queued off the UI thread). A new preview is asked for only
  // when the values have rested and the previous one is no longer being computed.
  std::function<void(const std::string& params)> preview;  // show these values
  std::function<void()> unpreview;                         // take the preview back
  std::function<bool()> previewBusy;                       // the last preview is still running
  std::function<void(bool ok, const std::string& params)> finish;  // Apply (ok) or Cancel
  ParamDialog(std::wstring t, std::string cmd, std::vector<ParamField> f);
  D2D1_SIZE_F Natural() override;
  void Arrange() override;
  void PaintContent() override;
  float CornerRadius() const override { return tok::RadiusLarge; }
  bool Animate(float dt) override;
  bool OnKey(UINT vk, Mods m) override;
  std::string Params() const;
  void Finish(bool ok);
 private:
  Toggle* previewToggle = nullptr;
  bool dirty = false, finished = false;
  double changedAt = 0;
  void Changed();
};

// ------------------------------------------------------------------------- curves --
class CurveEditor : public Widget {
 public:
  std::vector<D2D_POINT_2F> points{{0, 0}, {255, 255}};  // input -> output, 0..255, by input
  D2D1_COLOR_F ink{};  // a=0: the text colour (composite); else a channel's colour
  std::function<void()> onChange;
  void Reset() { points = {{0, 0}, {255, 255}}; active = -1; }
  bool IsIdentity() const;
  std::string PointsJson() const;  // [[in,out],...]
  float Eval(float x) const;       // the curve through the points (natural cubic spline)
  void Paint() override;
  void OnDown(const PointerEvent& e) override;
  void OnDrag(const PointerEvent& e) override;
  void OnUp(const PointerEvent& e, bool inside) override;
  bool OnKey(UINT vk, Mods m) override;
  int Active() const { return active; }
 private:
  int active = -1;
  bool dropping = false;  // dragged off the grid: removed on release
  Rect Plot() const;
  D2D_POINT_2F ToScreen(float x, float y) const;
};
