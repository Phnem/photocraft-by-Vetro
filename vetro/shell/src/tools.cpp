// PhotoCraft by Vetro -- the canvas tools beyond app.cpp's simple ones: the pens and the path
// selection tools (all on the document's work path), the source tools (Clone Stamp, Healing Brush)
// and Free Transform. Each is a CanvasTool; the engine does the work through its commands.
#include "shell.h"
#include "icons_phosphor.h"
#include "gestures.h"
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace {
// ===================================================================== the work path ==
struct Knot {
  D2D_POINT_2F a{}, in{}, out{};
  bool smooth = false;
};
struct SubPath {
  std::vector<Knot> knots;
  bool closed = false;
  std::string op = "combine";
};
std::vector<SubPath> work;  // the document's work path, as last read or set
std::string workTarget = "work";
uint64_t workDocument = 0;
uint64_t workLayer = 0, workRevision = ~0ull;
bool WorkChanged() { return workDocument != uint64_t(docInfo["id"].Int()) || workLayer != uint64_t(docInfo["activeLayer"].Int()) || workRevision != uint64_t(docInfo["revision"].Int()); }

const D2D1_COLOR_F PathInk = Rgba(.47f, .78f, 1.f, 1.f);  // paths read apart from the selection ants

D2D_POINT_2F Screen(D2D_POINT_2F p) { return canvas->ToScreen(p.x, p.y); }
float ScreenDist(D2D_POINT_2F docPoint, float sx, float sy) {
  auto s = Screen(docPoint);
  return std::hypot(s.x - sx, s.y - sy);
}
D2D_POINT_2F Plus(D2D_POINT_2F a, D2D_POINT_2F b) { return {a.x + b.x, a.y + b.y}; }
D2D_POINT_2F Minus(D2D_POINT_2F a, D2D_POINT_2F b) { return {a.x - b.x, a.y - b.y}; }
D2D_POINT_2F Mirror(D2D_POINT_2F about, D2D_POINT_2F p) { return {2 * about.x - p.x, 2 * about.y - p.y}; }

std::string P2(D2D_POINT_2F p) { return "[" + JsonNum(std::round(p.x * 100) / 100) + "," + JsonNum(std::round(p.y * 100) / 100) + "]"; }
std::string PathJson(const std::vector<SubPath>& subs) {
  std::string s = "{\"subpaths\":[";
  for (size_t i = 0; i < subs.size(); i++) {
    s += i ? ",{" : "{";
    s += "\"closed\":" + std::string(subs[i].closed ? "true" : "false") + ",\"op\":" + JsonQuote(subs[i].op) + ",\"knots\":[";
    for (size_t k = 0; k < subs[i].knots.size(); k++) {
      const Knot& n = subs[i].knots[k];
      s += k ? ",{" : "{";
      s += "\"anchor\":" + P2(n.a) + ",\"in\":" + P2(n.in) + ",\"out\":" + P2(n.out) + ",\"smooth\":" + (n.smooth ? "true" : "false") + "}";
    }
    s += "]}";
  }
  return s + "]}";
}
D2D_POINT_2F JP(const Json& j, D2D_POINT_2F fallback) { return j.size() >= 2 ? D2D_POINT_2F{float(j[0].Num()), float(j[1].Num())} : fallback; }
std::vector<SubPath> ParsePath(const Json& path) {
  std::vector<SubPath> out;
  const Json& subs = path["subpaths"];
  for (size_t i = 0; i < subs.size(); i++) {
    SubPath sp;
    sp.closed = subs[i]["closed"].Truthy(true);
    if (!subs[i]["op"].Str().empty()) sp.op = subs[i]["op"].Str();
    const Json& ks = subs[i]["knots"];
    for (size_t k = 0; k < ks.size(); k++) {
      const Json& kj = ks[k];
      Knot n;
      if (kj.kind == Json::Array) {
        n.a = n.in = n.out = JP(kj, {});
      } else {
        n.a = JP(kj["anchor"], {});
        n.in = JP(kj["in"], n.a);
        n.out = JP(kj["out"], n.a);
        n.smooth = kj["smooth"].Truthy();
      }
      sp.knots.push_back(n);
    }
    if (!sp.knots.empty()) out.push_back(std::move(sp));
  }
  return out;
}

void LoadWork() {
  if (!canvas->HasDocument()) {
    work.clear();
    return;
  }
  workDocument = uint64_t(docInfo["id"].Int());
  workLayer = uint64_t(docInfo["activeLayer"].Int()); workRevision = uint64_t(docInfo["revision"].Int());
  workTarget = !docInfo["hasWorkPath"].Truthy() && ActiveLayer() && ((*ActiveLayer())["kind"].Str() == "Shape" || !(*ActiveLayer())["vectorMask"].IsNull()) ? "layer" : "work";
  Exec("path.info", JsonObject().Str("name",workTarget).Text(), [](bool ok, const std::string& r) {
    work.clear();
    if (ok) work = ParsePath(Json::Parse(r)["path"]);
  }, false);
}
void SetWork(std::vector<SubPath> subs) {
  if (subs.empty()) {
    Exec("path.delete", JsonObject().Str("name",workTarget).Text(), [](bool ok, const std::string&) { if (ok) work.clear(); else LoadWork(); }, false);
    return;
  }
  JsonObject o;
  o.Str("name", workTarget).Add("path", PathJson(subs));
  if (workTarget == "layer") o.Num("layer",double(docInfo["activeLayer"].Int()));
  auto keep = std::make_shared<std::vector<SubPath>>(std::move(subs));
  Exec("path.set", o.Text(), [keep](bool ok, const std::string&) {
    if (ok) work = *keep;
    else LoadWork();
  });
}

// Screen geometry of subpaths: cubic segments from each knot's out handle to the next one's in.
ComPtr<ID2D1PathGeometry> Geometry(const std::vector<SubPath>& subs) {
  ComPtr<ID2D1PathGeometry> g;
  if (subs.empty() || FAILED(GfxFactory()->CreatePathGeometry(&g))) return g;
  ComPtr<ID2D1GeometrySink> sink;
  if (FAILED(g->Open(&sink))) return nullptr;
  for (auto& sp : subs) {
    if (sp.knots.empty()) continue;
    sink->BeginFigure(Screen(sp.knots[0].a), D2D1_FIGURE_BEGIN_HOLLOW);
    for (size_t i = 1; i < sp.knots.size(); i++)
      sink->AddBezier(D2D1::BezierSegment(Screen(sp.knots[i - 1].out), Screen(sp.knots[i].in), Screen(sp.knots[i].a)));
    if (sp.closed && sp.knots.size() > 1)
      sink->AddBezier(D2D1::BezierSegment(Screen(sp.knots.back().out), Screen(sp.knots[0].in), Screen(sp.knots[0].a)));
    sink->EndFigure(sp.closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
  }
  sink->Close();
  return g;
}
void DrawPathLine(ID2D1Geometry* g, float alpha) {
  Dc()->DrawGeometry(g, Ink(Rgba(0, 0, 0, .45f * alpha)), 2.6f);
  Dc()->DrawGeometry(g, Ink(Fade(PathInk, alpha)), 1.2f);
}
void DrawAnchor(D2D_POINT_2F s, bool filled) {
  Rect r = R(s.x - 3.5f, s.y - 3.5f, s.x + 3.5f, s.y + 3.5f);
  FillRect(r, filled ? PathInk : Rgba(1, 1, 1, 1));
  StrokeRound(r, 0, filled ? Rgba(1, 1, 1, .9f) : PathInk, 1);
}
void DrawHandles(const Knot& k) {
  auto a = Screen(k.a);
  for (auto h : {k.in, k.out}) {
    auto s = Screen(h);
    if (std::hypot(s.x - a.x, s.y - a.y) < 1) continue;
    Line(a.x, a.y, s.x, s.y, Fade(PathInk, .8f), 1);
    FillEllipse(s.x, s.y, 3.2f, 3.2f, PathInk);
  }
}

// The path actions: what a drawn path is for.
void MakeSelection(const char* mode = "replace") {
  JsonObject o;
  o.Str("name", workTarget).Str("mode", mode);
  bool replace = std::string(mode) == "replace";
  Exec("path.toSelection", o.Text(), [replace](bool ok, const std::string&) {
    // One closed subpath: the ants can follow its curve.
    if (!ok || !replace || work.size() != 1) return;
    const auto& k = work[0].knots;
    std::vector<D2D_POINT_2F> poly;
    size_t n = k.size(), segs = work[0].closed ? n : n - 1;
    for (size_t i = 0; i < segs; i++) {
      const Knot &a = k[i], &b = k[(i + 1) % n];
      for (int s = 0; s < 16; s++) {
        float t = float(s) / 16, u = 1 - t;
        poly.push_back({u * u * u * a.a.x + 3 * u * u * t * a.out.x + 3 * u * t * t * b.in.x + t * t * t * b.a.x,
                        u * u * u * a.a.y + 3 * u * u * t * a.out.y + 3 * u * t * t * b.in.y + t * t * t * b.a.y});
      }
    }
    ShowSelectionOutline(poly);
  });
}
void FillPath() { Exec("path.fill", JsonObject().Str("name",workTarget).Text()); }
void StrokePath(const char* tool = "brush") {
  JsonObject o;
  o.Str("name", workTarget).Str("tool", tool).Num("size", brush.size).Num("hardness", brush.hardness / 100);
  Exec("path.stroke", o.Text());
}
void ShapeFromPath() { Exec("layer.newFillLayer.solidColor", "{\"path\":\"work\"}"); }

void PathOptions(Container& dock, std::vector<Widget*>& out, const wchar_t* hint) {
  auto* l = dock.Add<Label>(hint, F_Label);
  out.push_back(l);
  auto button = [&](const wchar_t* label, std::function<void()> f) { out.push_back(dock.Add<TextButton>(label, TextButton::Secondary, std::move(f))); };
  button(L"Selection", [] { MakeSelection(); });
  button(L"Fill", [] { FillPath(); });
  button(L"Stroke", [] { StrokePath(); });
  button(L"Shape", [] { ShapeFromPath(); });
  button(L"Delete", [] { SetWork({}); });
}

// The right-click menu of the path tools (Photoshop's, for a work path). `prepare` runs first: the
// pen finishes the path it is drawing.
void PathMenu(std::vector<MenuItem>& rows, std::function<void()> prepare, bool hasPath) {
  auto with = [prepare](std::function<void()> f) {
    return [prepare, f] {
      if (prepare) prepare();
      f();
    };
  };
  rows.push_back(ActionItem(L"Make Selection", with([] { MakeSelection(); }), PhSelection, hasPath));
  rows.push_back(ActionItem(L"Add to Selection", with([] { MakeSelection("add"); }), nullptr, hasPath && docInfo["hasSelection"].Truthy()));
  rows.push_back(ActionItem(L"Subtract from Selection", with([] { MakeSelection("subtract"); }), nullptr, hasPath && docInfo["hasSelection"].Truthy()));
  rows.push_back(MenuItem::Separator());
  rows.push_back(ActionItem(L"Fill Path", with([] { FillPath(); }), PhPaintBucket, hasPath));
  MenuItem stroke = ActionItem(L"Stroke Path", nullptr, PhPaintBrush, hasPath);
  stroke.action = nullptr;
  for (auto [label, tool] : std::initializer_list<std::pair<const wchar_t*, const char*>>{
           {L"Brush", "brush"}, {L"Pencil", "pencil"}, {L"Eraser", "eraser"}, {L"Clone Stamp", "cloneStamp"}, {L"Smudge", "smudge"},
           {L"Blur", "blur"}, {L"Dodge", "dodge"}, {L"Burn", "burn"}})
    stroke.submenu.push_back(ActionItem(label, with([tool] { StrokePath(tool); })));
  rows.push_back(stroke);
  rows.push_back(MenuItem::Separator());
  rows.push_back(ActionItem(L"Create Shape Layer", with([] { ShapeFromPath(); }), PhRectangle, hasPath));
  rows.push_back(ActionItem(L"Create Vector Mask", with([] { Exec("layer.vectorMask.currentPath", "{\"name\":\"work\"}"); }), nullptr,
                            hasPath && ActiveLayer() != nullptr));
  rows.push_back(MenuItem::Separator());
  rows.push_back(ActionItem(L"Delete Path", with([] { SetWork({}); }), PhTrash, hasPath));
}

// Ramer-Douglas-Peucker: the fewest points within `tol` of the drawn line.
void Rdp(const std::vector<D2D_POINT_2F>& p, size_t a, size_t b, float tol, std::vector<bool>& keep) {
  if (b <= a + 1) return;
  float dx = p[b].x - p[a].x, dy = p[b].y - p[a].y, len = std::max(1e-6f, std::hypot(dx, dy));
  float worst = 0;
  size_t at = a;
  for (size_t i = a + 1; i < b; i++) {
    float d = std::fabs(dy * p[i].x - dx * p[i].y + p[b].x * p[a].y - p[b].y * p[a].x) / len;
    if (d > worst) { worst = d; at = i; }
  }
  if (worst <= tol) return;
  keep[at] = true;
  Rdp(p, a, at, tol, keep);
  Rdp(p, at, b, tol, keep);
}

// ============================================================================== pens ==
struct PathHit { int sub = -1, knot = -1; double t = .5; bool anchor = false; };
PathHit NearPath(const std::vector<SubPath>& paths, float sx, float sy) {
  PathHit result; float best = 7;
  for (size_t s = 0; s < paths.size(); s++) for (size_t k = 0; k < paths[s].knots.size(); k++)
    if (ScreenDist(paths[s].knots[k].a,sx,sy) < 7) return {int(s),int(k),.5,true};
  for (size_t s = 0; s < paths.size(); s++) {
    const auto& knots = paths[s].knots;
    size_t count = knots.empty() ? 0 : paths[s].closed ? knots.size() : knots.size() - 1;
    for (size_t k = 0; k < count; k++) {
      const auto& a = knots[k]; const auto& b = knots[(k+1)%knots.size()]; auto p0 = Screen(a.a),p1 = Screen(a.out),p2 = Screen(b.in),p3 = Screen(b.a);
      if (sx < std::min({p0.x,p1.x,p2.x,p3.x}) - 7 || sx > std::max({p0.x,p1.x,p2.x,p3.x}) + 7 || sy < std::min({p0.y,p1.y,p2.y,p3.y}) - 7 || sy > std::max({p0.y,p1.y,p2.y,p3.y}) + 7) continue;
      for (int i = 1; i < 32; i++) {
        float t = i/32.f,u = 1-t; D2D_POINT_2F p{u*u*u*p0.x + 3*u*u*t*p1.x + 3*u*t*t*p2.x + t*t*t*p3.x,u*u*u*p0.y + 3*u*u*t*p1.y + 3*u*t*t*p2.y + t*t*t*p3.y};
        float d = std::hypot(p.x-sx,p.y-sy); if (d < best) { best = d; result = {int(s),int(k),t,false}; }
      }
    }
  }
  return result;
}
JsonObject PathParams(int sub, int knot) { JsonObject p; p.Str("name",workTarget).Num("subpath",sub).Num("knot",knot); if (workTarget == "layer") p.Num("layer",double(docInfo["activeLayer"].Int())); return p; }
void PathEdit(const std::string& cmd, JsonObject p) { Exec(cmd,p.Text(),[](bool ok,const std::string& result) { if (ok) work = ParsePath(Json::Parse(result)["path"]); else LoadWork(); }); }
class PenTool : public CanvasTool {
 public:
  explicit PenTool(int k) : kind(k) {}
  void Activate() override { LoadWork(); }
  void Deactivate() override {
    if (editing && direct) direct->Cancel(); editing = false;
    if (cur.size() >= 2) Commit(false);
    cur.clear();
    trail.clear();
  }
  void Down(double x, double y, const PointerEvent& e) override {
    D2D_POINT_2F p{float(x), float(y)};
    editing = false; curEdit = -1;
    if (e.mods.ctrl) {
      if (!cur.empty()) {
        float best = 8; for (size_t i = 0; i < cur.size(); i++) if (ScreenDist(cur[i].a,e.x,e.y) < best) { best = ScreenDist(cur[i].a,e.x,e.y); curEdit = int(i); }
        if (curEdit >= 0) { editBefore = cur[size_t(curEdit)]; editStart = p; return; }
      }
      if (!direct) direct.reset(NewPathSelectTool(true)); editing = true; direct->Down(x,y,e); return;
    }
    if (!cur.empty() && e.mods.alt) {
      for (size_t i = 0; i < cur.size(); i++) if (ScreenDist(cur[i].a,e.x,e.y) < 8) { curEdit = int(i); cur[size_t(curEdit)].in = cur[size_t(curEdit)].out = cur[size_t(curEdit)].a; cur[size_t(curEdit)].smooth = false; convert = true; return; }
    }
    if (cur.empty()) {
      auto hit = NearPath(work,e.x,e.y);
      if (hit.sub >= 0) {
        if (e.mods.alt && hit.anchor) { if (!direct) direct.reset(NewPathSelectTool(true)); editing = true; direct->Down(x,y,e); return; }
        if (hit.anchor && kind == 0 && ContinuePath(hit.sub,hit.knot)) return;  // the end of an open path: the pen carries on from it
        auto args = PathParams(hit.sub,hit.knot); if (hit.anchor) PathEdit("path.deleteAnchor",args); else { args.Num("t",hit.t); PathEdit("path.addAnchor",args); } return;
      }
      if (workTarget == "layer") { work.clear(); workTarget = "work"; }
    }
    if (e.mods.shift && !cur.empty()) { auto d = gestures::Snap45(p.x - cur.back().a.x,p.y - cur.back().a.y); p = {float(cur.back().a.x + d.first),float(cur.back().a.y + d.second)}; }
    if (kind == 1) {
      trail = {p};
      drawing = true;
      return;
    }
    if (cur.size() >= 2 && ScreenDist(cur[0].a, e.x, e.y) <= 8) {
      Commit(true);  // back on the first point: a closed path
      return;
    }
    Knot k;
    k.a = k.in = k.out = p;
    cur.push_back(k);
    dragging = true;
    if (kind == 2) Curvature();
  }
  void Drag(double x, double y, const PointerEvent& e) override {
    D2D_POINT_2F p{float(x), float(y)};
    if (editing && direct) { direct->Drag(x,y,e); return; }
    if (curEdit >= 0 && size_t(curEdit) < cur.size()) {
      auto& k = cur[size_t(curEdit)];
      if (convert) { k.out = p; k.in = Mirror(k.a,p); k.smooth = true; }
      else { auto d = Minus(p,editStart); if (e.mods.shift) { auto snapped = gestures::Snap45(d.x,d.y); d = {float(snapped.first),float(snapped.second)}; } k = editBefore; k.a = Plus(k.a,d); k.in = Plus(k.in,d); k.out = Plus(k.out,d); }
      return;
    }
    if (kind == 1) {
      if (drawing && (trail.empty() || ScreenDist(trail.back(), e.x, e.y) >= 2)) trail.push_back(p);
      return;
    }
    if (!dragging || cur.empty()) return;
    Knot& k = cur.back();
    if (e.mods.shift) { auto d = gestures::Snap45(p.x-k.a.x,p.y-k.a.y); p = {float(k.a.x+d.first),float(k.a.y+d.second)}; }
    if (kind == 2) {
      k.a = p;  // a curvature point follows the hand until it lands
      Curvature();
      return;
    }
    // Pen: dragging out of a new point pulls symmetric handles, a smooth point.
    k.smooth = ScreenDist(k.a, e.x, e.y) > 2;
    k.out = k.smooth ? p : k.a;
    k.in = k.smooth ? Mirror(k.a, p) : k.a;
  }
  void Up(double x, double y, const PointerEvent& e) override {
    if (editing && direct) { direct->Up(x,y,e); editing = false; return; }
    if (curEdit >= 0) { Drag(x,y,e); curEdit = -1; convert = false; return; }
    if (kind == 1 && drawing) {
      Drag(x, y, e);
      drawing = false;
      Freeform();
      return;
    }
    dragging = false;
  }
  bool Key(UINT vk, Mods mods) override {
    if ((mods.ctrl || cur.empty()) && direct && direct->Key(vk,mods)) return true;
    if (vk == VK_RETURN && cur.size() >= 2) { Commit(false); return true; }
    if (vk == VK_ESCAPE && !cur.empty()) { Abandon(); return true; }
    if (vk == VK_BACK && !cur.empty()) {
      cur.pop_back();
      if (cur.empty()) Abandon();
      if (kind == 2) Curvature();
      return true;
    }
    return false;
  }
  void Overlay() override {
    if (editing && direct) { direct->Overlay(); return; }
    if (auto g = Geometry(work)) DrawPathLine(g.Get(), .8f);
    if (kind == 1) {
      std::vector<D2D_POINT_2F> s;
      for (auto& p : trail) s.push_back(Screen(p));
      if (auto g = Polyline(s, false)) DrawPathLine(g.Get(), 1);
      return;
    }
    if (cur.empty()) {
      if (canvas->PointerOver() && pointerX >= 0) { auto hit = NearPath(work,pointerX,pointerY); if (hit.sub >= 0) {
        Line(pointerX+10,pointerY+8,pointerX+18,pointerY+8,PathInk,1.4f); if (!hit.anchor) Line(pointerX+14,pointerY+4,pointerX+14,pointerY+12,PathInk,1.4f);
      } }
      return;
    }
    std::vector<SubPath> one(1);
    one[0].knots = cur;
    if (auto g = Geometry(one)) DrawPathLine(g.Get(), 1);
    if (!dragging && pointerX >= 0 && canvas->PointerOver()) {
      auto a = Screen(cur.back().a);
      Line(a.x, a.y, pointerX, pointerY, Fade(PathInk, .5f), 1);
      if (cur.size() >= 2 && ScreenDist(cur[0].a, pointerX, pointerY) <= 8) {
        auto f = Screen(cur[0].a);
        StrokeEllipse(f.x, f.y, 7, 7, PathInk, 1.4f);  // closes here
      }
    }
    for (size_t i = 0; i < cur.size(); i++) DrawAnchor(Screen(cur[i].a), i + 1 == cur.size());
    if (kind == 0) DrawHandles(cur.back());
  }
  bool Options(Container& dock, std::vector<Widget*>& out) override {
    PathOptions(dock, out, kind == 1 ? L"Draw the path" : kind == 2 ? L"Click points · Enter ends" : L"Click corners, drag curves · Enter ends");
    return true;
  }
  void Tick() override { if (!dragging && !drawing && !editing && WorkChanged() && !jobs::Busy()) { if (uint64_t(docInfo["id"].Int()) != workDocument) cur.clear(); LoadWork(); } }
  HCURSOR Cursor() override { return LoadCursor(nullptr,GetKeyState(VK_CONTROL) < 0 ? IDC_ARROW : IDC_CROSS); }
  void Context(double, double, const PointerEvent&, std::vector<MenuItem>& rows) override {
    bool drawing = cur.size() >= 2;
    if (drawing) {
      rows.push_back(ActionItem(L"Close Path", [this] { Commit(true); }));
      rows.push_back(ActionItem(L"End Open Path", [this] { Commit(false); }));
      rows.push_back(ActionItem(L"Delete Last Anchor Point", [this] {
        if (cur.empty()) return;
        cur.pop_back();
        if (kind == 2) Curvature();
      }));
      rows.push_back(MenuItem::Separator());
    }
    // A path being drawn becomes part of the work path first (closed: a selection wants an area).
    PathMenu(rows, drawing ? std::function<void()>([this] { Commit(cur.size() >= 3); }) : nullptr, drawing || !work.empty());
  }

 private:
  std::unique_ptr<CanvasTool> direct;
  bool editing = false, convert = false;
  int curEdit = -1;
  Knot editBefore; D2D_POINT_2F editStart{};
  // Curvature Pen: every inner point is smooth, its handles along the line through its neighbours.
  void Curvature() {
    size_t n = cur.size();
    for (size_t i = 0; i < n; i++) {
      Knot& k = cur[i];
      if (i == 0 || i + 1 == n) {
        k.in = k.out = k.a;
        k.smooth = false;
        continue;
      }
      D2D_POINT_2F t{(cur[i + 1].a.x - cur[i - 1].a.x) / 6, (cur[i + 1].a.y - cur[i - 1].a.y) / 6};
      k.in = Minus(k.a, t);
      k.out = Plus(k.a, t);
      k.smooth = true;
    }
  }
  // Freeform Pen: the drawn line, simplified, with smooth handles so it keeps flowing.
  void Freeform() {
    if (trail.size() < 2) {
      trail.clear();
      return;
    }
    float tol = 1.6f / std::max(.01f, canvas->Zoom());
    std::vector<bool> keep(trail.size(), false);
    keep.front() = keep.back() = true;
    Rdp(trail, 0, trail.size() - 1, tol, keep);
    std::vector<D2D_POINT_2F> pts;
    for (size_t i = 0; i < trail.size(); i++)
      if (keep[i]) pts.push_back(trail[i]);
    auto last = Screen(pts.back());
    bool closed = pts.size() > 3 && ScreenDist(pts.front(), last.x, last.y) <= 10;
    if (closed) pts.pop_back();
    size_t n = pts.size();
    cur.clear();
    for (size_t i = 0; i < n; i++) {
      Knot k;
      k.a = k.in = k.out = pts[i];
      bool end = !closed && (i == 0 || i + 1 == n);
      if (!end && n > 2) {
        D2D_POINT_2F prev = pts[(i + n - 1) % n], next = pts[(i + 1) % n];
        D2D_POINT_2F t{(next.x - prev.x) / 6, (next.y - prev.y) / 6};
        k.in = Minus(k.a, t);
        k.out = Plus(k.a, t);
        k.smooth = true;
      }
      cur.push_back(k);
    }
    trail.clear();
    Commit(closed);
  }
  // Clicking an end anchor of an open path takes that sub-path back into the path being drawn, its
  // end at the clicked anchor; Commit puts it back, longer. Esc or a too-short result restores it.
  bool ContinuePath(int sub, int knot) {
    if (sub < 0 || size_t(sub) >= work.size()) return false;
    const auto& sp = work[size_t(sub)];
    size_t n = sp.knots.size();
    if (sp.closed || n < 2 || knot < 0 || (knot != 0 && size_t(knot) + 1 != n)) return false;
    cur = sp.knots;
    if (knot == 0) { std::reverse(cur.begin(),cur.end()); for (auto& k : cur) std::swap(k.in,k.out); }
    continuedOp = sp.op; continuing = true;
    work.erase(work.begin() + sub);
    return true;
  }
  void Abandon() { cur.clear(); if (continuing) { continuing = false; LoadWork(); } }
  void Commit(bool closed) {
    if (cur.size() < 2) {
      Abandon();
      return;
    }
    SubPath sp;
    sp.knots = cur;
    sp.closed = closed;
    if (continuing) { sp.op = continuedOp; continuing = false; }
    auto subs = work;
    subs.push_back(std::move(sp));
    cur.clear();
    SetWork(std::move(subs));
  }
  int kind;
  std::vector<Knot> cur;
  std::vector<D2D_POINT_2F> trail;
  bool dragging = false, drawing = false, continuing = false;
  std::string continuedOp = "combine";
};

// ==================================================================== path selection ==
class PathSelectTool : public CanvasTool {
 public:
  explicit PathSelectTool(bool d) : direct(d) {}
  void Activate() override { LoadWork(); }
  void Down(double x, double y, const PointerEvent& e) override {
    start = {float(x), float(y)};
    before = work;
    moved = false;
    part = -1;
    if (direct) {
      if (Valid()) {
        const Knot& k = work[size_t(si)].knots[size_t(ki)];
        if (ScreenDist(k.in, e.x, e.y) <= 7 && ScreenDist(k.in, Screen(k.a).x, Screen(k.a).y) > 1) { part = 1; return; }
        if (ScreenDist(k.out, e.x, e.y) <= 7 && ScreenDist(k.out, Screen(k.a).x, Screen(k.a).y) > 1) { part = 2; return; }
      }
      for (size_t s = 0; s < work.size(); s++)
        for (size_t k = 0; k < work[s].knots.size(); k++)
          if (ScreenDist(work[s].knots[k].a, e.x, e.y) <= 7) {
            si = int(s);
            ki = int(k);
            part = e.mods.alt ? 4 : 0;
            return;
          }
      auto hit = NearPath(work,e.x,e.y);
      if (hit.sub >= 0) { si = hit.sub; ki = hit.knot; bendT = hit.t; part = 5; return; }
      si = ki = -1;
      return;
    }
    BOOL hit = FALSE;
    if (auto g = Geometry(work)) {
      g->StrokeContainsPoint(D2D1::Point2F(e.x, e.y), 8, nullptr, D2D1::IdentityMatrix(), &hit);
      if (!hit) g->FillContainsPoint(D2D1::Point2F(e.x, e.y), D2D1::IdentityMatrix(), &hit);
    }
    selected = hit != FALSE;
    part = selected ? 3 : -1;
  }
  void Drag(double x, double y, const PointerEvent& e) override {
    if (part < 0) return;
    D2D_POINT_2F p{float(x), float(y)}, d = Minus(p, start);
    if (e.mods.shift) { auto snap = gestures::Snap45(d.x,d.y); d = {float(snap.first),float(snap.second)}; p = Plus(start,d); }
    moved = std::hypot(d.x,d.y) > .001f;
    delta = d;
    work = before;
    if (part == 3) {
      for (auto& sp : work)
        for (auto& k : sp.knots) {
          k.a = Plus(k.a, d);
          k.in = Plus(k.in, d);
          k.out = Plus(k.out, d);
        }
      return;
    }
    if (!Valid()) return;
    Knot& k = work[size_t(si)].knots[size_t(ki)];
    if (part == 0) {
      k.a = Plus(k.a, d);
      k.in = Plus(k.in, d);
      k.out = Plus(k.out, d);
    } else if (part == 4) {
      k.out = moved ? p : k.a; k.in = moved ? Mirror(k.a,p) : k.a; k.smooth = moved;
    } else if (part == 5) {
      auto& next = work[size_t(si)].knots[(size_t(ki)+1)%work[size_t(si)].knots.size()];
      float weight = float(3*bendT*(1-bendT));
      D2D_POINT_2F shift{d.x / std::max(.001f,weight),d.y / std::max(.001f,weight)};
      k.out = Plus(k.out,shift); next.in = Plus(next.in,shift);
    } else {
      // A handle; a smooth point keeps its other handle opposite (Alt breaks it).
      if (e.mods.shift) { auto snap = gestures::Snap45(p.x-k.a.x,p.y-k.a.y); p = {float(k.a.x+snap.first),float(k.a.y+snap.second)}; }
      if (e.mods.alt) k.smooth = false;
      if (part == 1) {
        k.in = p;
        if (k.smooth) k.out = Mirror(k.a, p);
      } else {
        k.out = p;
        if (k.smooth) k.in = Mirror(k.a, p);
      }
    }
  }
  void Up(double x, double y, const PointerEvent& e) override {
    Drag(x, y, e);
    if (part >= 0 && (moved || part == 4)) {
      auto args = PathParams(si,ki);
      if (part == 0 || part == 3) MoveAnchors(delta);
      else if (part == 5) { args.Num("t",bendT).Add("move",P2(delta)); PathEdit("path.bendSegment",args); }
      else if (part == 4) { if (moved) args.Add("out",P2(work[size_t(si)].knots[size_t(ki)].out)); PathEdit("path.convertPoint",args); }
      else { args.Str("handle",part == 1 ? "in" : "out").Add("to",P2(part == 1 ? work[size_t(si)].knots[size_t(ki)].in : work[size_t(si)].knots[size_t(ki)].out)).Bool("independent",e.mods.alt); PathEdit("path.moveHandle",args); }
    }
    part = -1;
  }
  void Cancel() override { if (part >= 0) work = before; part = -1; }
  bool Key(UINT vk, Mods mods) override {
    if (vk == VK_ESCAPE && part >= 0) { Cancel(); return true; }
    if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN) {
      if (!(direct ? Valid() : selected)) return false;
      float step = mods.shift ? 10.f : 1.f;
      MoveAnchors({vk == VK_LEFT ? -step : vk == VK_RIGHT ? step : 0,vk == VK_UP ? -step : vk == VK_DOWN ? step : 0}); return true;
    }
    if (vk != VK_DELETE && vk != VK_BACK) return false;
    if (direct && Valid()) {
      PathEdit("path.deleteAnchor",PathParams(si,ki));
      si = ki = -1;
      return true;
    }
    if (!direct && selected) {
      selected = false;
      SetWork({});
      return true;
    }
    return false;
  }
  void Overlay() override {
    auto g = Geometry(work);
    if (!g) return;
    DrawPathLine(g.Get(), 1);
    for (size_t s = 0; s < work.size(); s++)
      for (size_t k = 0; k < work[s].knots.size(); k++) {
        bool on = direct ? (int(s) == si && int(k) == ki) : selected;
        if (direct || selected) DrawAnchor(Screen(work[s].knots[k].a), on);
      }
    if (direct && Valid()) DrawHandles(work[size_t(si)].knots[size_t(ki)]);
  }
  HCURSOR Cursor() override { return LoadCursor(nullptr, IDC_ARROW); }
  void Tick() override { if (part < 0 && WorkChanged() && !jobs::Busy()) { if (uint64_t(docInfo["id"].Int()) != workDocument || uint64_t(docInfo["activeLayer"].Int()) != workLayer) { si = ki = -1; selected = false; } LoadWork(); } }
  bool Options(Container& dock, std::vector<Widget*>& out) override {
    PathOptions(dock, out, direct ? L"Drag points and handles · Delete removes" : L"Drag the path · Delete removes");
    return true;
  }
  void Context(double, double, const PointerEvent& e, std::vector<MenuItem>& rows) override {
    if (direct) {
      for (size_t s = 0; s < work.size(); s++)
        for (size_t k = 0; k < work[s].knots.size(); k++)
          if (ScreenDist(work[s].knots[k].a, e.x, e.y) <= 7) {
            si = int(s);
            ki = int(k);
          }
      if (Valid()) {
        rows.push_back(ActionItem(L"Delete Anchor Point", [this] { Key(VK_DELETE, {}); }));
        rows.push_back(MenuItem::Separator());
      }
    }
    PathMenu(rows, nullptr, !work.empty());
  }

 private:
  void MoveAnchors(D2D_POINT_2F d) {
    std::string anchors = "[";
    if (direct) anchors += "[" + std::to_string(si) + "," + std::to_string(ki) + "]";
    else for (size_t s = 0; s < work.size(); s++) for (size_t k = 0; k < work[s].knots.size(); k++) { if (anchors.size() > 1) anchors += ","; anchors += "[" + std::to_string(s) + "," + std::to_string(k) + "]"; }
    auto args = PathParams(0,0); args.Add("anchors",anchors+"]").Add("move",P2(d)); PathEdit("path.moveAnchors",args);
  }
  bool Valid() const { return si >= 0 && size_t(si) < work.size() && ki >= 0 && size_t(ki) < work[size_t(si)].knots.size(); }
  bool direct;
  int si = -1, ki = -1, part = -1;  // part: 0 anchor, 1 in handle, 2 out handle, 3 the whole path
  bool selected = false, moved = false;
  D2D_POINT_2F start{};
  D2D_POINT_2F delta{};
  double bendT = .5;
  std::vector<SubPath> before;
};

// ======================================================================= source tools ==
// Clone Stamp and Healing Brush: Alt-click picks the source; strokes paint from it. Aligned, the
// source keeps its offset from stroke to stroke, as the engine reports it.
class SourceTool : public CanvasTool {
 public:
  explicit SourceTool(std::string c) : cmd(std::move(c)) {}
  void Down(double x, double y, const PointerEvent& e) override {
    if (e.mods.alt) {
      src = {float(x), float(y)};
      has = true;
      offsetKnown = false;
      ui::Toast(L"Source set");
      return;
    }
    if (!has) {
      ui::Toast(L"Alt-click to choose where to sample from", true);
      return;
    }
    pts = {{float(x), float(y)}};
    stroking = true;
  }
  void Drag(double x, double y, const PointerEvent&) override {
    if (stroking) pts.push_back({float(x), float(y)});
  }
  void Up(double x, double y, const PointerEvent&) override {
    if (!stroking) return;
    stroking = false;
    pts.push_back({float(x), float(y)});
    JsonObject o;
    o.Add("points", PointList(pts, true)).Num("size", brush.size).Num("hardness", brush.hardness).Num("opacity", std::max(1.0, brush.opacity));
    o.Bool("aligned", aligned).Str("sampleLayer", SampleNames[sample]);
    if (aligned && offsetKnown) o.Add("offset", "[" + JsonNum(offset.x) + "," + JsonNum(offset.y) + "]");
    else o.Add("source", Pt(src.x, src.y));
    pts.clear();
    Exec(cmd, o.Text(), [this](bool ok, const std::string& r) {
      if (!ok) return;
      const Json j = Json::Parse(r);
      if (j["offset"].size() >= 2) {
        offset = {float(j["offset"][0].Num()), float(j["offset"][1].Num())};
        offsetKnown = true;
      }
    });
  }
  void Overlay() override {
    // The stroke so far, then the brush and the source under it.
    std::vector<D2D_POINT_2F> s;
    for (auto& p : pts) s.push_back(Screen(p));
    if (auto g = Polyline(s, false)) Dc()->DrawGeometry(g.Get(), Ink(Rgba(1, .93f, .81f, .28f)), std::max(2.f, float(brush.size * canvas->Zoom())), RoundStyle());
    if (pointerX < 0 || !canvas->PointerOver()) return;
    float r = float(brush.size * .5 * canvas->Zoom());
    StrokeEllipse(pointerX, pointerY, r + .8f, r + .8f, Rgba(0, 0, 0, .5f), 1.2f);
    StrokeEllipse(pointerX, pointerY, r, r, Rgba(1, 1, 1, .9f), 1);
    if (!has) return;
    D2D_POINT_2F at = src;
    auto here = canvas->ToDoc(pointerX, pointerY);
    if (stroking && !pts.empty()) at = aligned && offsetKnown ? Plus(pts.back(), offset) : Plus(src, Minus(pts.back(), pts.front()));
    else if (aligned && offsetKnown) at = Plus(here, offset);
    auto c = Screen(at);
    StrokeEllipse(c.x, c.y, r, r, Fade(P.accent, .9f), 1.2f);
    Line(c.x - 6, c.y, c.x + 6, c.y, P.accent, 1.4f);
    Line(c.x, c.y - 6, c.x, c.y + 6, P.accent, 1.4f);
  }
  bool HidesCursor() override { return true; }
  bool Options(Container& dock, std::vector<Widget*>& out) override {
    auto slider = [&](const wchar_t* label, double lo, double hi, double v, const wchar_t* unit, std::function<void(double)> f, bool log = false) {
      auto* s = dock.Add<Slider>(label, lo, hi, v, 0, unit);
      s->compact = true;
      s->labelWidth = Measure(label, F_Label) + 12;
      s->logScale = log;
      s->onChange = std::move(f);
      out.push_back(s);
    };
    slider(L"Size", 1, 2500, brush.size, L" px", [](double v) { brush.size = v; }, true);
    slider(L"Hardness", 0, 100, brush.hardness, L"%", [](double v) { brush.hardness = v; });
    slider(L"Opacity", 1, 100, brush.opacity, L"%", [](double v) { brush.opacity = v; });
    auto* t = dock.Add<Toggle>(L"Aligned", aligned);
    t->onChange = [this](bool v) { aligned = v; };
    out.push_back(t);
    auto* l = dock.Add<SelectList>(std::vector<std::wstring>{L"Current Layer", L"Current & Below", L"All Layers"}, sample);
    l->onChange = [this](int i) { sample = i; };
    out.push_back(l);
    return true;
  }

 private:
  static constexpr const char* SampleNames[3] = {"current", "currentAndBelow", "all"};
  std::string cmd;
  bool has = false, offsetKnown = false, aligned = true, stroking = false;
  int sample = 0;
  D2D_POINT_2F src{}, offset{};
  std::vector<D2D_POINT_2F> pts;
};

// A cubic Bézier patch: 4×4 control points, row-major (rows run down the frame).
// ==================================================================== Perspective Crop ==
// A frame drawn on the canvas whose four corners then move freely onto the sides of something
// seen at an angle; Enter (or Apply) maps that quad onto an upright canvas.
class PerspectiveCropTool : public CanvasTool {
 public:
  void Down(double x, double y, const PointerEvent& e) override {
    start = {float(x), float(y)};
    for (int i = 0; i < 4; i++) q0[i] = q[i];
    grab = has ? Hit(e.x, e.y) : -1;
    if (grab < 0) {  // a new frame
      grab = 5;
      for (auto& c : q) c = start;
      has = true;
    }
    dragging = true;
  }
  void Drag(double x, double y, const PointerEvent&) override {
    if (!dragging) return;
    D2D_POINT_2F p{float(x), float(y)}, d = Minus(p, start);
    if (grab == 5) {
      float l = std::min(start.x, p.x), t = std::min(start.y, p.y), r = std::max(start.x, p.x), b = std::max(start.y, p.y);
      q[0] = {l, t};
      q[1] = {r, t};
      q[2] = {r, b};
      q[3] = {l, b};
    } else if (grab == 4) {
      for (int i = 0; i < 4; i++) q[i] = Plus(q0[i], d);
    } else if (grab >= 0) {
      q[grab] = Plus(q0[grab], d);
    }
  }
  void Up(double x, double y, const PointerEvent& e) override {
    Drag(x, y, e);
    dragging = false;
    if (grab == 5 && (std::fabs(q[2].x - q[0].x) * canvas->Zoom() < 6 || std::fabs(q[2].y - q[0].y) * canvas->Zoom() < 6)) has = false;
    grab = -1;
    RebuildOptions();
  }
  bool Key(UINT vk, Mods) override {
    if (!has) return false;
    if (vk == VK_RETURN) { Commit(); return true; }
    if (vk == VK_ESCAPE) { Cancel(); return true; }
    return false;
  }
  void Deactivate() override { has = false; }
  void Overlay() override {
    if (!has) return;
    D2D_POINT_2F s[4];
    for (int i = 0; i < 4; i++) s[i] = Screen(q[i]);
    // Shade the page outside the quad (alternate fill: page minus quad).
    Rect page = canvas->DocOnScreen();
    ComPtr<ID2D1PathGeometry> g;
    if (SUCCEEDED(GfxFactory()->CreatePathGeometry(&g))) {
      ComPtr<ID2D1GeometrySink> sink;
      if (SUCCEEDED(g->Open(&sink))) {
        sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);
        sink->BeginFigure({page.left, page.top}, D2D1_FIGURE_BEGIN_FILLED);
        D2D_POINT_2F pc[3] = {{page.right, page.top}, {page.right, page.bottom}, {page.left, page.bottom}};
        sink->AddLines(pc, 3);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->BeginFigure(s[0], D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLines(s + 1, 3);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        Dc()->FillGeometry(g.Get(), Ink(Rgba(0, 0, 0, .45f)));
      }
    }
    // The grid follows the perspective: lines between matching points of opposite sides.
    auto lerp = [](D2D_POINT_2F a, D2D_POINT_2F b, float t) { return D2D_POINT_2F{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}; };
    for (int i = 1; i < 4; i++) {
      float t = i / 4.f;
      D2D_POINT_2F a = lerp(s[0], s[1], t), b = lerp(s[3], s[2], t), c = lerp(s[0], s[3], t), d = lerp(s[1], s[2], t);
      Line(a.x, a.y, b.x, b.y, Rgba(1, 1, 1, .35f), .8f);
      Line(c.x, c.y, d.x, d.y, Rgba(1, 1, 1, .35f), .8f);
    }
    std::vector<D2D_POINT_2F> poly(s, s + 4);
    if (auto o = Polyline(poly, true)) {
      Dc()->DrawGeometry(o.Get(), Ink(Rgba(0, 0, 0, .55f)), 2.6f);
      Dc()->DrawGeometry(o.Get(), Ink(Convex() ? Rgba(1, .97f, .92f, .95f) : Rgba(1, .45f, .4f, .95f)), 1.2f);
    }
    for (int i = 0; i < 4; i++) {
      Rect r = R(s[i].x - 4.5f, s[i].y - 4.5f, s[i].x + 4.5f, s[i].y + 4.5f);
      FillRect(r, Rgba(1, 1, 1, 1));
      StrokeRound(r, 0, Rgba(0, 0, 0, .7f), 1);
    }
  }
  HCURSOR Cursor() override {
    int h = has ? Hit(pointerX, pointerY) : -1;
    return LoadCursor(nullptr, h >= 0 ? IDC_SIZEALL : IDC_CROSS);
  }
  bool Options(Container& dock, std::vector<Widget*>& out) override {
    out.push_back(dock.Add<Label>(has ? L"Move the corners onto the edges · Enter crops" : L"Draw a frame, then fit its corners", F_Label));
    if (has) {
      out.push_back(dock.Add<TextButton>(L"Cancel", TextButton::Secondary, [this] { Cancel(); }));
      out.push_back(dock.Add<TextButton>(L"Apply crop", TextButton::Primary, [this] { Commit(); }));
    }
    return true;
  }
  void Context(double, double, const PointerEvent&, std::vector<MenuItem>& rows) override {
    if (!has) return;
    rows.push_back(ActionItem(L"Apply crop", [this] { Commit(); }, PhCrop, Convex()));
    rows.push_back(ActionItem(L"Cancel", [this] { Cancel(); }));
  }

 private:
  // 0..3 a corner, 4 inside, -1 elsewhere.
  int Hit(float sx, float sy) const {
    for (int i = 0; i < 4; i++)
      if (ScreenDist(q[i], sx, sy) <= 9) return i;
    D2D_POINT_2F s[4];
    for (int i = 0; i < 4; i++) s[i] = Screen(q[i]);
    bool in = false;
    for (int i = 0, j = 3; i < 4; j = i++)
      if ((s[i].y > sy) != (s[j].y > sy) && sx < (s[j].x - s[i].x) * (sy - s[i].y) / (s[j].y - s[i].y) + s[i].x) in = !in;
    return in ? 4 : -1;
  }
  bool Convex() const {
    int sign = 0;
    for (int i = 0; i < 4; i++) {
      D2D_POINT_2F a = Minus(q[(i + 1) % 4], q[i]), b = Minus(q[(i + 2) % 4], q[(i + 1) % 4]);
      float z = a.x * b.y - a.y * b.x;
      if (std::fabs(z) < 1e-3f) return false;
      int sg = z > 0 ? 1 : -1;
      if (sign && sg != sign) return false;
      sign = sg;
    }
    return true;
  }
  void Cancel() {
    has = false;
    RebuildOptions();
  }
  void Commit() {
    if (!has) return;
    if (!Convex()) {
      ui::Toast(L"The frame has to stay convex", true);
      return;
    }
    std::string corners = "[";
    for (int i = 0; i < 4; i++) corners += (i ? "," : "") + P2(q[i]);
    JsonObject o;
    o.Add("corners", corners + "]");
    has = false;
    RebuildOptions();
    Exec("image.perspectiveCrop", o.Text());
  }
  bool has = false, dragging = false;
  int grab = -1;  // 0..3 corner, 4 move, 5 a new frame
  D2D_POINT_2F q[4]{}, q0[4]{}, start{};
};
}  // namespace

CanvasTool* NewPenTool(int kind) { return new PenTool(kind); }
CanvasTool* NewPathSelectTool(bool direct) { return new PathSelectTool(direct); }
CanvasTool* NewSourceTool(const std::string& command) { return new SourceTool(command); }

CanvasTool* NewPerspectiveCropTool() { return new PerspectiveCropTool(); }
