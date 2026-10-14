// Free Transform owns its frame and editable Warp model; the engine owns all document pixels.
#include "shell.h"
#include "guides.h"
#include "i18n.h"
#include "icons_phosphor.h"
#include <array>
#include <cmath>

namespace {
using Point = D2D_POINT_2F;
Point Add(Point a, Point b) { return {a.x + b.x, a.y + b.y}; }
Point Sub(Point a, Point b) { return {a.x - b.x, a.y - b.y}; }
Point Mul(Point a, float k) { return {a.x * k, a.y * k}; }
float Length(Point p) { return std::hypot(p.x, p.y); }
Point Unit(Point p) { return Mul(p, 1 / std::max(.001f, Length(p))); }
float Dot(Point a, Point b) { return a.x * b.x + a.y * b.y; }
Point LerpPoint(Point a, Point b, float t) { return Add(a, Mul(Sub(b, a), t)); }
float Bernstein(int i, float t) { float s = 1 - t; return i == 0 ? s*s*s : i == 1 ? 3*s*s*t : i == 2 ? 3*s*t*t : t*t*t; }
std::string Points(const std::vector<Point>& pts) { std::string s = "["; for (auto p : pts) { if (s.size() > 1) s += ','; s += Pt(p.x, p.y); } return s + "]"; }
std::string Numbers(const std::vector<float>& v) { std::string s = "["; for (float x : v) { if (s.size() > 1) s += ','; s += JsonNum(x); } return s + "]"; }

struct Mesh {
  std::vector<float> us{0, 1}, vs{0, 1};
  std::vector<Point> points;
  size_t Columns() const { return (us.size() - 1) * 3 + 1; }
  std::string JsonText() const { return "{\"us\":" + Numbers(us) + ",\"vs\":" + Numbers(vs) + ",\"points\":" + Points(points) + "}"; }
  bool Read(const Json& j) {
    if (j["us"].size() < 2 || j["vs"].size() < 2 || j["us"].size() > 17 || j["vs"].size() > 17) return false;
    Mesh m; m.us.clear(); m.vs.clear();
    for (auto [key, knots] : {std::pair{"us", &m.us}, std::pair{"vs", &m.vs}}) {
      for (size_t i = 0; i < j[key].size(); i++) {
        float v = float(j[key][i].Num(-1));
        if (!std::isfinite(v) || v < 0 || v > 1 || (!knots->empty() && v <= knots->back())) return false;
        knots->push_back(v);
      }
      if (knots->front() != 0 || knots->back() != 1) return false;
    }
    size_t count = m.Columns() * ((m.vs.size() - 1) * 3 + 1);
    if (j["points"].size() != count) return false;
    for (size_t i = 0; i < count; i++) {
      Point p{float(j["points"][i][0].Num()), float(j["points"][i][1].Num())};
      if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 1e7 || std::abs(p.y) > 1e7) return false;
      m.points.push_back(p);
    }
    *this = std::move(m); return true;
  }
  size_t Cell(const std::vector<float>& k, float t) const { for (size_t i = 0; i + 2 < k.size(); i++) if (t <= k[i + 1]) return i; return k.size() - 2; }
  Point At(float u, float v) const {
    if (points.empty()) return {};
    size_t x = Cell(us, u), y = Cell(vs, v), cols = Columns();
    float a = (u - us[x]) / (us[x + 1] - us[x]), b = (v - vs[y]) / (vs[y + 1] - vs[y]);
    Point out{};
    for (size_t r = 0; r < 4; r++) for (size_t c = 0; c < 4; c++) out = Add(out, Mul(points[(y * 3 + r) * cols + x * 3 + c], Bernstein(int(c), a) * Bernstein(int(r), b)));
    return out;
  }
};

class ReferenceGrid : public Widget {
 public:
  float NaturalWidth() const override { return 40; }
  int index = 4;
  std::function<void(int)> change;
  void Paint() override {
    for (int y = 0; y < 3; y++) for (int x = 0; x < 3; x++) {
      float cx = rect.left + (x + .5f) * W(rect) / 3, cy = rect.top + (y + .5f) * H(rect) / 3;
      FillEllipse(cx, cy, index == y * 3 + x ? 3.f : 1.6f, index == y * 3 + x ? 3.f : 1.6f, index == y * 3 + x ? P.accent : P.dim);
    }
  }
  void OnUp(const PointerEvent& e, bool inside) override { Widget::OnUp(e, inside); if (inside && e.button == 0) { index = std::clamp(int((e.y - rect.top) / H(rect) * 3), 0, 2) * 3 + std::clamp(int((e.x - rect.left) / W(rect) * 3), 0, 2); if (change) change(index); } }
};

const std::vector<std::string> WarpStyles = {"custom","arc","arcLower","arcUpper","arch","bulge","shellLower","shellUpper","flag","wave","fish","rise","fisheye","inflate","squeeze","twist"};
const std::vector<std::wstring> WarpLabels = {L"Custom",L"Arc",L"Arc Lower",L"Arc Upper",L"Arch",L"Bulge",L"Shell Lower",L"Shell Upper",L"Flag",L"Wave",L"Fish",L"Rise",L"Fisheye",L"Inflate",L"Squeeze",L"Twist"};

class TransformFrame : public CanvasTool {
 public:
  bool active = false, selection = false, warp = false;
  std::array<Point, 4> q{};
  Point pivot{};
  Mesh mesh;
  void Begin(bool outline) {
    if (active || docInfo.IsNull()) return;
    auto b = outline ? docInfo["selectionBounds"] : ActiveLayer() ? (*ActiveLayer())["bounds"] : Json{};
    if (!outline && docInfo["hasSelection"].Truthy() && ActiveLayer() && (*ActiveLayer())["kind"].Str() == "Pixel") {
      auto sb = docInfo["selectionBounds"]; double x = std::max(b[0].Num(), sb[0].Num()), y = std::max(b[1].Num(), sb[1].Num());
      double r = std::min(b[0].Num() + b[2].Num(), sb[0].Num() + sb[2].Num()), bot = std::min(b[1].Num() + b[3].Num(), sb[1].Num() + sb[3].Num());
      b = Json::Parse("[" + JsonNum(x) + "," + JsonNum(y) + "," + JsonNum(r - x) + "," + JsonNum(bot - y) + "]");
    }
    if (b.size() != 4 || b[2].Num() <= 0 || b[3].Num() <= 0) { ui::Toast(L"Nothing to transform on this layer", true); return; }
    frame = {float(b[0].Num()), float(b[1].Num()), float(b[0].Num() + b[2].Num()), float(b[1].Num() + b[3].Num())};
    q = {{{frame[0],frame[1]},{frame[2],frame[1]},{frame[2],frame[3]},{frame[0],frame[3]}}};
    selection = outline; pivot = Center(); reference = 4; warp = false; active = true; dragging = pending = false;
    interp = outline ? 1 : 0;
    interaction = 0; style = 0; grid = 1; bend = 50; hDistort = vDistort = 0; skew = 0; linked = true;
    xSign = ySign = 1;
    mesh = {}; undo.clear(); live = std::make_shared<LivePreview>(outline ? "select.transformSelection" : "edit.transform");
    EnterMode(this);
  }
  void StartWarp() {
    if (!active || warp) return;
    warp = true; mesh = {}; mesh.points.clear();
    undo.clear();
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) mesh.points.push_back(QuadAt(x / 3.f, y / 3.f));
    live->Hide(); live = std::make_shared<LivePreview>(selection ? "select.transformSelection" : "edit.transform.warp");
    pending = true; RebuildOptions();
  }
  void Down(double x, double y, const PointerEvent& e) override {
    if (!active) return;
    before = State(); start = {float(x), float(y)}; grab = warp ? WarpHit(e.x, e.y) : Hit(e.x, e.y);
    if (!warp && e.mods.alt && grab >= 8) { pivot = start; reference = -1; grab = 10; }
    dragging = grab >= 0;
    if(dragging) { BeginMoveSnap(); if(selection) BeginPointSnap(false); }
  }
  void Drag(double x, double y, const PointerEvent& e) override {
    if (!dragging) return;
    Point p{float(x),float(y)};
    // Snap geometry handles; the rotation gesture retains its angle constraints.
    if(warp || grab==10 || (grab>=0 && grab<8 && interaction!=2)) p=SnapViewPoint(p);
    Point d = Sub(p,start);
    if(!warp && grab==8) { auto offset=SnapMoveOffset(d.x,d.y,{}); d=offset; }
    q = before.q; pivot = before.pivot; xSign = before.xSign; ySign = before.ySign;
    if (warp) {
      mesh = before.mesh;
      if (grab < int(mesh.points.size())) {
        mesh.points[size_t(grab)] = Add(mesh.points[size_t(grab)], d);
        size_t row = size_t(grab) / mesh.Columns(), col = size_t(grab) % mesh.Columns();
        if (row % 3 == 0 && col % 3 == 0 && !e.mods.alt) {
          for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            int r = int(row)+dy,c = int(col)+dx;
            if ((dx || dy) && r >= 0 && c >= 0 && c < int(mesh.Columns()) && size_t(r)*mesh.Columns()+size_t(c) < mesh.points.size()) mesh.points[size_t(r)*mesh.Columns()+size_t(c)] = Add(mesh.points[size_t(r)*mesh.Columns()+size_t(c)],d);
          }
        }
      } else Bend(d);
      style = 0;
    } else if (grab == 10) { pivot = p; reference = -1; SyncNumbers(); return; }
    else if (grab == 8) { for (auto& a : q) a = Add(a,d); pivot = Add(pivot,d); }
    else if (grab == 9 || interaction == 2) {
      float angle = std::atan2(p.y - pivot.y, p.x - pivot.x) - std::atan2(start.y - pivot.y, start.x - pivot.x);
      if (e.mods.shift) angle = std::round(angle / (Pi / 12)) * (Pi / 12);
      Rotate(angle * 180 / Pi);
    } else if (grab < 4 && (interaction == 5 || (e.mods.ctrl && e.mods.alt && e.mods.shift))) {
      // The corner and its neighbour along the dominant drag direction move in mirror image.
      Point eu = Unit(Sub(before.q[1],before.q[0])), ev = Unit(Sub(before.q[3],before.q[0]));
      bool vertical = std::abs(Dot(d,ev)) > std::abs(Dot(d,eu)) * 1.05f + .5f;  // a diagonal drag keeps the horizontal pair
      Point edge = vertical ? ev : eu;
      Point mirrored = Sub(d,Mul(edge, 2 * Dot(d,edge)));
      q[size_t(grab)] = Add(q[size_t(grab)],d);
      size_t mate = size_t(vertical ? 3 - grab : grab ^ 1);
      q[mate] = Add(q[mate],mirrored);
    } else if (grab < 4 && (e.mods.ctrl || interaction == 4)) q[size_t(grab)] = Add(q[size_t(grab)],d);
    else if (grab >= 4 && grab < 8 && (e.mods.ctrl || interaction == 3)) {
      int edge = grab - 4; Point axis = Sub(q[size_t((edge + 1) % 4)],q[size_t(edge)]); axis = Mul(axis,1 / std::max(.001f,Length(axis)));
      Point move = Mul(axis,Dot(d,axis)); q[size_t(edge)] = Add(q[size_t(edge)],move); q[size_t((edge + 1) % 4)] = Add(q[size_t((edge + 1) % 4)],move);
    } else Scale(p, e.mods.shift ? !linked : linked, e.mods.alt);
    Changed(false);
  }
  void Up(double x, double y, const PointerEvent& e) override {
    if (!dragging) return;
    Drag(x,y,e); dragging = false; EndViewSnap();
    if (Same(before)) return;  // a click that changed nothing is not an undo step
    undo.push_back(before); BoundUndo(); pending = grab != 10;
  }
  void Tick() override { if (active && pending && live && !jobs::Busy() && !live->Busy() && (!dragging || ui::NowMs() - changedAt > 60)) { pending = false; live->Show(Params()); } }
  bool Key(UINT vk, Mods m) override {
    if (!active) return false;
    if (vk == VK_RETURN) { Finish(true); return true; }
    if (vk == VK_ESCAPE) { Finish(false); return true; }
    if (m.ctrl && vk == 'Z') { if (!undo.empty()) { Restore(undo.back()); undo.pop_back(); Changed(false); if (warp) RebuildOptions(); } return true; }
    if (vk >= VK_LEFT && vk <= VK_DOWN) {
      Save(); Point d{float(vk == VK_LEFT ? -1 : vk == VK_RIGHT ? 1 : 0), float(vk == VK_UP ? -1 : vk == VK_DOWN ? 1 : 0)}; d = Mul(d,m.shift ? 10.f : 1.f);
      for (auto& p : q) p = Add(p,d); for (auto& p : mesh.points) p = Add(p,d); pivot = Add(pivot,d); Changed(false); return true;
    }
    return true;
  }
  void Cancel() override { Finish(false); }
  void Deactivate() override { if (active) Finish(true); }
  void Finish(bool keep, bool restart = false) {
    if (!active) return;
    active = false; dragging = pending = false; EndViewSnap();
    bool outline = selection;
    if (live) live->Finish(keep && !Unchanged(), Params(),restart ? std::function<void()>([outline] { StartTransform(outline); }) : nullptr); live.reset(); warp = false; fields.clear(); LeaveMode(this);
  }
  void Overlay() override {
    if (!active) return;
    if (warp) {
      auto us = mesh.us, vs = mesh.vs;
      if (us.size() == 2) us = {0,1.f/3,2.f/3,1}; if (vs.size() == 2) vs = {0,1.f/3,2.f/3,1};
      for (int axis = 0; axis < 2; axis++) for (float k : axis ? vs : us) {
        std::vector<Point> line; for (int i = 0; i <= 48; i++) { auto p = axis ? mesh.At(i / 48.f,k) : mesh.At(k,i / 48.f); line.push_back(canvas->ToScreen(p.x,p.y)); }
        if (auto g = Polyline(line,false)) { Dc()->DrawGeometry(g.Get(),Ink(Rgba(0,0,0,.5f)),2.6f); Dc()->DrawGeometry(g.Get(),Ink(P.text),1); }
      }
      for (auto p : mesh.points) { auto s = canvas->ToScreen(p.x,p.y); FillEllipse(s.x,s.y,2.8f,2.8f,P.text); }
    } else {
      std::vector<Point> poly; for (auto p : q) poly.push_back(canvas->ToScreen(p.x,p.y));
      if (auto g = Polyline(poly,true)) { Dc()->DrawGeometry(g.Get(),Ink(Rgba(0,0,0,.55f)),2.6f); Dc()->DrawGeometry(g.Get(),Ink(P.text),1.2f); }
      for (int i = 0; i < 8; i++) { auto p = Handle(i), s = canvas->ToScreen(p.x,p.y); FillRect(RWH(s.x - 4,s.y - 4,8,8),P.text); }
      auto s = canvas->ToScreen(pivot.x,pivot.y); StrokeEllipse(s.x,s.y,5,5,P.text,1); Line(s.x - 8,s.y,s.x + 8,s.y,P.text); Line(s.x,s.y - 8,s.x,s.y + 8,P.text);
    }
    if (dragging) {
      wchar_t text[96]{}; swprintf(text,96,L"%.1f × %.1f px  ·  %.1f°",Length(Sub(q[1],q[0])),Length(Sub(q[3],q[0])),Angle());
      float w = Measure(text,F_Small) + 20; Rect r = RWH(std::min(pointerX + 16,ui::Bounds().right - w - 6),pointerY + 20,w,28);
      FillRound(r,8,P.tintFallback); Text(text,Inset(r,10,0),F_Small,P.text);
    }
  }
  HCURSOR Cursor() override { if (dragging) return LoadCursor(nullptr,IDC_SIZEALL); int h = warp ? WarpHit(pointerX,pointerY) : Hit(pointerX,pointerY); return LoadCursor(nullptr,h == 10 || h == 8 ? IDC_SIZEALL : h == 9 ? IDC_CROSS : h >= 0 ? IDC_SIZENWSE : IDC_ARROW); }
  Json Inspect() const { JsonObject p; p.Bool("active",active).Bool("selection",selection).Bool("warp",warp).Add("quad",Points(std::vector<Point>(q.begin(),q.end()))).Add("pivot",Pt(pivot.x,pivot.y)).Add("params",Params()); return Json::Parse(p.Text()); }
  bool Options(Container& dock, std::vector<Widget*>& out) override {
    if (!active) return false; fields.clear();
    auto button = [&](std::wstring label, std::function<void()> f) { out.push_back(dock.Add<TextButton>(label,TextButton::Secondary,std::move(f))); };
    if (warp) {
      auto* styles = dock.Add<SelectList>(WarpLabels,style); warpStyle = styles; out.push_back(styles);
      styles->onChange = [this](int i) { Save(); style = i; Preset(); };
      auto number = [&](const wchar_t* name, double v, std::function<void(double)> f) { auto* n = dock.Add<NumberField>(name,v,-100,100,1,L"%"); n->onChange = std::move(f); out.push_back(n); };
      number(L"Bend",bend,[this](double v) { Save(); bend = v; Preset(); });
      number(L"Horizontal",hDistort,[this](double v) { Save(); hDistort = v; Preset(); });
      number(L"Vertical",vDistort,[this](double v) { Save(); vDistort = v; Preset(); });
      auto* orientation = dock.Add<Toggle>(L"Vertical warp",vertical); orientation->onChange = [this](bool on) { Save(); vertical = on; Preset(); }; out.push_back(orientation);
      auto* sizes = dock.Add<SelectList>(std::vector<std::wstring>{L"Default",L"3 × 3",L"4 × 4",L"5 × 5"},grid == 1 ? 0 : grid - 2);
      sizes->onChange = [this](int i) { Save(); grid = i == 0 ? 1 : i + 2; Model("edit.transform.warpGrid",JsonObject().Num("size",grid)); }; out.push_back(sizes);
      button(L"Split crosswise",[this] { Split(0); }); button(L"Split horizontally",[this] { Split(1); }); button(L"Split vertically",[this] { Split(2); });
      button(L"Free Transform",[this] { Finish(true,true); });
    } else {
      auto* ref = dock.Add<ReferenceGrid>(); ref->index = reference; ref->tooltip = L"Reference point";
      ref->change = [this](int i) { reference = i; pivot = QuadAt((i % 3) / 2.f,(i / 3) / 2.f); SyncNumbers(); }; out.push_back(ref);
      auto number = [&](const wchar_t* name, double initial, double lo, double hi, const wchar_t* unit, std::function<void(double)> f) {
        auto* n = dock.Add<NumberField>(name,initial,lo,hi,1,unit); n->step = .25; n->onChange = std::move(f); out.push_back(n); fields.push_back(n);
      };
      number(L"X",pivot.x,-1e6,1e6,L"",[this](double x) { Save(); auto d = Point{float(x) - pivot.x,0}; for (auto& p : q) p = Add(p,d); pivot = Add(pivot,d); Changed(false); });
      number(L"Y",pivot.y,-1e6,1e6,L"",[this](double y) { Save(); auto d = Point{0,float(y) - pivot.y}; for (auto& p : q) p = Add(p,d); pivot = Add(pivot,d); Changed(false); });
      number(L"W",ScaleX(),-10000,10000,L"%",[this](double v) { if (std::abs(v) < .001) { SyncNumbers(); return; } Save(); float k = float(v) / ScaleX(); Stretch(k,linked ? k : 1); Changed(false); });
      auto* link = dock.Add<Toggle>(L"Link proportions",linked); link->onChange = [this](bool on) { linked = on; }; out.push_back(link);
      number(L"H",ScaleY(),-10000,10000,L"%",[this](double v) { if (std::abs(v) < .001) { SyncNumbers(); return; } Save(); float k = float(v) / ScaleY(); Stretch(linked ? k : 1,k); Changed(false); });
      number(L"Angle",Angle(),-3600,3600,L"°",[this](double v) { Save(); Rotate(float(v) - Angle()); Changed(false); });
      number(L"Skew",Skew(),-89,89,L"°",[this](double v) {
        Save(); float delta = float(std::tan(v * Pi / 180) - std::tan(Skew() * Pi / 180)); Point along = Unit(Sub(q[1],q[0]));
        for (auto& p : q) { auto r = Sub(p,pivot); p = Add(p,Mul(along,(along.x * r.y - along.y * r.x) * delta)); }  // shear along the frame's own axis
        Changed(false);
      });
      button(L"Warp",[this] { StartWarp(); });
    }
    auto* interpolation = dock.Add<SelectList>(std::vector<std::wstring>{L"Bicubic",L"Bilinear",L"Nearest"},interp);
    interpolation->onChange = [this](int i) { interp = i; Changed(false); }; out.push_back(interpolation);
    button(L"Cancel",[this] { Finish(false); }); out.push_back(dock.Add<TextButton>(L"Apply",TextButton::Primary,[this] { Finish(true); })); return true;
  }
  void Context(double x, double y, const PointerEvent&, std::vector<MenuItem>& rows) override {
    if (!active) return;
    const wchar_t* labels[] = {L"Free Transform",L"Scale",L"Rotate",L"Skew",L"Distort",L"Perspective"};
    for (int i = 0; i < 6; i++) { auto row = ActionItem(labels[i],[this,i] { interaction = i; }); row.checked = !warp && interaction == i; row.enabled = !warp; rows.push_back(row); }
    rows.push_back(ActionItem(L"Warp",[this] { StartWarp(); }));
    if (warp) {
      for (int i = 0; i < 3; i++) rows.push_back(ActionItem(i == 0 ? L"Split crosswise" : i == 1 ? L"Split horizontally" : L"Split vertically",[this,x,y,i] { Split(i,Point{float(x),float(y)}); }));
    }
    rows.push_back(MenuItem::Separator());
    rows.push_back(ActionItem(L"Flip Horizontal",[this] { Save(); Stretch(-1,1); Changed(false); },nullptr,!warp));
    rows.push_back(ActionItem(L"Flip Vertical",[this] { Save(); Stretch(1,-1); Changed(false); },nullptr,!warp));
    for (auto [label,degrees] : {std::pair{L"Rotate 180°",180.f},std::pair{L"Rotate 90° Clockwise",90.f},std::pair{L"Rotate 90° Counterclockwise",-90.f}})
      rows.push_back(ActionItem(label,[this,degrees] { Save(); Rotate(degrees); Changed(false); },nullptr,!warp));
    rows.push_back(MenuItem::Separator()); rows.push_back(ActionItem(L"Apply",[this] { Finish(true); })); rows.push_back(ActionItem(L"Cancel",[this] { Finish(false); }));
  }
  bool Command(const std::string& id) {
    if (!active) return false;
    if (id == "edit.transform.warp") { StartWarp(); return true; }
    if (!warp && (id == "edit.transform.rotate180" || id == "edit.transform.rotate90Cw" || id == "edit.transform.rotate90Ccw")) { Save(); Rotate(id.ends_with("180") ? 180.f : id.ends_with("Ccw") ? -90.f : 90.f); Changed(false); return true; }
    if (id == "edit.transform.flipHorizontal" || id == "edit.transform.flipVertical") { Save(); Stretch(id.ends_with("Horizontal") ? -1.f : 1.f,id.ends_with("Vertical") ? -1.f : 1.f); Changed(false); return true; }
    if (id.find("splitWarp") != std::string::npos) { StartWarp(); Split(id.ends_with("Horizontally") ? 1 : id.ends_with("Vertically") ? 2 : 0); return true; }
    return false;
  }
 private:
  static constexpr float Pi = 3.14159265359f;
  struct Snapshot { std::array<Point,4> q; Point pivot; Mesh mesh; double skew = 0; int xSign = 1, ySign = 1; int style = 0, grid = 1; double bend = 50, horizontal = 0, verticalDistort = 0; bool vertical = false; };
  Snapshot before;
  std::vector<Snapshot> undo;
  std::array<float,4> frame{};
  Point start{};
  std::vector<NumberField*> fields;
  SelectList* warpStyle = nullptr;
  std::shared_ptr<LivePreview> live;
  bool dragging = false, pending = false, linked = true, vertical = false;
  int grab = -1, interaction = 0, reference = 4, style = 0, grid = 1, interp = 0;
  int xSign = 1, ySign = 1;
  double bend = 50, hDistort = 0, vDistort = 0, skew = 0, changedAt = 0;
  float grabU = .5f, grabV = .5f;
  Snapshot State() const { return {q,pivot,mesh,skew,xSign,ySign,style,grid,bend,hDistort,vDistort,vertical}; }
  void Restore(const Snapshot& s) { q = s.q; pivot = s.pivot; mesh = s.mesh; skew = s.skew; xSign = s.xSign; ySign = s.ySign; style = s.style; grid = s.grid; bend = s.bend; hDistort = s.horizontal; vDistort = s.verticalDistort; vertical = s.vertical; }
  void BoundUndo() { if (undo.size() > 256) undo.erase(undo.begin()); }
  void Save() { undo.push_back(State()); BoundUndo(); }
  Point Center() const { return Mul(Add(Add(q[0],q[1]),Add(q[2],q[3])),.25f); }
  Point QuadAt(float u, float v) const { return LerpPoint(LerpPoint(q[0],q[1],u),LerpPoint(q[3],q[2],u),v); }
  Point Handle(int i) const { return i < 4 ? q[size_t(i)] : LerpPoint(q[size_t(i - 4)],q[size_t((i - 3) % 4)],.5f); }
  float Angle() const { return std::atan2((q[1].y - q[0].y) * xSign,(q[1].x - q[0].x) * xSign) * 180 / Pi; }
  float ScaleX() const { return std::max(.001f,Length(Sub(q[1],q[0])) / (frame[2] - frame[0]) * 100) * xSign; }
  // The height across the frame (a skewed side is longer than the frame is tall) and the shear
  // along the top edge, both read from the corners so a drag by the edge and the fields agree.
  float Perp() const { Point u = Unit(Sub(q[1],q[0])), v = Sub(q[3],q[0]); return u.x * v.y - u.y * v.x; }
  float ScaleY() const { return std::max(.001f,std::abs(Perp()) / (frame[3] - frame[1]) * 100) * ySign; }
  float Skew() const { Point u = Unit(Sub(q[1],q[0])), v = Sub(q[3],q[0]); float perp = Perp(); return std::abs(perp) < .001f ? 0.f : std::atan(Dot(v,u) / perp) * 180 / Pi; }
  bool Same(const Snapshot& s) const {
    auto same = [](Point a, Point b) { return std::abs(a.x - b.x) < 1e-4f && std::abs(a.y - b.y) < 1e-4f; };
    for (size_t i = 0; i < 4; i++) if (!same(q[i],s.q[i])) return false;
    if (!same(pivot,s.pivot) || mesh.points.size() != s.mesh.points.size()) return false;
    for (size_t i = 0; i < mesh.points.size(); i++) if (!same(mesh.points[i],s.mesh.points[i])) return false;
    return true;
  }
  void SyncNumbers() { if (fields.size() != 6) return; double values[] = {pivot.x,pivot.y,ScaleX(),ScaleY(),Angle(),Skew()}; for (size_t i = 0; i < fields.size(); i++) if (!ui::IsCaptured(fields[i])) fields[i]->Set(values[i]); }
  void Changed(bool save = true) { if (save) Save(); pending = true; changedAt = ui::NowMs(); SyncNumbers(); if (warp && warpStyle) warpStyle->index = style; ui::RequestFrame(); }
  void Rotate(float degrees) { float r = degrees * Pi / 180, cs = std::cos(r), sn = std::sin(r); for (auto& p : q) { auto v = Sub(p,pivot); p = Add(pivot,{v.x * cs - v.y * sn,v.x * sn + v.y * cs}); } }
  void Stretch(float x, float y) { Point u = Sub(q[1],q[0]); u = Mul(u,1 / std::max(.001f,Length(u))); Point v{-u.y,u.x}; for (auto& p : q) { auto d = Sub(p,pivot); p = Add(pivot,Add(Mul(u,Dot(d,u) * x),Mul(v,Dot(d,v) * y))); } xSign *= x < 0 ? -1 : 1; ySign *= y < 0 ? -1 : 1; }
  int Hit(float sx, float sy) const {
    for (int i = 0; i < 8; i++) { auto p = Handle(i), s = canvas->ToScreen(p.x,p.y); if (std::abs(s.x - sx) <= 7 && std::abs(s.y - sy) <= 7) return i; }
    auto p = canvas->ToScreen(pivot.x,pivot.y); if (Length(Sub(p,{sx,sy})) < 8) return 10;
    bool inside = false; std::array<Point,4> s; for (size_t i = 0; i < 4; i++) s[i] = canvas->ToScreen(q[i].x,q[i].y);
    for (int i = 0,j = 3; i < 4; j = i++) if ((s[size_t(i)].y > sy) != (s[size_t(j)].y > sy) && sx < (s[size_t(j)].x - s[size_t(i)].x) * (sy - s[size_t(i)].y) / (s[size_t(j)].y - s[size_t(i)].y) + s[size_t(i)].x) inside = !inside;
    return inside ? 8 : 9;
  }
  void Scale(Point p, bool link, bool center) {
    Point o = before.q[0], u = Sub(before.q[1],o), v = Sub(before.q[3],o); float det = u.x * v.y - u.y * v.x;
    if (std::abs(det) < 1e-6) return;
    auto local = [&](Point p) { p = Sub(p,o); return Point{(p.x * v.y - p.y * v.x) / det,(u.x * p.y - u.y * p.x) / det}; };
    auto cursor = local(p), anchor = center ? local(pivot) : Point{.5f,.5f}; float sx = 1,sy = 1;
    if (grab < 4) {
      float x = grab == 1 || grab == 2 ? 1.f : 0.f, y = grab >= 2 ? 1.f : 0.f;
      if (!center) anchor = {1 - x,1 - y};
      if (std::abs(x - anchor.x) < 1e-5 || std::abs(y - anchor.y) < 1e-5) return;
      sx = (cursor.x - anchor.x) / (x - anchor.x); sy = (cursor.y - anchor.y) / (y - anchor.y);
      if (link) sx = sy = std::abs(sx) > std::abs(sy) ? sx : sy;
    } else {
      int edge = grab - 4;
      if (edge == 0 || edge == 2) { float y = edge == 0 ? 0.f : 1.f; if (!center) anchor.y = 1 - y; if (std::abs(y - anchor.y) < 1e-5) return; sy = (cursor.y - anchor.y) / (y - anchor.y); }
      else { float x = edge == 1 ? 1.f : 0.f; if (!center) anchor.x = 1 - x; if (std::abs(x - anchor.x) < 1e-5) return; sx = (cursor.x - anchor.x) / (x - anchor.x); }
    }
    for (size_t i = 0; i < 4; i++) { auto a = local(before.q[i]); q[i] = Add(o,Add(Mul(u,anchor.x + (a.x - anchor.x) * sx),Mul(v,anchor.y + (a.y - anchor.y) * sy))); }
    if(link&&grab<4) {
      Point fixed=Add(o,Add(Mul(u,anchor.x),Mul(v,anchor.y))),moving=q[size_t(grab)];
      Point snapped=SnapViewRay(moving,fixed);float length=Length(Sub(moving,fixed));
      if(length>1e-5){float factor=Dot(Sub(snapped,fixed),Sub(moving,fixed))/(length*length);for(auto& point:q)point=Add(fixed,Mul(Sub(point,fixed),factor));sx*=factor;sy*=factor;}
    }
    xSign = before.xSign * (sx < 0 ? -1 : 1); ySign = before.ySign * (sy < 0 ? -1 : 1);
    if (reference >= 0) pivot = QuadAt((reference % 3) / 2.f,(reference / 3) / 2.f);
  }
  int WarpHit(float sx, float sy) {
    float best = 8; int index = -1;
    for (size_t i = 0; i < mesh.points.size(); i++) { auto p = mesh.points[i], s = canvas->ToScreen(p.x,p.y); float d = Length(Sub(s,{sx,sy})); if (d < best) { best = d; index = int(i); } }
    if (index >= 0) return index;
    best = 1e9;
    for (int y = 0; y <= 32; y++) for (int x = 0; x <= 32; x++) { auto p = mesh.At(x / 32.f,y / 32.f), s = canvas->ToScreen(p.x,p.y); float d = Length(Sub(s,{sx,sy})); if (d < best) { best = d; grabU = x / 32.f; grabV = y / 32.f; } }
    return best < std::max(12.f,Length(Sub(q[2],q[0])) * canvas->Zoom() / 25) ? int(mesh.points.size()) : -1;
  }
  void Bend(Point d) {
    size_t x = mesh.Cell(mesh.us,grabU), y = mesh.Cell(mesh.vs,grabV), cols = mesh.Columns();
    float u = (grabU - mesh.us[x]) / (mesh.us[x + 1] - mesh.us[x]), v = (grabV - mesh.vs[y]) / (mesh.vs[y + 1] - mesh.vs[y]);
    float weights[16]{}, sum = 0;
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) { float w = (r == 0 || r == 3) && (c == 0 || c == 3) ? 0 : Bernstein(c,u) * Bernstein(r,v); weights[r*4 + c] = w; sum += w*w; }
    for (size_t r = 0; r < 4; r++) for (size_t c = 0; c < 4; c++) { size_t i = (y*3 + r) * cols + x*3 + c; mesh.points[i] = Add(mesh.points[i],Mul(d,weights[r*4 + c] / std::max(.1f,sum))); }
  }
  JsonObject ModelParams() const { JsonObject p; p.Add("rect","[" + JsonNum(frame[0]) + "," + JsonNum(frame[1]) + "," + JsonNum(frame[2]) + "," + JsonNum(frame[3]) + "]").Str("style","custom").Add("mesh",mesh.JsonText()); return p; }
  void Model(const std::string& command, JsonObject extra, bool remap = false) {
    auto p = remap ? JsonObject() : ModelParams();
    if (remap) p.Add("rect","[" + JsonNum(frame[0]) + "," + JsonNum(frame[1]) + "," + JsonNum(frame[2]) + "," + JsonNum(frame[3]) + "]").Str("style",WarpStyles[size_t(style)]).Num("bend",bend).Num("hDistort",hDistort).Num("vDistort",vDistort).Bool("vertical",vertical);
    auto extras = Json::Parse(extra.Text());
    for (auto& [key,value] : extras.o) p.Add(key.c_str(),JsonText(value));
    Exec(command,p.Text(),[this,remap](bool ok,const std::string& result) {
      if (!ok || !active || !warp) return; Mesh next;
      if (!next.Read(Json::Parse(result)["warp"]["mesh"])) { ui::Toast(L"Warp grid is too large",true); return; }
      if (remap) for (auto& p : next.points) p = QuadAt((p.x - frame[0]) / (frame[2] - frame[0]),(p.y - frame[1]) / (frame[3] - frame[1]));
      mesh = std::move(next); Changed(false);
    });
  }
  void Preset() { if (!warp) return; if (style == 0) { Changed(false); return; } Model("edit.transform.warpGrid",JsonObject().Num("size",grid),true); }
  void Split(int kind, std::optional<Point> at = std::nullopt) { if (!warp) return; Save(); JsonObject p; if (at) p.Add("at",Pt(at->x,at->y)); Model(kind == 0 ? "edit.transform.splitWarpCrosswise" : kind == 1 ? "edit.transform.splitWarpHorizontally" : "edit.transform.splitWarpVertically",p); style = 0; }
  std::string Params() const {
    auto p = warp ? ModelParams() : JsonObject();
    if (!warp) { p.Add("rect","[" + JsonNum(frame[0]) + "," + JsonNum(frame[1]) + "," + JsonNum(frame[2]) + "," + JsonNum(frame[3]) + "]"); p.Add("quad",Points(std::vector<Point>(q.begin(),q.end()))); }
    p.Str("interpolation",interp == 0 ? "bicubic" : interp == 1 ? "bilinear" : "nearest"); return p.Text();
  }
  bool Unchanged() const {
    if (warp) {
      if (mesh.points.empty()) return true;
      for (size_t r = 0; r < (mesh.vs.size() - 1) * 3 + 1; r++) for (size_t c = 0; c < mesh.Columns(); c++) {
        size_t x = std::min(c / 3,mesh.us.size() - 2), y = std::min(r / 3,mesh.vs.size() - 2);
        float u = mesh.us[x] + (mesh.us[x + 1] - mesh.us[x]) * float(c - x*3) / 3, v = mesh.vs[y] + (mesh.vs[y + 1] - mesh.vs[y]) * float(r - y*3) / 3;
        if (Length(Sub(mesh.points[r * mesh.Columns() + c],{frame[0] + (frame[2] - frame[0]) * u,frame[1] + (frame[3] - frame[1]) * v})) > .01f) return false;
      }
      return true;
    }
    std::array<Point,4> initial = {{{frame[0],frame[1]},{frame[2],frame[1]},{frame[2],frame[3]},{frame[0],frame[3]}}};
    for (size_t i = 0; i < 4; i++) if (Length(Sub(q[i],initial[i])) > .01f) return false; return true;
  }
};
TransformFrame transform;
}
void StartTransform(bool selection) { transform.Begin(selection); }
bool TransformCommand(const std::string& id) {
  if (id == "edit.transform.warp" && !transform.active) { transform.Begin(false); transform.StartWarp(); return true; }
  return transform.Command(id);
}
Json TransformState() { return transform.Inspect(); }
