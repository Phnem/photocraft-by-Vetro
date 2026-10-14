// PhotoCraft by Vetro -- the shell: the window, the floating docks, menus, tools and panels.
// Everything here is assembled from the interaction classes in widgets.h; the engine is reached
// only through the bridge (bridge.h).
#include "shell.h"
#include "panels.h"
#include "i18n.h"
#include "gestures.h"
#include "clipboard.h"
#include "documents.h"
#include "guides.h"
#include "color_picker.h"
#include "workspace_panels.h"
#include <share.h>
#include "icons_custom.h"
#include "icons_phosphor.h"
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <windowsx.h>
#include <map>
#include <set>

// The parts tools.cpp and editors.cpp share (shell.h) have external linkage; the rest of this file is
// the shell's own.
// ============================================================================ state ==
HWND hwnd = nullptr;
float dpiScale = 1;
PcBridge* engine = nullptr;
Canvas* canvas = nullptr;
Json docInfo, sessionInfo;
Json marks;  // Count markers, notes and the Ruler line of the active document (pc_marks)
uint64_t infoRevision = ~0ull;
float pointerX = -1, pointerY = -1;
bool animating = true;
double lastFrame = 0;
bool refreshWanted = false;
constexpr UINT WM_JOBS = WM_APP + 2;

struct CatalogRow {
  std::vector<std::string> path;
  std::string label, shortcut, id;
  bool live = false;
};
std::vector<CatalogRow> catalog;
std::map<std::string, std::string> paramsDoc;

void Refresh();
void RunCommand(const std::string& id, const std::wstring& label);
void AskText(double x, double y, std::wstring title, std::wstring initial, std::function<void(const std::wstring&)> done);
void Layout(bool snap);
void FinishNudge();
void PickColour(double x, double y, bool background);
bool temporaryErase = false;
bool brushSettingsPending = false;
bool layersFocused = false, visibilityPainting = false;
RecentDocuments recentDocuments;
DocumentTabs* documentTabs = nullptr;
StartScreen* startScreen = nullptr;
std::unique_ptr<Menu> tabMenu;
DWORD ownClipboardSequence = 0;
bool closingDocuments = false;
void PrepareDocumentChange();
void SwitchDocument(uint64_t id);
void CloseDocument(uint64_t id, std::function<void(bool)> after = nullptr);
void CloseDocuments(std::vector<uint64_t> ids, std::function<void(bool)> after = nullptr);
void InitializeDocuments();
std::vector<MenuItem> RecentMenu();
void CopyClipboard(bool merged);
void PasteClipboard(const std::string& command);
bool ShellEnabled(const std::string& id);

void VisitLayers(const Json& list, const std::function<void(const Json&)>& visit, int depth = 0) {
  if (depth > 128) return;
  for (size_t i = 0; i < list.size(); i++) {
    visit(list[i]);
    VisitLayers(list[i]["children"], visit, depth + 1);
  }
}
std::string UniqueLayerName(const std::wstring& base, bool numbered = true) {
  std::set<std::string> names;
  VisitLayers(docInfo["layers"], [&](const Json& l) { names.insert(l["name"].Str()); });
  std::string prefix = Narrow(base);
  if (!numbered && !names.count(prefix)) return prefix;
  for (size_t n = 1; n <= names.size() + 1; n++) {
    std::string name = prefix + " " + std::to_string(n);
    if (!names.count(name)) return name;
  }
  return prefix;
}
std::string NamedParams(const std::string& id, const std::string& params) {
  Json p = Json::Parse(params);
  if (p.kind != Json::Object || !p["name"].IsNull()) return params;
  std::wstring base;
  bool numbered = true;
  if (id == "file.new") {
    std::set<std::string> names;
    for (const auto& doc : sessionInfo["documents"].a) names.insert(doc["name"].Str());
    std::string prefix = Narrow(i18n::Tr(L"Untitled"));
    size_t n = 1;
    while (names.count(prefix + "-" + std::to_string(n))) n++;
    Json name; name.kind = Json::String; name.s = prefix + "-" + std::to_string(n);
    p.o.emplace_back("name", std::move(name));
    return JsonText(p);
  }
  if (id.starts_with("layer.newAdjustmentLayer.") || id.starts_with("layer.newFillLayer.")) {
    base = CommandLabel(id);
    // Menu labels may start with "New "; use the adjustment's own name.
    const std::map<std::string, std::wstring> names{
      {"brightnessContrast", L"Brightness/Contrast"}, {"levels", L"Levels"}, {"curves", L"Curves"},
      {"exposure", L"Exposure"}, {"vibrance", L"Vibrance"}, {"hueSaturation", L"Hue/Saturation"},
      {"colorBalance", L"Color Balance"}, {"blackAndWhite", L"Black & White"}, {"photoFilter", L"Photo Filter"},
      {"channelMixer", L"Channel Mixer"}, {"colorLookup", L"Color Lookup"}, {"invert", L"Invert"},
      {"posterize", L"Posterize"}, {"threshold", L"Threshold"}, {"gradientMap", L"Gradient Map"},
      {"selectiveColor", L"Selective Color"}, {"solidColor", L"Solid Color"}, {"gradient", L"Gradient Fill"}, {"pattern", L"Pattern Fill"}};
    auto name = names.find(id.substr(id.rfind('.') + 1));
    if (name != names.end()) base = i18n::Tr(name->second);
  }
  if (id == "layer.new.layer") base = i18n::Tr(L"Layer");
  if (id == "layer.new.group") base = i18n::Tr(L"Group");
  if (id == "shape.create") {
    const auto& k = p["kind"].Str();
    base = i18n::Tr(k == "rect" ? L"Rectangle" : k == "roundedRect" ? L"Rounded Rectangle" : k == "ellipse" ? L"Ellipse" :
                    k == "polygon" ? L"Polygon" : k == "star" ? L"Star" : k == "line" ? L"Line" : L"Shape");
  }
  if (id == "layer.duplicate") {
    const Json* l = ActiveLayer();
    int count = 0;
    VisitLayers(docInfo["layers"], [&](const Json& item) {
      if (item["selected"].Truthy()) count++;
      if (!p["layer"].IsNull() && item["id"].Int() == p["layer"].Int()) l = &item;
    });
    if (l && (count <= 1 || !p["layer"].IsNull())) {
      base = W((*l)["name"].Str()) + L" " + i18n::Tr(L"copy");
      numbered = false;
    }
  }
  if (base.empty()) return params;
  Json name; name.kind = Json::String; name.s = UniqueLayerName(base, numbered);
  p.o.emplace_back("name", std::move(name));
  return JsonText(p);
}

std::wstring W(const std::string& s) { return Widen(s); }
// A message in the interface language with one piece filled in ("Opened %ls").
std::wstring Fmt(const wchar_t* english, const std::wstring& piece) {
  std::wstring f = i18n::Tr(english);
  size_t at = f.find(L"%ls");
  if (at != std::wstring::npos) f.replace(at, 3, piece);
  return f;
}

std::wstring CommandLabel(const std::string& id) {
  for (auto& r : catalog)
    if (r.id == id) {
      std::wstring l = W(r.label);
      while (!l.empty() && (l.back() == L'.' || l.back() == L'\u2026')) l.pop_back();
      return l;
    }
  return {};
}

// Engine calls go through the engine thread (jobs.h) and end in Refresh(): the canvas re-renders
// what changed, panels re-read.
void Exec(const std::string& id, const std::string& params, Then then, bool toastError) {
  if (id != "shape.info" && id != "gradient.fill.get" && id != "gradient.presets.list" && id != "brush.get" && id != "brush.presets.list" && id != "type.info" && id != "type.fonts" && id != "type.hitTest" && id != "cloneSource.list") { FinishTextEditing(); FinishNudge(); FinishParameterPreviews(); }
  std::string named = NamedParams(id, params);
  std::string copySuffix = Narrow(i18n::Tr(L"copy"));
  std::string initialLayer = Narrow(i18n::Tr(L"Layer")) + " 1", backgroundLayer = Narrow(i18n::Tr(L"Background"));
  auto out = std::make_shared<std::pair<int, std::string>>(0, "");
  jobs::Run(
      [id, named, out, copySuffix, initialLayer, backgroundLayer] {
        char* result = nullptr;
        out->first = pc_execute(engine, id.c_str(), named.c_str(), &result);
        out->second = TakeString(result);
        if (out->first == 1 && id == "file.new" && initialLayer != "Layer 1") {
          Json info = Json::Parse(TakeString(pc_document_info(engine)));
          VisitLayers(info["layers"], [&](const Json& l) {
            std::string name = l["name"].Str() == "Layer 1" ? initialLayer : l["name"].Str() == "Background" ? backgroundLayer : "";
            if (name.empty()) return;
            JsonObject o; o.Num("layer", double(l["id"].Int())).Str("name", name);
            char* r = nullptr; pc_execute(engine, "layer.renameLayer", o.Text().c_str(), &r); TakeString(r);
          });
        }
        // Multi-layer duplication does not accept `name`; localise each returned copy.
        if (out->first == 1 && id == "layer.duplicate" && copySuffix != "copy") {
          Json copies = Json::Parse(out->second), info = Json::Parse(TakeString(pc_document_info(engine)));
          for (size_t i = 0; i < copies["layers"].size(); i++) {
            int64_t copy = copies["layers"][i].Int();
            VisitLayers(info["layers"], [&](const Json& l) {
              if (l["id"].Int() != copy) return;
              std::string name = l["name"].Str();
              size_t at = name.rfind(" copy");
              if (at == std::string::npos) return;
              name.replace(at + 1, 4, copySuffix);
              JsonObject o; o.Num("layer", double(copy)).Str("name", name);
              char* r = nullptr;
              pc_execute(engine, "layer.renameLayer", o.Text().c_str(), &r);
              TakeString(r);
            });
          }
        }
        if (out->first != 1 && out->second.empty()) out->second = LastError();
      },
      [out, then, toastError] {
        if (out->first != 1 && toastError) ui::Toast(W(out->second), true);
        infoRevision = ~0ull;
        Refresh();
        if (then) then(out->first == 1, out->second);
      },
      CommandLabel(id));
}

void Undo(int steps) {
  FinishTextEditing();
  FinishNudge();
  FinishParameterPreviews();
  if (steps == 0) return;
  jobs::Run(
      [steps] {
        for (int i = 0; i < std::abs(steps); i++) {
          if (steps > 0) pc_undo(engine);
          else pc_redo(engine);
        }
      },
      [] { Refresh(); }, steps > 0 ? L"Undo" : L"Redo");
}

// --------------------------------------------------------------------- live previews --
LivePreview::LivePreview(std::string command) : cmd(std::move(command)), state(std::make_shared<State>()) {}
void LivePreview::Show(const std::string& params) {
  auto s = state;
  std::string c = cmd;
  s->inFlight++;
  jobs::Run(
      [s, c, params] {
        if (s->applied) {
          pc_undo(engine);
          s->applied = false;
        }
        // Only what this preview added is taken back later: a command that leaves no history step
        // (a setting) must not undo the user's last edit.
        uint64_t steps = pc_history_len(engine), rev0 = 0, rev1 = 0;
        int32_t dmg[4]{};
        pc_revision(engine, &rev0, dmg);
        char* r = nullptr;
        int ok = pc_execute(engine, c.c_str(), params.c_str(), &r);
        std::string text = TakeString(r);
        pc_revision(engine, &rev1, dmg);
        s->applied = ok == 1 && (pc_history_len(engine) != steps || rev1 != rev0);
        s->params = params;
        s->error = ok == 1 ? "" : (text.empty() ? LastError() : text);
      },
      [s] {
        s->inFlight--;
        s->appliedUi = s->applied;
        if (!s->error.empty()) ui::Toast(W(s->error), true);
        s->error.clear();
        Refresh();
      },
      CommandLabel(c));
}
void LivePreview::Hide() {
  auto s = state;
  s->inFlight++;
  jobs::Run(
      [s] {
        if (s->applied) pc_undo(engine);
        s->applied = false;
      },
      [s] {
        s->inFlight--;
        s->appliedUi = false;
        Refresh();
      });
}
void LivePreview::Finish(bool keep, const std::string& params, std::function<void()> after) {
  auto s = state;
  std::string c = cmd;
  s->inFlight++;
  jobs::Run(
      [s, c, params, keep] {
        if (keep) {
          if (!(s->applied && s->params == params)) {
            if (s->applied) pc_undo(engine);
            char* r = nullptr;
            int ok = pc_execute(engine, c.c_str(), params.c_str(), &r);
            std::string text = TakeString(r);
            s->error = ok == 1 ? "" : (text.empty() ? LastError() : text);
          }
        } else if (s->applied) {
          pc_undo(engine);
        }
        s->applied = false;
      },
      [s, after] {
        s->inFlight--;
        s->appliedUi = false;
        if (!s->error.empty()) ui::Toast(W(s->error), true);
        s->error.clear();
        Refresh();
        if (after) after();
      },
      CommandLabel(c));
}

std::string Hex(const Json& rgba) {
  auto c = [&](size_t i) { return std::clamp(int(std::lround(rgba[i].Num() * 255)), 0, 255); };
  char buf[16];
  snprintf(buf, sizeof buf, "#%02x%02x%02x", c(0), c(1), c(2));
  return buf;
}
D2D1_COLOR_F ColourOf(const Json& rgba) { return Rgba(float(rgba[0].Num()), float(rgba[1].Num()), float(rgba[2].Num()), 1.f); }

// ======================================================================= tool state ==
BrushState brush;
struct SelectState {
  int mode = 0;  // replace add subtract intersect
  double feather = 0, tolerance = 32;
  bool contiguous = true, antiAlias = true, sampleAllLayers = false;
  int sampleSize = 1;
} sel;
const char* const SelectModes[] = {"replace", "add", "subtract", "intersect"};
int gradientStyle = 0;
const char* const GradientStyles[] = {"linear", "radial", "angle", "reflected", "diamond"};
double typeSize = 48;
double shapeRadius = 30;  // the Rounded Rectangle's corners, document px
bool typeVertical = false;

const char* ModeFor(const PointerEvent& e) {
  if (e.mods.shift && e.mods.alt) return "intersect";
  if (e.mods.shift) return "add";
  if (e.mods.alt) return "subtract";
  return SelectModes[sel.mode];
}

// The selection outline the shell drew last, so ellipses and lassos show their own shape.
struct SelectionShape {
  enum Kind { None, RectK, EllipseK, PolyK } kind = None;
  double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  std::vector<D2D_POINT_2F> pts;
  std::vector<int> bounds;  // the engine's bounds when it was drawn
} shape;

void RememberShapeBounds() {
  shape.bounds.clear();
  const Json& sb = docInfo["selectionBounds"];
  for (size_t i = 0; i < sb.size(); i++) shape.bounds.push_back(int(sb[i].Int()));
}
void ShowSelectionOutline(const std::vector<D2D_POINT_2F>& polygon) {
  if (polygon.size() < 3) return;
  shape.kind = SelectionShape::PolyK;
  shape.pts = polygon;
  RememberShapeBounds();
}

struct NudgeState {
  std::shared_ptr<LivePreview> preview;
  std::string command, params;
  int dx = 0, dy = 0;
  double at = 0;
  SelectionShape outline;
};
NudgeState nudge;
void FinishNudge() {
  auto preview = std::move(nudge.preview);
  std::string params = nudge.params;
  nudge = {};
  if (preview) preview->Finish(true, params);
}
bool ArrowNudge(UINT vk, Mods m, bool selectionTool, bool moveTool) {
  if (vk < VK_LEFT || vk > VK_DOWN || m.alt || (!m.ctrl && !selectionTool && !moveTool)) return false;
  if (!canvas->HasDocument()) return true;
  bool selection = selectionTool && !m.ctrl;
  if (selection && !docInfo["hasSelection"].Truthy()) return true;
  std::string command = selection ? "select.transformSelection" : "layer.translate";
  if (nudge.preview && (nudge.command != command || ui::NowMs() - nudge.at > 700)) FinishNudge();
  if (!nudge.preview) {
    nudge.command = command;
    nudge.preview = std::make_shared<LivePreview>(command);
    if (selection) nudge.outline = shape;
  }
  int step = m.shift ? 10 : 1;
  nudge.dx += vk == VK_LEFT ? -step : vk == VK_RIGHT ? step : 0;
  nudge.dy += vk == VK_UP ? -step : vk == VK_DOWN ? step : 0;
  nudge.at = ui::NowMs();
  if (selection) {
    shape = nudge.outline;
    shape.x0 += nudge.dx; shape.x1 += nudge.dx; shape.y0 += nudge.dy; shape.y1 += nudge.dy;
    for (auto& p : shape.pts) { p.x += float(nudge.dx); p.y += float(nudge.dy); }
    if (shape.bounds.size() == 4) { shape.bounds[0] += nudge.dx; shape.bounds[1] += nudge.dy; }
  }
  JsonObject o;
  if (selection) o.Add("matrix", "[1,0,0,1," + std::to_string(nudge.dx) + "," + std::to_string(nudge.dy) + "]").Str("interpolation", "nearest");
  else o.Num("dx", nudge.dx).Num("dy", nudge.dy);
  nudge.params = o.Text();
  nudge.preview->Show(nudge.params);
  return true;
}

void AntsDocumentBox(double x0, double y0, double x1, double y1, bool ellipse);
void SizeHud(double width, double height);
bool hasBrushAnchor = false;
double brushAnchorX = 0, brushAnchorY = 0;

void DrawAnts(ID2D1Geometry* g) {
  float offset = float(fmod(ui::NowMs() / 60.0, 8.0));
  Dc()->DrawGeometry(g, Ink(Rgba(0, 0, 0, .85f)), 1.2f);
  Dc()->DrawGeometry(g, Ink(Rgba(1, 1, 1, .95f)), 1.2f, DashStyle(offset));
}
void AntsRect(const Rect& r) {
  Microsoft::WRL::ComPtr<ID2D1RectangleGeometry> g;
  GfxFactory()->CreateRectangleGeometry(r, &g);
  if (g) DrawAnts(g.Get());
}
void AntsEllipse(const Rect& r) {
  Microsoft::WRL::ComPtr<ID2D1EllipseGeometry> g;
  GfxFactory()->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(MidX(r), MidY(r)), W(r) / 2, H(r) / 2), &g);
  if (g) DrawAnts(g.Get());
}
Microsoft::WRL::ComPtr<ID2D1PathGeometry> Polyline(const std::vector<D2D_POINT_2F>& pts, bool close) {
  Microsoft::WRL::ComPtr<ID2D1PathGeometry> g;
  if (pts.size() < 2) return g;
  GfxFactory()->CreatePathGeometry(&g);
  if (!g) return g;
  Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
  g->Open(&sink);
  sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_HOLLOW);
  for (size_t i = 1; i < pts.size(); i++) sink->AddLine(pts[i]);
  sink->EndFigure(close ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
  sink->Close();
  return g;
}
void AntsPoly(const std::vector<D2D_POINT_2F>& pts, bool close) {
  auto g = Polyline(pts, close);
  if (g) DrawAnts(g.Get());
}
Rect ScreenRect(double x0, double y0, double x1, double y1) {
  auto a = canvas->ToScreen(std::min(x0, x1), std::min(y0, y1));
  auto b = canvas->ToScreen(std::max(x0, x1), std::max(y0, y1));
  return R(a.x, a.y, b.x, b.y);
}
std::string PointList(const std::vector<D2D_POINT_2F>& pts, bool pressure) {
  std::string list = "[";
  for (size_t i = 0; i < pts.size(); i++)
    list += (i ? ",[" : "[") + JsonNum(std::round(pts[i].x * 10) / 10) + "," + JsonNum(std::round(pts[i].y * 10) / 10) + (pressure ? ",1]" : "]");
  return list + "]";
}

// ============================================================================ tools ==
// The Brush, Pencil and Eraser: a live stroke rendered by the engine dab by dab.
struct BrushTool : CanvasTool {
  std::string cmd;
  bool erase;
  double t0 = 0;
  bool stroking = false;
  double lastX = 0, lastY = 0, lastPush = 0;
  float lastP = 1;
  BrushTool(std::string c, bool e) : cmd(std::move(c)), erase(e) {}
  std::string Params(double x, double y, float p) {
    JsonObject o;
    o.Add("points", "[[" + JsonNum(x) + "," + JsonNum(y) + "," + JsonNum(p) + ",0,0,0,0]]");
    o.Num("size", brush.size).Num("smoothing", brush.smoothing / 100).Num("zoom", canvas->Zoom());
    if (cmd == "paint.stroke") o.Num("hardness", brush.hardness / 100).Num("flow", brush.flow / 100);
    o.Num("opacity", brush.opacity / 100);
    if (erase || temporaryErase) o.Bool("erase", true);
    return o.Text();
  }
  void Down(double x, double y, const PointerEvent& e) override {
    t0 = e.timeMs;
    double startX = e.mods.shift && hasBrushAnchor ? brushAnchorX : x;
    double startY = e.mods.shift && hasBrushAnchor ? brushAnchorY : y;
    int32_t d[4]{};
    if (pc_stroke_begin(engine, cmd.c_str(), Params(startX, startY, e.pressure).c_str(), d) != 1) {
      ui::Toast(W(LastError()), true);
      return;
    }
    canvas->Damage({d[0], d[1], d[2], d[3]});
    stroking = true;
    lastX = x; lastY = y; lastP = e.pressure; lastPush = e.timeMs;
    if (startX != x || startY != y) Push(x, y, e.pressure, e.timeMs);
  }
  void Push(double x, double y, float p, double t) {
    double pt[4] = {x, y, p, t - t0};
    int32_t d[4]{};
    if (pc_stroke_push(engine, pt, 1, d) == 1) canvas->Damage({d[0], d[1], d[2], d[3]});
    lastPush = t;
  }
  void Drag(double x, double y, const PointerEvent& e) override {
    if (!stroking) return;
    lastX = x; lastY = y; lastP = e.pressure;
    Push(x, y, e.pressure, e.timeMs);
  }
  // With smoothing the brush hangs back on its string; while the pointer rests it keeps catching
  // up, which the engine works out from the time on each point.
  void Tick() override {
    if (!stroking || brush.smoothing <= 0) return;
    double now = ui::NowMs();
    if (now - lastPush >= 12) Push(lastX, lastY, lastP, now);
  }
  void Up(double x, double y, const PointerEvent& e) override {
    if (!stroking) return;
    Drag(x, y, e);
    stroking = false;
    char* r = nullptr;
    int ok = pc_stroke_end(engine, 1, &r);
    std::string text = TakeString(r);
    if (!ok && text != "no stroke in progress") ui::Toast(W(text), true);
    if (ok) { hasBrushAnchor = true; brushAnchorX = x; brushAnchorY = y; }
    Refresh();
  }
  void Cancel() override {
    if (!stroking) return;
    stroking = false;
    char* r = nullptr; pc_stroke_end(engine, 0, &r); TakeString(r);
    Refresh();
  }
  void Deactivate() override { Cancel(); }
  void Overlay() override {
    if (pointerX < 0 || !canvas->PointerOver()) return;
    float r = float(brush.size * .5 * canvas->Zoom());
    if (r < 3) {
      Line(pointerX - 6, pointerY, pointerX + 6, pointerY, Rgba(1, 1, 1, .9f), 1);
      Line(pointerX, pointerY - 6, pointerX, pointerY + 6, Rgba(1, 1, 1, .9f), 1);
      return;
    }
    StrokeEllipse(pointerX, pointerY, r + .8f, r + .8f, Rgba(0, 0, 0, .55f), 1.2f);
    StrokeEllipse(pointerX, pointerY, r, r, Rgba(1, 1, 1, .9f), 1);
  }
  bool HidesCursor() override { return true; }
};

// Retouching brushes the engine runs as one command over a recorded path.
struct PathStrokeTool : CanvasTool {
  std::string cmd;
  std::vector<D2D_POINT_2F> pts;
  explicit PathStrokeTool(std::string c) : cmd(std::move(c)) {}
  void Down(double x, double y, const PointerEvent&) override { pts = {{float(x), float(y)}}; }
  void Drag(double x, double y, const PointerEvent&) override {
    // Points closer than a screen pixel add nothing but jitter.
    if (pts.empty() || std::hypot(pts.back().x - x, pts.back().y - y) * canvas->Zoom() >= 1) pts.push_back({float(x), float(y)});
  }
  void Up(double x, double y, const PointerEvent&) override {
    if (pts.empty()) return;
    pts.push_back({float(x), float(y)});
    JsonObject o;
    o.Add("points", PointList(pts, true)).Num("size", brush.size);
    if(cmd=="paint.historyBrush")AddHistoryBrushSource(o);
    const std::string& doc = paramsDoc[cmd];
    if (doc.find("\"hardness\":0..100") != std::string::npos) o.Num("hardness", brush.hardness);
    else if (doc.find("\"hardness\"") != std::string::npos) o.Num("hardness", brush.hardness / 100);
    if (doc.find("\"opacity\":1..100") != std::string::npos) o.Num("opacity", std::max(1.0, brush.opacity));
    pts.clear();
    Exec(cmd, o.Text());
  }
  void Overlay() override {
    std::vector<D2D_POINT_2F> s;
    for (auto& p : pts) s.push_back(canvas->ToScreen(p.x, p.y));
    if (auto g = Polyline(s, false)) {
      float w = std::max(2.f, float(brush.size * canvas->Zoom()));
      Dc()->DrawGeometry(g.Get(), Ink(Rgba(1, .93f, .81f, .28f)), w, RoundStyle());
    }
    if (pointerX >= 0 && canvas->PointerOver()) {
      float r = float(brush.size * .5 * canvas->Zoom());
      StrokeEllipse(pointerX, pointerY, r, r, Rgba(1, 1, 1, .9f), 1);
    }
  }
  bool HidesCursor() override { return true; }
};

struct MarqueeTool : CanvasTool {
  bool ellipse;
  const char* line;  // "row", "column" or null
  double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  bool dragging = false;
  gestures::Marquee frame;
  std::string operation;
  bool downAlt = false;
  explicit MarqueeTool(bool e, const char* l = nullptr) : ellipse(e), line(l) {}
  void Down(double x, double y, const PointerEvent& e) override {
    frame.Begin(x, y); x0 = x1 = x; y0 = y1 = y; dragging = true;
    operation = ModeFor(e); downAlt = e.mods.alt;
  }
  void Drag(double x, double y, const PointerEvent& e) override {
    frame.Move(x, y, e.mods.shift && !line, e.mods.alt && !downAlt && !line, GetKeyState(VK_SPACE) < 0);
    x0 = frame.left; y0 = frame.top; x1 = frame.right; y1 = frame.bottom;
    if (!line) SizeHud(x1 - x0, y1 - y0);
  }
  void Up(double x, double y, const PointerEvent& e) override {
    Drag(x, y, e);
    dragging = false;
    if (line) {
      JsonObject o;
      o.Num("x", std::floor(x)).Num("y", std::floor(y)).Num("width", 1).Num("height", 1).Str("line", line).Str("mode", operation);
      Exec("select.rect", o.Text());
      shape.kind = SelectionShape::None;
      return;
    }
    double l = std::floor(std::min(x0, x1)), t = std::floor(std::min(y0, y1));
    double r = std::ceil(std::max(x0, x1)), b = std::ceil(std::max(y0, y1));
    if (r - l < 2 && b - t < 2) {
      Exec("select.deselect");
      return;
    }
    JsonObject o;
    o.Num("x", l).Num("y", t).Num("width", r - l).Num("height", b - t).Str("mode", operation).Bool("ellipse", ellipse).Bool("antiAlias",sel.antiAlias);
    if (sel.feather > 0) o.Num("feather", sel.feather);
    bool el = ellipse;
    bool replace = operation == "replace";
    Exec("select.rect", o.Text(), [el, l, t, r, b, replace](bool ok, const std::string&) {
      if (!ok) return;
      if (!replace) { shape.kind = SelectionShape::None; return; }
      shape.kind = el ? SelectionShape::EllipseK : SelectionShape::RectK;
      shape.x0 = l; shape.y0 = t; shape.x1 = r; shape.y1 = b;
      RememberShapeBounds();
    });
  }
  void Overlay() override {
    if (!dragging) return;
    AntsDocumentBox(x0, y0, x1, y1, ellipse);
  }
  bool SpaceRepositions() const override { return dragging; }
  void Tick() override {
    if (!dragging) return;
    PointerEvent e; e.mods.shift = GetKeyState(VK_SHIFT) < 0; e.mods.alt = GetKeyState(VK_MENU) < 0;
    Drag(frame.px, frame.py, e);
  }
  void Cancel() override { dragging = false; }
  void Deactivate() override { Cancel(); }
};

struct LassoTool : CanvasTool {
  std::string cmd;
  std::vector<D2D_POINT_2F> pts;
  explicit LassoTool(std::string c = "select.lasso") : cmd(std::move(c)) {}
  void Down(double x, double y, const PointerEvent&) override { pts = {{float(x), float(y)}}; }
  void Drag(double x, double y, const PointerEvent&) override {
    if (pts.empty() || std::hypot(pts.back().x - x, pts.back().y - y) > 1.5 / std::max(.01f, canvas->Zoom())) pts.push_back({float(x), float(y)});
  }
  void Up(double x, double y, const PointerEvent& e) override {
    Drag(x, y, e);
    if (pts.size() < 3) {
      pts.clear();
      if (cmd == "select.lasso") Exec("select.deselect");
      return;
    }
    JsonObject o;
    o.Add("points", PointList(pts, false));
    if (cmd == "select.quick") o.Num("size", brush.size).Bool("sampleAllLayers",sel.sampleAllLayers).Bool("enhanceEdge",sel.antiAlias).Str("mode", e.mods.alt ? "subtract" : "add");
    else o.Str("mode", ModeFor(e)).Bool("antiAlias",sel.antiAlias).Num("feather",sel.feather);
    bool lasso = cmd == "select.lasso" || cmd == "select.magneticLasso";
    auto drawn = pts;
    Exec(cmd, o.Text(), [lasso, drawn](bool ok, const std::string&) {
      if (!ok) return;
      if (lasso) {
        shape.kind = SelectionShape::PolyK;
        shape.pts = drawn;
        RememberShapeBounds();
      } else {
        shape.kind = SelectionShape::None;
      }
    });
    pts.clear();
  }
  void Overlay() override {
    std::vector<D2D_POINT_2F> s;
    for (auto& p : pts) s.push_back(canvas->ToScreen(p.x, p.y));
    AntsPoly(s, false);
  }
};

struct ClickTool : CanvasTool {
  std::function<void(double, double, const PointerEvent&)> click;
  HCURSOR cursor;
  ClickTool(std::function<void(double, double, const PointerEvent&)> c, HCURSOR cur) : click(std::move(c)), cursor(cur) {}
  void Down(double, double, const PointerEvent&) override {}
  void Drag(double, double, const PointerEvent&) override {}
  void Up(double x, double y, const PointerEvent& e) override { click(x, y, e); }
  HCURSOR Cursor() override { return cursor; }
};

struct PolygonLassoTool : CanvasTool {
  std::vector<D2D_POINT_2F> points;
  D2D_POINT_2F next{};
  std::string operation;
  bool finishingClick = false;
  D2D_POINT_2F Constrain(double x, double y, bool shift) const {
    if (shift && !points.empty()) {
      auto [dx, dy] = gestures::Snap45(x - points.back().x, y - points.back().y);
      return {float(points.back().x + dx), float(points.back().y + dy)};
    }
    return {float(x), float(y)};
  }
  void Commit() {
    if (points.size() < 3) return;
    auto drawn = points;
    JsonObject o; o.Add("points", PointList(points, false)).Str("mode", operation).Bool("antiAlias",sel.antiAlias);
    if (sel.feather > 0) o.Num("feather", sel.feather);
    bool replace = operation == "replace";
    points.clear();
    Exec("select.lasso", o.Text(), [drawn, replace](bool ok, const std::string&) {
      if (ok && replace) ShowSelectionOutline(drawn);
      else shape.kind = SelectionShape::None;
    });
  }
  void Down(double x, double y, const PointerEvent& e) override {
    finishingClick = false;
    if (points.empty()) operation = ModeFor(e);
    next = Constrain(x, y, e.mods.shift);
    if (points.size() >= 3) {
      auto first = canvas->ToScreen(points.front().x, points.front().y);
      if (std::hypot(first.x - e.x, first.y - e.y) < 8) { Commit(); finishingClick = true; return; }
    }
    if (points.empty() || std::hypot(next.x - points.back().x, next.y - points.back().y) > .01f) points.push_back(next);
    if (e.clicks == 2) { Commit(); finishingClick = true; }
  }
  void Drag(double x, double y, const PointerEvent& e) override { next = Constrain(x, y, e.mods.shift); }
  void Up(double, double, const PointerEvent&) override {}  // clicks keep the path open
  bool Key(UINT vk, Mods) override {
    if (points.empty()) return false;
    if (vk == VK_BACK || vk == VK_DELETE) { points.pop_back(); return true; }
    if (vk == VK_ESCAPE) { points.clear(); return true; }
    if (vk == VK_RETURN) { Commit(); return true; }
    return false;
  }
  void Overlay() override {
    if (points.empty()) return;
    std::vector<D2D_POINT_2F> screen;
    for (auto p : points) screen.push_back(canvas->ToScreen(p.x, p.y));
    if (pointerX >= 0 && canvas->PointerOver()) {
      auto p = canvas->ToDoc(pointerX, pointerY);
      next = Constrain(p.x, p.y, GetKeyState(VK_SHIFT) < 0);
      screen.push_back(canvas->ToScreen(next.x, next.y));
    }
    AntsPoly(screen, false);
    for (auto p : points) {
      auto s = canvas->ToScreen(p.x, p.y);
      FillEllipse(s.x, s.y, 2.5f, 2.5f, P.accent);
    }
  }
  void Cancel() override { points.clear(); }
  void Deactivate() override { Cancel(); }
};

struct ZoomTool : CanvasTool {
  float x0 = 0, y0 = 0, lastX = 0;
  bool dragged = false;
  void Down(double, double, const PointerEvent& e) override {
    if (e.clicks == 2) { canvas->ActualPixels(true); return; }
    x0 = lastX = e.x; y0 = e.y; dragged = false;
  }
  void Drag(double, double, const PointerEvent& e) override {
    if (std::fabs(e.x - x0) > 3) dragged = true;
    if (dragged) canvas->ZoomBy(std::exp((e.x - lastX) * .012f), x0, y0);
    lastX = e.x;
  }
  void Up(double, double, const PointerEvent& e) override {
    if (e.clicks != 2 && !dragged) canvas->ZoomBy(e.mods.alt ? 1 / 1.6f : 1.6f, x0, y0);
  }
};

struct RotateViewTool : CanvasTool {
  double start = 0;
  float degrees = 0;
  double PointerAngle(const PointerEvent& e) const {
    auto center = canvas->ToScreen(canvas->DocW() / 2.0, canvas->DocH() / 2.0);
    return std::atan2(e.y - center.y, e.x - center.x) * 57.295779513;
  }
  void Down(double, double, const PointerEvent& e) override { start = PointerAngle(e); degrees = canvas->Rotation(); }
  void Drag(double, double, const PointerEvent& e) override {
    double d = PointerAngle(e) - start;
    if (d > 180) d -= 360;
    if (d < -180) d += 360;
    double a = degrees + d;
    if (e.mods.shift) a = std::round(a / 15) * 15;
    canvas->RotateTo(float(a), false);
  }
  void Up(double x, double y, const PointerEvent& e) override { Drag(x, y, e); }
  bool Key(UINT vk, Mods) override {
    if (vk == VK_ESCAPE) { canvas->RotateTo(0); return true; }
    return false;
  }
  bool Options(Container& dock, std::vector<Widget*>& out) override {
    auto* s = dock.Add<Slider>(L"Rotation", -180, 180, canvas->Rotation(), 0, L"°");
    s->compact = true; s->onChange = [](double a) { canvas->RotateTo(float(a)); };
    out.push_back(s);
    out.push_back(dock.Add<TextButton>(L"Reset View", TextButton::Secondary, [] { canvas->RotateTo(0); }));
    return true;
  }
};

struct DragTool : CanvasTool {
  std::function<void(double, double, double, double, const PointerEvent&)> done;
  bool line;  // draws a line rather than a box while dragging
  std::function<void(const Rect&)> outline;  // what the box previews (null: the box itself)
  double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  bool dragging = false;
  DragTool(std::function<void(double, double, double, double, const PointerEvent&)> d, bool l) : done(std::move(d)), line(l) {}
  void Down(double x, double y, const PointerEvent&) override { x0 = x1 = x; y0 = y1 = y; dragging = true; }
  void Drag(double x, double y, const PointerEvent&) override { x1 = x; y1 = y; }
  void Up(double x, double y, const PointerEvent& e) override {
    x1 = x;
    y1 = y;
    dragging = false;
    done(x0, y0, x1, y1, e);
  }
  void Overlay() override {
    if (!dragging) return;
    auto a = canvas->ToScreen(x0, y0), b = canvas->ToScreen(x1, y1);
    if (line) {
      Line(a.x, a.y, b.x, b.y, Rgba(0, 0, 0, .5f), 3);
      Line(a.x, a.y, b.x, b.y, Rgba(1, .97f, .92f, .95f), 1.4f);
      FillEllipse(a.x, a.y, 3.5f, 3.5f, P.text);
      FillEllipse(b.x, b.y, 3.5f, 3.5f, P.accent);
    } else {
      Rect r = R(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y));
      if (outline) {
        outline(r);
        return;
      }
      FillRect(r, Rgba(1, .93f, .81f, .08f));
      StrokeRound(r, 0, Rgba(1, .93f, .81f, .9f), 1);
    }
  }
};

bool moveAutoSelect = true, moveSelectGroup = false, moveControls = false;
struct MoveTool : CanvasTool {
  double x0 = 0, y0 = 0, dx = 0, dy = 0;
  bool dragging = false, copied = false, copyWanted = false, ready = true, copyPending = false, cancelled = false;
  int copySteps = 0;
  std::shared_ptr<LivePreview> live;
  std::string sent;
  std::string Params() const { JsonObject o; o.Num("dx", dx).Num("dy", dy); return o.Text(); }
  void Apply() {
    if (cancelled || !ready || copyPending || (dx == 0 && dy == 0 && !live)) return;
    if (!live && copyWanted && !copied) {
      copyPending = true;
      uint64_t before = pc_history_len(engine);
      Exec("layer.duplicate", "{\"inPlace\":true}", [this, before](bool ok, const std::string&) {
        copyPending = false;
        if (!ok) { ready = false; return; }
        copied = true;
        uint64_t after = pc_history_len(engine);
        copySteps = int(std::min<uint64_t>(after > before ? after - before : 0, 10000));
        if (cancelled) { if (copySteps) Undo(copySteps); copied = false; return; }
        Apply();
      });
      return;
    }
    if (!live) live = std::make_shared<LivePreview>("layer.translate");
    std::string p = Params();
    if (!dragging) {
      live->Finish(true, p); live.reset(); dx = dy = 0;
    } else if (!live->Busy() && sent != p) {
      sent = p; live->Show(p);
    }
  }
  void Down(double x, double y, const PointerEvent& e) override {
    x0 = x; y0 = y; dx = dy = 0; dragging = true; copied = false; copyWanted = e.mods.alt; copyPending = cancelled = false; copySteps = 0;
    sent.clear();
    live.reset(); ready = true;
    if (moveControls && !e.mods.alt && !e.mods.ctrl && !docInfo["hasSelection"].Truthy()) {
      if (const Json* layer = ActiveLayer()) {
        const Json& b = (*layer)["bounds"];
        if (b.size() == 4) {
          for (auto [u, v] : {std::pair{b[0].Num(), b[1].Num()}, std::pair{b[0].Num() + b[2].Num(), b[1].Num()},
                              std::pair{b[0].Num() + b[2].Num(), b[1].Num() + b[3].Num()}, std::pair{b[0].Num(), b[1].Num() + b[3].Num()}}) {
            auto s = canvas->ToScreen(u, v);
            if (std::hypot(s.x - e.x, s.y - e.y) <= 8) {
              dragging = false; StartTransform();
              if (CanvasTool* m = Mode()) m->Down(x, y, e);
              return;
            }
          }
        }
      }
    }
    if (moveAutoSelect != e.mods.ctrl) {
      ready = false;
      JsonObject o; o.Num("x", x).Num("y", y).Str("target", moveSelectGroup ? "group" : "layer").Str("mode", e.mods.shift ? "toggle" : "replace");
      Exec("layer.pickAt", o.Text(), [this](bool ok, const std::string& r) {
        ready = ok && !Json::Parse(r)["layer"].IsNull();
        if (ready) BeginMoveSnap();
        Apply();
      });
    } else BeginMoveSnap();
  }
  void Drag(double x, double y, const PointerEvent& e) override {
    if (!dragging) return;
    double tx = x - x0, ty = y - y0;
    if (e.mods.shift) { auto snapped = gestures::Snap45(tx, ty); tx = snapped.first; ty = snapped.second; }
    auto snapped = SnapMoveOffset(tx, ty, e.mods); tx = snapped.x; ty = snapped.y;
    dx = std::round(tx); dy = std::round(ty);
    if (!live && e.mods.alt) copyWanted = true;
    Apply();
  }
  void Up(double x, double y, const PointerEvent& e) override {
    Drag(x, y, e);
    dragging = false;
    Apply();
    EndViewSnap();
  }
  void Tick() override { if (dragging && live) Apply(); }
  void Cancel() override {
    cancelled = true;
    dragging = false;
    if (live) live->Finish(false, "{}");
    live.reset();
    if (copied) { copied = false; if (copySteps) Undo(copySteps); }
    dx = dy = 0;
    EndViewSnap();
  }
  void Deactivate() override { if (dragging) Cancel(); }
  bool Key(UINT vk, Mods) override {
    if (vk == VK_ESCAPE && dragging) { Cancel(); return true; }
    return false;
  }
  void Overlay() override {
    if (!dragging && !moveControls) return;
    // The selected layers' bounds follow the hand until the move commits.
    VisitLayers(docInfo["layers"], [](const Json& l) {
      if (!l["selected"].Truthy()) return;
      const Json& b = l["bounds"];
      if (b.size() < 4) return;
      std::vector<D2D_POINT_2F> points;
      for (auto [x, y] : {std::pair{b[0].Num(), b[1].Num()}, std::pair{b[0].Num() + b[2].Num(), b[1].Num()},
                          std::pair{b[0].Num() + b[2].Num(), b[1].Num() + b[3].Num()}, std::pair{b[0].Num(), b[1].Num() + b[3].Num()}})
        points.push_back(canvas->ToScreen(x, y));
      if (auto g = Polyline(points, true)) Dc()->DrawGeometry(g.Get(), Ink(P.accent), 1.2f);
      if (moveControls) for (auto p : points) FillRound(RWH(p.x - 3, p.y - 3, 6, 6), 1, P.accent);
    });
  }
  bool Options(Container& dock, std::vector<Widget*>& out) override {
    auto* a = dock.Add<Toggle>(L"Auto-Select", moveAutoSelect);
    a->onChange = [](bool v) { moveAutoSelect = v; }; out.push_back(a);
    auto* target = dock.Add<SelectList>(std::vector<std::wstring>{L"Layer", L"Group"}, moveSelectGroup ? 1 : 0);
    target->onChange = [](int v) { moveSelectGroup = v == 1; }; out.push_back(target);
    auto* controls = dock.Add<Toggle>(L"Show Transform Controls", moveControls);
    controls->onChange = [](bool v) { moveControls = v; }; out.push_back(controls);
    out.push_back(dock.Add<TextButton>(L"Transform", TextButton::Secondary, [] { StartTransform(); }));
    MoveAlignmentOptions(dock,out);
    return true;
  }
  HCURSOR Cursor() override { return LoadCursor(nullptr, IDC_SIZEALL); }
};

// ------------------------------------------------------------------------ tool table --
struct ToolDef {
  std::string key;
  std::wstring name;
  const wchar_t* icon = nullptr;
  bool stroked = false;
  wchar_t letter = 0;
  std::shared_ptr<CanvasTool> impl;  // null: not in this shell yet (or the hand)
  bool available = true;
  bool hand = false;
};
std::vector<std::vector<std::vector<ToolDef>>> sections;  // section -> slot -> tools
std::vector<int> slotChoice;                             // flat slot index -> current tool
ToolDef* current = nullptr;
ToolDef* springTool = nullptr;
UINT springKey = 0;
double springAt = 0;
bool springRestore = false;
gestures::PercentKeys percentKeys;

void TypeAt(double x, double y);
void NoteClick(double x, double y, const PointerEvent& e);
void PickColour(double x, double y, bool background) {
  SampleColorAt(x,y,background);
}

ToolDef T(std::string key, std::wstring name, const wchar_t* icon, wchar_t letter, CanvasTool* impl, bool stroked = false) {
  ToolDef d;
  d.key = std::move(key);
  d.name = std::move(name);
  d.icon = icon;
  d.letter = letter;
  d.impl.reset(impl);
  d.stroked = stroked;
  d.available = impl != nullptr;
  return d;
}

std::string Pt(double x, double y) { return "[" + JsonNum(x) + "," + JsonNum(y) + "]"; }

void BuildTools() {
  HCURSOR cross = LoadCursor(nullptr, IDC_CROSS);
  // The Eyedropper's own cursor: its tip (lower left of the Phosphor glyph) is the sampled pixel.
  static HCURSOR dropperCursor = nullptr;
  if (!dropperCursor) dropperCursor = IconCursor(PhEyedropper, GetSystemMetricsForDpi(SM_CXCURSOR, GetDpiForWindow(hwnd)), 34, 222);
  if (!dropperCursor) dropperCursor = cross;
  auto cropTool = NewCropTool();
  auto wand = new ClickTool([](double x, double y, const PointerEvent& e) {
    JsonObject o;
    o.Num("x", std::floor(x)).Num("y", std::floor(y)).Num("tolerance", sel.tolerance).Bool("contiguous", sel.contiguous).Bool("antiAlias",sel.antiAlias).Bool("sampleAllLayers",sel.sampleAllLayers).Num("sampleSize",sel.sampleSize).Str("mode", ModeFor(e));
    Exec("select.magicWand", o.Text());
    shape.kind = SelectionShape::None;
  }, cross);
  auto sampler = new ClickTool([](double x, double y, const PointerEvent&) {
    JsonObject o;
    o.Num("x", x).Num("y", y);
    Exec("view.colorSamplers.add", o.Text());
  }, cross);
  auto redEye = new ClickTool([](double x, double y, const PointerEvent&) {
    JsonObject o;
    o.Num("x", std::floor(x)).Num("y", std::floor(y));
    Exec("paint.redEye", o.Text());
  }, cross);
  auto magicEraser = new ClickTool([](double x, double y, const PointerEvent&) {
    JsonObject o;
    o.Num("x", std::floor(x)).Num("y", std::floor(y)).Num("tolerance", sel.tolerance).Bool("contiguous", sel.contiguous);
    Exec("paint.magicEraser", o.Text());
  }, cross);
  auto bucket = new ClickTool([](double x, double y, const PointerEvent&) {
    JsonObject o;
    o.Num("x", std::floor(x)).Num("y", std::floor(y)).Num("tolerance", sel.tolerance).Bool("contiguous", sel.contiguous);
    Exec("paint.bucket", o.Text());
  }, cross);
  auto gradient = NewGradientTool();
  // Shapes: a frame dragged out (Shift: square), filled with the foreground colour. The Rounded
  // Rectangle takes its corner radius from the options dock; the Triangle fills its frame point up.
  auto shapeTool = [](std::string kind) { return NewShapeTool(kind); };
  auto zoomTool = new ZoomTool();
  auto typeTool = NewTypeTool(false);
  auto vtypeTool = NewTypeTool(true);
  auto noteTool = new ClickTool(NoteClick, cross);
  // Count: a click adds the next number; Alt-click takes back the marker under it.
  auto countTool = new ClickTool([](double x, double y, const PointerEvent& e) {
    JsonObject o;
    o.Num("x", std::round(x)).Num("y", std::round(y));
    if (e.mods.alt) o.Num("radius", std::max(6.0, 8 / std::max(.01, double(canvas->Zoom()))));
    Exec(e.mods.alt ? "count.remove" : "count.add", o.Text(), nullptr, !e.mods.alt);
  }, cross);
  auto subjectTool = new ClickTool([](double, double, const PointerEvent& e) {
    JsonObject o;
    o.Str("mode", ModeFor(e));
    Exec("select.subject", o.Text());
    shape.kind = SelectionShape::None;
  }, cross);
  auto rulerTool = new DragTool([](double x0, double y0, double x1, double y1, const PointerEvent&) {
    if (std::hypot(x1 - x0, y1 - y0) < 1) return;
    JsonObject o;
    o.Add("start", Pt(x0, y0)).Add("end", Pt(x1, y1));
    Exec("image.analysis.rulerTool", o.Text(), [x0, y0, x1, y1](bool ok, const std::string&) {
      if (!ok) return;
      wchar_t buf[96];
      swprintf(buf, 96, i18n::Tr(L"Ruler: %.1f px at %.1f°").c_str(), std::hypot(x1 - x0, y1 - y0), std::atan2(-(y1 - y0), x1 - x0) * 57.29578);
      ui::Toast(buf);
    });
  }, true);
  auto artboardTool = new DragTool([](double x0, double y0, double x1, double y1, const PointerEvent&) {
    double l = std::min(x0, x1), t = std::min(y0, y1), w = std::fabs(x1 - x0), h = std::fabs(y1 - y0);
    if (w < 4 || h < 4) return;
    JsonObject o;
    o.Add("rect", "[" + JsonNum(std::round(l)) + "," + JsonNum(std::round(t)) + "," + JsonNum(std::round(w)) + "," + JsonNum(std::round(h)) + "]");
    Exec("layer.new.artboard", o.Text());
  }, false);

  // The toolbar of crates/ui-egui/src/panels.rs (TOOL_SECTIONS), slot by slot.
  sections.clear();
  using S = std::vector<ToolDef>;
  auto slot = [](std::initializer_list<ToolDef> l) {
    S v;
    for (auto& t : l) v.push_back(t);
    return v;
  };
  sections.push_back({slot({T("move", L"Move", PhArrowsOutCardinal, 'V', new MoveTool()), T("artboard", L"Artboard", CuArtboard, 'V', artboardTool, true)})});
  sections.push_back({
      slot({T("rectMarquee", L"Rectangular Marquee", PhSelection, 'M', new MarqueeTool(false)),
            T("ellipseMarquee", L"Elliptical Marquee", CuEllipseMarquee, 'M', new MarqueeTool(true), true),
            T("rowMarquee", L"Single Row Marquee", CuRowMarquee, 'M', new MarqueeTool(false, "row"), true),
            T("columnMarquee", L"Single Column Marquee", CuColumnMarquee, 'M', new MarqueeTool(false, "column"), true)}),
      slot({T("lasso", L"Lasso", PhLasso, 'L', new LassoTool()), T("polygonLasso", L"Polygonal Lasso", PhPolygon, 'L', new PolygonLassoTool()),
            T("magneticLasso", L"Magnetic Lasso", PhLasso, 'L', new LassoTool("select.magneticLasso"))}),
      slot({T("quickSelection", L"Quick Selection", PhSelectionPlus, 'W', new LassoTool("select.quick")), T("magicWand", L"Magic Wand", PhMagicWand, 'W', wand),
            T("objectSelection", L"Object Selection", PhSelectionAll, 'W', subjectTool)}),
      slot({T("crop", L"Crop", PhCrop, 'C', cropTool), T("perspectiveCrop", L"Perspective Crop", PhFrameCorners, 'C', NewPerspectiveCropTool())}),
      slot({T("eyedropper", L"Eyedropper", PhEyedropper, 'I', NewEyedropperTool(dropperCursor)),
            T("colorSampler", L"Color Sampler", PhCrosshair, 'I', sampler), T("ruler", L"Ruler", PhRuler, 'I', rulerTool), T("note", L"Note", PhNote, 'I', noteTool),
            T("count", L"Count", PhHash, 'I', countTool)}),
  });
  sections.push_back({
      slot({T("spotHealing", L"Spot Healing Brush", PhBandaids, 'J', new PathStrokeTool("paint.spotHealing")),
            T("remove", L"Remove", PhSparkle, 'J', new PathStrokeTool("paint.remove")),
            T("healingBrush", L"Healing Brush", PhFirstAid, 'J', NewSourceTool("paint.healingBrush")), T("patch",L"Patch",PhBandaids,'J',NewPatchTool(false)), T("contentAwareMove",L"Content-Aware Move",PhArrowsOutCardinal,'J',NewPatchTool(true)), T("redEye", L"Red Eye", PhEye, 'J', redEye)}),
      slot({T("brush", L"Brush", PhPaintBrush, 'B', new BrushTool("paint.stroke", false)), T("pencil", L"Pencil", PhPencilSimple, 'B', new BrushTool("paint.pencil", false)),
            T("mixerBrush", L"Mixer Brush", PhPaintRoller, 'B', new PathStrokeTool("paint.mixerBrush")),
            T("colorReplacement", L"Color Replacement", PhDropHalf, 'B', new PathStrokeTool("paint.colorReplacement"))}),
      slot({T("cloneStamp", L"Clone Stamp", PhStamp, 'S', NewSourceTool("paint.cloneStamp")),
            T("patternStamp", L"Pattern Stamp", PhStamp, 'S', new PathStrokeTool("paint.patternStamp"))}),
      slot({T("historyBrush", L"History Brush", PhClockCounterClockwise, 'Y', new PathStrokeTool("paint.historyBrush"))}),
      slot({T("eraser", L"Eraser", PhEraser, 'E', new BrushTool("paint.stroke", true)),
            T("backgroundEraser", L"Background Eraser", PhEraser, 'E', new PathStrokeTool("paint.backgroundEraser")),
            T("magicEraser", L"Magic Eraser", PhMagicWand, 'E', magicEraser)}),
      slot({T("gradient", L"Gradient", PhGradient, 'G', gradient), T("paintBucket", L"Paint Bucket", PhPaintBucket, 'G', bucket)}),
      slot({T("blur", L"Blur", PhDrop, 0, new PathStrokeTool("paint.blur")), T("sharpen", L"Sharpen", PhTriangle, 0, new PathStrokeTool("paint.sharpen")),
            T("smudge", L"Smudge", CuSmudge, 0, new PathStrokeTool("paint.smudge"), true)}),
      slot({T("dodge", L"Dodge", CuDodge, 'O', new PathStrokeTool("paint.dodge"), true), T("burn", L"Burn", CuBurn, 'O', new PathStrokeTool("paint.burn"), true),
            T("sponge", L"Sponge", PhSprayBottle, 'O', new PathStrokeTool("paint.sponge"))}),
  });
  sections.push_back({
      slot({T("pen", L"Pen", PhPenNib, 'P', NewPenTool(0)), T("freeformPen", L"Freeform Pen", PhBezierCurve, 'P', NewPenTool(1)),
            T("curvaturePen", L"Curvature Pen", PhPath, 'P', NewPenTool(2))}),
      slot({T("type", L"Horizontal Type", PhTextT, 'T', typeTool), T("verticalType", L"Vertical Type", PhTextAa, 'T', vtypeTool)}),
      slot({T("pathSelection", L"Path Selection", PhCursor, 'A', NewPathSelectTool(false)),
            T("directSelection", L"Direct Selection", PhCursorClick, 'A', NewPathSelectTool(true))}),
      slot({T("rectangle", L"Rectangle", PhRectangle, 'U', shapeTool("rect")),
            T("roundedRectangle", L"Rounded Rectangle", CuRoundRect, 'U', shapeTool("roundedRect"), true),
            T("ellipse", L"Ellipse", PhCircle, 'U', shapeTool("ellipse")), T("triangle", L"Triangle", CuTriangle, 'U', shapeTool("triangle"), true),
            T("polygon", L"Polygon", PhPolygon, 'U', shapeTool("polygon")), T("star", L"Star", PhStar, 'U', shapeTool("star")),
            T("line", L"Line", PhLineSegment, 'U', shapeTool("line"))}),
  });
  ToolDef hand = T("hand", L"Hand", PhHand, 'H', nullptr);
  hand.available = true;
  hand.hand = true;
  sections.push_back({slot({hand, T("rotateView", L"Rotate View", PhArrowClockwise, 'R', new RotateViewTool())}), slot({T("zoom", L"Zoom", PhMagnifyingGlass, 'Z', zoomTool)})});
  size_t slots = 0;
  for (auto& s : sections) slots += s.size();
  slotChoice.assign(slots, 0);
}

// ============================================================================ docks ==
// A dock described by functions: its natural size, how it lays its children out, what it paints
// under them. Every surface below is one of these.
class PanelDock : public Dock {
 public:
  std::function<D2D1_SIZE_F()> natural;
  std::function<void(PanelDock&)> arrange;
  std::function<void(PanelDock&)> body;
  D2D1_SIZE_F Natural() override { return natural ? natural() : D2D1_SIZE_F{100, 44}; }
  void Arrange() override {
    if (arrange) arrange(*this);
  }
  void PaintBody() override {
    if (body) body(*this);
  }
};

PanelDock *brandDock = nullptr, *menuDock = nullptr, *optionsDock = nullptr, *systemDock = nullptr, *toolsDock = nullptr, *propsDock = nullptr,
          *layersDock = nullptr, *statusDock = nullptr, *zoomDock = nullptr;
std::vector<TextButton*> menuTriggers;
std::unique_ptr<Menu> openMenu;
int openMenuIndex = -1;
std::vector<std::string> menuNames;
std::vector<IconButton*> toolButtons;
std::unique_ptr<Menu> flyout;
std::unique_ptr<ParamDialog> dialog;

// ------------------------------------------------------------------------------ menus --
std::wstring ShortcutText(const std::string& s) {
  // "Cmd+Shift+N" -> "Ctrl+Shift+N"; "Cmd++" keeps its plus.
  std::vector<std::string> parts;
  std::string cur;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '+' && !cur.empty()) { parts.push_back(cur); cur.clear(); }
    else cur += s[i];
  }
  if (!cur.empty()) parts.push_back(cur);
  std::string out;
  for (auto& p : parts) {
    std::string q = p == "Cmd" ? "Ctrl" : p == "Opt" ? "Alt" : p;
    if (!out.empty()) out += "+";
    out += q;
  }
  return W(out);
}

// Commands this shell answers itself (window and view state, file dialogs).
bool ShellCommand(const std::string& id) {
  static const std::set<std::string> ids = {"file.open", "file.save", "file.saveAs", "file.export.quickExportAsPng", "file.export.exportAs",
                                            "file.exit", "file.close", "file.saveACopy", "view.zoomIn", "view.zoomOut", "view.fitOnScreen",
                                            "view.100", "view.actualPixels", "view.printSize", "edit.undo", "edit.redo", "edit.stepBackward",
                                            "edit.stepForward", "window.layers", "window.history", "window.properties", "window.adjustments",
                                            "window.tools", "window.options", "window.brushes", "window.brushSettings", "window.layerFilters", "window.cloneSource", "window.panel.brush", "window.panel.brushSettings", "window.panel.brushes", "window.panel.cloneSource", "window.character", "window.paragraph", "window.panel.character", "window.panel.paragraph", "edit.freeTransform"};
  return ids.count(id) > 0 || IsGuideCommand(id) || IsWorkspacePanelCommand(id);
}

std::vector<MenuItem> BuildMenu(const std::string& top) {
  std::vector<const CatalogRow*> rows;
  std::string ids;
  for (auto& r : catalog)
    if (!r.path.empty() && r.path[0] == top) {
      rows.push_back(&r);
      if (r.id != "---") ids += r.id + "\n";
    }
  std::string enabled = TakeString(pc_enabled_many(engine, ids.c_str()));
  size_t e = 0;
  MenuItem root;
  for (auto* r : rows) {
    // Descend into (or open) the submenu this row belongs to: consecutive rows share it.
    MenuItem* parent = &root;
    for (size_t d = 1; d < r->path.size(); d++) {
      std::wstring name = W(r->path[d]);
      if (parent->submenu.empty() || parent->submenu.back().separator || parent->submenu.back().label != name || parent->submenu.back().action) {
        MenuItem sub;
        sub.label = name;
        parent->submenu.push_back(std::move(sub));
      }
      parent = &parent->submenu.back();
    }
    if (r->id == "---") {
      parent->submenu.push_back(MenuItem::Separator());
      continue;
    }
    MenuItem it;
    it.label = i18n::TrId(r->id, W(r->label));
    it.shortcut = r->shortcut.empty() ? L"" : ShortcutText(r->shortcut);
    bool on = e < enabled.size() && enabled[e] == '1';
    e++;
    it.enabled = (r->live && on) || ShellCommand(r->id);
    if (r->id == "edit.paste" || r->id == "file.newFromClipboard" || r->id == "file.close" || r->id == "file.save" || r->id == "file.saveAs" || r->id == "file.saveACopy") it.enabled = ShellEnabled(r->id);
    std::string id = r->id;
    std::wstring label = it.label;
    it.action = [id, label] { RunCommand(id, label); };
    if (IsGuideCommand(id)) it.checked = GuideCommandChecked(id);
    if (IsWorkspacePanelCommand(id)) it.checked = WorkspacePanelChecked(id);
    parent->submenu.push_back(std::move(it));
  }
  if (top == "File") {
    MenuItem recent; recent.label = L"Recent documents"; recent.submenu = RecentMenu();
    root.submenu.insert(root.submenu.begin() + std::min<size_t>(2, root.submenu.size()), std::move(recent));
  }
  // Drop doubled and edge separators; a submenu is enabled when anything in it is.
  std::function<void(std::vector<MenuItem>&)> tidy = [&](std::vector<MenuItem>& v) {
    std::vector<MenuItem> out;
    for (auto& it : v) {
      if (it.separator && (out.empty() || out.back().separator)) continue;
      if (!it.action && !it.separator) {
        tidy(it.submenu);
        if (it.submenu.empty()) continue;
        it.enabled = std::any_of(it.submenu.begin(), it.submenu.end(), [](const MenuItem& m) { return m.enabled && !m.separator; });
      }
      out.push_back(std::move(it));
    }
    while (!out.empty() && out.back().separator) out.pop_back();
    v = std::move(out);
  };
  tidy(root.submenu);
  return root.submenu;
}

MenuItem CommandItem(const std::string& id, std::wstring label, const wchar_t* icon) {
  MenuItem it;
  it.label = label.empty() ? CommandLabel(id) : std::move(label);
  if (it.label.empty()) it.label = W(id);
  it.label = i18n::TrId(id, it.label);  // by command id first, as the menus do
  it.icon = icon;
  for (auto& r : catalog)
    if (r.id == id) {
      if (!r.shortcut.empty()) it.shortcut = ShortcutText(r.shortcut);
      break;
    }
  it.enabled = ShellCommand(id) || pc_enabled(engine, id.c_str()) == 1;
  if (id == "edit.paste" || id == "file.newFromClipboard" || id == "file.close") it.enabled = ShellEnabled(id);
  std::wstring l = it.label;
  it.action = [id, l] { RunCommand(id, l); };
  return it;
}
MenuItem ActionItem(std::wstring label, std::function<void()> action, const wchar_t* icon, bool enabled) {
  MenuItem it;
  it.label = std::move(label);
  it.icon = icon;
  it.enabled = enabled;
  it.action = std::move(action);
  return it;
}

// ======================================================================= brush sizing ==
// Alt+wheel steps the diameter; Alt+right-drag sets it from the hand (left/right) and the
// hardness (up/down), as Photoshop does. A small readout follows the pointer meanwhile.
extern std::vector<Widget*> optionWidgets;  // the options dock's tool controls (below)
std::wstring sizeHud;
double sizeHudUntil = 0;
void SizeHud(double width, double height) {
  wchar_t text[96];
  swprintf(text, 96, i18n::Tr(L"W %.0f px  ×  H %.0f px").c_str(), width, height);
  sizeHud = text;
  sizeHudUntil = ui::NowMs() + 180;
}
void AntsDocumentBox(double x0, double y0, double x1, double y1, bool ellipse) {
  std::vector<D2D_POINT_2F> screen;
  if (ellipse) {
    double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    for (int i = 0; i < 64; i++) {
      double a = i * .098174770425;
      screen.push_back(canvas->ToScreen(cx + (x1 - x0) / 2 * std::cos(a), cy + (y1 - y0) / 2 * std::sin(a)));
    }
  } else {
    for (auto [x, y] : {std::pair{x0, y0}, std::pair{x1, y0}, std::pair{x1, y1}, std::pair{x0, y1}}) screen.push_back(canvas->ToScreen(x, y));
  }
  AntsPoly(screen, true);
}
double* SizeTarget(bool& shared) {
  shared = false;
  CanvasTool* t = Mode() ? Mode() : canvas->tool;
  if (!t) return nullptr;
  if (double* own = t->BrushSize()) return own;
  if (!t->HidesCursor() && !(current && current->key == "quickSelection")) return nullptr;
  shared = true;
  return &brush.size;
}
void BrushResized(bool hardness) {
  bool shared = false;
  double* size = SizeTarget(shared);
  if (!size) return;
  if (shared) {
    for (Widget* w : optionWidgets)
      if (auto* s = dynamic_cast<Slider*>(w)) {
        if (s->label == L"Size") s->Set(brush.size);
        if (s->label == L"Hardness") s->Set(brush.hardness);
      }
  } else if (CanvasTool* t = Mode() ? Mode() : canvas->tool) {
    t->BrushSized();
  }
  wchar_t buf[96];
  if (hardness && shared) swprintf(buf, 96, i18n::Tr(L"%.0f px  ·  %.0f%%").c_str(), *size, brush.hardness);
  else swprintf(buf, 96, i18n::Tr(L"%.0f px").c_str(), *size);
  sizeHud = buf;
  sizeHudUntil = ui::NowMs() + 900;
  animating = true;
  ui::RequestFrame();
}
bool BrushWheel(float notches) {
  bool shared = false;
  double* size = SizeTarget(shared);
  if (!size || notches == 0) return false;
  *size = std::round(std::clamp(*size * std::pow(1.12, double(notches)), 1.0, shared ? 2500.0 : 15000.0));
  BrushResized(false);
  return true;
}
double dragSize0 = 0, dragHard0 = 0;
bool BrushDrag(float dx, float dy, int phase) {
  bool shared = false;
  double* size = SizeTarget(shared);
  if (!size) return false;
  if (phase == 0) {
    dragSize0 = *size;
    dragHard0 = brush.hardness;
    return true;
  }
  // The circle's edge follows the hand: dx screen pixels of radius each way.
  double z = std::max(.01, double(canvas->Zoom()));
  *size = std::round(std::clamp(dragSize0 + 2 * dx / z, 1.0, shared ? 2500.0 : 15000.0));
  if (shared) brush.hardness = std::round(std::clamp(dragHard0 - dy * .5, 0.0, 100.0));
  BrushResized(shared);
  if (phase == 2) sizeHudUntil = ui::NowMs() + 600;
  return true;
}

// ========================================================================= language ==
// The brand block's menu: the interface language, applied at once and remembered.
std::unique_ptr<Menu> languageMenu;
void ApplyLanguage(const std::string& code) {
  i18n::Load(code);
  RebuildOptions();
  Refresh();
  Layout(false);
  animating = true;
  ui::RequestFrame();
}
void OpenLanguageMenu() {
  std::vector<MenuItem> rows;
  for (auto& l : i18n::Languages()) {
    std::string code = l.code;
    MenuItem it = ActionItem(l.name, [code] {
      i18n::Save(code);
      ApplyLanguage(code);
    });
    it.checked = i18n::Code() == code;
    rows.push_back(it);
  }
  languageMenu = std::make_unique<Menu>(std::move(rows));
  languageMenu->minWidth = 180;
  languageMenu->Open(brandDock->rect, Side::Below);
}

// The canvas's right click: the tool's own menu, or the editing basics Photoshop offers there.
std::unique_ptr<Menu> contextMenu;
bool IsBrushTool();
void CanvasContext(double x, double y, const PointerEvent& e) {
  FinishNudge();
  if (!Mode() && IsBrushTool() && !e.mods.ctrl && !e.mods.alt) { OpenBrushPalette(e); return; }
  if (ui::AnyPopoverOpen()) ui::ClosePopovers();
  if (e.mods.ctrl || (current && current->key == "move" && !Mode())) {
    JsonObject o; o.Num("x", x).Num("y", y).Bool("list", true);
    Exec("layer.pickAt", o.Text(), [e](bool ok, const std::string& result) {
      if (!ok) return;
      Json hits = Json::Parse(result);
      std::vector<MenuItem> rows;
      for (size_t i = 0; i < hits["layers"].size(); i++) {
        const Json& l = hits["layers"][i];
        uint64_t id = uint64_t(l["layer"].Int());
        rows.push_back(ActionItem(W(l["name"].Str()), [id] {
          JsonObject p; p.Num("layer", double(id)).Str("mode", "replace"); Exec("layer.select", p.Text());
        }, PhImage));
      }
      if (rows.empty()) rows.push_back(ActionItem(L"No layers here", nullptr, nullptr, false));
      contextMenu = std::make_unique<Menu>(std::move(rows));
      contextMenu->minWidth = 220;
      contextMenu->Open(R(e.x + 2, e.y - 8, e.x + 2, e.y - 8), Side::Below);
    });
    return;
  }
  std::vector<MenuItem> rows;
  if (CanvasTool* m = Mode()) m->Context(x, y, e, rows);
  else if (canvas->tool) canvas->tool->Context(x, y, e, rows);
  if (rows.empty()) {
    if (docInfo["hasSelection"].Truthy()) {
      rows.push_back(CommandItem("select.deselect"));
      rows.push_back(CommandItem("select.inverse", L"Select Inverse"));
      rows.push_back(CommandItem("select.modify.feather", L"Feather…"));
      rows.push_back(MenuItem::Separator());
      rows.push_back(CommandItem("layer.new.layerViaCopy"));
      rows.push_back(CommandItem("layer.new.layerViaCut"));
    } else {
      rows.push_back(CommandItem("select.all", L"Select All"));
      rows.push_back(CommandItem("select.reselect"));
    }
    rows.push_back(MenuItem::Separator());
    rows.push_back(CommandItem("edit.freeTransform", L"Free Transform", PhBoundingBox));
    rows.push_back(CommandItem("edit.fill", L"Fill…"));
    rows.push_back(CommandItem("edit.stroke", L"Stroke…"));
    rows.push_back(CommandItem("select.subject", L"Select Subject"));
    rows.push_back(CommandItem("select.colorRange", L"Color Range…"));
    rows.push_back(CommandItem("select.loadSelection", L"Load Selection…"));
    if (docInfo["hasSelection"].Truthy()) {
      rows.push_back(CommandItem("select.refineEdge", L"Select and Mask…"));
      rows.push_back(CommandItem("select.saveSelection", L"Save Selection…"));
      rows.push_back(CommandItem("select.toWorkPath", L"Make Work Path…"));
      rows.push_back(CommandItem("select.transformSelection", L"Transform Selection"));
      rows.push_back(CommandItem("edit.contentAwareFill", L"Content-Aware Fill…"));
      rows.push_back(CommandItem("filter.lastFilter", L"Last Filter"));
      rows.push_back(CommandItem("edit.fade", L"Fade…"));
    }
    rows.push_back(MenuItem::Separator());
    rows.push_back(CommandItem("edit.undo", L"Undo"));
    rows.push_back(CommandItem("edit.redo", L"Redo"));
  }
  // Separators only between rows.
  while (!rows.empty() && rows.back().separator) rows.pop_back();
  if (rows.empty()) return;
  contextMenu = std::make_unique<Menu>(std::move(rows));
  contextMenu->minWidth = 220;
  contextMenu->Open(R(e.x + 2, e.y - 8, e.x + 2, e.y - 8), Side::Below);
}

void OpenTopMenu(int i) {
  if (i < 0 || size_t(i) >= menuNames.size()) return;
  if (openMenu && openMenu->open && openMenuIndex == i) return;
  if (openMenu) openMenu->CloseTree();
  for (auto* t : menuTriggers) t->active = false;
  openMenu = std::make_unique<Menu>(BuildMenu(menuNames[size_t(i)]));
  openMenu->minWidth = 230;
  openMenuIndex = i;
  menuTriggers[size_t(i)]->active = true;
  openMenu->onClosed = [i] {
    if (size_t(i) < menuTriggers.size()) menuTriggers[size_t(i)]->active = false;
  };
  openMenu->Open(menuTriggers[size_t(i)]->rect, Side::Below);
}

// ------------------------------------------------------------------------- dialogs --
void OpenDialog(const std::string& id, const std::wstring& label, const Rect& from) {
  if (OpenEditor(id, label, from)) return;
  auto fields = ParseParams(paramsDoc[id]);
  // Sizes start at the document's own.
  for (auto& f : fields) {
    if (f.key == "width" && docInfo["width"].Num() > 0) f.def = docInfo["width"].Num();
    if (f.key == "height" && docInfo["height"].Num() > 0) f.def = docInfo["height"].Num();
  }
  std::wstring title = label;
  while (!title.empty() && (title.back() == L'.' || title.back() == L'…')) title.pop_back();
  if (dialog && dialog->open) dialog->Finish(false);
  dialog = std::make_unique<ParamDialog>(title, id, std::move(fields));
  // Settings and size changes apply on OK only: they are not edits a preview can take back.
  bool noPreview = id == "image.imageSize" || id == "image.canvasSize" || id.rfind("edit.colorSettings", 0) == 0 || id.rfind("edit.preferences", 0) == 0 ||
                   id.rfind("view.", 0) == 0;
  if (noPreview) {
    // A settings dialog sends only what was changed: the rest keeps its current value.
    if (id.rfind("image.", 0) != 0) dialog->form->touchedOnly = true;
    dialog->finish = [id](bool ok, const std::string& p) {
      if (ok) Exec(id, p);
    };
  } else {
    // The live preview is the command itself, taken back with undo when the values move on; it
    // runs on the engine thread, so a heavy filter never stops the interface.
    auto live = std::make_shared<LivePreview>(id);
    dialog->preview = [live](const std::string& p) { live->Show(p); };
    dialog->unpreview = [live] { live->Hide(); };
    dialog->previewBusy = [live] { return live->Busy(); };
    dialog->finish = [live](bool ok, const std::string& p) { live->Finish(ok, p); };
  }
  dialog->Open(from, Side::Center);
}

// ------------------------------------------------------------------------------ files --
extern std::unique_ptr<Menu> docMenu;
// Test seams exercise cancellation and save failure without showing modal system dialogs.
std::function<std::wstring(bool, const std::wstring&)> fileDialogOverride;
std::function<int(const std::wstring&)> closePromptOverride;
std::wstring FileDialog(bool save, const std::wstring& suggested = L"") {
  if (fileDialogOverride) return fileDialogOverride(save, suggested);
  // The system dialog is modal: the menu that asked for it goes first.
  if (openMenu) openMenu->CloseTree();
  if (docMenu) docMenu->CloseTree();
  ui::ClosePopovers();
  Microsoft::WRL::ComPtr<IFileDialog> fd;
  if (FAILED(CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fd)))) return {};
  COMDLG_FILTERSPEC openTypes[] = {
      {L"Images", L"*.jpg;*.jpeg;*.png;*.psd;*.psb;*.tif;*.tiff;*.webp;*.bmp;*.gif;*.pcraft;*.heic;*.heif;*.avif;*.exr;*.hdr;*.dng;*.cr2;*.tga;*.ora;*.kra;*.afphoto"},
      {L"All files", L"*.*"}};
  COMDLG_FILTERSPEC saveTypes[] = {{L"PhotoCraft", L"*.pcraft"}, {L"Photoshop", L"*.psd"}, {L"PNG", L"*.png"},
                                   {L"JPEG", L"*.jpg"},          {L"TIFF", L"*.tif"},      {L"WebP", L"*.webp"}};
  const wchar_t* exts[] = {L".pcraft", L".psd", L".png", L".jpg", L".tif", L".webp"};
  if (save) {
    fd->SetFileTypes(ARRAYSIZE(saveTypes), saveTypes);
    auto dot = suggested.find_last_of(L'.');
    std::wstring ext = dot == std::wstring::npos ? L".pcraft" : suggested.substr(dot);
    for (UINT i = 0; i < ARRAYSIZE(exts); i++)
      if (ext == exts[i]) fd->SetFileTypeIndex(i + 1);
    fd->SetDefaultExtension(ext.c_str() + 1);
    if (!suggested.empty()) fd->SetFileName(suggested.c_str());
  } else {
    fd->SetFileTypes(ARRAYSIZE(openTypes), openTypes);
  }
  if (FAILED(fd->Show(hwnd))) return {};
  Microsoft::WRL::ComPtr<IShellItem> item;
  if (FAILED(fd->GetResult(&item))) return {};
  PWSTR path = nullptr;
  if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return {};
  std::wstring out = path;
  CoTaskMemFree(path);
  return out;
}

void OpenPath(const std::wstring& path) {
  PrepareDocumentChange();
  auto out = std::make_shared<std::pair<int, std::string>>(0, "");
  std::string utf8 = Narrow(path);
  std::wstring name = path.substr(path.find_last_of(L"\\/") + 1);
  jobs::Run(
      [utf8, out] {
        char* r = nullptr;
        out->first = pc_open(engine, utf8.c_str(), &r);
        out->second = TakeString(r);
      },
      [out, name, path] {
        if (!out->first) {
          ui::Toast(W(out->second), true);
          return;
        }
        shape.kind = SelectionShape::None;
        if (!recentDocuments.Add(path)) ui::Toast(L"Recent documents could not be saved.", true);
        canvas->Reload();
        infoRevision = ~0ull;
        Refresh();
        ui::Toast(Fmt(L"Opened %ls", name));
      },
      Fmt(L"Opening %ls", name));
}
void OpenFile() {
  auto p = FileDialog(false);
  if (!p.empty()) OpenPath(p);
}
bool SaveDocument(bool as, bool copy = false, const wchar_t* forcedExt = nullptr, std::function<void(bool)> then = nullptr) {
  FinishTextEditing();
  FinishParameterPreviews();
  if (docInfo.IsNull()) return false;
  std::wstring path = W(docInfo["path"].Str());
  std::wstring ext = path.empty() || path.find_last_of(L'.') == std::wstring::npos ? L"" : path.substr(path.find_last_of(L'.') + 1);
  for (auto& c : ext) c = wchar_t(towlower(c));
  bool layered = ext == L"pcraft" || ext == L"psd" || ext == L"psb";
  if (as || copy || path.empty() || !layered) {
    std::wstring name = W(docInfo["name"].Str());
    auto dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos) name = name.substr(0, dot);
    path = FileDialog(true, name + (forcedExt ? forcedExt : L".pcraft"));
    if (path.empty()) { if (then) then(false); return false; }
  }
  auto out = std::make_shared<std::pair<int, std::string>>(0, "");
  std::string utf8 = Narrow(path);
  std::wstring name = path.substr(path.find_last_of(L"\\/") + 1);
  jobs::Run(
      [utf8, copy, out] {
        char* r = nullptr;
        out->first = pc_save(engine, utf8.c_str(), copy ? 0 : 1, &r);
        out->second = TakeString(r);
      },
      [out, name, path, copy, then] {
        if (!out->first) {
          ui::Toast(W(out->second), true);
          if (then) then(false);
          return;
        }
        if (!copy && !recentDocuments.Add(path)) ui::Toast(L"Recent documents could not be saved.", true);
        ui::Toast(Fmt(L"Saved %ls", name));
        infoRevision = ~0ull;
        Refresh();
        if (then) then(true);
      },
      Fmt(L"Saving %ls", name));
  return true;
}

// Resolve stable ids on the engine thread: closing or reordering a tab changes every index.
int DocumentIndex(uint64_t id) {
  auto s = Json::Parse(TakeString(pc_session_info(engine)));
  const auto& docs = s["documents"];
  for (size_t i = 0; i < docs.size(); i++) if (uint64_t(docs[i]["id"].Int()) == id) return int(i);
  return -1;
}
uint64_t ActiveDocumentId() { return uint64_t(docInfo["id"].Int()); }
void PrepareDocumentChange() {
  FinishTextEditing();
  CloseGradientEditor();
  FinishNudge();
  FinishParameterPreviews();
  if (!canvas) return;
  canvas->CancelInput();
  if (auto* mode = Mode()) { mode->Deactivate(); LeaveMode(mode); }
  else if (canvas->tool) canvas->tool->Deactivate();
  if (dialog && dialog->open) dialog->Finish(false);
  ui::ClosePopovers(); hasBrushAnchor = false; shape.kind = SelectionShape::None;
}
void SwitchDocument(uint64_t id) {
  if (id == ActiveDocumentId()) return;
  PrepareDocumentChange();
  jobs::Run([id] { int i = DocumentIndex(id); if (i >= 0) pc_set_active(engine, size_t(i)); }, [] { infoRevision = ~0ull; Refresh(); });
}
void CloseDocument(uint64_t id, std::function<void(bool)> after) {
  PrepareDocumentChange();
  jobs::Run([id] { int i = DocumentIndex(id); if (i >= 0) pc_set_active(engine, size_t(i)); }, [id, after] {
    infoRevision = ~0ull; Refresh();
    if (ActiveDocumentId() != id) { if (after) after(true); return; }
    auto close = [id, after] {
      jobs::Run([id] { int i = DocumentIndex(id); if (i >= 0) { pc_set_active(engine, size_t(i)); pc_close_active(engine); } },
                [after] { infoRevision = ~0ull; Refresh(); if (after) after(true); });
    };
    const auto& docs = sessionInfo["documents"]; int i = int(sessionInfo["active"].Int(-1));
    bool dirty = i >= 0 && docs[size_t(i)]["dirty"].Truthy();
    if (!dirty) { close(); return; }
    auto question = Fmt(L"Save changes to %ls before closing?", W(docInfo["name"].Str()));
    int answer = closePromptOverride ? closePromptOverride(question) : MessageBoxW(hwnd, question.c_str(), L"PhotoCraft by Vetro", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (answer == IDNO) close();
    else if (answer == IDYES) SaveDocument(false, false, nullptr, [close, after](bool ok) { if (ok) close(); else if (after) after(false); });
    else if (after) after(false);
  });
}
void CloseDocuments(std::vector<uint64_t> ids, std::function<void(bool)> after) {
  if (ids.empty()) { if (after) after(true); return; }
  uint64_t id = ids.front(); ids.erase(ids.begin());
  CloseDocument(id, [ids, after](bool ok) { if (ok) CloseDocuments(ids, after); else if (after) after(false); });
}
std::vector<MenuItem> RecentMenu() {
  std::vector<MenuItem> rows;
  for (const auto& path : recentDocuments.paths) {
    MenuItem item; item.label = path; item.action = [path] { OpenPath(path); }; rows.push_back(std::move(item));
  }
  if (rows.empty()) { MenuItem empty; empty.label = L"No recent documents"; empty.enabled = false; rows.push_back(empty); }
  else { rows.push_back(MenuItem::Separator()); rows.push_back(ActionItem(L"Clear recent documents", [] { recentDocuments.Clear(); Refresh(); })); }
  return rows;
}
bool ShellEnabled(const std::string& id) {
  if (id == "edit.paste" || id == "file.newFromClipboard") return clipboard::Available();
  return !docInfo.IsNull();
}
void CopyClipboard(bool merged) {
  ownClipboardSequence = 0;
  Exec(merged ? "edit.copyMerged" : "edit.copy", "{}", [](bool ok, const std::string&) {
    if (!ok) return;
    auto image = std::make_shared<clipboard::Image>(); auto error = std::make_shared<std::string>();
    auto prepared = std::make_shared<std::shared_ptr<clipboard::PreparedImage>>();
    jobs::Run([image, error, prepared] {
      if (!pc_clipboard_size(engine, &image->width, &image->height)) { *error = "The clipboard is empty."; return; }
      uint64_t bytes = uint64_t(image->width) * image->height * 4;
      if (bytes > clipboard::MaxBytes) { *error = "The clipboard image is too large."; return; }
      try { image->rgba.resize(size_t(bytes)); } catch (...) { *error = "The clipboard image is too large."; return; }
      if (!pc_clipboard_read(engine, image->rgba.data(), image->rgba.size())) { *error = TakeString(pc_last_error()); return; }
      try { *prepared = clipboard::Prepare(*image); } catch (...) { *error = "The clipboard image is too large."; return; }
      if (!*prepared) *error = "The clipboard image could not be encoded.";
    }, [error, prepared] {
      if (!error->empty()) { ui::Toast(W(*error), true); return; }
      std::wstring message;
      if (!clipboard::Publish(hwnd, *prepared, message)) { ui::Toast(message, true); return; }
      ownClipboardSequence = GetClipboardSequenceNumber();
    });
  });
}
void PasteClipboard(const std::string& command) {
  uint32_t copiedW = 0, copiedH = 0;
  if (ownClipboardSequence && ownClipboardSequence == GetClipboardSequenceNumber() && pc_clipboard_size(engine, &copiedW, &copiedH)) {
    Exec(command); return; // retain layers, HDR pixels and Paste in Place coordinates of our copy
  }
  ownClipboardSequence = 0;
  auto status = std::make_shared<int>(0); auto why = std::make_shared<std::wstring>();
  jobs::Run([status, why] {
    clipboard::Image image;
    auto result = clipboard::Read(hwnd, image, *why);
    if (result == clipboard::Result::Error) return;
    if (result == clipboard::Result::Empty) { pc_clipboard_clear(engine); *status = -1; return; }
    *status = pc_clipboard_set(engine, image.width, image.height, image.rgba.data(), image.rgba.size());
    if (!*status) *why = W(TakeString(pc_last_error()));
  }, [status, why, command] {
    if (*status == 1) Exec(command);
    else if (*status == -1) ui::Toast(L"Nothing to paste: the clipboard holds no image");
    else ui::Toast(*why, true);
  }, L"Reading clipboard");
}
void DropPaths(const std::vector<std::wstring>& paths, float x, float y, bool center) {
  PrepareDocumentChange();
  bool place = !docInfo.IsNull() && Inside(canvas->free, x, y);
  auto point = canvas->ToDoc(x, y);
  for (const auto& path : paths) {
    if (!place) { OpenPath(path); continue; }
    JsonObject p; p.Str("path", Narrow(path)).Bool("smartObject", true);
    if (!center) p.Add("center", "[" + JsonNum(point.x) + "," + JsonNum(point.y) + "]");
    Exec("file.placeEmbedded", p.Text());
  }
}
void InitializeDocuments() {
  documentTabs = new DocumentTabs(); documentTabs->activate = SwitchDocument;
  documentTabs->dismiss = [](uint64_t id) { CloseDocument(id); };
  documentTabs->context = [](uint64_t id, Rect from) {
    std::vector<MenuItem> rows;
    rows.push_back(ActionItem(L"Close", [id] { CloseDocument(id); }));
    rows.push_back(ActionItem(L"Close others", [id] {
      std::vector<uint64_t> ids; const auto& docs = sessionInfo["documents"];
      for (size_t i = 0; i < docs.size(); i++) if (uint64_t(docs[i]["id"].Int()) != id) ids.push_back(uint64_t(docs[i]["id"].Int()));
      CloseDocuments(ids, [id](bool) { SwitchDocument(id); });
    }, nullptr, sessionInfo["documents"].size() > 1));
    std::wstring path;
    const auto& docs = sessionInfo["documents"];
    for (size_t i = 0; i < docs.size(); i++) if (uint64_t(docs[i]["id"].Int()) == id) path = W(docs[i]["path"].Str());
    rows.push_back(ActionItem(L"Reveal in Explorer", [path] {
      PIDLIST_ABSOLUTE item = ILCreateFromPathW(path.c_str());
      if (item) { SHOpenFolderAndSelectItems(item, 0, nullptr, 0); ILFree(item); }
    }, nullptr, !path.empty()));
    tabMenu = std::make_unique<Menu>(std::move(rows)); tabMenu->Open(from, Side::Below);
  };
  documentTabs->reorder = [](uint64_t id, size_t to) {
    int from = DocumentIndex(id); if (from < 0 || size_t(from) == to) return;
    Exec("document.move", JsonObject().Num("document", from).Num("to", double(to)).Text());
  };
  startScreen = new StartScreen(); startScreen->create = [] { RunCommand("file.new", L"New…"); }; startScreen->open = [] { OpenFile(); }; startScreen->openPath = OpenPath;
  ui::AddLayer(documentTabs); ui::AddLayer(startScreen);
}

// ---------------------------------------------------------------------- run commands --
PanelDock* DockFor(const std::string& id);
void RunCommand(const std::string& id, const std::wstring& label) {
  FinishNudge();
  if (ToolPanelCommand(id)) return;
  if (LayerPanelCommand(id)) return;
  if (RetouchPanelCommand(id)) return;
  if (TextPanelCommand(id)) return;
  if (WorkspacePanelCommand(id)) return;
  if (GuideCommand(id)) return;
  Rect from = openMenuIndex >= 0 && size_t(openMenuIndex) < menuTriggers.size() ? menuTriggers[size_t(openMenuIndex)]->rect : ui::Bounds();
  if (id == "file.open") return OpenFile();
  if (id == "select.saveSelection" || id == "select.loadSelection") {
    bool save = id == "select.saveSelection";
    std::vector<MenuItem> rows;
    if (save) rows.push_back(ActionItem(L"New selection channel…",[] {
      AskText(docInfo["width"].Num()/2,docInfo["height"].Num()/2,L"Save Selection",L"",[](const std::wstring& name) {
        Exec("select.saveSelection",JsonObject().Str("name",Narrow(name)).Str("channel","new").Text());
      });
    }));
    auto channel = [&](std::wstring name,std::string source) {
      MenuItem row; row.label = std::move(name);
      for (auto [title,op] : {std::pair{L"Replace","replace"},std::pair{L"Add","add"},std::pair{L"Subtract","subtract"},std::pair{L"Intersect","intersect"}}) {
        row.submenu.push_back(ActionItem(title,[save,source,op] { Exec(save ? "select.saveSelection" : "select.loadSelection",JsonObject().Add("channel",source).Str("operation",!save && std::string(op) == "replace" ? "new" : op).Text()); }));
      }
      if (!save) row.submenu.push_back(ActionItem(L"Load inverted",[source] { Exec("select.loadSelection",JsonObject().Add("channel",source).Bool("invert",true).Text()); }));
      rows.push_back(std::move(row));
    };
    const auto& alpha = docInfo["channels"]["alpha"];
    for (size_t i = 0; i < alpha.size(); i++) channel(W(alpha[i]["name"].Str()),JsonNum(alpha[i]["index"].Num()));
    if (!save) for (auto [title,source] : {std::pair{L"Composite","composite"},std::pair{L"Layer Transparency","transparency"},std::pair{L"Layer Mask","mask"},std::pair{L"Vector Mask","vectorMask"}}) channel(title,JsonQuote(source));
    tabMenu = std::make_unique<Menu>(std::move(rows)); tabMenu->Open(from,Side::Below); return;
  }
  if (id == "file.save") { SaveDocument(false); return; }
  if (id == "file.saveAs") { SaveDocument(true); return; }
  if (id == "file.saveACopy") { SaveDocument(true, true); return; }
  if (id == "file.export.quickExportAsPng") { SaveDocument(true, true, L".png"); return; }
  if (id == "file.export.exportAs") { SaveDocument(true, true, L".jpg"); return; }
  if (id == "file.exit") { PostMessage(hwnd, WM_CLOSE, 0, 0); return; }
  if (id == "edit.copy" || id == "edit.copyMerged") { CopyClipboard(id == "edit.copyMerged"); return; }
  if (id == "edit.paste" || id == "file.newFromClipboard" || id.starts_with("edit.pasteSpecial.")) { PasteClipboard(id); return; }
  if (id == "file.close") {
    if (!docInfo.IsNull()) CloseDocument(ActiveDocumentId());
    return;
  }
  if (id == "edit.undo" || id == "edit.stepBackward") { Undo(1); return; }
  if (id == "edit.redo" || id == "edit.stepForward") { Undo(-1); return; }
  if (id == "edit.transform" || id == "edit.freeTransform") { StartTransform(); return; }
  if (id == "select.transformSelection") { StartTransform(true); return; }
  if (TransformCommand(id)) return;
  if (id == "view.zoomIn") { canvas->ZoomBy(1.25f, MidX(canvas->free), MidY(canvas->free)); return; }
  if (id == "view.zoomOut") { canvas->ZoomBy(.8f, MidX(canvas->free), MidY(canvas->free)); return; }
  if (id == "view.fitOnScreen") { canvas->Fit(true); return; }
  if (id == "view.100" || id == "view.actualPixels" || id == "view.printSize") { canvas->ActualPixels(true); return; }
  if (auto* d = DockFor(id)) { d->visible = !d->visible; return; }
  if (ShellCommand(id)) {
    ui::Toast(Fmt(L"%ls is not in this shell yet", i18n::Tr(label)), true);
    return;
  }
  if (OpenEditor(id, label, from)) return;
  auto fields = ParseParams(paramsDoc[id]);
  bool ellipsis = !label.empty() && (label.back() == L'…' || label.back() == L'.');
  if (!fields.empty() && ellipsis) {
    OpenDialog(id, label, from);
    return;
  }
  Exec(id);
}

// ---------------------------------------------------------------------- tool choice --
ToolDef& SlotTool(size_t flat) {
  size_t i = 0;
  for (auto& s : sections)
    for (auto& slot : s) {
      if (i == flat) return slot[size_t(std::clamp(slotChoice[i], 0, int(slot.size()) - 1))];
      i++;
    }
  return sections[0][0][0];
}
void RebuildOptions();
void SelectTool(ToolDef* t, bool temporary = false) {
  FinishNudge();
  if (!t) return;
  if (!t->available && !t->hand) {
    ui::Toast(Fmt(L"%ls is not in this shell yet", i18n::Tr(t->name)), true);
    return;
  }
  if (!temporary) {
    springTool = nullptr; springKey = 0; springRestore = false;
    if (CanvasTool* m = Mode()) m->Deactivate();  // Free Transform applies, Liquify keeps its strokes
    if (canvas->tool && canvas->tool != t->impl.get()) canvas->tool->Deactivate();
  }
  current = t;
  GuideToolChanged(t->key);
  canvas->tool = t->hand ? nullptr : t->impl.get();
  if (canvas->tool) canvas->tool->Activate();
  size_t flat = 0;
  for (auto& s : sections)
    for (auto& slot : s) {
      for (size_t k = 0; k < slot.size(); k++)
        if (&slot[k] == t) slotChoice[flat] = int(k);
      flat++;
    }
  for (size_t i = 0; i < toolButtons.size(); i++) {
    ToolDef& d = SlotTool(i);
    toolButtons[i]->icon = d.icon;
    toolButtons[i]->stroked = d.stroked;
    toolButtons[i]->tooltip = d.name + (d.letter ? std::wstring(L"  (") + d.letter + L")" : L"");
    toolButtons[i]->SetSelected(&d == current);
  }
  RebuildOptions();
}
void ToolByLetter(wchar_t letter, bool cycle, bool temporary = false) {
  size_t flat = 0;
  for (auto& s : sections)
    for (auto& slot : s) {
      for (auto& t : slot)
        if (t.letter == letter) {
          ToolDef& cur = slot[size_t(slotChoice[flat])];
          if (cycle && &cur == current) {
            // Shift + letter: the next available tool of the slot.
            for (size_t k = 1; k <= slot.size(); k++) {
              ToolDef& n = slot[(size_t(slotChoice[flat]) + k) % slot.size()];
              if (n.letter == letter && (n.available || n.hand)) { SelectTool(&n, temporary); return; }
            }
          }
          SelectTool(cur.letter == letter && (cur.available || cur.hand) ? &cur : &t, temporary);
          return;
        }
      flat++;
    }
}

// ============================================================================ panels ==
// Swatches: the foreground over the background. The front one picks, the back one swaps.
class Swatches : public Widget {
 public:
  void Paint() override {
    D2D1_COLOR_F fg = ColourOf(sessionInfo["foreground"]), bg = ColourOf(sessionInfo["background"]);
    Rect back = RWH(rect.left + 15, rect.top + 15, 21, 21), front = RWH(rect.left + 3, rect.top + 3, 21, 21);
    FillRound(back, 6, bg);
    StrokeRound(back, 6, Rgba(1, 1, 1, .30f), 1);
    SoftShadow(RR(front, 6), .8f, .4f);
    FillRound(front, 6, fg);
    StrokeRound(front, 6, Rgba(1, 1, 1, .50f + .3f * hover.v), 1.2f);
  }
  void OnUp(const PointerEvent& e, bool inside) override {
    Widget::OnUp(e, inside);
    if (!inside) return;
    Rect front = RWH(rect.left + 3, rect.top + 3, 21, 21);
    bool background=!Inside(front,e.x,e.y);
    auto c=ColourOf(sessionInfo[background?"background":"foreground"]);
    OpenColorPicker(c,[background](D2D1_COLOR_F color){
      char b[16];snprintf(b,sizeof b,"#%02x%02x%02x",int(color.r*255+.5f),int(color.g*255+.5f),int(color.b*255+.5f));
      Exec("tools.setColors",JsonObject().Str(background?"background":"foreground",b).Text());
    },rect);
  }
};

// One row of the Layers panel: eye, thumbnail, name; the selected row lifts. As in Photoshop:
// a right click opens the layer menu, a double click on the name renames and elsewhere opens the
// layer style, Alt-click on the line between two rows clips the upper one to the lower, and a drag
// moves the layer (Alt: a copy) above, below or into a group.
class LayerRow;
void AskText(double x, double y, std::wstring title, std::wstring initial, std::function<void(const std::wstring&)> done);
void LayerMenu(LayerRow* row, const PointerEvent& e);
extern std::vector<LayerRow*> layerRows;

struct LayerDrag {
  bool pressed = false, moving = false, copy = false;
  float y0 = 0;
  LayerRow* source = nullptr;
  LayerRow* target = nullptr;
  int position = 0;  // 0 above, 1 below, 2 into (a group)
};
LayerDrag layerDrag;
constexpr float ClipBand = 6;  // Alt: this close to the line between two rows clips

class LayerEye : public IconButton {
 public:
  LayerRow* row;
  bool value = false, solo = false;
  std::set<uint64_t> visited;
  explicit LayerEye(LayerRow* r) : IconButton(PhEye, L"Show or hide", nullptr), row(r) { iconSize = 15; }
  void OnDown(const PointerEvent& e) override;
  void OnDrag(const PointerEvent& e) override;
  void OnUp(const PointerEvent& e, bool inside) override;
};
IconButton* newLayerAction = nullptr;
IconButton* deleteLayerAction = nullptr;

extern Tabs* propsTabs;
class LayerRow : public Container {
 public:
  uint64_t id = 0;
  int depth = 0;
  std::wstring name, kind;
  bool visibleLayer = true, selectedRow = false, clipped = false, group = false, expanded = true;
  Json info;
  Microsoft::WRL::ComPtr<ID2D1Bitmap1> thumb;
  Spring sel{0, motion::SlideK, motion::SlideC};
  LayerEye* eye = nullptr;
  IconButton* disclosure = nullptr;
  LayerRow() {
    eye = Add<LayerEye>(this);
    disclosure = Add<IconButton>(PhCaretRight, L"Expand or collapse group", [this] {
      JsonObject o; o.Num("layer", double(id)).Bool("expanded", !expanded).Bool("all", GetKeyState(VK_MENU) < 0);
      Exec("layer.setExpanded", o.Text());
    });
    disclosure->iconSize = 12;
  }
  bool Interactive() const override { return true; }
  Widget* HitTest(float x, float y) override {
    if (!visible || !Inside(rect, x, y)) return nullptr;
    if (Widget* h = eye->HitTest(x, y)) return h;
    if (group && disclosure->HitTest(x, y)) return disclosure;
    for (auto& c : children) if(c.get()!=eye && c.get()!=disclosure) if(auto* h=c->HitTest(x,y)) return h;
    return this;
  }
  bool Animate(float dt) override {
    sel.To(selectedRow ? 1.f : 0.f);
    bool m = Container::Animate(dt);
    eye->rect = RWH(rect.left + 6, MidY(rect) - 15, 30, 30);
    disclosure->visible = group;
    disclosure->rect = RWH(rect.left + 38 + Indent(), MidY(rect) - 9, 18, 18);
    PlaceLayerAccessories(*this,Thumb());
    return sel.Step(dt) || m || (hover.v > .01f && GetKeyState(VK_MENU) < 0);
  }
  float Indent() const { return float(depth) * 14.f + (clipped ? 16.f : 0.f); }
  Rect Thumb() const { return RWH(rect.left + 58 + Indent(), MidY(rect) - 17, 34, 34); }
  Rect NameBox() const {
    Rect th = Thumb();
    return R(th.right + 10 + LayerAccessoryWidth(info), rect.top, std::min(rect.right - 35, th.right + 10 + LayerAccessoryWidth(info) + Measure(name, F_Row) + 8), rect.bottom);
  }
  int Index() const {
    for (size_t i = 0; i < layerRows.size(); i++)
      if (layerRows[i] == this) return int(i);
    return -1;
  }
  // Alt near a line between rows: the row that would clip (the upper of the two), or null.
  LayerRow* ClipCandidate(float y) {
    int i = Index();
    if (i < 0) return nullptr;
    if (y > rect.bottom - ClipBand && size_t(i) + 1 < layerRows.size()) return this;
    if (y < rect.top + ClipBand && i > 0) return layerRows[size_t(i) - 1];
    return nullptr;
  }
  void Paint() override {
    float s = std::clamp(sel.v, 0.f, 1.f);
    FillRound(rect, 12, Fade(P.hover, hover.v * (1 - s)));
    if (s > .01f) {
      FillRound(rect, 12, Fade(Rgba(1, 1, 1, .12f), s));
      StrokeRound(Inset(rect, .5f), 12, Fade(Rgba(1, 1, 1, .09f), s), 1);
      FillRound(R(rect.left + 2, MidY(rect) - 8, rect.left + 4.5f, MidY(rect) + 8), 1.2f, Fade(P.accent, s));
    }
    eye->rect = RWH(rect.left + 6, MidY(rect) - 15, 30, 30);
    eye->icon = visibleLayer ? PhEye : PhEyeSlash;
    eye->Paint();
    if (group) {
      disclosure->icon = expanded ? PhCaretDown : PhCaretRight;
      disclosure->Paint();
    }
    Rect th = Thumb();
    if (clipped) {
      // Photoshop's clipping arrow: down into the layer below.
      float ax = th.left - 9, ay = MidY(rect);
      Line(ax - 4, ay - 6, ax - 4, ay + 4, P.dim, 1.4f);
      Line(ax - 4, ay + 4, ax + 3, ay + 4, P.dim, 1.4f);
      Line(ax + 1, ay + 1.5f, ax + 3.5f, ay + 4, P.dim, 1.4f);
      Line(ax + 1, ay + 6.5f, ax + 3.5f, ay + 4, P.dim, 1.4f);
    }
    if (thumb) {
      PushClip(th, 6);
      Checkerboard(th, 4);
      DrawBitmap(thumb.Get(), th, visibleLayer ? 1.f : .5f, nullptr, false);
      PopClip();
    } else {
      FillRound(th, 8, Rgba(1, 1, 1, .07f));
      // Kinds as photocraft-doc names them (LayerContent::kind_name).
      const wchar_t* ic = group ? PhFolderSimple : kind == L"Type" ? PhTextT : kind == L"Adjustment" ? PhCircleHalf
                        : kind == L"Fill" ? PhPaintBucket : kind == L"Shape" ? PhRectangle : kind == L"Smart Object" ? PhSquareHalf : PhImage;
      IconAt(ic, MidX(th), MidY(th), 17, P.dim);
    }
    StrokeRound(th, 6, Rgba(1, 1, 1, .10f), 1);
    D2D1_COLOR_F c = Mix(P.dim, P.text, std::clamp(s + hover.v * .4f, 0.f, 1.f));
    Rect nb = NameBox();
    Text(name, R(nb.left, rect.top, rect.right - 35, rect.bottom), F_Row, visibleLayer ? c : Fade(c, .55f));
    PaintLayerIndicators(info,rect);
    for(auto& child : children) if(child.get()!=eye && child.get()!=disclosure) child->Paint();
    // The base of a clipping group is underlined, as in Photoshop.
    int i = Index();
    if (i >= 0 && size_t(i) > 0 && layerRows[size_t(i) - 1]->clipped && !clipped)
      Line(nb.left, MidY(rect) + 9, std::min(nb.right - 8, rect.right - 10), MidY(rect) + 9, Fade(c, .7f), 1);
    // Alt held over a line between rows: where the click would clip.
    if (hover.v > .5f && GetKeyState(VK_MENU) < 0 && !layerDrag.moving && pointerX >= 0) {
      if (LayerRow* up = ClipCandidate(pointerY)) {
        float y = up == this ? rect.bottom : rect.top;
        Line(rect.left + 8, y, rect.right - 8, y, P.accent, 2);
      }
    }
    // Where a dragged layer would land.
    if (layerDrag.moving && layerDrag.target == this) {
      if (layerDrag.position == 2) StrokeRound(Inset(rect, 1), 11, P.accent, 2);
      else {
        float y = layerDrag.position == 0 ? rect.top : rect.bottom;
        FillRound(R(rect.left + 8, y - 1.5f, rect.right - 8, y + 1.5f), 1.5f, P.accent);
      }
    }
    if (layerDrag.moving && layerDrag.source == this) FillRound(rect, 12, Rgba(0, 0, 0, .25f));
  }
  void OnDown(const PointerEvent& e) override {
    Container::OnDown(e);
    if (e.button != 0) return;
    if (e.mods.ctrl && Inside(Thumb(), e.x, e.y)) {
      layerDrag = {};
      JsonObject o; o.Num("layer", double(id)).Str("channel", "transparency");
      o.Str("operation", e.mods.shift && e.mods.alt ? "intersect" : e.mods.shift ? "add" : e.mods.alt ? "subtract" : "replace");
      Exec("select.loadSelection", o.Text());
      return;
    }
    if (e.clicks == 2) {
      layerDrag = {};
      if (Inside(Thumb(), e.x, e.y) && (kind == L"Smart Object" || kind == L"SmartObject" || kind == L"Smart")) {
        JsonObject o; o.Num("layer", double(id));
        Exec("layer.smartObjects.editContents", o.Text(), [](bool ok, const std::string&) {
          if (ok) { hasBrushAnchor = false; canvas->Reload(); infoRevision = ~0ull; Refresh(); }
        });
      } else if(Inside(Thumb(),e.x,e.y)&&(kind==L"Type"||kind==L"Adjustment")) {
        auto layer=id;bool type=kind==L"Type";
        Exec("layer.select",JsonObject().Num("layer",double(layer)).Str("mode","replace").Text(),[layer,type](bool ok,const std::string&){
          if(!ok)return;if(type){Exec("type.info",JsonObject().Num("layer",double(layer)).Text(),[layer](bool good,const std::string& s){if(!good)return;std::string key=Json::Parse(s)["orientation"].Str()=="Vertical"?"verticalType":"type";for(auto& section:sections)for(auto& slot:section)for(auto& tool:slot)if(tool.key==key){auto* previous=current;SelectTool(&tool);EditTypeLayer(layer,true,[previous]{SelectTool(previous);});return;}});}else{propsDock->visible=true;propsTabs->index=0;propsTabs->onChange(0);}
        });
      } else if (Inside(NameBox(), e.x, e.y)) {
        // Rename in place: the popover opens at the row.
        uint64_t layer = id;
        auto at = canvas->ToDoc(rect.left, rect.top);
        AskText(at.x, at.y, L"Rename Layer", name, [layer](const std::wstring& t) {
          if (t.empty()) return;
          JsonObject o;
          o.Num("layer", double(layer)).Str("name", Narrow(t));
          Exec("layer.renameLayer", o.Text());
        });
      } else {
        // Anywhere else on the row: the layer's style (Photoshop's double-click for fx).
        JsonObject o;
        o.Num("layer", double(id)).Str("mode", "replace");
        Exec("layer.select", o.Text(), [](bool ok, const std::string&) {
          if (ok) RunCommand("layer.layerStyle.blendingOptions", L"Blending Options");
        });
      }
      return;
    }
    layerDrag = {};
    layerDrag.pressed = true;
    layerDrag.y0 = e.y;
    layerDrag.source = this;
  }
  void OnDrag(const PointerEvent& e) override {
    Container::OnDrag(e);
    if (!layerDrag.pressed || layerDrag.source != this) return;
    if (!layerDrag.moving && std::fabs(e.y - layerDrag.y0) < 5) return;
    layerDrag.moving = true;
    layerDrag.copy = e.mods.alt;
    layerDrag.target = nullptr;
    for (LayerRow* r : layerRows) {
      if (e.y < r->rect.top || e.y > r->rect.bottom) continue;
      float t = (e.y - r->rect.top) / std::max(1.f, H(r->rect));
      layerDrag.target = r;
      layerDrag.position = r->group && t > .3f && t < .7f ? 2 : t < .5f ? 0 : 1;
    }
    if (!layerDrag.target && !layerRows.empty()) {
      // Past the ends of the list: the very top or bottom.
      bool above = e.y < layerRows.front()->rect.top;
      layerDrag.target = above ? layerRows.front() : layerRows.back();
      layerDrag.position = above ? 0 : 1;
    }
    if (layerDrag.target == this) layerDrag.target = nullptr;  // where it already is
  }
  void OnUp(const PointerEvent& e, bool inside) override {
    Widget::OnUp(e, inside);
    if (e.button == 1) {
      if (inside) LayerMenu(this, e);
      return;
    }
    if (e.button != 0) return;
    if (layerDrag.moving && layerDrag.source == this) {
      if ((newLayerAction && Inside(newLayerAction->rect, e.x, e.y)) || (deleteLayerAction && Inside(deleteLayerAction->rect, e.x, e.y))) {
        bool duplicate = newLayerAction && Inside(newLayerAction->rect, e.x, e.y);
        JsonObject o;
        if (!selectedRow) o.Num("layer", double(id));
        layerDrag = {};
        Exec(duplicate ? "layer.duplicate" : "layer.delete", o.Text());
        return;
      }
      LayerRow* target = layerDrag.target;
      int position = layerDrag.position;
      bool copy = e.mods.alt;
      layerDrag = {};
      if (!target) return;
      JsonObject o;
      // The selected layers move together when the dragged one is among them.
      if (selectedRow) {
        std::string ids = "[";
        for (LayerRow* r : layerRows)
          if (r->selectedRow) ids += (ids.size() > 1 ? "," : "") + std::to_string(r->id);
        o.Add("layers", ids + "]");
      } else {
        o.Num("layer", double(id));
      }
      o.Num("target", double(target->id)).Str("position", position == 2 ? "into" : position == 0 ? "above" : "below").Bool("copy", copy);
      Exec("layer.moveTo", o.Text());
      return;
    }
    layerDrag = {};
    if (!inside || e.clicks == 2 || (e.mods.ctrl && Inside(Thumb(), e.x, e.y))) return;
    if (e.mods.alt) {
      if (LayerRow* up = ClipCandidate(e.y)) {
        JsonObject o;
        o.Num("layer", double(up->id)).Bool("clipped", !up->clipped);
        Exec("layer.setProps", o.Text());
        return;
      }
    }
    JsonObject o;
    o.Num("layer", double(id)).Str("mode", e.mods.ctrl ? "toggle" : e.mods.shift ? "range" : "replace");
    bool pixels=Inside(Thumb(),e.x,e.y);
    Exec("layer.select", o.Text(),[pixels](bool ok,const std::string&){if(ok&&pixels)Exec("channel.target","{\"channel\":\"composite\"}");});
  }
};

std::map<uint64_t, bool> soloVisibility;
uint64_t soloLayer = 0;
int64_t soloDocument = -1;
bool ContainsLayer(const Json& layer, uint64_t id) {
  bool found = uint64_t(layer["id"].Int()) == id;
  VisitLayers(layer["children"], [&](const Json& l) { found |= uint64_t(l["id"].Int()) == id; });
  return found;
}
void RestoreSpringTool() {
  if (!springTool || canvas->Drawing()) return;
  auto* previous = springTool;
  springTool = nullptr; springKey = 0; springRestore = false;
  SelectTool(previous, true);
}
void ReleaseToolKey(UINT vk) {
  if (vk != springKey || !springTool) return;
  if (ui::NowMs() - springAt >= 200 || canvas->Drawing()) {
    springRestore = true;
    RestoreSpringTool();
  } else {
    if (springTool->impl && springTool != current) springTool->impl->Deactivate();
    springTool = nullptr; springKey = 0; springRestore = false;
  }
}
bool IsBrushTool() {
  return current && (dynamic_cast<BrushTool*>(current->impl.get()) || dynamic_cast<PathStrokeTool*>(current->impl.get()) ||
                     current->key == "cloneStamp" || current->key == "healingBrush");
}
void SetBrushSettings() {
  if (canvas->Drawing()) { brushSettingsPending = true; RebuildOptions(); return; }
  brushSettingsPending = false;
  JsonObject o;
  o.Num("size", brush.size).Num("hardness", brush.hardness / 100).Num("opacity", brush.opacity / 100).Num("flow", brush.flow / 100);
  Exec("tools.setBrush", o.Text());
  RebuildOptions();
}
void LayerEye::OnDown(const PointerEvent& e) {
  if (e.button != 0) return;
  visibilityPainting = true;
  value = !row->visibleLayer; solo = e.mods.alt; visited.clear();
  if (!solo) { OnDrag(e); return; }
  int64_t document = docInfo["id"].Int();
  if (soloDocument != document) { soloVisibility.clear(); soloLayer = 0; soloDocument = document; }
  bool restore = soloLayer == row->id && !soloVisibility.empty();
  if (soloVisibility.empty()) VisitLayers(docInfo["layers"], [](const Json& l) { soloVisibility[uint64_t(l["id"].Int())] = l["visible"].Truthy(true); });
  uint64_t target = row->id;
  std::vector<std::string> changes;
  std::function<void(const Json&, bool)> collect = [&](const Json& layers, bool inTarget) {
    for (size_t i = 0; i < layers.size(); i++) {
      const Json& l = layers[i]; uint64_t id = uint64_t(l["id"].Int());
      bool visible = restore ? (soloVisibility.count(id) ? soloVisibility[id] : l["visible"].Truthy(true)) :
                     inTarget ? (soloVisibility.count(id) ? soloVisibility[id] : l["visible"].Truthy(true)) : ContainsLayer(l, target);
      JsonObject o; o.Num("layer", double(id)).Bool("visible", visible); changes.push_back(o.Text());
      for (auto* r : layerRows) if (r->id == id) r->visibleLayer = visible;
      collect(l["children"], inTarget || id == target);
    }
  };
  collect(docInfo["layers"], false);
  soloLayer = restore ? 0 : target;
  if (restore) soloVisibility.clear();
  jobs::Run([changes] {
    for (const auto& p : changes) { char* r = nullptr; pc_execute(engine, "layer.setProps", p.c_str(), &r); TakeString(r); }
  }, [] { infoRevision = ~0ull; Refresh(); });
}
void LayerEye::OnDrag(const PointerEvent& e) {
  if (solo || !visibilityPainting) return;
  for (auto* r : layerRows) {
    if (!Inside(r->eye->rect, e.x, e.y) || !visited.insert(r->id).second) continue;
    r->visibleLayer = value;
    JsonObject o; o.Num("layer", double(r->id)).Bool("visible", value);
    Exec("layer.setProps", o.Text());
  }
}
void LayerEye::OnUp(const PointerEvent& e, bool) {
  if (e.button != 0) return;
  OnDrag(e);
  visibilityPainting = false;
  infoRevision = ~0ull; Refresh();
}

// The Layers panel's right-click menu: Photoshop's, for the layer under the pointer (selected
// first unless it already is, so a menu over several selected layers acts on all of them).
void LayerMenu(LayerRow* row, const PointerEvent& e) {
  auto open = [x = e.x, y = e.y, clipped = row->clipped, group = row->group] {
    std::vector<MenuItem> rows;
    MenuItem colors;colors.label=L"Color Label";
    for(auto color:{"none","red","orange","yellow","green","seafoam","blue","indigo","magenta","fuchsia","violet","gray"})colors.submenu.push_back(ActionItem(HumanKey(color),[color]{Exec("layer.setLabelColor",JsonObject().Str("color",color).Text());}));
    rows.push_back(colors);
    MenuItem locks;locks.label=L"Locks";
    for(auto key:{"transparency","pixels","position","artboard","all"})locks.submenu.push_back(ActionItem(HumanKey(key),[key]{const Json* l=ActiveLayer();bool on=l&&(*l)["locks"][key].Truthy();Exec("layer.lockLayers",JsonObject().Bool(key,!on).Text());}));
    rows.push_back(locks);

    rows.push_back(CommandItem("layer.layerStyle.blendingOptions", L"Blending Options…"));
    rows.push_back(MenuItem::Separator());
    rows.push_back(CommandItem("layer.duplicate", L"Duplicate Layer…"));
    rows.push_back(CommandItem("layer.delete", L"Delete Layer", PhTrash));
    MenuItem rename = ActionItem(L"Rename Layer", [] {
      const Json* l = ActiveLayer();
      if (!l) return;
      uint64_t layer = uint64_t((*l)["id"].Int());
      auto at = canvas->ToDoc(pointerX, pointerY);
      AskText(at.x, at.y, L"Rename Layer", W((*l)["name"].Str()), [layer](const std::wstring& t) {
        if (t.empty()) return;
        JsonObject o;
        o.Num("layer", double(layer)).Str("name", Narrow(t));
        Exec("layer.renameLayer", o.Text());
      });
    });
    rows.push_back(rename);
    rows.push_back(MenuItem::Separator());
    rows.push_back(CommandItem("layer.groupLayers", L"Group Layers", PhFolderSimple));
    if (group) rows.push_back(CommandItem("layer.ungroupLayers", L"Ungroup Layers"));
    rows.push_back(CommandItem("layer.smartObjects.convertToSmartObject", L"Convert to Smart Object"));
    rows.push_back(CommandItem("layer.rasterize.layer", L"Rasterize Layer"));
    rows.push_back(CommandItem("layer.rasterize.layerStyle", L"Rasterize Layer Style"));
    rows.push_back(MenuItem::Separator());
    rows.push_back(clipped ? CommandItem("layer.releaseClippingMask", L"Release Clipping Mask")
                           : CommandItem("layer.createClippingMask", L"Create Clipping Mask"));
    rows.push_back(MenuItem::Separator());
    rows.push_back(CommandItem("layer.layerStyle.copyLayerStyle", L"Copy Layer Style"));
    rows.push_back(CommandItem("layer.layerStyle.pasteLayerStyle", L"Paste Layer Style"));
    rows.push_back(CommandItem("layer.layerStyle.clear", L"Clear Layer Style"));
    rows.push_back(MenuItem::Separator());
    rows.push_back(CommandItem("layer.mergeDown", L"Merge Down"));
    rows.push_back(CommandItem("layer.mergeVisible", L"Merge Visible"));
    rows.push_back(CommandItem("layer.flattenImage", L"Flatten Image"));
    contextMenu = std::make_unique<Menu>(std::move(rows));
    contextMenu->minWidth = 230;
    contextMenu->Open(R(x + 2, y - 8, x + 2, y - 8), Side::Below);
  };
  if (row->selectedRow) {
    open();
    return;
  }
  JsonObject o;
  o.Num("layer", double(row->id)).Str("mode", "replace");
  Exec("layer.select", o.Text(), [open](bool ok, const std::string&) {
    if (ok) open();
  });
}

class HistoryRow : public Widget {
 public:
  std::wstring label;
  int index = 0;
  bool current = false;
  std::function<void(int)> jump;
  void Paint() override {
    FillRound(rect, 10, Fade(P.hover, hover.v));
    if (current) FillRound(rect, 10, Rgba(1, 1, 1, .11f));
    PaintHistorySource(rect,index);
    IconAt(current ? PhCheckCircle : PhClock, rect.left + 36, MidY(rect), 14, current ? P.text : P.faint);
    Text(label, R(rect.left + 52, rect.top, rect.right - 8, rect.bottom), F_Row, current ? P.text : P.dim);
  }
  void OnUp(const PointerEvent& e, bool inside) override {
    Widget::OnUp(e, inside);
    if (inside && !HistorySourceClick(e,rect,index) && jump) jump(index);
  }
};

Tabs* propsTabs = nullptr;
Tabs* layersTabs = nullptr;
ScrollArea* layerList = nullptr;
ScrollArea* historyList = nullptr;
Container* propsBody = nullptr;
Container* adjustBody = nullptr;
std::vector<LayerRow*> layerRows;
std::map<uint64_t, std::pair<uint64_t, Microsoft::WRL::ComPtr<ID2D1Bitmap1>>> thumbs;
Slider *layerOpacity = nullptr, *layerFill = nullptr;
SelectList* layerBlend = nullptr;
Toggle* layerVisible = nullptr;
ScrollArea* shapeProperties = nullptr;
ScrollArea* adjustmentProperties = nullptr;
ScrollArea* documentProperties = nullptr;
std::vector<NumberField*> documentFields;
Toggle *documentRulers = nullptr, *documentGrid = nullptr;
Label *layerName = nullptr, *layerKind = nullptr;
std::vector<IconButton*> layerActions;
Container* layerFooter = nullptr;
Container* layerLocks = nullptr;
std::map<uint64_t, float> layerScroll;
uint64_t layerDocument = 0;

const std::vector<std::wstring> BlendNames = {L"Normal", L"Dissolve", L"Darken", L"Multiply", L"Color Burn", L"Linear Burn", L"Darker Color",
                                              L"Lighten", L"Screen", L"Color Dodge", L"Linear Dodge (Add)", L"Lighter Color", L"Overlay",
                                              L"Soft Light", L"Hard Light", L"Vivid Light", L"Linear Light", L"Pin Light", L"Hard Mix",
                                              L"Difference", L"Exclusion", L"Subtract", L"Divide", L"Hue", L"Saturation", L"Color", L"Luminosity"};

const Json* FindLayer(const Json& list, int64_t id) {
  for (size_t i = 0; i < list.size(); i++) {
    const Json& l = list[i];
    if (l["id"].Int() == id) return &l;
    if (const Json* c = FindLayer(l["children"], id)) return c;
  }
  return nullptr;
}
const Json* ActiveLayer() {
  if (docInfo["activeLayer"].IsNull()) return nullptr;
  return FindLayer(docInfo["layers"], docInfo["activeLayer"].Int());
}

void RebuildLayers() {
  uint64_t document = uint64_t(docInfo["id"].Int());
  if (document != layerDocument) {
    layerScroll[layerDocument] = layerList->offset.target;
    layerList->offset.Reset(layerScroll[document]);
    layerDocument = document;
    thumbs.clear();
  }
  ClearLayerDetails();
  layerList->Clear();
  layerRows.clear();
  std::function<void(const Json&, int)> add = [&](const Json& list, int depth) {
    for (size_t i = 0; i < list.size(); i++) {
      const Json& l = list[i];
      if(!LayerMatches(l)) { if(l["kind"].Str()=="Group")add(l["children"],depth+1); continue; }
      auto* row = layerList->Add<LayerRow>();
      row->info=l;
      AddLayerAccessories(*row,l);
      row->id = uint64_t(l["id"].Int());
      row->depth = depth;
      row->name = W(l["name"].Str());
      row->kind = W(l["kind"].Str());
      row->visibleLayer = l["visible"].Truthy(true);
      row->selectedRow = l["selected"].Truthy();
      row->clipped = l["clipped"].Truthy();
      row->group = l["kind"].Str() == "Group";
      row->expanded = l["expanded"].Truthy(true);
      row->sel.Reset(row->selectedRow ? 1.f : 0.f);
      layerRows.push_back(row);
      AddLayerDetails(*layerList,l);
      if (row->group && row->expanded) add(l["children"], depth + 1);
    }
  };
  add(docInfo["layers"], 0);
  layerList->contentHeight = 0;
  for (auto& child : layerList->children) layerList->contentHeight += LayerDetailHeight(child.get()) + 2;
  // Thumbnails of pixel layers, rebuilt when the document moved.
  Snapshot snap(pc_snapshot(engine));
  uint64_t rev = uint64_t(docInfo["revision"].Int());
  for (auto* row : layerRows) {
    auto& cached = thumbs[row->id];
    if (!cached.second || cached.first != rev) {
      std::vector<uint8_t> px(48 * 48 * 4);
      if (snap && pc_layer_thumb(snap.p, row->id, 48, px.data(), px.size(), 48 * 4) == 1) cached = {rev, MakeBitmap(48, 48, px.data(), 48 * 4)};
      else cached = {rev, nullptr};
    }
    row->thumb = cached.second;
  }
}

void RebuildLayerFilter() { if(layerList) RebuildLayers(); ui::RequestFrame(); }
void RebuildHistory() {
  historyList->Clear();
  BuildHistoryExtras(*historyList);
  const Json& h = docInfo["history"];
  int n = int(h.size());
  for (int i = 0; i < n; i++) {
    auto* row = historyList->Add<HistoryRow>();
    row->label = W(h[size_t(i)].Str());
    row->index = i;
    row->current = i == n - 1;
    row->jump = [n](int target) { Undo(n - 1 - target); };
  }
  historyList->offset.To(1e6f);  // the newest state in view
}

void SyncProperties() {
  const Json* l = ActiveLayer();
  bool has = l != nullptr;
  layerName->text = has ? W((*l)["name"].Str()) : L"No layer";
  bool document = !has && !docInfo.IsNull();
  if (document) layerName->text = i18n::Tr(L"Canvas");
  std::wstring kind = has ? W((*l)["kind"].Str()) : L"";
  if (!kind.empty()) kind[0] = wchar_t(towupper(kind[0]));
  layerKind->text = has ? kind + L" layer" : L"Open a photo to begin";  // "Pixel layer": whole, so it translates
  if (document) layerKind->text = W(docInfo["mode"].Str()) + L" · " + W(JsonNum(docInfo["depth"].Num())) + L" " + i18n::Tr(L"bit");
  for (Widget* w : std::initializer_list<Widget*>{layerOpacity, layerFill, layerBlend, layerVisible}) { w->enabled = has; w->visible = has || !document; }
  if (documentProperties) {
    documentProperties->visible = document;
    const char* keys[] = {"width", "height", "resolution"};
    for (size_t i=0;i<documentFields.size();i++) if (!ui::IsCaptured(documentFields[i])) documentFields[i]->Set(docInfo[keys[i]].Num());
    documentRulers->Set(GuideState()["rulers"].Truthy()); documentGrid->Set(GuideState()["grid"].Truthy());
  }
  SyncShapeProperties();
  SyncAdjustmentProperties();
  SyncLayerControls();
  SyncTextPanels();
  if (layerLocks) {
    const char* keys[] = {"transparency", "pixels", "position", "artboard", "all"};
    for (size_t i = 0; i < layerLocks->children.size(); i++) {
      auto* button = static_cast<IconButton*>(layerLocks->children[i].get());
      button->enabled = has;
      button->SetSelected(has && (*l)["locks"][keys[i]].Truthy());
    }
  }
  if (!has) return;
  if (!ui::IsCaptured(layerOpacity)) layerOpacity->Set(std::round((*l)["opacity"].Num(1) * 100));
  if (!ui::IsCaptured(layerFill)) layerFill->Set(std::round((*l)["fill"].Num(1) * 100));
  layerVisible->Set((*l)["visible"].Truthy(true));
  std::wstring blend = W((*l)["blend"].Str());
  for (size_t i = 0; i < BlendNames.size(); i++)
    if (_wcsicmp(BlendNames[i].c_str(), blend.c_str()) == 0) layerBlend->index = int(i);
}

// ----------------------------------------------------------------- the options dock --
std::vector<Widget*> optionWidgets;
TextButton* docPill = nullptr;
std::unique_ptr<Menu> docMenu;

void RebuildOptions() {
  if (!optionsDock) return;
  // Keep the document pill (child 0); the rest is the tool's.
  while (optionsDock->children.size() > 1) optionsDock->children.pop_back();
  optionWidgets.clear();
  auto slider = [&](const wchar_t* label, double lo, double hi, double v, int dec, const wchar_t* unit, std::function<void(double)> change, bool log = false) {
    auto* s = optionsDock->Add<Slider>(label, lo, hi, v, dec, unit);
    s->compact = true;
    s->labelWidth = Measure(label, F_Label) + 12;
    s->logScale = log;
    s->onChange = std::move(change);
    optionWidgets.push_back(s);
    return s;
  };
  if (CanvasTool* m = Mode()) {
    if (m->Options(*optionsDock, optionWidgets)) return;
  }
  if (!current) return;
  if (current->impl && current->impl->Options(*optionsDock, optionWidgets)) return;
  const std::string& k = current->key;
  bool brushLike = dynamic_cast<BrushTool*>(current->impl.get()) || dynamic_cast<PathStrokeTool*>(current->impl.get()) || k == "quickSelection";
  if (brushLike) {
    slider(L"Size", 1, 2500, brush.size, 0, L" px", [](double v) { brush.size = v; }, true);
    if (k != "quickSelection") {
      slider(L"Hardness", 0, 100, brush.hardness, 0, L"%", [](double v) { brush.hardness = v; });
      slider(L"Opacity", 1, 100, brush.opacity, 0, L"%", [](double v) { brush.opacity = v; });
      if (k == "brush" || k == "eraser") slider(L"Flow", 1, 100, brush.flow, 0, L"%", [](double v) { brush.flow = v; });
      if (k == "brush" || k == "eraser" || k == "pencil") slider(L"Smoothing", 0, 100, brush.smoothing, 0, L"%", [](double v) { brush.smoothing = v; });
    }
  }
  if (k == "rectMarquee" || k == "ellipseMarquee" || k == "lasso" || k == "polygonLasso" || k == "magicWand") {
    auto* s = optionsDock->Add<Segmented>(std::vector<std::wstring>{L"New", L"Add", L"Subtract", L"Intersect"}, sel.mode);
    s->onChange = [](int i) { sel.mode = i; };
    optionWidgets.push_back(s);
    if (k == "rectMarquee" || k == "ellipseMarquee") slider(L"Feather", 0, 250, sel.feather, 0, L" px", [](double v) { sel.feather = v; });
  }
  if (k == "magicWand" || k == "paintBucket" || k == "magicEraser") {
    slider(L"Tolerance", 0, 255, sel.tolerance, 0, L"", [](double v) { sel.tolerance = v; });
    auto* t = optionsDock->Add<Toggle>(L"Contiguous", sel.contiguous);
    t->onChange = [](bool v) { sel.contiguous = v; };
    optionWidgets.push_back(t);
  }
  if (k == "magicWand" || k == "lasso" || k == "polygonLasso" || k == "ellipseMarquee" || k == "quickSelection" || k == "objectSelection") {
    auto* aa = optionsDock->Add<Toggle>(L"Anti-alias",sel.antiAlias); aa->onChange = [](bool on) { sel.antiAlias = on; }; optionWidgets.push_back(aa);
  }
  if (k == "magicWand" || k == "quickSelection" || k == "objectSelection") {
    auto* all = optionsDock->Add<Toggle>(L"Sample All Layers",sel.sampleAllLayers); all->onChange = [](bool on) { sel.sampleAllLayers = on; }; optionWidgets.push_back(all);
    if (k == "magicWand") {
      auto* sizes = optionsDock->Add<SelectList>(std::vector<std::wstring>{L"Point Sample",L"3 × 3",L"5 × 5",L"11 × 11",L"31 × 31",L"51 × 51",L"101 × 101"},0);
      const int values[] = {1,3,5,11,31,51,101}; for (int i = 0; i < 7; i++) if (sel.sampleSize == values[i]) sizes->index = i;
      sizes->onChange = [](int i) { const int values[] = {1,3,5,11,31,51,101}; sel.sampleSize = values[std::clamp(i,0,6)]; }; optionWidgets.push_back(sizes);
    }
    optionWidgets.push_back(optionsDock->Add<TextButton>(L"Select Subject",TextButton::Secondary,[] { Exec("select.subject"); }));
    optionWidgets.push_back(optionsDock->Add<TextButton>(L"Select and Mask…",TextButton::Secondary,[] { RunCommand("select.refineEdge",L"Select and Mask…"); }));
  }
  if (k == "gradient") {
    auto* s = optionsDock->Add<SelectList>(std::vector<std::wstring>{L"Linear", L"Radial", L"Angle", L"Reflected", L"Diamond"}, gradientStyle);
    s->onChange = [](int i) { gradientStyle = i; };
    optionWidgets.push_back(s);
  }
  if (k == "type") slider(L"Size", 4, 600, typeSize, 0, L" pt", [](double v) { typeSize = v; }, true);
  if (k == "roundedRectangle") slider(L"Radius", 0, 500, shapeRadius, 0, L" px", [](double v) { shapeRadius = v; });
  if (k == "hand" || k == "zoom" || k == "move") {
    optionWidgets.push_back(optionsDock->Add<TextButton>(L"Fit", TextButton::Secondary, [] { canvas->Fit(true); }));
    optionWidgets.push_back(optionsDock->Add<TextButton>(L"100%", TextButton::Secondary, [] { canvas->ActualPixels(true); }));
  }
  if (optionWidgets.empty()) optionWidgets.push_back(optionsDock->Add<Label>(current->name, F_Label));
}

float OptionWidth(Widget* w) {
  if (w->NaturalWidth() > 0) return w->NaturalWidth();
  if (dynamic_cast<IconButton*>(w)) return 36;
  if (auto* n = dynamic_cast<NumberField*>(w)) return n->Natural();
  if (auto* c = dynamic_cast<ColorField*>(w)) return c->labelWidth + 92;
  if (auto* s = dynamic_cast<Slider*>(w)) return s->labelWidth + 100 + s->ValueColumn();
  if (auto* s = dynamic_cast<Segmented*>(w)) return 72.f * float(s->options.size());
  if (auto* t = dynamic_cast<Toggle*>(w)) return Measure(t->label, F_Label) + 54;
  if (auto* s = dynamic_cast<SelectList*>(w)) {  // the longest option plus the field's padding and chevron, so no choice is cut off
    float widest = 0; for (auto& o : s->options) widest = std::max(widest, Measure(o, F_Row));
    return std::clamp(widest + 48, 130.f, 240.f);
  }
  if (auto* t = dynamic_cast<TextButton*>(w)) return t->Natural();
  if (auto* l = dynamic_cast<Label*>(w)) return Measure(l->text, l->face) + 8;
  return 100;
}

// ------------------------------------------------------------------------- type input --
// A small popover for the text of a new type layer: the one place the shell takes typing.
class TypePopover : public Popover {
 public:
  std::wstring text;
  double x = 0, y = 0;
  std::wstring title = L"New text layer", hint = L"Enter to place  ·  Esc to cancel";
  std::function<void(const std::wstring&)> onEnter;  // null: a type layer at (x, y)
  TypePopover() { modal = true; }
  D2D1_SIZE_F Natural() override { return {380, 128}; }
  float CornerRadius() const override { return tok::RadiusLarge; }
  void PaintContent() override {
    Text(title, R(target.left + 20, target.top + 14, target.right - 20, target.top + 40), F_Title, P.text);
    Rect field = R(target.left + 20, target.top + 50, target.right - 20, target.top + 86);
    FillRound(field, 11, Rgba(1, 1, 1, .08f));
    StrokeRound(field, 11, Rgba(1, .93f, .81f, .30f), 1);
    Text(text.empty() ? L"Type, then press Enter" : text, Inset(field, 12, 0), F_Row, text.empty() ? P.faint : P.text);
    float cw = Measure(text, F_Row);
    if (fmod(ui::NowMs(), 1000) < 600) Line(field.left + 13 + cw, field.top + 9, field.left + 13 + cw, field.bottom - 9, P.accent, 1.4f);
    Text(hint, R(target.left + 20, target.top + 92, target.right - 20, target.bottom - 8), F_Small, P.faint);
  }
  bool Animate(float dt) override { return Popover::Animate(dt) || open; }
  bool OnKey(UINT vk, Mods) override {
    if (vk == VK_ESCAPE) { Close(); return true; }
    if (vk == VK_RETURN) {
      std::wstring t = text;
      Close();
      if (onEnter) {
        onEnter(t);
      } else if (!t.empty()) {
        JsonObject o;
        o.Num("x", x).Num("y", y).Str("text", Narrow(t)).Num("size", typeSize).Str("color", Hex(sessionInfo["foreground"]));
        if (typeVertical) o.Str("orientation", "vertical");
        Exec("type.create", o.Text());
      }
      return true;
    }
    if (vk == VK_BACK && !text.empty()) text.pop_back();
    return true;
  }
  void Char(wchar_t c) {
    if (c >= 32 && c != 127) text += c;
  }
};
std::unique_ptr<TypePopover> typePopover;
// A line of text asked for at document point (x, y).
void AskText(double x, double y, std::wstring title, std::wstring initial, std::function<void(const std::wstring&)> done) {
  typePopover = std::make_unique<TypePopover>();
  typePopover->x = x;
  typePopover->y = y;
  typePopover->title = std::move(title);
  typePopover->text = std::move(initial);
  typePopover->hint = L"Enter to keep  ·  Esc to cancel";
  typePopover->onEnter = std::move(done);
  auto s = canvas->ToScreen(x, y);
  typePopover->Open(RWH(s.x - 10, s.y - 10, 20, 20), Side::Below);
}
D2D1_COLOR_F HexColor(const std::string& h, D2D1_COLOR_F fallback) {
  unsigned r = 0, g = 0, b = 0;
  if (h.size() != 7 || h[0] != '#' || sscanf_s(h.c_str() + 1, "%02x%02x%02x", &r, &g, &b) != 3) return fallback;
  return Rgba(r / 255.f, g / 255.f, b / 255.f, 1);
}
// The note whose icon is under screen point (sx, sy), or -1.
constexpr float NoteIcon = 22;
int NoteAt(float sx, float sy) {
  const Json& notes = marks["notes"];
  for (size_t i = notes.size(); i-- > 0;) {
    const Json& pos = notes[i]["position"];
    if (pos.size() < 2) continue;
    auto a = canvas->ToScreen(pos[0].Num(), pos[1].Num());
    if (sx >= a.x && sx <= a.x + NoteIcon && sy >= a.y && sy <= a.y + NoteIcon) return int(i);
  }
  return -1;
}
// The Note tool: a click on a note edits its text (Alt deletes it), elsewhere adds one there.
void NoteClick(double x, double y, const PointerEvent& e) {
  int at = NoteAt(e.x, e.y);
  if (at >= 0) {
    if (e.mods.alt) {
      Exec("notes.delete", "{\"index\":" + std::to_string(at) + "}");
      return;
    }
    AskText(x, y, L"Note", W(marks["notes"][size_t(at)]["text"].Str()), [at](const std::wstring& t) {
      JsonObject o;
      o.Num("index", at).Str("text", Narrow(t));
      Exec("notes.set", o.Text());
    });
    return;
  }
  double nx = std::round(x), ny = std::round(y);
  AskText(x, y, L"New note", L"", [nx, ny](const std::wstring& t) {
    JsonObject o;
    o.Num("x", nx).Num("y", ny).Str("text", Narrow(t));
    Exec("notes.add", o.Text());
  });
}
void TypeAt(double x, double y) {
  typePopover = std::make_unique<TypePopover>();
  typePopover->x = std::round(x);
  typePopover->y = std::round(y);
  auto s = canvas->ToScreen(x, y);
  typePopover->Open(RWH(s.x - 10, s.y - 10, 20, 20), Side::Below);
}

// ----------------------------------------------------------------------- the overlay --
// Selection ants live above the document and below every dock.
class SelectionOverlay : public Widget {
 public:
  void Paint() override {
    PaintMarks();
    PaintAnts();
    if (ui::NowMs() < sizeHudUntil && pointerX >= 0) {
      float w = Measure(sizeHud, F_Label) + 20;
      Rect r = RWH(pointerX + 16, pointerY + 16, w, 26);
      FillRound(r, 9, Rgba(.09f, .09f, .1f, .9f));
      StrokeRound(r, 9, Rgba(1, .93f, .81f, .25f), 1);
      Text(sizeHud, Inset(r, 10, 0), F_Label, P.text);
    }
  }
  // Count markers (numbered, in their group's colour) and note icons, as Photoshop shows them.
  void PaintMarks() {
    if (!canvas->HasDocument() || marks.IsNull()) return;
    const Json& groups = marks["counts"];
    for (size_t g = 0; g < groups.size(); g++) {
      const Json& grp = groups[g];
      if (!grp["visible"].Truthy(true)) continue;
      D2D1_COLOR_F c = HexColor(grp["color"].Str(), Rgba(1, 0, 0, 1));
      float r = 2.5f + float(grp["markerSize"].Num(1));
      const Json& pts = grp["points"];
      for (size_t i = 0; i < pts.size(); i++) {
        auto s = canvas->ToScreen(pts[i][0].Num(), pts[i][1].Num());
        FillEllipse(s.x, s.y, r + 1.2f, r + 1.2f, Rgba(0, 0, 0, .55f));
        FillEllipse(s.x, s.y, r, r, c);
        std::wstring n = std::to_wstring(i + 1);
        Rect t = R(s.x + r + 3, s.y - r - 14, s.x + r + 60, s.y - r + 4);
        Text(n, R(t.left + 1, t.top + 1, t.right + 1, t.bottom + 1), F_Label, Rgba(0, 0, 0, .7f));
        Text(n, t, F_Label, c);
      }
    }
    const Json& notes = marks["notes"];
    int hot = canvas->PointerOver() ? NoteAt(pointerX, pointerY) : -1;
    for (size_t i = 0; i < notes.size(); i++) {
      const Json& pos = notes[i]["position"];
      if (pos.size() < 2) continue;
      auto a = canvas->ToScreen(pos[0].Num(), pos[1].Num());
      Rect box = RWH(a.x, a.y, NoteIcon, NoteIcon);
      FillRound(Inset(box, -1.5f, -1.5f), 6, Rgba(0, 0, 0, .5f));
      FillRound(box, 5, HexColor(notes[i]["color"].Str(), Rgba(1, 1, .51f, 1)));
      Icon(PhNote, Inset(box, 3, 3), Rgba(.12f, .1f, .06f, 1));
      std::wstring text = W(notes[i]["text"].Str());
      if (int(i) == hot && !text.empty()) {
        float w = std::min(320.f, Measure(text, F_Row) + 24);
        Rect tip = RWH(box.right + 8, box.top, w, 32);
        FillRound(tip, 10, Rgba(.09f, .09f, .1f, .92f));
        StrokeRound(tip, 10, Rgba(1, .93f, .81f, .25f), 1);
        Text(text, Inset(tip, 12, 0), F_Row, P.text);
      }
    }
  }
  void PaintAnts() {
    if (!canvas->HasDocument() || !docInfo["hasSelection"].Truthy()) return;
    const Json& b = docInfo["selectionBounds"];
    if (b.size() < 4) return;
    std::vector<int> now = {int(b[0].Int()), int(b[1].Int()), int(b[2].Int()), int(b[3].Int())};
    bool ours = shape.kind != SelectionShape::None && shape.bounds == now;
    if (ours && shape.kind == SelectionShape::EllipseK) {
      AntsDocumentBox(shape.x0, shape.y0, shape.x1, shape.y1, true);
    } else if (ours && shape.kind == SelectionShape::PolyK) {
      std::vector<D2D_POINT_2F> s;
      for (auto& p : shape.pts) s.push_back(canvas->ToScreen(p.x, p.y));
      AntsPoly(s, true);
    } else {
      AntsDocumentBox(b[0].Num(), b[1].Num(), b[0].Num() + b[2].Num(), b[1].Num() + b[3].Num(), false);
    }
  }
  bool Animate(float) override { return (canvas->HasDocument() && docInfo["hasSelection"].Truthy()) || ui::NowMs() < sizeHudUntil + 50; }
  Widget* HitTest(float, float) override { return nullptr; }
  bool Interactive() const override { return false; }
};

// ============================================================================= build ==
std::wstring StatusText() {
  if (jobs::Busy() && jobs::BusyForMs() > 250) {
    std::wstring l = jobs::Label();
    return i18n::Tr(l.empty() ? std::wstring(L"Working") : l) + L"…";
  }
  if (docInfo.IsNull()) return L"PhotoCraft by Vetro  ·  drop a photo here or press Ctrl+O";
  wchar_t buf[256];
  swprintf(buf, 256, i18n::Tr(L"%.0f%%   ·   %ls %d-bit   ·   %d × %d px   ·   %d layers   ·   %ls%ls").c_str(), canvas->Zoom() * GfxDpi() * 100,
           W(docInfo["mode"].Str()).c_str(), int(docInfo["depth"].Int()), int(docInfo["width"].Int()), int(docInfo["height"].Int()),
           int(docInfo["layers"].size()), canvas->PathName().c_str(), GfxHasCanvasLut() ? i18n::Tr(L" · monitor profile").c_str() : L"");
  return buf;
}
bool Working() { return canvas->Busy() || (jobs::Busy() && jobs::BusyForMs() > 120); }

PanelDock* DockFor(const std::string& id) {
  if (id == "window.layers" || id == "window.history") return layersDock;
  if (id == "window.properties" || id == "window.adjustments") return propsDock;
  if (id == "window.tools") return toolsDock;
  if (id == "window.options") return optionsDock;
  return nullptr;
}

void BuildDocks() {
  // --- brand: a separate floating block (Tokyo Glass). It also moves the window.
  brandDock = new PanelDock();
  brandDock->draggable = false;
  brandDock->natural = [] { return D2D1_SIZE_F{Measure(L"PhotoCraft", F_Brand) + 62, 44}; };
  brandDock->home = [](const Rect& w, D2D1_SIZE_F s) { return RWH(w.left + tok::Margin, w.top + tok::Margin, s.width, s.height); };
  brandDock->body = [](PanelDock& d) {
    Icon(CuBrand, RWH(d.rect.left + 14, MidY(d.rect) - 10, 20, 20), P.text, true);
    Line(d.rect.left + 20.5f, MidY(d.rect) + 3.5f, d.rect.left + 27.5f, MidY(d.rect) - 3.5f, P.accent, 1.8f);
    Text(L"PhotoCraft", R(d.rect.left + 42, d.rect.top, d.rect.right - 10, d.rect.bottom), F_Brand, P.text);
  };

  // --- menus: the Photoshop menu tree from the engine's catalog.
  menuDock = new PanelDock();
  menuDock->revealDelay = .04f;
  for (auto& r : catalog)
    if (!r.path.empty() && std::find(menuNames.begin(), menuNames.end(), r.path[0]) == menuNames.end()) menuNames.push_back(r.path[0]);
  for (size_t i = 0; i < menuNames.size(); i++) {
    auto* t = menuDock->Add<TextButton>(W(menuNames[i]), TextButton::Quiet, nullptr);
    t->face = F_Menu;
    int index = int(i);
    t->onPressed = [index] {
      if (openMenu && openMenu->open && openMenuIndex == index) {
        openMenu->CloseTree();
        return;
      }
      OpenTopMenu(index);
    };
    t->onHoverEnter = [index] {
      if (openMenu && openMenu->open && openMenuIndex != index) OpenTopMenu(index);
    };
    menuTriggers.push_back(t);
  }
  menuDock->natural = [] {
    float w = 12;
    for (auto* t : menuTriggers) w += t->Natural() + 2;
    return D2D1_SIZE_F{w + 4, 44};
  };
  menuDock->home = [](const Rect& w, D2D1_SIZE_F s) { return RWH(brandDock->Target().right + 10, w.top + tok::Margin, s.width, s.height); };
  menuDock->arrange = [](PanelDock& d) {
    float x = d.rect.left + 8;
    for (auto* t : menuTriggers) {
      float tw = t->Natural();
      t->rect = R(x, d.rect.top + 6, x + tw, d.rect.bottom - 6);
      x += tw + 2;
    }
  };

  // --- system: window actions in their own compact dock at the edge.
  systemDock = new PanelDock();
  systemDock->draggable = false;
  systemDock->revealDelay = .04f;
  systemDock->Add<IconButton>(PhMinus, L"Minimise", [] { ShowWindow(hwnd, SW_MINIMIZE); });
  systemDock->Add<IconButton>(PhCornersOut, L"Maximise", [] { ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE); });
  systemDock->Add<IconButton>(PhX, L"Close", [] { PostMessage(hwnd, WM_CLOSE, 0, 0); });
  systemDock->natural = [] { return D2D1_SIZE_F{3 * 36 + 16, 44}; };
  systemDock->home = [](const Rect& w, D2D1_SIZE_F s) { return RWH(w.right - tok::Margin - s.width, w.top + tok::Margin, s.width, s.height); };
  systemDock->arrange = [](PanelDock& d) {
    float x = d.rect.left + 8;
    for (size_t i = 0; i < d.children.size(); i++) {
      d.children[i]->rect = RWH(x, d.rect.top + 4, 36, 36);
      if (i == 1) static_cast<IconButton*>(d.children[i].get())->icon = IsZoomed(hwnd) ? PhCornersIn : PhCornersOut;
      x += 36;
    }
  };

  // --- options: the document pill, then the active tool's parameters.
  optionsDock = new PanelDock();
  optionsDock->revealDelay = .08f;
  docPill = optionsDock->Add<TextButton>(L"No document", TextButton::Quiet, nullptr);
  docPill->face = F_Row;
  docPill->tooltip = L"Open documents";
  docPill->onClick = [] {
    std::vector<MenuItem> rows;
    const Json& docs = sessionInfo["documents"];
    for (size_t i = 0; i < docs.size(); i++) {
      MenuItem m;
      m.label = W(docs[i]["name"].Str()) + (docs[i]["dirty"].Truthy() ? L"  •" : L"");
      m.checked = int64_t(i) == sessionInfo["active"].Int(-1);
      uint64_t id = uint64_t(docs[i]["id"].Int());
      m.action = [id] {
        SwitchDocument(id);
      };
      rows.push_back(std::move(m));
    }
    if (!rows.empty()) rows.push_back(MenuItem::Separator());
    MenuItem open;
    open.label = L"Open…";
    open.icon = PhFolderOpen;
    open.shortcut = L"Ctrl+O";
    open.action = [] { OpenFile(); };
    rows.push_back(open);
    if (docs.size() > 0) {
      MenuItem save;
      save.label = L"Save";
      save.icon = PhFloppyDisk;
      save.shortcut = L"Ctrl+S";
      save.action = [] { SaveDocument(false); };
      rows.push_back(save);
      MenuItem close;
      close.label = L"Close";
      close.icon = PhX;
      close.action = [] { RunCommand("file.close", L"Close"); };
      rows.push_back(close);
    }
    docMenu = std::make_unique<Menu>(std::move(rows));
    docMenu->minWidth = 240;
    docMenu->Open(docPill->rect, Side::Below);
  };
  optionsDock->natural = [] {
    float limit = std::max(220.f, W(ui::Bounds()) - menuDock->Target().left - tok::Margin);
    float row = std::min(docPill->Natural(), limit - 20) + 20, widest = row;
    int rows = 1;
    for (auto* o : optionWidgets) {
      float width = std::min(OptionWidth(o), limit - 20) + 14;
      if (row + width > limit) { widest = std::max(widest, row); row = 12; rows++; }
      row += width;
    }
    return D2D1_SIZE_F{std::min(limit, std::max(widest, row)), 12.f + 40.f * rows - 8};
  };
  optionsDock->home = [](const Rect& w, D2D1_SIZE_F s) {
    float x = menuDock->Target().right + 10;
    float y = w.top + tok::Margin;
    if (x + s.width > systemDock->Target().left - 10) {
      x = menuDock->Target().left;
      y = menuDock->Target().bottom + 10;
    }
    return RWH(x, y, s.width, s.height);
  };
  optionsDock->arrange = [](PanelDock& d) {
    float x = d.rect.left + 6;
    float y = d.rect.top + 6;
    float pw = std::min(docPill->Natural(), W(d.rect) - 20);
    docPill->rect = RWH(x, y, pw, 32);
    x += pw + 14;
    for (auto* o : optionWidgets) {
      float w = std::min(OptionWidth(o), W(d.rect) - 20);
      if (x + w > d.rect.right - 6) { x = d.rect.left + 6; y += 40; }
      float h = dynamic_cast<Segmented*>(o) ? 28.f : 30.f;
      o->rect = RWH(x, y + (32 - h) / 2, w, h);
      x += w + 14;
    }
  };
  optionsDock->body = [](PanelDock& d) {
    float x = docPill->rect.right + 7;
    Line(x, d.rect.top + 12, x, d.rect.bottom - 12, P.sep, 1);
  };

  // --- tools: vertical, the toolbar's flyout slots.
  toolsDock = new PanelDock();
  toolsDock->revealDelay = .12f;
  size_t flat = 0;
  for (size_t s = 0; s < sections.size(); s++) {
    for (size_t k = 0; k < sections[s].size(); k++, flat++) {
      size_t slotIndex = flat;
      auto* b = toolsDock->Add<IconButton>(nullptr, L"", [slotIndex] { SelectTool(&SlotTool(slotIndex)); });
      b->onDoubleClick = [slotIndex] {
        auto& t = SlotTool(slotIndex);
        SelectTool(&t);
        if (t.key == "hand" || t.key == "rotateView") { canvas->RotateTo(0); canvas->Fit(true); }
        if (t.key == "zoom") canvas->ActualPixels(true);
      };
      b->iconSize = 20;
      b->flyout = sections[s][k].size() > 1;
      b->onLongPress = [s, k, b] {
        // The flyout: every tool of the slot, grown out of its button.
        auto& sl = sections[s][k];
        if (sl.size() < 2) return;
        std::vector<MenuItem> rows;
        for (auto& t : sl) {
          MenuItem m;
          m.label = t.name;
          m.icon = t.icon;
          m.iconStroked = t.stroked;
          m.shortcut = t.letter ? std::wstring(1, t.letter) : L"";
          m.checked = &t == current;
          m.enabled = t.available;
          ToolDef* tp = &t;
          m.action = [tp] { SelectTool(tp); };
          rows.push_back(std::move(m));
        }
        flyout = std::make_unique<Menu>(std::move(rows));
        flyout->minWidth = 240;
        flyout->Open(b->rect, Side::Right);
      };
      toolButtons.push_back(b);
    }
  }
  auto* sw = toolsDock->Add<Swatches>();
  sw->tooltip = L"Foreground / background  (X swaps, D resets)";
  toolsDock->natural = [] {
    float h = 10 + float(toolButtons.size()) * 38 + float(sections.size() - 1) * 9 + 56;
    return D2D1_SIZE_F{52, h};
  };
  toolsDock->home = [](const Rect& w, D2D1_SIZE_F s) {
    float top = std::max(optionsDock->Target().bottom, menuDock->Target().bottom) + 14;
    float y = std::max(top, MidY(w) - s.height / 2);
    return RWH(w.left + tok::Margin, y, s.width, s.height);
  };
  toolsDock->arrange = [](PanelDock& d) {
    float y = d.rect.top + 7;
    size_t i = 0;
    for (size_t s = 0; s < sections.size(); s++) {
      if (s > 0) y += 9;
      for (size_t k = 0; k < sections[s].size(); k++, i++) {
        toolButtons[i]->rect = RWH(d.rect.left + 7, y, 38, 38);
        y += 38;
      }
    }
    d.children.back()->rect = RWH(d.rect.left + 7, y + 10, 40, 40);
  };
  toolsDock->body = [](PanelDock& d) {
    float y = d.rect.top + 7;
    for (size_t s = 0; s < sections.size(); s++) {
      if (s > 0) {
        Line(d.rect.left + 15, y + 4.5f, d.rect.right - 15, y + 4.5f, P.sep, 1);
        y += 9;
      }
      y += 38 * float(sections[s].size());
    }
    Line(d.rect.left + 15, y + 5, d.rect.right - 15, y + 5, P.sep, 1);
  };

  // --- properties / adjustments.
  propsDock = new PanelDock();
  propsDock->revealDelay = .16f;
  propsTabs = propsDock->Add<Tabs>(std::vector<std::wstring>{L"Properties", L"Adjustments"}, 0);
  propsBody = propsDock->Add<Container>();
  adjustBody = propsDock->Add<Container>();
  propsTabs->onChange = [](int i) {
    propsBody->visible = i == 0;
    adjustBody->visible = i == 1;
  };
  adjustBody->visible = false;
  layerName = propsBody->Add<Label>(L"No layer", F_Title);
  layerName->colour = P.text;
  layerKind = propsBody->Add<Label>(L"Open a photo to begin", F_Small);
  layerKind->colour = P.faint;
  layerBlend = propsBody->Add<SelectList>(BlendNames, 0);
  layerBlend->label = L"Blend";
  layerBlend->labelWidth = 64;
  layerBlend->onChange = [](int i) {
    JsonObject o;
    o.Str("blend", Narrow(BlendNames[size_t(i)]));
    Exec("layer.setProps", o.Text());
  };
  layerOpacity = propsBody->Add<Slider>(L"Opacity", 0, 100, 100, 0, L"%");
  layerOpacity->compact = true;
  layerOpacity->labelWidth = 64;
  layerOpacity->onCommit = [](double v) {
    JsonObject o;
    o.Num("opacity", v / 100);
    Exec("layer.setProps", o.Text());
  };
  layerFill = propsBody->Add<Slider>(L"Fill", 0, 100, 100, 0, L"%");
  layerFill->compact = true;
  layerFill->labelWidth = 64;
  layerFill->onCommit = [](double v) {
    JsonObject o;
    o.Num("fill", v / 100);
    Exec("layer.setProps", o.Text());
  };
  layerVisible = propsBody->Add<Toggle>(L"Visible", true);
  shapeProperties = BuildShapeProperties(*propsBody);
  adjustmentProperties = BuildAdjustmentProperties(*propsBody);
  documentProperties = propsBody->Add<ScrollArea>();
  documentProperties->visible = false;
  const char* documentKeys[] = {"width", "height", "resolution"};
  for (auto key : documentKeys) {
    auto* n = documentProperties->Add<NumberField>(HumanKey(key), 1, 1, std::string(key)=="resolution"?10000:32768, 0, std::string(key)=="resolution"?L" ppi":L" px");
    n->onCommit = [key](double value) {
      JsonObject p; p.Num(key,value);
      if (std::string(key)=="resolution") p.Str("resample","none");
      Exec("image.imageSize",p.Text());
    };
    documentFields.push_back(n);
  }
  documentRulers = documentProperties->Add<Toggle>(L"Rulers",false);
  documentRulers->onChange=[](bool on) { if (on!=GuideState()["rulers"].Truthy()) GuideCommand("view.rulers"); };
  documentGrid = documentProperties->Add<Toggle>(L"Grid",false);
  documentGrid->onChange=[](bool on) { if (on!=GuideState()["grid"].Truthy()) GuideCommand("view.show.grid"); };
  for (auto [label,id] : {std::pair{L"Canvas Size…","image.canvasSize"}, {L"Image Size…","image.imageSize"}, {L"New Guide Layout…","view.newGuideLayout"}})
    documentProperties->Add<TextButton>(label,TextButton::Secondary,[id,label] { if(!GuideCommand(id)) OpenDialog(id,label,propsDock->rect); });
  layerVisible->onChange = [](bool v) {
    JsonObject o;
    o.Bool("visible", v);
    Exec("layer.setProps", o.Text());
  };
  struct Adj {
    const char* id;
    const wchar_t* label;
    const wchar_t* icon;
  };
  static const Adj adjustments[] = {
      {"brightnessContrast", L"Brightness/Contrast…", PhSun}, {"levels", L"Levels…", PhFaders},
      {"exposure", L"Exposure…", PhAperture},                 {"vibrance", L"Vibrance…", PhSparkle},
      {"hueSaturation", L"Hue/Saturation…", PhPalette},       {"colorBalance", L"Color Balance…", PhSlidersHorizontal},
      {"blackWhite", L"Black & White…", PhCircleHalf},        {"photoFilter", L"Photo Filter…", PhAperture},
      {"invert", L"Invert", PhSquareHalf},                         {"posterize", L"Posterize…", PhSquaresFour},
      {"threshold", L"Threshold…", PhDropHalf},               {"selectiveColor", L"Selective Color…", PhDrop},
      {"colorLookup", L"Color Lookup…", PhGridFour},          {"channelMixer", L"Channel Mixer…", PhStack},
      {"curves", L"Curves…", PhPath},
  };
  for (auto& a : adjustments) {
    std::string id = std::string("layer.newAdjustmentLayer.") + a.id;
    std::wstring label = a.label;
    auto* b = adjustBody->Add<IconButton>(a.icon, label, nullptr);
    b->onClick = [id, label, b] {
      if (ParseParams(paramsDoc[id]).empty()) Exec(id);
      else OpenDialog(id, label, b->rect);
    };
  }
  propsDock->natural = [] { return D2D1_SIZE_F{304, 246.f + (((shapeProperties && shapeProperties->visible) || (adjustmentProperties && adjustmentProperties->visible)) && propsTabs->index == 0 ? 300.f : documentProperties && documentProperties->visible && propsTabs->index==0 ? 180.f : 0.f)}; };
  propsDock->home = [](const Rect& w, D2D1_SIZE_F s) {
    float top = systemDock->Target().bottom + 12;
    if (optionsDock->Target().right > w.right - 330) top = std::max(top, optionsDock->Target().bottom + 12);
    return RWH(w.right - tok::Margin - s.width, top, s.width, s.height);
  };
  propsDock->arrange = [](PanelDock& d) {
    Rect r = d.rect;
    propsTabs->rect = R(r.left + 12, r.top + 10, r.right - 12, r.top + 40);
    float x0 = r.left + 18, x1 = r.right - 18, y = r.top + 56;
    layerName->rect = R(x0, y, x1, y + 24);
    layerKind->rect = R(x0, y + 22, x1, y + 38);
    y += 48;
    layerBlend->rect = R(x0, y, x1, y + 32);
    y += 42;
    layerOpacity->rect = R(x0, y, x1, y + 28);
    y += 32;
    layerFill->rect = R(x0, y, x1, y + 28);
    y += 32;
    layerVisible->rect = R(x0, y, x1, y + 28);
    LayoutShapeProperties(R(x0,y + 36,x1,r.bottom - 14));
    LayoutAdjustmentProperties(R(x0,y + 36,x1,r.bottom - 14));
    if (documentProperties) {
      if (documentProperties->visible) {
        documentRulers->Set(GuideState()["rulers"].Truthy());
        documentGrid->Set(GuideState()["grid"].Truthy());
      }
      documentProperties->rect=R(x0,r.top+104,x1,r.bottom-14);
      float dy=documentProperties->rect.top-documentProperties->Scroll();
      for(auto& c:documentProperties->children) { c->rect=R(x0,dy,x1-6,dy+30);dy+=36; }
      documentProperties->contentHeight=float(documentProperties->children.size())*36;
    }
    float bx = r.left + 16, by = r.top + 58;
    int col = 0;
    for (auto& c : adjustBody->children) {
      c->rect = RWH(bx + float(col) * 55.f, by, 46, 46);
      if (++col == 5) { col = 0; by += 56; }
    }
  };
  propsDock->body = [](PanelDock& d) { Line(d.rect.left + 16, d.rect.top + 48, d.rect.right - 16, d.rect.top + 48, P.sep, 1); };

  // --- layers / history.
  layersDock = new PanelDock();
  layersDock->revealDelay = .2f;
  layersTabs = layersDock->Add<Tabs>(std::vector<std::wstring>{L"Layers", L"History"}, 0);
  layerList = layersDock->Add<ScrollArea>();
  historyList = layersDock->Add<ScrollArea>();
  historyList->visible = false;
  layerFooter = layersDock->Add<Container>();
  layerLocks = layersDock->Add<Container>();
  const wchar_t* lockIcons[] = {PhSquareHalf, PhPaintBrush, PhArrowsOutCardinal, PhFrameCorners, PhLockSimple};
  int lockIndex = 0;
  for (auto key : {"transparency", "pixels", "position", "artboard", "all"}) {
    layerLocks->Add<IconButton>(lockIcons[lockIndex++], HumanKey(key), [key] {
      const auto* layer = ActiveLayer();
      if (layer) Exec("layer.lockLayers", JsonObject().Bool(key, !(*layer)["locks"][key].Truthy()).Text());
    });
  }
  layersTabs->onChange = [](int i) {
    layerList->visible = i == 0;
    historyList->visible = i == 1;
    layerFooter->visible = i == 0;
    layerLocks->visible = i == 0;
  };
  auto action = [](const wchar_t* icon, const wchar_t* tip, std::function<void()> f, bool stroked = false) {
    auto* b = layerFooter->Add<IconButton>(icon, tip, std::move(f));
    b->iconSize = 18;
    b->stroked = stroked;
    layerActions.push_back(b);
    return b;
  };
  action(CuFx, L"Layer style", [] { RunCommand("layer.layerStyle.blendingOptions", L"Blending Options…"); }, true);
  action(CuQuickMask, L"Add layer mask", [] { Exec("layer.layerMask.revealAll"); }, true);
  action(PhCircleHalf, L"New adjustment layer", [] {
    propsDock->visible = true;
    propsTabs->index = 1;
    propsTabs->onChange(1);
  });
  action(PhFolderSimple, L"New group", [] { Exec("layer.new.group"); });
  newLayerAction = action(PhPlus, L"New layer", [] { Exec("layer.new.layer"); });
  deleteLayerAction = action(PhTrash, L"Delete layer", [] { Exec("layer.delete"); });
  layersDock->natural = [] {
    Rect w = ui::Bounds();
    float top = propsDock->visible ? propsDock->Target().bottom + 12 : systemDock->Target().bottom + 12;
    return D2D1_SIZE_F{304, std::max(220.f, w.bottom - 64 - top)};
  };
  layersDock->home = [](const Rect& w, D2D1_SIZE_F s) {
    float top = propsDock->visible ? propsDock->Target().bottom + 12 : systemDock->Target().bottom + 12;
    return RWH(w.right - tok::Margin - s.width, top, s.width, s.height);
  };
  layersDock->arrange = [](PanelDock& d) {
    Rect r = d.rect;
    layersTabs->rect = R(r.left + 12, r.top + 10, r.right - 12, r.top + 40);
    layerLocks->rect = R(r.left + 12, r.top + 52, r.right - 12, r.top + 82);
    for (size_t i = 0; i < layerLocks->children.size(); i++) {
      auto* button = static_cast<IconButton*>(layerLocks->children[i].get());
      button->rect = RWH(layerLocks->rect.left + float(i) * W(layerLocks->rect) / 5, layerLocks->rect.top, W(layerLocks->rect) / 5, 28);
    }
    Rect list = R(r.left + 8, r.top + 90, r.right - 8, r.bottom - 54);
    layerList->rect = list;
    historyList->rect = R(list.left, list.top, list.right, r.bottom - 10);
    float y = list.top - layerList->Scroll();
    for (auto& c : layerList->children) {
      float h=LayerDetailHeight(c.get()); c->rect = R(list.left + 2, y, list.right - 6, y + h);
      y += h + 2;
    }
    layerList->contentHeight = y + layerList->Scroll() - list.top;
    float hy = historyList->rect.top - historyList->Scroll();
    for (auto& c : historyList->children) {
      c->rect = R(list.left + 2, hy, list.right - 6, hy + 32);
      hy += 34;
    }
    historyList->contentHeight = float(historyList->children.size()) * 34;
    layerFooter->rect = R(r.left + 8, r.bottom - 48, r.right - 8, r.bottom - 6);
    float step = W(layerFooter->rect) / float(layerActions.size());
    for (size_t i = 0; i < layerActions.size(); i++)
      layerActions[i]->rect = RWH(layerFooter->rect.left + step * float(i) + (step - 38) / 2, layerFooter->rect.top + 2, 38, 38);
  };
  layersDock->body = [](PanelDock& d) {
    Line(d.rect.left + 16, d.rect.top + 48, d.rect.right - 16, d.rect.top + 48, P.sep, 1);
    if (layerFooter->visible) Line(d.rect.left + 16, d.rect.bottom - 52, d.rect.right - 16, d.rect.bottom - 52, P.sep, 1);
    if (layerList->visible && layerList->children.empty()) Text(L"No layers yet", R(layerList->rect.left, layerList->rect.top + 10, layerList->rect.right, layerList->rect.top + 40), F_Label, P.faint, Align::Center);
  };

  // --- status: a short capsule, bottom centre.
  statusDock = new PanelDock();
  statusDock->radius = 17;
  statusDock->revealDelay = .24f;
  statusDock->natural = [] { return D2D1_SIZE_F{Measure(StatusText(), F_Small) + 44 + (Working() ? 24.f : 0.f), 34}; };
  statusDock->home = [](const Rect& w, D2D1_SIZE_F s) { return RWH(MidX(w) - s.width / 2, w.bottom - tok::Margin - s.height, s.width, s.height); };
  statusDock->body = [](PanelDock& d) {
    Rect r = d.rect;
    if (Working()) {
      // Working: three vermilion beads breathing in turn.
      float t = float(fmod(ui::NowMs() / 1000.0, 1.0));
      for (int i = 0; i < 3; i++) {
        float a = .3f + .7f * float(std::max(0.0, std::sin((t - float(i) * .16f) * 6.2831853)));
        FillEllipse(r.left + 20 + float(i) * 7.f, MidY(r), 2.2f, 2.2f, Fade(P.accent, a));
      }
      r.left += 24;
    }
    Text(StatusText(), r, F_Small, P.dim, Align::Center);
  };

  // --- zoom: bottom left in a wide window (Tokyo Glass).
  zoomDock = new PanelDock();
  zoomDock->radius = 20;
  zoomDock->revealDelay = .26f;
  zoomDock->Add<IconButton>(PhMagnifyingGlassMinus, L"Zoom out  (Ctrl+-)", [] { RunCommand("view.zoomOut", L"Zoom Out"); });
  zoomDock->Add<IconButton>(PhCornersOut, L"Fit on screen  (Ctrl+0)", [] { canvas->Fit(true); });
  zoomDock->Add<IconButton>(PhMagnifyingGlassPlus, L"Zoom in  (Ctrl+=)", [] { RunCommand("view.zoomIn", L"Zoom In"); });
  zoomDock->natural = [] { return D2D1_SIZE_F{3 * 36 + 14, 40}; };
  zoomDock->home = [](const Rect& w, D2D1_SIZE_F s) { return RWH(w.left + tok::Margin, w.bottom - tok::Margin - s.height, s.width, s.height); };
  zoomDock->arrange = [](PanelDock& d) {
    float x = d.rect.left + 7;
    for (auto& c : d.children) {
      c->rect = RWH(x, d.rect.top + 2, 36, 36);
      x += 36;
    }
  };

  for (auto* d : {brandDock, menuDock, optionsDock, systemDock, toolsDock, propsDock, layersDock, statusDock, zoomDock}) ui::AddLayer(d);
  InitializeDocuments();
  InitializeToolPanels();
  InitializeLayerControls();
  InitializeRetouchPanel();
  InitializeTextPanels();
  InitializeWorkspacePanels();
  InitializeGuides();
}

// ============================================================================ modes ==
CanvasTool* modeTool = nullptr;
CanvasTool* Mode() { return modeTool; }
void EnterMode(CanvasTool* t) {
  if (modeTool && modeTool != t) modeTool->Deactivate();
  modeTool = t;
  canvas->tool = t;
  RebuildOptions();
}
void LeaveMode(CanvasTool* t) {
  if (modeTool != t) return;
  modeTool = nullptr;
  canvas->tool = current && !current->hand ? current->impl.get() : nullptr;
  RebuildOptions();
}

// ============================================================================ sync ==
// The colour table of the active document on this window's monitor, rebuilt when either changes.
uint64_t lutKey = ~0ull;
void UpdateLut(bool force) {
  uint64_t key = pc_display_key(engine);
  if (!force && key == lutKey) return;
  lutKey = key;
  constexpr UINT N = 33;
  std::vector<uint8_t> table(size_t(N) * N * N * 4);
  int r = pc_canvas_lut(engine, N, table.data(), table.size());
  wchar_t test[8] = {};
  if (GetEnvironmentVariableW(L"VETRO_LUT_TEST", test, 8) > 0) {
    // A check of the table's layout: red passes through, green and blue go dark. A picture that
    // keeps its red is read with red fastest, as the engine writes it.
    for (size_t i = 0; i + 3 < table.size(); i += 4) {
      size_t r = (i / 4) % N;
      table[i] = uint8_t(r * 255 / (N - 1));
      table[i + 1] = 0;
      table[i + 2] = 0;
      table[i + 3] = 255;
    }
    r = 2;
  }
  GfxSetCanvasLut(r == 2 ? table.data() : nullptr, N);
  canvas->Invalidate();
}

// The monitor the window is on: its ICC profile goes to the engine (Monitor Profile "auto").
HMONITOR displayMonitor = nullptr;
void UpdateDisplay() {
  HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
  if (!mon || mon == displayMonitor) return;
  displayMonitor = mon;
  MONITORINFOEXW mi{};
  mi.cbSize = sizeof mi;
  if (!GetMonitorInfoW(mon, &mi)) return;
  std::vector<uint8_t> icc;
  if (HDC dc = CreateDCW(L"DISPLAY", mi.szDevice, nullptr, nullptr)) {
    wchar_t path[MAX_PATH * 2] = {};
    DWORD n = ARRAYSIZE(path);
    if (GetICMProfileW(dc, &n, path)) {
      HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
      if (f != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size{};
        if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart < (64 << 20)) {
          icc.resize(size_t(size.QuadPart));
          DWORD got = 0;
          if (!ReadFile(f, icc.data(), DWORD(icc.size()), &got, nullptr) || got != icc.size()) icc.clear();
        }
        CloseHandle(f);
      }
    }
    DeleteDC(dc);
  }
  uint32_t id = 2166136261u;  // a stable id: FNV-1a of the device name
  for (const wchar_t* c = mi.szDevice; *c; c++) id = (id ^ uint32_t(*c)) * 16777619u;
  std::string name = Narrow(mi.szDevice);
  RECT r = mi.rcMonitor;
  jobs::Run([id, name, icc, r] { pc_set_display(engine, id, name.c_str(), icc.empty() ? nullptr : icc.data(), icc.size(), r.left, r.top, r.right - r.left, r.bottom - r.top); },
            [] { UpdateLut(true); });
}

void Refresh() {
  if (!canvas) return;
  // While the engine works, the panels keep what they show; the last job's completion refreshes.
  if (jobs::Busy()) {
    refreshWanted = true;
    return;
  }
  refreshWanted = false;
  sessionInfo = Json::Parse(TakeString(pc_session_info(engine)));
  static uint64_t shownDocument = 0;
  int64_t selected = sessionInfo["active"].Int(-1);
  uint64_t document = selected >= 0 ? uint64_t(sessionInfo["documents"][size_t(selected)]["id"].Int()) : 0;
  if (shownDocument != document) { shownDocument = document; canvas->Reload(); }
  else canvas->Sync();
  uint64_t rev = 0;
  int32_t dmg[4]{};
  pc_revision(engine, &rev, dmg);
  int64_t active = sessionInfo["active"].Int(-1);
  static uint64_t anchorDocument = 0;
  if (document != anchorDocument) { hasBrushAnchor = false; shape.kind = SelectionShape::None; anchorDocument = document; }
  uint64_t key = rev * 1315423911ull + document;
  if (key != infoRevision) {
    infoRevision = key;
    docInfo = Json::Parse(TakeString(pc_document_info(engine)));
    SyncGuidePreferences();
    marks = Json::Parse(TakeString(pc_marks(engine)));
    if (layerList) {
      if (!visibilityPainting && !layerDrag.pressed) RebuildLayers();
      RebuildHistory();
      SyncProperties();
    }
  }
  if (docPill) {
    std::wstring name = docInfo.IsNull() ? L"No document" : W(docInfo["name"].Str());
    const Json& docs = sessionInfo["documents"];
    if (!docInfo.IsNull() && active >= 0 && size_t(active) < docs.size() && docs[size_t(active)]["dirty"].Truthy()) name += L"  •";
    docPill->label = name;
  }
  if (documentTabs) documentTabs->Sync(sessionInfo);
  if (startScreen) { startScreen->visible = document == 0; startScreen->Sync(recentDocuments.paths); }
  UpdateLut(false);
  std::wstring title = docInfo.IsNull() ? L"PhotoCraft by Vetro" : W(docInfo["name"].Str()) + L" — PhotoCraft by Vetro";
  SetWindowTextW(hwnd, title.c_str());
  SyncWorkspacePanels();
  animating = true;
  ui::RequestFrame();
}

void Layout(bool snap) {
  Rect w = ui::Bounds();
  LayoutToolPanels(snap);
  LayoutLayerControls(snap);
  LayoutRetouchPanel(snap);
  LayoutTextPanels(snap);
  LayoutWorkspacePanels(snap);
  // Order matters: later docks are placed against earlier ones.
  for (auto* d : {brandDock, menuDock, systemDock, optionsDock, toolsDock, propsDock, layersDock, statusDock, zoomDock})
    if (d) d->Place(w, snap);
  // The free area is measured where the docks are going, not where their springs are now.
  float left = toolsDock->visible ? toolsDock->Target().right + 12 : w.left + 12;
  float right = layersDock->visible ? layersDock->Target().left - 12 : w.right - 12;
  float top = std::max(menuDock->Target().bottom, optionsDock->Target().bottom) + 12;
  float bottom = statusDock->Target().top - 10;
  if (documentTabs) {
    documentTabs->rect = R(left, top, right, top + 36);
    documentTabs->Layout();
    if (documentTabs->visible) top += 44;
  }
  canvas->free = R(left, top, right, bottom);
  LayoutGuides(canvas->free);
  if (startScreen) { startScreen->rect = canvas->free; startScreen->Layout(); }
  canvas->rect = w;
}

// ======================================================================== shortcuts ==
struct Shortcut {
  bool ctrl, shift, alt;
  UINT vk;
};
std::vector<std::pair<Shortcut, std::pair<std::string, std::wstring>>> shortcuts;

UINT KeyCode(const std::string& k) {
  if (k.size() == 1) {
    char c = char(toupper(static_cast<unsigned char>(k[0])));
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return UINT(c);
    switch (c) {
      case '[': return VK_OEM_4;
      case ']': return VK_OEM_6;
      case '=': case '+': return VK_OEM_PLUS;
      case '-': return VK_OEM_MINUS;
      case ',': return VK_OEM_COMMA;
      case '.': return VK_OEM_PERIOD;
      case '/': return VK_OEM_2;
      case ';': return VK_OEM_1;
      case '\'': return VK_OEM_7;
      case '\\': return VK_OEM_5;
      case '`': return VK_OEM_3;
    }
    return 0;
  }
  if (k.size() >= 2 && k[0] == 'F' && isdigit(static_cast<unsigned char>(k[1]))) return UINT(VK_F1 + atoi(k.c_str() + 1) - 1);
  if (k == "Delete" || k == "Del") return VK_DELETE;
  if (k == "Backspace") return VK_BACK;
  if (k == "Esc" || k == "Escape") return VK_ESCAPE;
  if (k == "Enter" || k == "Return") return VK_RETURN;
  if (k == "Tab") return VK_TAB;
  if (k == "Space") return VK_SPACE;
  if (k == "Left") return VK_LEFT;
  if (k == "Right") return VK_RIGHT;
  if (k == "Up") return VK_UP;
  if (k == "Down") return VK_DOWN;
  if (k == "PageUp") return VK_PRIOR;
  if (k == "PageDown") return VK_NEXT;
  if (k == "Home") return VK_HOME;
  if (k == "End") return VK_END;
  return 0;
}

void BuildShortcuts() {
  for (auto& r : catalog) {
    if (r.shortcut.empty() || r.id == "---") continue;
    Shortcut s{false, false, false, 0};
    std::string rest = r.shortcut;
    for (;;) {
      size_t p = rest.find('+');
      if (p == std::string::npos || p == 0 || p == rest.size() - 1) break;
      std::string mod = rest.substr(0, p);
      if (mod == "Cmd" || mod == "Ctrl") s.ctrl = true;
      else if (mod == "Shift") s.shift = true;
      else if (mod == "Alt" || mod == "Opt") s.alt = true;
      else break;
      rest = rest.substr(p + 1);
    }
    s.vk = KeyCode(rest);
    if (s.vk && (s.ctrl || s.alt || (s.vk >= VK_F1 && s.vk <= VK_F24))) shortcuts.push_back({s, {r.id, W(r.label)}});
  }
}

bool HandleKey(UINT vk, Mods m) {
  if (ui::Key(vk, m)) return true;
  if (TextEditorKey(vk,m)) return true;
  if (GuideKey(vk,m)) return true;
  bool selectionTool = current && (current->key.find("Marquee") != std::string::npos || current->key.find("lasso") != std::string::npos ||
                                   current->key.find("Lasso") != std::string::npos || current->key == "magicWand" || current->key == "quickSelection" || current->key == "objectSelection");
  bool arrow = vk >= VK_LEFT && vk <= VK_DOWN;
  // The repeated arrows join their own queued preview even when a previous repeat is still running.
  if (!Mode() && (!jobs::Busy() || nudge.preview) && ArrowNudge(vk, m, selectionTool, current && current->key == "move")) return true;
  if (!arrow && vk != VK_SHIFT && vk != VK_CONTROL && vk != VK_MENU) FinishNudge();
  if (!(vk >= '0' && vk <= '9')) percentKeys.Reset();
  if (jobs::Busy()) {
    // The view moves while the engine works; anything that would touch the document waits.
    if (m.ctrl && (vk == '0' || vk == '1' || vk == VK_OEM_PLUS || vk == VK_ADD || vk == VK_OEM_MINUS || vk == VK_SUBTRACT)) {
    } else {
      return true;
    }
  }
  if (m.ctrl && vk == 'T') {
    if (m.alt && !m.shift) {
      if (Mode()) Mode()->Key(VK_RETURN, {});
      Exec("layer.duplicate", "{}", [](bool ok, const std::string&) { if (ok) StartTransform(); });
    } else if (m.shift) {
      if (Mode()) Mode()->Key(VK_RETURN, {});
      Exec(m.alt ? "edit.transform.againCopy" : "edit.transform.again");
    } else StartTransform();
    return true;
  }
  if (canvas->tool && canvas->tool->Key(vk, m)) return true;
  if (m.ctrl && !m.alt) {
    if (vk == VK_TAB) {
      const auto& docs = sessionInfo["documents"]; size_t count = docs.size();
      if (count) {
        size_t i = size_t(sessionInfo["active"].Int(0));
        size_t next = m.shift ? (i + count - 1) % count : (i + 1) % count;
        SwitchDocument(uint64_t(docs[next]["id"].Int()));
      }
      return true;
    }
    if (vk == 'W') { RunCommand("file.close", L"Close"); return true; }
    if (vk == 'C') { CopyClipboard(m.shift); return true; }
    if (vk == 'V' && !m.shift) { PasteClipboard("edit.paste"); return true; }
    if (vk == '0') { canvas->Fit(true); return true; }
    if (vk == '1') { canvas->ActualPixels(true); return true; }
    if (vk == VK_OEM_PLUS || vk == VK_ADD) { RunCommand("view.zoomIn", L""); return true; }
    if (vk == VK_OEM_MINUS || vk == VK_SUBTRACT) { RunCommand("view.zoomOut", L""); return true; }
    if (vk == 'Z' && !m.shift) { RunCommand("edit.undo", L"Undo"); return true; }
    if ((vk == 'Z' && m.shift) || vk == 'Y') { RunCommand("edit.redo", L"Redo"); return true; }
    if (vk == 'O' && !m.shift) { OpenFile(); return true; }
    if (vk == 'S' && !m.shift) { SaveDocument(false); return true; }
    if (vk == 'S' && m.shift) { SaveDocument(true); return true; }
  }
  for (auto& sc : shortcuts) {
    const Shortcut& s = sc.first;
    if (s.vk == vk && s.ctrl == m.ctrl && s.shift == m.shift && s.alt == m.alt) {
      RunCommand(sc.second.first, sc.second.second);
      return true;
    }
  }
  if (m.ctrl || m.alt) return false;

  if (vk == VK_ESCAPE) {

    return false;
  }
  if (vk == VK_DELETE || vk == VK_BACK) {
    if (layersFocused || !docInfo["hasSelection"].Truthy()) Exec("layer.delete");
    else Exec("edit.clear");
    return true;
  }
  if (vk == 'X') { Exec("tools.swapColors"); return true; }
  if (vk == 'D') { Exec("tools.defaultColors"); return true; }
  if (vk == VK_OEM_3 && IsBrushTool()) { temporaryErase = true; return true; }
  if (vk >= '0' && vk <= '9' && IsBrushTool()) {
    int value = percentKeys.Push(int(vk - '0'), m.shift, ui::NowMs());
    if (m.shift) brush.flow = value; else brush.opacity = value;
    SetBrushSettings();
    return true;
  }
  if (vk == VK_OEM_4 || vk == VK_OEM_6) {
    if (m.shift) brush.hardness = std::clamp(brush.hardness + (vk == VK_OEM_6 ? 25 : -25), 0.0, 100.0);
    else brush.size = std::round(std::clamp(brush.size * (vk == VK_OEM_6 ? 1.15 : 1 / 1.15), 1.0, 2500.0));
    SetBrushSettings();
    return true;
  }
  if (vk >= 'A' && vk <= 'Z') {
    if (springKey == vk) return true;
    if (!Mode() && !canvas->Drawing()) {
      RestoreSpringTool();
      ToolDef* before = current;
      ToolByLetter(wchar_t(vk), m.shift, true);
      if (current != before) { springTool = before; springKey = vk; springAt = ui::NowMs(); }
    }
    return true;
  }
  return false;
}

// ============================================================================= gate ==
// While the engine works: the view (pan, zoom, docks) and an open dialog stay live; a press that
// would start something on the document waits.
double busyNoticeAt = 0;
bool InputAllowed(Widget* target, const PointerEvent& e) {
  FinishNudge();
  layersFocused = false;
  for (Widget* w = target; w; w = w->parent) if (w == layersDock) layersFocused = true;
  if (!jobs::Busy()) return true;
  if (!target) return true;
  if (Popover* p = ui::PopoverOf(target); p && (p->modal || p->pinned)) return true;
  if (dynamic_cast<Dock*>(target) || target->parent == zoomDock) return true;
  if (target == canvas && (e.button == 2 || canvas->spaceHand || canvas->spaceZoom || !canvas->tool ||
                          (current && (current->key == "zoom" || current->key == "rotateView")))) return true;
  if (ui::NowMs() - busyNoticeAt > 1500) {
    busyNoticeAt = ui::NowMs();
    std::wstring l = jobs::Label();
    ui::Toast(Fmt(L"%ls… one moment", i18n::Tr(l.empty() ? std::wstring(L"Working") : l)));
  }
  return false;
}

// ============================================================================ frame ==
// VETRO_TRACE=1: per-frame timings to %TEMP%etro_trace.txt (scene, interface, present, input).
FILE* traceFile = nullptr;
double traceInput = 0;
int traceMoves = 0;
void Frame() {
  double now = ui::NowMs();
  double t0 = now;
  FlushParameterPreviews();
  if (brushSettingsPending && !canvas->Drawing() && !jobs::Busy()) SetBrushSettings();
  if (springRestore) RestoreSpringTool();
  if (nudge.preview && now - nudge.at > 700 && GetAsyncKeyState(VK_LEFT) >= 0 && GetAsyncKeyState(VK_RIGHT) >= 0 &&
      GetAsyncKeyState(VK_UP) >= 0 && GetAsyncKeyState(VK_DOWN) >= 0) FinishNudge();
  float dt = lastFrame > 0 ? float((now - lastFrame) / 1000.0) : 1.f / 60;
  lastFrame = now;
  canvas->TakeResults();
  if (canvas->tool) canvas->tool->Tick();
  TickWorkspacePanels();
  Layout(false);
  bool moving = ui::Animate(dt);
  bool scene = canvas->SceneDirty();
  if (scene) {
    GfxBeginScene();
    canvas->PaintScene();
    GfxEndScene();
  }
  double t1 = ui::NowMs();
  GfxBeginInterface();
  ui::Paint();
  double t2 = ui::NowMs();
  GfxPresent();
  if (traceFile) {
    double t3 = ui::NowMs();
    fprintf(traceFile, "%9.1f frame %5.1f | scene%c %5.1f ui %5.1f present %5.1f | input %5.1f ms over %d moves\n", t0, t3 - t0, scene ? '*' : ' ', t1 - t0, t2 - t1,
            t3 - t2, traceInput, traceMoves);
    fflush(traceFile);
    traceInput = 0;
    traceMoves = 0;
  }
  animating = moving || canvas->SceneDirty() || bool(nudge.preview) || springRestore;
}

PointerEvent MouseEvent(LPARAM lp, WPARAM wp, int button) {
  PointerEvent e;
  e.x = float(GET_X_LPARAM(lp)) / dpiScale;
  e.y = float(GET_Y_LPARAM(lp)) / dpiScale;
  e.button = button;
  e.timeMs = ui::NowMs();
  e.mods.ctrl = (wp & MK_CONTROL) != 0;
  e.mods.shift = (wp & MK_SHIFT) != 0;
  e.mods.alt = GetKeyState(VK_MENU) < 0;
  return e;
}

bool CloseRequested() {
  if (closingDocuments || jobs::Busy()) return false;
  const Json& docs = sessionInfo["documents"];
  if (!docs.size()) return true;
  std::vector<uint64_t> ids;
  for (size_t i = 0; i < docs.size(); i++) ids.push_back(uint64_t(docs[i]["id"].Int()));
  closingDocuments = true;
  CloseDocuments(ids, [](bool ok) { closingDocuments = false; if (ok) DestroyWindow(hwnd); });
  return false;
}

LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  static bool tracking = false;
  switch (msg) {
    case WM_NCCALCSIZE:
      if (wp && IsZoomed(h)) {
        // A maximised borderless window overhangs the monitor by its frame.
        auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
        UINT dpi = GetDpiForWindow(h);
        int f = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
        p->rgrc[0].left += f;
        p->rgrc[0].top += f;
        p->rgrc[0].right -= f;
        p->rgrc[0].bottom -= f;
        return 0;
      }
      if (wp) return 0;
      break;
    case WM_NCHITTEST: {
      POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(h, &pt);
      RECT rc;
      GetClientRect(h, &rc);
      if (!IsZoomed(h)) {
        int b = int(6 * dpiScale);
        bool l = pt.x < b, r = pt.x >= rc.right - b, t = pt.y < b, bo = pt.y >= rc.bottom - b;
        if (t && l) return HTTOPLEFT;
        if (t && r) return HTTOPRIGHT;
        if (bo && l) return HTBOTTOMLEFT;
        if (bo && r) return HTBOTTOMRIGHT;
        if (l) return HTLEFT;
        if (r) return HTRIGHT;
        if (t) return HTTOP;
        if (bo) return HTBOTTOM;
      }
      return HTCLIENT;
    }
    case WM_SIZE: {
      RECT rc;
      GetClientRect(h, &rc);
      GfxResize(UINT(rc.right), UINT(rc.bottom), dpiScale);
      ui::SetBounds(R(0, 0, float(rc.right) / dpiScale, float(rc.bottom) / dpiScale));
      if (canvas && brandDock) {
        Layout(false);
        canvas->Fit(false);
        Frame();
        animating = true;
      }
      return 0;
    }
    case WM_DPICHANGED: {
      dpiScale = HIWORD(wp) / 96.f;
      UpdateDisplay();
      auto* r = reinterpret_cast<RECT*>(lp);
      SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_GETMINMAXINFO: {
      auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
      mm->ptMinTrackSize = {LONG(900 * dpiScale), LONG(600 * dpiScale)};
      return 0;
    }
    case WM_MOUSEMOVE: {
      if (!tracking) {
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
        TrackMouseEvent(&t);
        tracking = true;
      }
      auto e = MouseEvent(lp, wp, 0);
      pointerX = e.x;
      pointerY = e.y;
      double ti = ui::NowMs();
      ui::PointerMove(e);
      traceInput += ui::NowMs() - ti;
      traceMoves++;
      animating = true;
      return 0;
    }
    case WM_MOUSELEAVE:
      tracking = false;
      pointerX = pointerY = -1;
      ui::PointerLeave();
      animating = true;
      return 0;
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
    case WM_LBUTTONDBLCLK: case WM_RBUTTONDBLCLK: case WM_MBUTTONDBLCLK: {
      int button = (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) ? 0 : (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK) ? 1 : 2;
      if (button != 0 && (wp & MK_LBUTTON)) return 0;  // the left button's drag owns the pointer
      auto e = MouseEvent(lp, wp, button);
      e.clicks = (msg == WM_LBUTTONDBLCLK || msg == WM_RBUTTONDBLCLK || msg == WM_MBUTTONDBLCLK) ? 2 : 1;
      // The brand block moves the window, like a title bar.
      if (button == 0 && brandDock && Inside(brandDock->rect, e.x, e.y) && !ui::AnyPopoverOpen()) {
        if (e.clicks == 2) {
          ShowWindow(h, IsZoomed(h) ? SW_RESTORE : SW_MAXIMIZE);
          return 0;
        }
        // A drag moves the window; a click (the pointer stayed put) opens the language choice.
        POINT before{}, after{};
        GetCursorPos(&before);
        SendMessage(h, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        GetCursorPos(&after);
        if (std::abs(after.x - before.x) <= 3 && std::abs(after.y - before.y) <= 3) OpenLanguageMenu();
        return 0;
      }
      SetCapture(h);
      ui::PointerDown(e);
      animating = true;
      return 0;
    }
    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: {
      int button = msg == WM_LBUTTONUP ? 0 : msg == WM_RBUTTONUP ? 1 : 2;
      if (button != 0 && (wp & MK_LBUTTON)) return 0;
      ReleaseCapture();
      ui::PointerUp(MouseEvent(lp, wp, button));
      animating = true;
      return 0;
    }
    case WM_POINTERDOWN: case WM_POINTERUPDATE: case WM_POINTERUP: {
      // A pen's pressure reaches the brush; mouse and touch fall through to the mouse messages.
      UINT32 id = GET_POINTERID_WPARAM(wp);
      POINTER_INPUT_TYPE type = PT_POINTER;
      if (!GetPointerType(id, &type) || type != PT_PEN) break;
      POINTER_PEN_INFO pen{};
      if (!GetPointerPenInfo(id, &pen)) break;
      POINT pt = pen.pointerInfo.ptPixelLocation;
      ScreenToClient(h, &pt);
      PointerEvent e;
      e.x = float(pt.x) / dpiScale;
      e.y = float(pt.y) / dpiScale;
      e.pen = true;
      e.pressure = (pen.penMask & PEN_MASK_PRESSURE) ? float(pen.pressure) / 1024.f : 1.f;
      e.timeMs = ui::NowMs();
      e.mods.ctrl = GetKeyState(VK_CONTROL) < 0;
      e.mods.shift = GetKeyState(VK_SHIFT) < 0;
      e.mods.alt = GetKeyState(VK_MENU) < 0;
      e.button = (pen.penFlags & PEN_FLAG_BARREL) ? 1 : 0;
      pointerX = e.x;
      pointerY = e.y;
      if (msg == WM_POINTERDOWN) {
        SetCapture(h);
        ui::PointerDown(e);
      } else if (msg == WM_POINTERUP) {
        ReleaseCapture();
        ui::PointerUp(e);
      } else {
        ui::PointerMove(e);
      }
      animating = true;
      return 0;
    }
    case WM_MOUSEWHEEL: {
      POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(h, &pt);
      PointerEvent e;
      e.x = float(pt.x) / dpiScale;
      e.y = float(pt.y) / dpiScale;
      e.mods.ctrl = (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) != 0;
      e.mods.shift = (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) != 0;
      e.mods.alt = GetKeyState(VK_MENU) < 0;
      ui::Wheel(float(GET_WHEEL_DELTA_WPARAM(wp)), e);
      animating = true;
      return 0;
    }
    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT) {
        bool overCanvas = canvas && canvas->PointerOver() && !ui::AnyPopoverOpen();
        if (overCanvas && canvas->tool && canvas->tool->HidesCursor() && !canvas->spaceHand && canvas->HasDocument()) SetCursor(nullptr);
        else SetCursor(overCanvas ? canvas->Cursor() : LoadCursor(nullptr, IDC_ARROW));
        return TRUE;
      }
      break;
    case WM_KEYDOWN: case WM_SYSKEYDOWN: {
      Mods m;
      m.ctrl = GetKeyState(VK_CONTROL) < 0;
      m.shift = GetKeyState(VK_SHIFT) < 0;
      m.alt = GetKeyState(VK_MENU) < 0;
      if ((wp == VK_CONTROL || wp == VK_MENU) && GetKeyState(VK_SPACE) < 0 && !ui::AnyPopoverOpen()) {
        canvas->spaceZoom = m.ctrl ? 1 : m.alt ? -1 : 0;
        canvas->spaceHand = canvas->spaceZoom == 0;
        return 0;
      }
      if (wp == VK_SPACE && !ui::AnyPopoverOpen()) {
        if (!canvas->spaceHand) {
          canvas->spaceZoom = m.ctrl ? 1 : m.alt ? -1 : 0;
          canvas->spaceHand = canvas->spaceZoom == 0;
          SendMessage(h, WM_SETCURSOR, WPARAM(h), HTCLIENT);
        }
        return 0;
      }
      if (HandleKey(UINT(wp), m)) {
        animating = true;
        return 0;
      }
      if (msg == WM_SYSKEYDOWN) break;
      return 0;
    }
    case WM_KEYUP: case WM_SYSKEYUP:
      ReleaseToolKey(UINT(wp));
      if (wp == VK_OEM_3) temporaryErase = false;
      if (wp == VK_SPACE) {
        canvas->spaceHand = false;
        canvas->spaceZoom = 0;
        SendMessage(h, WM_SETCURSOR, WPARAM(h), HTCLIENT);
      }
      if ((wp == VK_CONTROL || wp == VK_MENU) && GetKeyState(VK_SPACE) < 0) { canvas->spaceZoom = 0; canvas->spaceHand = true; }
      animating = true;
      return 0;
    case WM_KILLFOCUS:
      if (!canvas) break;
      FinishNudge();
      canvas->CancelInput();
      RestoreSpringTool();
      temporaryErase = false; percentKeys.Reset();
      visibilityPainting = false; layerDrag = {};
      ui::Release(); infoRevision = ~0ull; Refresh();
      return 0;
    case WM_CHAR:
      if (ui::Char(wchar_t(wp)) || TextEditorChar(wchar_t(wp))) { animating = true; return 0; }
      if (jobs::Busy()) return 0;
      if (typePopover && typePopover->open) {
        typePopover->Char(wchar_t(wp));
        animating = true;
      }
      return 0;
    case WM_SYSCHAR:
      return 0;  // no beep for Alt shortcuts
    case WM_SYSCOMMAND:
      if ((wp & 0xFFF0) == SC_KEYMENU) return 0;  // Alt is a modifier (sampling, sizing), not a menu key
      break;
    case WM_DROPFILES: {
      HDROP drop = reinterpret_cast<HDROP>(wp);
      UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
      std::vector<std::wstring> paths;
      POINT point{}; DragQueryPoint(drop, &point);
      for (UINT i = 0; i < n; i++) {
        UINT length = DragQueryFileW(drop, i, nullptr, 0);
        std::wstring path(size_t(length) + 1, L'\0');
        if (DragQueryFileW(drop, i, path.data(), length + 1)) { path.resize(length); paths.push_back(std::move(path)); }
      }
      DragFinish(drop);
      if (jobs::Busy() || closingDocuments) { ui::Toast(L"Working… one moment"); return 0; }
      DropPaths(paths, float(point.x) / dpiScale, float(point.y) / dpiScale, GetKeyState(VK_SHIFT) < 0);
      return 0;
    }
    case WM_APP + 1:
      animating = true;
      return 0;
    case WM_WORKSPACE_PANELS:
      TickWorkspacePanels();animating=true;return 0;
    case WM_JOBS:
      jobs::Pump();
      if (refreshWanted && !jobs::Busy()) Refresh();
      animating = true;
      return 0;
    case WM_DISPLAYCHANGE:
    case WM_EXITSIZEMOVE:
      displayMonitor = nullptr;
      UpdateDisplay();
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(h, &ps);
      EndPaint(h, &ps);
      animating = true;
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_CLOSE:
      if (CloseRequested()) DestroyWindow(h);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

void LoadCatalog() {
  Json rows = Json::Parse(TakeString(pc_catalog()));
  for (size_t i = 0; i < rows.size(); i++) {
    const Json& r = rows[i];
    CatalogRow c;
    for (size_t k = 0; k < r[0].size(); k++) c.path.push_back(r[0][k].Str());
    c.label = r[1].Str();
    c.shortcut = r[2].Str();
    c.id = r[3].Str();
    if(c.id.starts_with("window.panel.") && IsWorkspacePanelCommand(c.id))c.id="window."+c.id.substr(13);
    static const std::map<std::string,std::string> nativePanels{{"window.panel.brushes","window.brushes"},{"window.panel.brushSettings","window.brushSettings"},{"window.panel.cloneSource","window.cloneSource"},{"window.panel.character","window.character"},{"window.panel.paragraph","window.paragraph"}};
    if(auto panel=nativePanels.find(c.id);panel!=nativePanels.end())c.id=panel->second;
    c.live = r[4].Truthy();
    catalog.push_back(std::move(c));
  }
  Json cmds = Json::Parse(TakeString(pc_commands()));
  for(auto item : {std::pair{"window.brushes","Brushes"},std::pair{"window.brushSettings","Brush Settings"},std::pair{"window.layerFilters","Layer Filters"},std::pair{"window.cloneSource","Clone Source"},std::pair{"window.character","Character"},std::pair{"window.paragraph","Paragraph"},std::pair{"window.color","Color"},std::pair{"window.swatches","Swatches"},std::pair{"window.gradients","Gradients"},std::pair{"window.patterns","Patterns"},std::pair{"window.channels","Channels"},std::pair{"window.paths","Paths"},std::pair{"window.navigator","Navigator"},std::pair{"window.histogram","Histogram"},std::pair{"window.info","Info"}}) {
    if(std::none_of(catalog.begin(),catalog.end(),[item](const CatalogRow& r){return r.id==item.first;})) {
      CatalogRow c;c.id=item.first;c.label=item.second;c.path={"Window"};c.live=true;catalog.push_back(c);
    }
  }
  for (size_t i = 0; i < cmds.size(); i++) paramsDoc[cmds[i]["id"].Str()] = cmds[i]["params"].Str();
}
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  BOOL animations = TRUE;
  SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
  reducedMotion = !animations;

  engine = pc_new();
  if (engine) i18n::Load(i18n::Saved());
  if (!engine) {
    MessageBoxW(nullptr, L"The PhotoCraft engine could not start.", L"PhotoCraft by Vetro", MB_ICONERROR);
    return 1;
  }
  LoadCatalog();
  recentDocuments.Load();

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof wc;
  wc.style = CS_DBLCLKS;
  wc.lpfnWndProc = Proc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
  wc.lpszClassName = L"PhotoCraftVetro";
  RegisterClassExW(&wc);
  hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_ACCEPTFILES, wc.lpszClassName, L"PhotoCraft by Vetro", WS_OVERLAPPEDWINDOW,
                         CW_USEDEFAULT, CW_USEDEFAULT, 1600, 1000, nullptr, nullptr, inst, nullptr);
  if (!hwnd) return 1;
  dpiScale = float(GetDpiForWindow(hwnd)) / 96.f;
  DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
  DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof corners);
  BOOL dark = TRUE;
  DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);
  ui::Init(hwnd);
  RECT rc;
  GetClientRect(hwnd, &rc);
  if (!GfxCreate(hwnd, dpiScale)) {
    MessageBoxW(nullptr, L"Direct3D 11 / Direct2D could not start.", L"PhotoCraft by Vetro", MB_ICONERROR);
    return 1;
  }
  ui::SetBounds(R(0, 0, float(rc.right) / dpiScale, float(rc.bottom) / dpiScale));

  {
    wchar_t v[8] = {}, dir[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"VETRO_TRACE", v, 8) && GetTempPathW(MAX_PATH, dir)) traceFile = _wfsopen((std::wstring(dir) + L"vetro_trace.txt").c_str(), L"w", _SH_DENYNO);
  }
  jobs::Init(hwnd, WM_JOBS);
  canvas = new Canvas(engine);
  {
    uint32_t low = 0;
    int32_t high = 0;
    wchar_t cpu[8] = {};
    if (GetEnvironmentVariableW(L"VETRO_CPU_CANVAS", cpu, 8) == 0 && GfxAdapterLuid(&low, &high)) canvas->UseGpu(low, high);
  }
  ui::AddLayer(canvas);
  canvas->onContext = CanvasContext;
  canvas->onBrushWheel = BrushWheel;
  canvas->onBrushDrag = BrushDrag;
  canvas->onSample = [](double x, double y, const PointerEvent&) {
    // Clone/healing own Alt as their source selector.
    if (!current || (current->key == "cloneStamp" || current->key == "healingBrush") || !IsBrushTool()) return false;
    PickColour(x, y, false); return true;
  };
  ui::SetGate(InputAllowed);
  ui::AddLayer(new SelectionOverlay());
  BuildTools();
  BuildDocks();
  BuildShortcuts();
  SelectTool(&sections[2][1][0]);  // the Brush
  Refresh();
  UpdateDisplay();
  SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
  ShowWindow(hwnd, SW_SHOWMAXIMIZED);
  UpdateWindow(hwnd);

  // A file named on the command line (Open With, drop onto the exe) opens at once.
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; argv && i < argc; i++) OpenPath(argv[i]);
  if (argv) LocalFree(argv);

  MSG m;
  bool running = true;
  while (running) {
    if (animating) {
      while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
        if (m.message == WM_QUIT) { running = false; break; }
        TranslateMessage(&m);
        DispatchMessageW(&m);
      }
      if (running) Frame();  // Present waits for the display: the loop is paced by vsync
    } else {
      if (GetMessageW(&m, nullptr, 0, 0) <= 0) break;
      TranslateMessage(&m);
      DispatchMessageW(&m);
    }
  }
  jobs::Shutdown();
  if (traceFile) fclose(traceFile);
  delete canvas;
  canvas = nullptr;
  pc_free(engine);
  GfxDestroy();
  CoUninitialize();
  return 0;
}
