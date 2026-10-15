// PhotoCraft by Vetro -- commands with an editor of their own: Curves (points on a curve), Camera Raw
// (tabs of develop settings), Liquify (a tool panel and strokes on the canvas) and Layer Style (the
// effect list). Each is built from the interaction classes; the engine previews every settled
// change on its own thread (LivePreview) and OK keeps the last one.
#include "shell.h"
#include "icons_phosphor.h"
#include <array>
#include <cmath>

namespace {
// ============================================================================ Editor ==
// A modal editor surface: title, content, Preview, Cancel, OK; previews once the values rest.
class Editor : public Popover {
 public:
  explicit Editor(std::wstring t) : title(std::move(t)) { modal = true; }
  float CornerRadius() const override { return tok::RadiusLarge; }
  void Changed() {
    dirty = true;
    changedAt = ui::NowMs();
  }
  bool Animate(float dt) override {
    bool moving = Popover::Animate(dt);
    if (open) Layout();
    if (open && dirty && (!previewToggle || previewToggle->on)) {
      if (ui::NowMs() - changedAt > 180 && shell.v > .9f && !Busy()) {
        dirty = false;
        Preview();
      } else {
        moving = true;
      }
    }
    return moving;
  }
  void Arrange() override { Layout(); }
  bool OnKey(UINT vk, Mods m) override {
    if (vk == VK_ESCAPE) { Finish(false); return true; }
    if (vk == VK_RETURN) { Finish(true); return true; }
    return Key(vk, m);
  }
  void Finish(bool keep) {
    if (finished) return;
    finished = true;
    Done(keep);
    Close();
  }

 protected:
  virtual void Layout() = 0;  // children inside `target`, every frame (scrolling moves them)
  virtual bool Key(UINT, Mods) { return false; }
  virtual void Preview() = 0;
  virtual void Unpreview() = 0;
  virtual bool Busy() = 0;
  virtual void Done(bool keep) = 0;

  void AddFooter() {
    previewToggle = Add<Toggle>(L"Preview", true);
    previewToggle->onChange = [this](bool on) {
      if (on) Changed();
      else Unpreview();
    };
    cancel = Add<TextButton>(L"Cancel", TextButton::Secondary, [this] { Finish(false); });
    ok = Add<TextButton>(L"OK", TextButton::Primary, [this] { Finish(true); });
  }
  void PlaceFooter() {
    float x0 = target.left + 22, x1 = target.right - 22, by = target.bottom - 20 - 36;
    previewToggle->rect = R(x0, by + 4, x0 + 112, by + 32);
    ok->rect = R(x1 - 96, by, x1, by + 36);
    cancel->rect = R(x1 - 96 - 8 - 92, by, x1 - 104, by + 36);
  }
  void PaintFrame() {
    Text(title, R(target.left + 22, target.top + 20, target.right - 22, target.top + 50), F_Title, P.text);
    float sepY = target.bottom - 20 - 36 - 12;
    Line(target.left + 22, sepY, target.right - 22, sepY, P.sep, 1);
  }
  float BodyTop() const { return target.top + 64; }
  float BodyBottom() const { return target.bottom - 20 - 36 - 20; }

  std::wstring title;
  Toggle* previewToggle = nullptr;
  TextButton *cancel = nullptr, *ok = nullptr;
  bool dirty = true, finished = false;
  double changedAt = 0;
};

std::unique_ptr<Popover> editor;  // the open editor (one at a time)

template <class T>
void ShowEditor(std::unique_ptr<T> e, const Rect& from) {
  if (auto* old = dynamic_cast<Editor*>(editor.get()); old && old->open) old->Finish(false);
  e->Open(from, Side::Center);
  editor = std::move(e);
}

bool Identity(const std::vector<D2D_POINT_2F>& pts) {
  for (auto& q : pts)
    if (std::fabs(q.x - q.y) > .5f) return false;
  return true;
}
std::string PointsJson(const std::vector<D2D_POINT_2F>& pts) {
  std::string out = "[";
  for (size_t i = 0; i < pts.size(); i++) out += (i ? ",[" : "[") + JsonNum(std::lround(pts[i].x)) + "," + JsonNum(std::lround(pts[i].y)) + "]";
  return out + "]";
}
std::vector<ParamField> Pick(const std::vector<ParamField>& all, std::initializer_list<const char*> keys) {
  std::vector<ParamField> out;
  for (const char* k : keys)
    for (auto& f : all)
      if (f.key == k) out.push_back(f);
  return out;
}

// ============================================================================ Curves ==
class CurvesDialog : public Editor {
 public:
  CurvesDialog(const std::string& cmd, std::wstring t) : Editor(std::move(t)), live(cmd) {
    rgb = _stricmp(docInfo["mode"].Str().c_str(), "rgb") == 0;
    if (rgb) {
      channel = Add<Segmented>(std::vector<std::wstring>{L"RGB", L"Red", L"Green", L"Blue"}, 0);
      channel->onChange = [this](int i) { Switch(i); };
    }
    curve = Add<CurveEditor>();
    curve->onChange = [this] {
      chans[ch] = curve->points;
      Changed();
    };
    reset = Add<TextButton>(L"Reset", TextButton::Secondary, [this] {
      for (auto& c : chans) c = {{0, 0}, {255, 255}};
      curve->Reset();
      Changed();
    });
    for (auto& c : chans) c = curve->points;
    AddFooter();
  }
  D2D1_SIZE_F Natural() override { return {400, 64 + (rgb ? 40.f : 0.f) + 300 + 46 + 12 + 36 + 20}; }
  void PaintContent() override {
    PaintFrame();
    Text(L"Click adds a point · drag one off the grid to remove it", R(reset->rect.right + 12, reset->rect.top, target.right - 22, reset->rect.bottom), F_Small, P.faint);
  }

 protected:
  void Layout() override {
    float x0 = target.left + 22, x1 = target.right - 22, y = BodyTop();
    if (channel) {
      channel->rect = R(x0, y, x1, y + 28);
      y += 40;
    }
    curve->rect = R(x0, y, x1, y + 300);
    y += 308;
    reset->rect = R(x0, y, x0 + 84, y + 30);
    PlaceFooter();
  }
  bool Key(UINT vk, Mods m) override { return curve->OnKey(vk, m); }
  void Preview() override { live.Show(Params()); }
  void Unpreview() override { live.Hide(); }
  bool Busy() override { return live.Busy(); }
  void Done(bool keep) override { live.Finish(keep, Params()); }

 private:
  void Switch(int i) {
    chans[ch] = curve->points;
    ch = i;
    curve->points = chans[i];
    static const D2D1_COLOR_F inks[4] = {{0, 0, 0, 0}, {1, .38f, .32f, 1}, {.42f, .9f, .48f, 1}, {.45f, .62f, 1, 1}};
    curve->ink = inks[i];
  }
  std::string Params() const {
    static const char* const keys[4] = {"points", "red", "green", "blue"};
    JsonObject o;
    for (int i = 0; i < 4; i++)
      if (i == 0 || !Identity(chans[i])) o.Add(keys[i], PointsJson(chans[i]));
    return o.Text();
  }
  LivePreview live;
  bool rgb = false;
  Segmented* channel = nullptr;
  CurveEditor* curve = nullptr;
  TextButton* reset = nullptr;
  std::vector<D2D_POINT_2F> chans[4];
  int ch = 0;
};

// ======================================================================== Camera Raw ==
class CameraRawDialog : public Editor {
 public:
  explicit CameraRawDialog(std::wstring t) : Editor(std::move(t)), live("filter.cameraRaw") {
    auto all = ParseParams(paramsDoc["filter.cameraRaw"]);
    tabs = Add<Tabs>(std::vector<std::wstring>{L"Basic", L"Curve", L"HSL", L"Grading", L"Detail", L"Effects"}, 0);
    tabs->onChange = [this](int i) { ShowTab(i); };
    scroll = Add<ScrollArea>();
    auto form = [&](std::initializer_list<const char*> keys) {
      auto* f = scroll->Add<ParamForm>(Pick(all, keys), true);
      f->onChange = [this] { Changed(); };
      return f;
    };
    forms[0] = form({"temperature", "tint", "exposure", "contrast", "highlights", "shadows", "whites", "blacks", "texture", "clarity", "dehaze", "vibrance", "saturation"});
    forms[1] = form({"curveHighlights", "curveLights", "curveDarks", "curveShadows"});
    curve = scroll->Add<CurveEditor>();
    curve->onChange = [this] { Changed(); };
    // HSL: one row of eight hues for Hue, Saturation or Luminance.
    hslMode = scroll->Add<Segmented>(std::vector<std::wstring>{L"Hue", L"Saturation", L"Luminance"}, 0);
    hslMode->onChange = [this](int m) {
      for (int i = 0; i < 8; i++) hsl[i]->Set(hslValues[m][i]);
    };
    static const wchar_t* const hues[8] = {L"Red", L"Orange", L"Yellow", L"Green", L"Aqua", L"Blue", L"Purple", L"Magenta"};
    for (int i = 0; i < 8; i++) {
      hsl[i] = scroll->Add<Slider>(hues[i], -100, 100, 0, 0);
      hsl[i]->compact = true;
      hsl[i]->labelWidth = 118;
      hsl[i]->onChange = [this, i](double v) {
        hslValues[hslMode->index][i] = v;
        Changed();
      };
    }
    // Color Grading: hue, saturation and luminance of each range.
    gradeRange = scroll->Add<Segmented>(std::vector<std::wstring>{L"Shadows", L"Midtones", L"Highlights", L"Global"}, 0);
    gradeRange->onChange = [this](int r) {
      for (int i = 0; i < 3; i++) grade[i]->Set(gradeValues[r][i]);
    };
    static const wchar_t* const gradeNames[3] = {L"Hue", L"Saturation", L"Luminance"};
    const double lo[3] = {0, 0, -100}, hi[3] = {360, 100, 100};
    for (int i = 0; i < 3; i++) {
      grade[i] = scroll->Add<Slider>(gradeNames[i], lo[i], hi[i], 0, 0);
      grade[i]->compact = true;
      grade[i]->labelWidth = 118;
      grade[i]->onChange = [this, i](double v) {
        gradeValues[gradeRange->index][i] = v;
        Changed();
      };
    }
    forms[3] = form({"gradeBlending", "gradeBalance"});
    forms[4] = form({"sharpenAmount", "sharpenRadius", "sharpenDetail", "sharpenMasking", "noiseLuminance", "noiseLuminanceDetail", "noiseColor", "noiseColorDetail"});
    forms[5] = form({"grainAmount", "grainSize", "grainRoughness", "vignetteAmount", "vignetteMidpoint", "vignetteRoundness", "vignetteFeather", "vignetteHighlights", "vignetteStyle"});
    AddFooter();
    ShowTab(0);
    dirty = false;  // nothing to show until a value moves
  }
  D2D1_SIZE_F Natural() override { return {440, std::min(780.f, H(ui::Bounds()) - 40)}; }
  void PaintContent() override { PaintFrame(); }

 protected:
  void Layout() override {
    float x0 = target.left + 22, x1 = target.right - 22;
    tabs->rect = R(x0 - 6, BodyTop() - 6, x1 + 6, BodyTop() + 24);
    scroll->rect = R(x0 - 4, BodyTop() + 34, x1 + 4, BodyBottom());
    float top = scroll->rect.top + 6 - scroll->Scroll(), y = top, l = x0, r = x1 - 6;
    switch (tab) {
      case 0: case 4: case 5:
        forms[tab]->Place(l, y, r);
        y = forms[tab]->rect.bottom;
        break;
      case 1:
        curve->rect = R(l, y, r, y + std::min(r - l, 300.f));
        y = curve->rect.bottom + 10;
        forms[1]->Place(l, y, r);
        y = forms[1]->rect.bottom;
        break;
      case 2:
        hslMode->rect = R(l, y, r, y + 28);
        y += 40;
        for (auto* s : hsl) {
          s->rect = R(l, y + 2, r, y + 30);
          y += 34;
        }
        break;
      case 3:
        gradeRange->rect = R(l, y, r, y + 28);
        y += 40;
        for (auto* s : grade) {
          s->rect = R(l, y + 2, r, y + 30);
          y += 34;
        }
        y += 8;
        forms[3]->Place(l, y, r);
        y = forms[3]->rect.bottom;
        break;
    }
    scroll->contentHeight = y - top + 12;
    scroll->Clamp();
    PlaceFooter();
  }
  void Preview() override { live.Show(Params()); }
  void Unpreview() override { live.Hide(); }
  bool Busy() override { return live.Busy(); }
  void Done(bool keep) override { live.Finish(keep, Params()); }

 private:
  void ShowTab(int i) {
    tab = i;
    for (int k = 0; k < 6; k++)
      if (forms[k]) forms[k]->visible = k == i || (i == 1 && k == 1) || (i == 3 && k == 3);
    if (forms[2]) forms[2]->visible = false;
    curve->visible = i == 1;
    hslMode->visible = i == 2;
    for (auto* s : hsl) s->visible = i == 2;
    gradeRange->visible = i == 3;
    for (auto* s : grade) s->visible = i == 3;
    scroll->offset.Reset(0);
  }
  std::string Params() const {
    JsonObject o;
    for (auto* f : forms)
      if (f) f->Write(o);
    static const char* const hslKeys[3] = {"hslHue", "hslSat", "hslLum"};
    for (int m = 0; m < 3; m++) {
      bool any = false;
      std::string a = "[";
      for (int i = 0; i < 8; i++) {
        any |= std::fabs(hslValues[m][i]) > .01;
        a += (i ? "," : "") + JsonNum(std::round(hslValues[m][i]));
      }
      if (any) o.Add(hslKeys[m], a + "]");
    }
    static const char* const gradeKeys[4] = {"gradeShadows", "gradeMidtones", "gradeHighlights", "gradeGlobal"};
    for (int r = 0; r < 4; r++) {
      if (std::fabs(gradeValues[r][1]) < .01 && std::fabs(gradeValues[r][2]) < .01) continue;
      o.Add(gradeKeys[r], "{\"hue\":" + JsonNum(std::round(gradeValues[r][0])) + ",\"sat\":" + JsonNum(std::round(gradeValues[r][1])) +
                              ",\"lum\":" + JsonNum(std::round(gradeValues[r][2])) + "}");
    }
    if (!curve->IsIdentity()) o.Add("pointCurve", curve->PointsJson());
    return o.Text();
  }
  LivePreview live;
  Tabs* tabs = nullptr;
  ScrollArea* scroll = nullptr;
  ParamForm* forms[6] = {};
  CurveEditor* curve = nullptr;
  Segmented *hslMode = nullptr, *gradeRange = nullptr;
  Slider* hsl[8] = {};
  Slider* grade[3] = {};
  double hslValues[3][8] = {};
  double gradeValues[4][3] = {};
  int tab = 0;
};

// =========================================================================== Liquify ==
struct LiquifyTool {
  const char* id;
  const wchar_t* name;
  const wchar_t* icon;
};
const LiquifyTool LiquifyTools[] = {
    {"forwardWarp", L"Forward Warp", PhHandSwipeRight}, {"reconstruct", L"Reconstruct", PhClockCounterClockwise},
    {"smooth", L"Smooth", PhWaves},                     {"twirlCw", L"Twirl Clockwise", PhSpiral},
    {"twirlCcw", L"Twirl Counterclockwise", PhArrowCounterClockwise}, {"pucker", L"Pucker", PhArrowsIn},
    {"bloat", L"Bloat", PhArrowsOut},                   {"pushLeft", L"Push Left", PhArrowLineLeft},
    {"freeze", L"Freeze Mask", PhSnowflake},            {"thaw", L"Thaw Mask", PhFire},
};

// The area a stroke of `width` covers along `pts` (round ends and turns), or null.
Microsoft::WRL::ComPtr<ID2D1Geometry> StrokeArea(const std::vector<D2D_POINT_2F>& pts, float width) {
  Microsoft::WRL::ComPtr<ID2D1Geometry> out;
  std::vector<D2D_POINT_2F> line = pts;
  if (line.size() == 1) line.push_back({line[0].x + .01f, line[0].y});  // a dab
  auto g = Polyline(line, false);
  if (!g) return out;
  Microsoft::WRL::ComPtr<ID2D1PathGeometry> wide;
  Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
  if (FAILED(GfxFactory()->CreatePathGeometry(&wide)) || FAILED(wide->Open(&sink))) return out;
  if (SUCCEEDED(g->Widen(width, RoundStyle(), nullptr, .5f, sink.Get())) && SUCCEEDED(sink->Close())) out = wide;
  return out;
}
Microsoft::WRL::ComPtr<ID2D1Geometry> Combine(ID2D1Geometry* a, ID2D1Geometry* b, D2D1_COMBINE_MODE mode) {
  Microsoft::WRL::ComPtr<ID2D1Geometry> out;
  Microsoft::WRL::ComPtr<ID2D1PathGeometry> g;
  Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
  if (FAILED(GfxFactory()->CreatePathGeometry(&g)) || FAILED(g->Open(&sink))) return out;
  if (SUCCEEDED(a->CombineWithGeometry(b, mode, nullptr, .5f, sink.Get())) && SUCCEEDED(sink->Close())) out = g;
  return out;
}

class Liquify;
// The tool panel: pinned beside the canvas, so strokes go to the document while it is open.
class LiquifyPanel : public Popover {
 public:
  explicit LiquifyPanel(Liquify* owner);
  D2D1_SIZE_F Natural() override { return {300, 64 + 2 * 46 + 12 + 4 * 34 + 12 + 36 + 12 + 36 + 20}; }
  float CornerRadius() const override { return tok::RadiusLarge; }
  void Arrange() override;
  void PaintContent() override {
    Text(L"Liquify", R(target.left + 20, target.top + 18, target.right - 20, target.top + 46), F_Title, P.text);
    Text(LiquifyTools[tool].name, R(target.left + 20, target.top + 40, target.right - 20, target.top + 60), F_Small, P.faint);
  }
  bool Animate(float dt) override {
    bool m = Popover::Animate(dt);
    for (size_t i = 0; i < tools.size(); i++) tools[i]->SetSelected(int(i) == tool);
    return m;
  }
  bool OnKey(UINT vk, Mods m) override;
  void SyncSize() {
    if (!sliders.empty()) sliders[0]->Set(size);
  }
  int tool = 0;
  double size = 100, density = 50, pressure = 100, rate = 80;

 private:
  Liquify* owner;
  std::vector<IconButton*> tools;
  std::vector<Slider*> sliders;
  TextButton *restore = nullptr, *cancel = nullptr, *ok = nullptr;
};

class Liquify : public CanvasTool {
 public:
  void Begin() {
    if (active || !canvas->HasDocument()) return;
    strokes.clear();
    redo.clear();
    frozen.Reset();
    live = std::make_shared<LivePreview>("filter.liquify");
    panel = std::make_unique<LiquifyPanel>(this);
    Rect w = ui::Bounds();
    panel->Open(R(w.right - 330, MidY(w) - 20, w.right - 320, MidY(w) + 20), Side::Left);
    active = true;
    pending = false;
    EnterMode(this);
  }
  void Finish(bool keep) {
    if (!active) return;
    active = false;
    if (live) live->Finish(keep && !strokes.empty(), Params());
    live.reset();
    if (panel) panel->Close();
    LeaveMode(this);
  }
  void RestoreAll() {
    JsonObject o;
    o.Str("tool", "reconstructAll").Num("amount", 100);
    strokes.push_back(o.Text());
    redo.clear();
    pending = true;
  }
  void Down(double x, double y, const PointerEvent& e) override {
    if (!active) return;
    pts = {{float(x), float(y), e.pen ? e.pressure : 1.f}};
  }
  void Drag(double x, double y, const PointerEvent& e) override {
    if (!active || pts.empty()) return;
    auto& last = pts.back();
    if (std::hypot(last[0] - x, last[1] - y) * canvas->Zoom() >= 2) pts.push_back({float(x), float(y), e.pen ? e.pressure : 1.f});
  }
  void Up(double x, double y, const PointerEvent& e) override {
    if (!active || pts.empty()) return;
    Drag(x, y, e);
    std::string list = "[";
    for (size_t i = 0; i < pts.size(); i++)
      list += (i ? ",[" : "[") + JsonNum(std::round(pts[i][0] * 10) / 10) + "," + JsonNum(std::round(pts[i][1] * 10) / 10) + "," + JsonNum(std::round(pts[i][2] * 100) / 100) + "]";
    list += "]";
    JsonObject o;
    o.Str("tool", LiquifyTools[panel->tool].id).Num("size", std::round(panel->size)).Num("density", std::round(panel->density));
    o.Num("pressure", std::round(panel->pressure)).Num("rate", std::round(panel->rate)).Add("points", list);
    strokes.push_back(o.Text());
    redo.clear();
    // The freeze mask, kept here as an area so it can be shown: Freeze adds, Thaw takes away.
    std::string id = LiquifyTools[panel->tool].id;
    if (id == "freeze" || id == "thaw") {
      std::vector<D2D_POINT_2F> line;
      for (auto& q : pts) line.push_back({q[0], q[1]});
      if (auto area = StrokeArea(line, float(panel->size))) {
        if (id == "freeze") frozen = frozen ? Combine(frozen.Get(), area.Get(), D2D1_COMBINE_MODE_UNION) : area;
        else if (frozen) frozen = Combine(frozen.Get(), area.Get(), D2D1_COMBINE_MODE_EXCLUDE);
      }
    }
    pts.clear();
    pending = true;
  }
  void Tick() override {
    if (active && pending && pts.empty() && live && !live->Busy()) {
      pending = false;
      if(strokes.empty())live->Hide();else live->Show(Params());
    }
  }
  void Deactivate() override {
    if (active) Finish(true);
  }
  void Overlay() override {
    if (!active) return;
    // The frozen area in red, as Photoshop shows its mask (document space -> screen).
    std::string id = LiquifyTools[panel->tool].id;
    bool masking = id == "freeze" || id == "thaw";
    if (frozen) {
      D2D_POINT_2F o = canvas->ToScreen(0, 0);
      float z = canvas->Zoom();
      D2D1_MATRIX_3X2_F previous;
      Dc()->GetTransform(&previous);
      Dc()->SetTransform(D2D1::Matrix3x2F::Scale(z, z) * D2D1::Matrix3x2F::Translation(o.x, o.y) * *D2D1::Matrix3x2F::ReinterpretBaseType(&previous));
      Dc()->FillGeometry(frozen.Get(), Ink(Rgba(1, .22f, .26f, .38f)));
      Dc()->SetTransform(previous);
    }
    std::vector<D2D_POINT_2F> s;
    for (auto& p : pts) s.push_back(canvas->ToScreen(p[0], p[1]));
    if (masking && !s.empty()) {
      // The mask stroke being painted, at the brush's width.
      if (s.size() == 1) s.push_back({s[0].x + .01f, s[0].y});
      if (auto g = Polyline(s, false))
        Dc()->DrawGeometry(g.Get(), Ink(id == "freeze" ? Rgba(1, .22f, .26f, .38f) : Rgba(1, 1, 1, .22f)), float(panel->size * canvas->Zoom()), RoundStyle());
    } else if (auto g = Polyline(s, false)) {
      Dc()->DrawGeometry(g.Get(), Ink(Rgba(1, .93f, .81f, .5f)), 1.4f, RoundStyle());
    }
    if (pointerX < 0 || !canvas->PointerOver()) return;
    float r = float(panel->size * .5 * canvas->Zoom());
    StrokeEllipse(pointerX, pointerY, r + .8f, r + .8f, Rgba(0, 0, 0, .5f), 1.2f);
    StrokeEllipse(pointerX, pointerY, r, r, Rgba(1, 1, 1, .9f), 1);
    float inner = r * float(1 - panel->density / 100);
    if (inner > 2) StrokeEllipse(pointerX, pointerY, inner, inner, Rgba(1, 1, 1, .35f), 1);
  }
  bool HidesCursor() override { return true; }
  double* BrushSize() override { return active && panel ? &panel->size : nullptr; }
  void BrushSized() override {
    if (panel) panel->SyncSize();
  }
  bool Options(Container& dock, std::vector<Widget*>& out) override {
    if (!active) return false;
    out.push_back(dock.Add<Label>(L"Liquify  ·  paint on the photo  ·  Enter applies, Esc cancels", F_Label));
    return true;
  }
  bool Key(UINT vk, Mods m) override {
    if (!active) return false;
    if(m.ctrl&&(vk=='Z'||vk=='Y')) {
      if(!pts.empty())return true;
      bool forward=vk=='Y'||m.shift;
      auto& from=forward?redo:strokes;auto& to=forward?strokes:redo;
      if(!from.empty()){to.push_back(std::move(from.back()));from.pop_back();RebuildFrozen();pending=true;}
      return true;
    }
    if (vk == VK_RETURN) { Finish(true); return true; }
    if (vk == VK_ESCAPE) { Finish(false); return true; }
    return false;
  }
  Json State() const {return Json::Parse(JsonObject().Bool("active",active).Num("strokes",double(strokes.size())).Num("redo",double(redo.size())).Text());}

 private:
  void RebuildFrozen() {
    frozen.Reset();
    for(auto& item:strokes) {
      auto stroke=Json::Parse(item);auto tool=stroke["tool"].Str();
      if(tool!="freeze"&&tool!="thaw")continue;
      std::vector<D2D_POINT_2F> line;for(auto& p:stroke["points"].a)line.push_back({float(p[0].Num()),float(p[1].Num())});
      if(auto area=StrokeArea(line,float(stroke["size"].Num()))) {
        if(tool=="freeze")frozen=frozen?Combine(frozen.Get(),area.Get(),D2D1_COMBINE_MODE_UNION):area;
        else if(frozen)frozen=Combine(frozen.Get(),area.Get(),D2D1_COMBINE_MODE_EXCLUDE);
      }
    }
  }
  std::string Params() const {
    std::string list = "[";
    for (size_t i = 0; i < strokes.size(); i++) list += (i ? "," : "") + strokes[i];
    JsonObject o;
    o.Add("strokes", list + "]");
    return o.Text();
  }
  bool active = false, pending = false;
  std::vector<std::array<float, 3>> pts;
  std::vector<std::string> strokes;
  std::vector<std::string> redo;
  Microsoft::WRL::ComPtr<ID2D1Geometry> frozen;  // document pixels
  std::shared_ptr<LivePreview> live;
  std::unique_ptr<LiquifyPanel> panel;
};
Liquify liquify;

LiquifyPanel::LiquifyPanel(Liquify* o) : owner(o) {
  closeInHeader=true;
  pinned = true;
  for (size_t i = 0; i < std::size(LiquifyTools); i++) {
    auto* b = Add<IconButton>(LiquifyTools[i].icon, LiquifyTools[i].name, [this, i] { tool = int(i); });
    b->iconSize = 19;
    tools.push_back(b);
  }
  auto slider = [&](const wchar_t* label, double lo, double hi, double& v, bool log = false) {
    auto* s = Add<Slider>(label, lo, hi, v, 0, lo >= 1 && hi > 1000 ? L" px" : L"");
    s->compact = true;
    s->labelWidth = 76;
    s->logScale = log;
    double* target = &v;
    s->onChange = [target](double x) { *target = x; };
    sliders.push_back(s);
  };
  slider(L"Size", 1, 15000, size, true);
  slider(L"Density", 0, 100, density);
  slider(L"Pressure", 1, 100, pressure);
  slider(L"Rate", 0, 100, rate);
  restore = Add<TextButton>(L"Restore All", TextButton::Secondary, [this] { owner->RestoreAll(); });
  cancel = Add<TextButton>(L"Cancel", TextButton::Secondary, [this] { owner->Finish(false); });
  ok = Add<TextButton>(L"OK", TextButton::Primary, [this] { owner->Finish(true); });
}
void LiquifyPanel::Arrange() {
  float x0 = target.left + 16, x1 = target.right - 16, y = target.top + 64;
  float step = (x1 - x0) / 5;
  for (size_t i = 0; i < tools.size(); i++) tools[i]->rect = RWH(x0 + step * float(i % 5) + (step - 40) / 2, y + float(i / 5) * 46, 40, 40);
  y += 2 * 46 + 12;
  for (auto* s : sliders) {
    s->rect = R(x0 + 4, y, x1 - 4, y + 28);
    y += 34;
  }
  y += 12;
  restore->rect = R(x0 + 4, y, x1 - 4, y + 36);
  y += 48;
  ok->rect = R(x1 - 100, y, x1 - 4, y + 36);
  cancel->rect = R(x1 - 100 - 8 - 92, y, x1 - 108, y + 36);
}
bool LiquifyPanel::OnKey(UINT vk, Mods m) { return owner->Key(vk, m); }

// ======================================================================= Layer Style ==
struct EffectDef {
  const char* cmd;
  const wchar_t* name;
};
const EffectDef Effects[] = {
    {"layer.layerStyle.blendingOptions", L"Blending Options"}, {"layer.layerStyle.dropShadow", L"Drop Shadow"},
    {"layer.layerStyle.innerShadow", L"Inner Shadow"},         {"layer.layerStyle.outerGlow", L"Outer Glow"},
    {"layer.layerStyle.innerGlow", L"Inner Glow"},             {"layer.layerStyle.bevelEmboss", L"Bevel & Emboss"},
    {"layer.layerStyle.satin", L"Satin"},                      {"layer.layerStyle.colorOverlay", L"Color Overlay"},
    {"layer.layerStyle.gradientOverlay", L"Gradient Overlay"}, {"layer.layerStyle.patternOverlay", L"Pattern Overlay"},
    {"layer.layerStyle.stroke", L"Stroke"},
};
constexpr size_t EffectCount = std::size(Effects);

// A row of the effect list: the selected one lifts; an applied one carries a check.
class EffectRow : public Widget {
 public:
  std::wstring label;
  bool current = false, applied = false;
  std::function<void()> pick;
  void Paint() override {
    FillRound(rect, 10, Fade(P.hover, hover.v * (current ? 0 : 1)));
    if (current) {
      FillRound(rect, 10, Rgba(1, 1, 1, .11f));
      FillRound(R(rect.left + 2, MidY(rect) - 7, rect.left + 4.5f, MidY(rect) + 7), 1.2f, P.accent);
    }
    if (applied) IconAt(PhCheck, rect.left + 18, MidY(rect), 13, P.text);
    Text(label, R(rect.left + 34, rect.top, rect.right - 8, rect.bottom), F_Row, current ? P.text : P.dim);
  }
  void OnUp(const PointerEvent& e, bool inside) override {
    Widget::OnUp(e, inside);
    if (inside && pick) pick();
  }
};

class LayerStyleDialog : public Editor {
 public:
  explicit LayerStyleDialog(size_t first) : Editor(L"Layer Style") {
    for (size_t i = 0; i < EffectCount; i++) {
      auto* r = Add<EffectRow>();
      r->label = Effects[i].name;
      r->pick = [this, i] { Select(i); };
      rows.push_back(r);
    }
    scroll = Add<ScrollArea>();
    AddFooter();
    Select(first);
    dirty = false;
  }
  D2D1_SIZE_F Natural() override { return {700, std::min(640.f, H(ui::Bounds()) - 40)}; }
  void PaintContent() override {
    PaintFrame();
    float x = target.left + 22 + 196;
    Line(x, BodyTop(), x, BodyBottom(), P.sep, 1);
    if (form && form->fields.empty())
      Text(L"Nothing to set for this effect here.", R(scroll->rect.left + 8, scroll->rect.top + 4, scroll->rect.right, scroll->rect.top + 28), F_Label, P.dim);
  }

 protected:
  void Layout() override {
    float x0 = target.left + 22, x1 = target.right - 22, y = BodyTop();
    for (auto* r : rows) {
      r->rect = R(x0, y, x0 + 184, y + 34);
      y += 36;
    }
    scroll->rect = R(x0 + 210, BodyTop(), x1, BodyBottom());
    if (form) {
      form->Place(scroll->rect.left + 4, scroll->rect.top + 4 - scroll->Scroll(), scroll->rect.right - 10);
      scroll->contentHeight = form->Height() + 12;
      scroll->Clamp();
    }
    PlaceFooter();
  }
  void Preview() override {
    if (touched[sel] && form) previews[sel]->Show(Params());
  }
  void Unpreview() override {
    if (previews[sel]) previews[sel]->Hide();
  }
  bool Busy() override {
    for (auto& p : previews)
      if (p && p->Busy()) return true;
    return false;
  }
  void Done(bool keep) override {
    if (keep) {
      Commit(sel);
      return;
    }
    if (previews[sel]) previews[sel]->Finish(false, Params());
    Undo(committed);  // the effects kept while switching rows
  }

 private:
  std::string Params() const {
    JsonObject o;
    if (form) form->Write(o);
    return o.Text();
  }
  // Blending Options show what the layer has now (and PhotoCraft's own defaults for the rest).
  static void StartFromLayer(std::vector<ParamField>& fields) {
    const Json* l = ActiveLayer();
    auto squash = [](std::string s) {
      std::string o;
      for (char c : s)
        if (c != ' ' && c != '(' && c != ')') o += char(tolower(static_cast<unsigned char>(c)));
      return o;
    };
    for (auto& f : fields) {
      if (l && f.key == "opacity") f.def = std::round((*l)["opacity"].Num(1) * 100);
      if (l && f.key == "fillOpacity") f.def = std::round((*l)["fill"].Num(1) * 100);
      if (l && f.key == "blend")
        for (size_t c = 0; c < f.choices.size(); c++)
          if (squash(f.choices[c]) == squash((*l)["blend"].Str())) f.choice = int(c);
      if (f.key == "blendClippedLayersAsGroup" || f.key == "transparencyShapesLayer") f.flag = true;
    }
  }
  // Keep the effect being edited (if it was changed) before another takes the pane.
  void Commit(size_t i) {
    if (!touched[i] || !previews[i]) return;
    previews[i]->Finish(true, Params());
    previews[i] = std::make_shared<LivePreview>(Effects[i].cmd);
    touched[i] = false;
    rows[i]->applied = true;
    committed++;
  }
  void Select(size_t i) {
    if (form && i == sel) return;
    if (form) Commit(sel);
    sel = i;
    for (size_t k = 0; k < rows.size(); k++) rows[k]->current = k == i;
    if (!previews[i]) previews[i] = std::make_shared<LivePreview>(Effects[i].cmd);
    auto fields = ParseParams(paramsDoc[Effects[i].cmd]);
    fields.erase(std::remove_if(fields.begin(), fields.end(), [](const ParamField& f) { return f.key == "add" || f.key == "layer"; }), fields.end());
    if (i == 0) StartFromLayer(fields);
    scroll->Clear();
    form = scroll->Add<ParamForm>(std::move(fields), true);
    form->touchedOnly = true;  // the effect's own values stand until one is changed
    form->onChange = [this] {
      touched[sel] = true;
      Changed();
    };
    scroll->offset.Reset(0);
    Layout();
  }
  std::vector<EffectRow*> rows;
  ScrollArea* scroll = nullptr;
  ParamForm* form = nullptr;
  size_t sel = 0;
  std::shared_ptr<LivePreview> previews[EffectCount];
  bool touched[EffectCount] = {};
  int committed = 0;
};
}  // namespace

Json LiquifyState(){return liquify.State();}

bool OpenEditor(const std::string& id, const std::wstring& label, const Rect& from) {
  if (!canvas->HasDocument()) return false;
  std::wstring title = label.empty() ? CommandLabel(id) : label;
  while (!title.empty() && (title.back() == L'.' || title.back() == L'…')) title.pop_back();
  if (id == "image.adjustments.curves" || id == "layer.newAdjustmentLayer.curves") {
    ShowEditor(std::make_unique<CurvesDialog>(id, title), from);
    return true;
  }
  if (id == "filter.cameraRaw") {
    ShowEditor(std::make_unique<CameraRawDialog>(title), from);
    return true;
  }
  if (id == "filter.liquify") {
    liquify.Begin();
    return true;
  }
  for (size_t i = 0; i < EffectCount; i++)
    if (id == Effects[i].cmd) {
      ShowEditor(std::make_unique<LayerStyleDialog>(i), from);
      return true;
    }
  return false;
}
