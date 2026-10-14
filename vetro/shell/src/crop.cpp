#include "shell.h"
#include "icons_phosphor.h"
#include "i18n.h"
#include <array>

namespace {
using Point = D2D_POINT_2F;
Point Add(Point a, Point b) { return {a.x + b.x,a.y + b.y}; }
Point Sub(Point a, Point b) { return {a.x - b.x,a.y - b.y}; }
Point Rotate(Point p, double angle) { double a = angle * 3.141592653589793 / 180, c = std::cos(a),s = std::sin(a); return {float(p.x*c - p.y*s),float(p.x*s + p.y*c)}; }
class CropFrame : public CanvasTool {
 public:
  Rect box{};
  double angle = 0, ratioW = 1, ratioH = 1, targetW = 0, targetH = 0, resolution = 300;
  int overlay = 0, ratio = 0;
  bool has = false, deletePixels = true, contentAware = false, straighten = false;
  void Activate() override { Reset(); }
  void Reset() { box = R(0,0,float(canvas->DocW()),float(canvas->DocH())); has = canvas->HasDocument(); fresh = true; angle = 0; resolution = docInfo["resolution"].Num(300); }
  Point Center() const { return {MidX(box),MidY(box)}; }
  Point World(float u, float v) const { return Add(Center(),Rotate({(u - .5f) * W(box),(v - .5f) * H(box)},angle)); }
  void Down(double x, double y, const PointerEvent& e) override {
    if (!canvas->HasDocument()) return;
    if (!has) Reset();
    start = {float(x),float(y)}; before = box; beforeAngle = angle;
    grab = straighten ? 11 : Hit(e.x,e.y);
    if (fresh && grab == 8) grab = 10;
    dragging = true; fresh = false;
    if (grab == 10) { angle = 0; box = R(float(x),float(y),float(x),float(y)); }
  }
  void Drag(double x, double y, const PointerEvent& e) override {
    if (!dragging) return; Point p{float(x),float(y)}, d = Sub(p,start); cursor = p;
    if (grab == 11) return;
    if (grab == 9) {
      Point c{MidX(before),MidY(before)};
      angle = beforeAngle + (std::atan2(p.y-c.y,p.x-c.x) - std::atan2(start.y-c.y,start.x-c.x)) * 180 / 3.141592653589793;
      if (e.mods.shift) angle = std::round(angle / 15) * 15;
      SyncControls();
      return;
    }
    if (grab == 8) { box = Offset(before,d.x,d.y); return; }
    double aspect = Aspect();
    if (grab == 10) {
      float w = std::abs(p.x - start.x), h = std::abs(p.y - start.y);
      if (aspect > 0) h = float(w / aspect); else if (e.mods.shift) w = h = std::max(w,h);
      box = RWH(p.x < start.x ? start.x - w : start.x,p.y < start.y ? start.y - h : start.y,w,h); has = true; return;
    }
    Point local = Rotate(d,-beforeAngle); float l = before.left,t = before.top,r = before.right,b = before.bottom;
    bool left = grab == 0 || grab == 3 || grab == 7, right = grab == 1 || grab == 2 || grab == 5;
    bool top = grab == 0 || grab == 1 || grab == 4, bottom = grab == 2 || grab == 3 || grab == 6;
    if (left) l = std::min(l + local.x,r - 1); if (right) r = std::max(r + local.x,l + 1);
    if (top) t = std::min(t + local.y,b - 1); if (bottom) b = std::max(b + local.y,t + 1);
    if (aspect > 0) { float h = float((r-l) / aspect); if (top) t = b - h; else if (bottom) b = t + h; else { float c = (t+b)/2; t = c-h/2; b = c+h/2; } }
    Point offset{(l+r-before.left-before.right)/2,(t+b-before.top-before.bottom)/2}; offset = Rotate(offset,beforeAngle);
    Point center{MidX(before)+offset.x,MidY(before)+offset.y}; box = RWH(center.x-(r-l)/2,center.y-(b-t)/2,r-l,b-t);
  }
  void Up(double x, double y, const PointerEvent& e) override {
    if (!dragging) return; Drag(x,y,e); dragging = false;
    if (grab == 11) {
      auto d = Sub(cursor,start); if (std::hypot(d.x,d.y) > 1) {
        angle = std::atan2(d.y,d.x) * 180 / 3.141592653589793;
        while (angle > 45) angle -= 90; while (angle < -45) angle += 90;
      }
      straighten = false;
    }
    if (W(box) < 1 || H(box) < 1) has = false;
    SyncControls();
  }
  bool Key(UINT vk, Mods m) override {
    if (vk == VK_RETURN && has) { Commit(); return true; }
    if (vk == VK_ESCAPE && has) { has = dragging = false; return true; }
    if (vk == 'O' && !m.ctrl && !m.alt) { overlay = (overlay + 1) % 6; SyncControls(); return true; }
    return false;
  }
  void Cancel() override { dragging = false; has = false; }
  void Deactivate() override { Cancel(); }
  HCURSOR Cursor() override { return LoadCursor(nullptr,straighten ? IDC_CROSS : dragging && grab == 8 ? IDC_SIZEALL : IDC_CROSS); }
  void Commit() {
    if (!has || W(box) < 1 || H(box) < 1) return;
    if ((targetW > 0) != (targetH > 0)) { ui::Toast(L"Set both output width and height",true); return; }
    Constrain();  // an output size fixes the frame's ratio, so the crop is resampled, never stretched
    JsonObject p;
    p.Num("x",std::round(box.left)).Num("y",std::round(box.top)).Num("width",std::max(1.0,std::round(double(W(box))))).Num("height",std::max(1.0,std::round(double(H(box)))))
      .Num("angle",angle).Bool("deleteCroppedPixels",deletePixels);
    if (targetW > 0 && targetH > 0) p.Num("targetWidth",std::round(targetW)).Num("targetHeight",std::round(targetH));
    if (resolution > 0) p.Num("resolution",resolution);
    if (contentAware) p.Str("fillLayerName",UniqueLayerName(i18n::Tr(L"Content-Aware Crop Fill"),true));
    Exec(contentAware ? "image.cropContentAware" : "image.crop",p.Text(),[this](bool ok,const std::string&) { if (ok) has = false; });
  }
  bool Options(Container& dock, std::vector<Widget*>& out) override {
    auto* presets = dock.Add<SelectList>(std::vector<std::wstring>{L"Unconstrained",L"Original Ratio",L"1 : 1",L"4 : 3",L"3 : 2",L"16 : 9",L"Custom Ratio"},ratio);
    ratioField = presets; presets->onChange = [this](int i) { ClearTarget(); ratio = i; Constrain(); }; out.push_back(presets);
    auto number = [&](const wchar_t* label, double v, double lo, double hi, std::function<void(double)> change) {
      auto* n = dock.Add<NumberField>(label,v,lo,hi,1); n->onChange = std::move(change);
      std::wstring name = label;
      if (name == L"Angle") angleField = n; else if (name == L"Ratio W") ratioWField = n; else if (name == L"Ratio H") ratioHField = n;
      else if (name == L"Output W") targetWField = n; else if (name == L"Output H") targetHField = n;
      out.push_back(n);
    };
    number(L"Ratio W",ratioW,.01,10000,[this](double v) { ClearTarget(); ratioW = v; ratio = 6; Constrain(); }); number(L"Ratio H",ratioH,.01,10000,[this](double v) { ClearTarget(); ratioH = v; ratio = 6; Constrain(); });
    number(L"Output W",targetW,0,100000,[this](double v) { targetW = v; TargetRatio(); }); number(L"Output H",targetH,0,100000,[this](double v) { targetH = v; TargetRatio(); });
    number(L"Resolution",resolution,1,100000,[this](double v) { resolution = v; }); number(L"Angle",angle,-360,360,[this](double v) { angle = v; });
    auto* overlays = dock.Add<SelectList>(std::vector<std::wstring>{L"Rule of Thirds",L"Grid",L"Diagonal",L"Triangle",L"Golden Ratio",L"Golden Spiral"},overlay);
    overlayField = overlays; overlays->onChange = [this](int i) { overlay = i; }; out.push_back(overlays);
    auto* remove = dock.Add<Toggle>(L"Delete Cropped Pixels",deletePixels); remove->onChange = [this](bool on) { deletePixels = on; }; out.push_back(remove);
    auto* aware = dock.Add<Toggle>(L"Content-Aware",contentAware); aware->onChange = [this](bool on) { contentAware = on; }; out.push_back(aware);
    out.push_back(dock.Add<TextButton>(L"Straighten",TextButton::Secondary,[this] { straighten = true; }));
    out.push_back(dock.Add<TextButton>(L"Clear",TextButton::Secondary,[this] { ClearTarget(); ratio = 0; ratioW = ratioH = 1; SyncControls(); }));
    out.push_back(dock.Add<TextButton>(L"Cancel",TextButton::Secondary,[this] { Cancel(); })); out.push_back(dock.Add<TextButton>(L"Apply crop",TextButton::Primary,[this] { Commit(); })); return true;
  }
  void Context(double, double, const PointerEvent&, std::vector<MenuItem>& rows) override {
    rows.push_back(ActionItem(L"Apply crop",[this] { Commit(); },nullptr,has)); rows.push_back(ActionItem(L"Cancel",[this] { Cancel(); }));
    rows.push_back(ActionItem(L"Straighten",[this] { straighten = true; })); rows.push_back(ActionItem(L"Reset",[this] { Reset(); }));
  }
  void Overlay() override {
    if (!has) return;
    auto screen = [&](float u,float v) { auto p = World(u,v); return canvas->ToScreen(p.x,p.y); };
    std::vector<Point> poly{screen(0,0),screen(1,0),screen(1,1),screen(0,1)};
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> shade;
    if (SUCCEEDED(GfxFactory()->CreatePathGeometry(&shade))) {
      Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
      if (SUCCEEDED(shade->Open(&sink))) {
        sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE); auto r = canvas->free;
        sink->BeginFigure({r.left,r.top},D2D1_FIGURE_BEGIN_FILLED); Point outer[] = {{r.right,r.top},{r.right,r.bottom},{r.left,r.bottom}}; sink->AddLines(outer,3); sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->BeginFigure(poly[0],D2D1_FIGURE_BEGIN_FILLED); sink->AddLines(poly.data()+1,3); sink->EndFigure(D2D1_FIGURE_END_CLOSED); sink->Close();
        Dc()->FillGeometry(shade.Get(),Ink(Rgba(0,0,0,.5f)));
      }
    }
    if (auto g = Polyline(poly,true)) { Dc()->DrawGeometry(g.Get(),Ink(Rgba(0,0,0,.6f)),2.6f); Dc()->DrawGeometry(g.Get(),Ink(P.text),1.2f); }
    auto line = [&](float u,float v,float a,float b) { auto p = screen(u,v),q = screen(a,b); Line(p.x,p.y,q.x,q.y,Rgba(1,1,1,.6f),.9f); };
    if (overlay == 0 || overlay == 1 || overlay == 4) {
      int n = overlay == 1 ? 8 : 3; for (int i = 1; i < n; i++) { float k = overlay == 4 ? (i == 1 ? .381966f : .618034f) : float(i)/n; line(k,0,k,1); line(0,k,1,k); }
    } else if (overlay == 2) { line(0,0,1,1); line(1,0,0,1); }
    else if (overlay == 3) { line(0,0,1,1); float t = H(box)*H(box) / std::max(1.f,W(box)*W(box)+H(box)*H(box)); line(0,1,t,t); line(1,0,1-t,1-t); }
    else {
      std::vector<Point> spiral; for (int i = 0; i <= 160; i++) { double t = i * 4 * 3.141592653589793 / 160, r = .48 * std::exp(-.3063489625 * t); spiral.push_back(screen(float(.5+r*std::cos(t)),float(.5+r*std::sin(t)))); }
      if (auto g = Polyline(spiral,false)) Dc()->DrawGeometry(g.Get(),Ink(Rgba(1,1,1,.7f)),1);
    }
    for (int i = 0; i < 8; i++) { auto p = i < 4 ? poly[size_t(i)] : screen(i == 4 || i == 6 ? .5f : i == 5 ? 1.f : 0.f,i == 5 || i == 7 ? .5f : i == 6 ? 1.f : 0.f); FillRound(RWH(p.x-4,p.y-4,8,8),1,P.text); }
    if (dragging && grab == 11) { auto a = canvas->ToScreen(start.x,start.y),b = canvas->ToScreen(cursor.x,cursor.y); Line(a.x,a.y,b.x,b.y,P.accent,1.6f); }
  }
  Json Inspect() const { JsonObject p; p.Bool("has",has).Num("x",box.left).Num("y",box.top).Num("width",W(box)).Num("height",H(box)).Num("angle",angle).Num("overlay",overlay); return Json::Parse(p.Text()); }
 private:
  bool dragging = false, fresh = true;
  NumberField *angleField = nullptr, *ratioWField = nullptr, *ratioHField = nullptr, *targetWField = nullptr, *targetHField = nullptr;
  void ClearTarget() { targetW = targetH = 0; if (targetWField) targetWField->Set(0); if (targetHField) targetHField->Set(0); }
  SelectList *ratioField = nullptr,*overlayField = nullptr;
  void SyncControls() {
    if (angleField) angleField->Set(angle); if (ratioField) ratioField->index = ratio; if (overlayField) overlayField->index = overlay;
    if (ratioWField) ratioWField->Set(ratioW); if (ratioHField) ratioHField->Set(ratioH);
  }
  // Both output sizes set: the frame keeps their ratio (Custom Ratio), as the Crop tool's W x H does.
  void TargetRatio() { if (targetW > 0 && targetH > 0) { ratioW = targetW; ratioH = targetH; ratio = 6; Constrain(); } }
  void Constrain() { double a = Aspect(); if (has && a > 0) { float h = float(W(box)/a), cy = MidY(box); box.top = cy-h/2; box.bottom = cy+h/2; } SyncControls(); }
  int grab = -1; Point start{},cursor{}; Rect before{}; double beforeAngle = 0;
  double Aspect() const { if (targetW > 0 && targetH > 0) return targetW / targetH; if (ratio == 0) return 0; if (ratio == 1) return double(canvas->DocW()) / std::max(1u,canvas->DocH()); const double values[] = {0,0,1,4.0/3,3.0/2,16.0/9}; return ratio == 6 ? ratioW / std::max(.01,ratioH) : values[std::clamp(ratio,0,5)]; }
  int Hit(float x, float y) const {
    const Point uv[] = {{0,0},{1,0},{1,1},{0,1},{.5f,0},{1,.5f},{.5f,1},{0,.5f}};
    for (int i = 0; i < 8; i++) { auto p = World(uv[i].x,uv[i].y),s = canvas->ToScreen(p.x,p.y); if (std::hypot(s.x-x,s.y-y) < 8) return i; }
    auto p = canvas->ToDoc(x,y); p = Add(Center(),Rotate(Sub(p,Center()),-angle)); return Inside(box,p.x,p.y) ? 8 : 9;
  }
};
CropFrame* crop = nullptr;
}
CanvasTool* NewCropTool() { crop = new CropFrame(); return crop; }
Json CropState() { return crop ? crop->Inspect() : Json{}; }
