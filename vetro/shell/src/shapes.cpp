#include "shell.h"
#include "i18n.h"
#include <array>

namespace {
std::string ColorHex(D2D1_COLOR_F c) { char text[12]{}; snprintf(text,sizeof text,"#%02x%02x%02x",std::clamp(int(c.r * 255 + .5f),0,255),std::clamp(int(c.g * 255 + .5f),0,255),std::clamp(int(c.b * 255 + .5f),0,255)); return text; }
D2D1_COLOR_F Color(const Json& j, D2D1_COLOR_F fallback) {
  auto s = j.Str(); unsigned r = 0,g = 0,b = 0;
  if (s.size() >= 7 && s[0] == '#' && sscanf_s(s.c_str() + 1,"%02x%02x%02x",&r,&g,&b) == 3) return Rgba(r/255.f,g/255.f,b/255.f,1);
  return fallback;
}
struct Style {
  D2D1_COLOR_F fill = Rgba(0,0,0,1), stroke = Rgba(0,0,0,1);
  bool filled = true, outlined = false, picked = false, strokePicked = false, dashPicked = false;
  double strokeWidth = 1, width = 0, height = 0, radius = 30;
  int dash = 0, align = 0, operation = 0;
} creation;
struct ShapeData {
  uint64_t doc = 0, layer = 0;
  Json info;
  std::array<double,4> rect{}, radii{};
  Style style;
} selected;
ScrollArea* properties = nullptr;
std::vector<Widget*> rows;
NumberField *propWidth = nullptr, *propHeight = nullptr, *propRadius = nullptr, *propStrokeWidth = nullptr;
ColorField *propFill = nullptr, *propStroke = nullptr;
Toggle *propFilled = nullptr, *propOutlined = nullptr;
SelectList *propDash = nullptr, *propAlign = nullptr;
std::shared_ptr<LivePreview> shapePreview;
std::string previewParams;
uint64_t requestedDoc = 0, requestedLayer = 0, requestedRevision = ~0ull;
bool syncing = false;
bool pending = false;
void RefreshShapeOptions();
bool HasShape() { return ActiveLayer() && (*ActiveLayer())["kind"].Str() == "Shape" && selected.layer == uint64_t((*ActiveLayer())["id"].Int()) && selected.doc == uint64_t(docInfo["id"].Int()); }
std::string Stroke(const Style& s, const Json* original = nullptr) {
  if (!s.outlined) return "null";
  const char* align[] = {"center","inside","outside"};
  JsonObject p;
  if (original && !s.strokePicked && !(*original)["fill"].IsNull()) p.Add("fill",JsonText((*original)["fill"])); else p.Str("color",ColorHex(s.stroke));
  p.Num("width",s.strokeWidth).Str("align",align[std::clamp(s.align,0,2)]).Add("dashes",original && !s.dashPicked && !(*original)["dashes"].IsNull() ? JsonText((*original)["dashes"]) : s.dash == 1 ? "[8,4]" : s.dash == 2 ? "[1,3]" : "[]");
  if (s.dash == 2) p.Str("cap","round"); return p.Text();
}
void PaintParams(JsonObject& p, const Style& s) { p.Add("fill",s.filled ? JsonQuote(ColorHex(s.fill)) : "null").Add("stroke",Stroke(s)); }
void UpdateControls() {
  if (!properties || !HasShape()) return;
  syncing = true;
  if (!ui::IsCaptured(propWidth)) propWidth->Set(selected.rect[2]); if (!ui::IsCaptured(propHeight)) propHeight->Set(selected.rect[3]);
  if (!ui::IsCaptured(propRadius)) propRadius->Set(selected.radii[0]); if (!ui::IsCaptured(propStrokeWidth)) propStrokeWidth->Set(selected.style.strokeWidth);
  propRadius->enabled = !selected.info["live"]["radii"].IsNull();
  propFill->color = selected.style.fill; propFill->set = selected.style.filled; propStroke->color = selected.style.stroke; propStroke->set = selected.style.outlined;
  propFilled->Set(selected.style.filled); propOutlined->Set(selected.style.outlined); propDash->index = selected.style.dash; propAlign->index = selected.style.align;
  syncing = false;
}
void Edit(JsonObject p, bool commit) {
  if (!HasShape() || syncing) return;
  p.Num("layer",double(selected.layer)); previewParams = p.Text();
  pending = true;
  if (commit) FinishParameterPreviews();
}
void Geometry(bool commit) {
  JsonObject p;
  if (!selected.info["live"]["rect"].IsNull()) {
    std::string rect = "["; for (double x : selected.rect) { if (rect.size() > 1) rect += ','; rect += JsonNum(x); } p.Add("rect",rect + "]");
  } else {
    auto b = selected.info["bounds"]; double sx = selected.rect[2] / std::max(1.0,b[2].Num()), sy = selected.rect[3] / std::max(1.0,b[3].Num());
    p.Add("transform","[" + JsonNum(sx) + ",0,0," + JsonNum(sy) + "," + JsonNum(b[0].Num() * (1 - sx)) + "," + JsonNum(b[1].Num() * (1 - sy)) + "]");
  }
  if (propRadius->enabled) { std::string r = "["; for (double v : selected.radii) { if (r.size() > 1) r += ','; r += JsonNum(v); } p.Add("radii",r + "]"); }
  Edit(p,commit); UpdateControls();
}
void StyleEdit(bool commit) {
  JsonObject p;
  p.Add("fill",selected.style.filled ? !selected.style.picked && !selected.info["fill"].IsNull() ? JsonText(selected.info["fill"]) : JsonQuote(ColorHex(selected.style.fill)) : "null")
   .Add("stroke",Stroke(selected.style,&selected.info["stroke"])); Edit(p,commit);
}
void Fetch() {
  if (!ActiveLayer() || (*ActiveLayer())["kind"].Str() != "Shape") {
    bool had = selected.layer != 0; selected = {}; requestedLayer = 0;
    if (properties) properties->visible = false; if (had) RefreshShapeOptions(); return;
  }
  if (shapePreview || pending) return;
  auto doc = uint64_t(docInfo["id"].Int()), layer = uint64_t((*ActiveLayer())["id"].Int()), revision = uint64_t(docInfo["revision"].Int());
  if (doc == requestedDoc && layer == requestedLayer && revision == requestedRevision) { if (properties) properties->visible = HasShape(); return; }
  requestedDoc = doc; requestedLayer = layer; requestedRevision = revision;
  if (properties) properties->visible = HasShape();
  Exec("shape.info",JsonObject().Num("layer",double(layer)).Text(),[doc,layer](bool ok,const std::string& result) {
    if (!ok || uint64_t(docInfo["id"].Int()) != doc || !ActiveLayer() || (*ActiveLayer())["kind"].Str() != "Shape" || uint64_t((*ActiveLayer())["id"].Int()) != layer) return;
    selected.doc = doc; selected.layer = layer; selected.info = Json::Parse(result);
    auto rect = selected.info["live"]["rect"].size() == 4 ? selected.info["live"]["rect"] : selected.info["bounds"];
    for (int i = 0; i < 4; i++) { selected.rect[size_t(i)] = rect[i].Num(); selected.radii[size_t(i)] = selected.info["live"]["radii"][i].Num(); }
    auto stroke = selected.info["stroke"];
    selected.style.filled = !selected.info["fill"].IsNull(); selected.style.fill = Color(selected.info["fill"],Rgba(0,0,0,1));
    selected.style.outlined = !stroke.IsNull(); selected.style.stroke = Color(stroke["fill"],Rgba(0,0,0,1)); selected.style.strokeWidth = stroke["width"].Num(1);
    selected.style.dash = stroke["dashes"].size() ? (stroke["cap"].Str() == "round" ? 2 : 1) : 0;
    selected.style.align = stroke["align"].Str() == "inside" ? 1 : stroke["align"].Str() == "outside" ? 2 : 0;
    selected.style.picked = selected.style.strokePicked = selected.style.dashPicked = false;
    if (properties) { properties->visible = true; UpdateControls(); }
    RefreshShapeOptions();
  },false);
}
void AddStyleControls(Container& dock, std::vector<Widget*>& out, bool current) {
  Style& s = current ? selected.style : creation;
  auto* fill = dock.Add<ColorField>(L"Fill",s.fill,s.filled); fill->labelWidth = Measure(L"Fill",F_Label) + 10; out.push_back(fill);
  fill->onChange = [current](D2D1_COLOR_F c) { auto& s = current ? selected.style : creation; s.fill = c; s.filled = true; s.picked = true; if (current) { StyleEdit(true); UpdateControls(); } };
  auto* filled = dock.Add<Toggle>(L"With Fill",s.filled); out.push_back(filled); filled->onChange = [current](bool v) { (current ? selected.style : creation).filled = v; if (current) StyleEdit(true); };
  auto* stroke = dock.Add<ColorField>(L"Stroke",s.stroke,s.outlined); stroke->labelWidth = Measure(L"Stroke",F_Label) + 10; out.push_back(stroke);
  stroke->onChange = [current](D2D1_COLOR_F c) { auto& s = current ? selected.style : creation; s.stroke = c; s.outlined = true; s.strokePicked = true; if (current) { StyleEdit(true); UpdateControls(); } };
  auto* outlined = dock.Add<Toggle>(L"With Stroke",s.outlined); out.push_back(outlined); outlined->onChange = [current](bool v) { (current ? selected.style : creation).outlined = v; if (current) StyleEdit(true); };
  auto* width = dock.Add<NumberField>(L"Stroke Width",s.strokeWidth,0,10000,1,L" px"); out.push_back(width);
  width->onChange = [current](double v) { auto& s = current ? selected.style : creation; s.strokeWidth = v; s.outlined = v > 0; if (current) StyleEdit(false); };
  width->onCommit = [current](double) { if (current) FinishParameterPreviews(); };
  auto* dash = dock.Add<SelectList>(std::vector<std::wstring>{L"Solid",L"Dashed",L"Dotted"},s.dash); out.push_back(dash);
  dash->onChange = [current](int i) { auto& s = current ? selected.style : creation; s.dash = i; s.dashPicked = true; if (current) StyleEdit(true); };
  auto* align = dock.Add<SelectList>(std::vector<std::wstring>{L"Stroke Center",L"Inside",L"Outside"},s.align); out.push_back(align);
  align->onChange = [current](int i) { (current ? selected.style : creation).align = i; if (current) StyleEdit(true); };
}

class ShapeTool : public CanvasTool {
 public:
  explicit ShapeTool(std::string k) : kind(std::move(k)) {}
  void Down(double x, double y, const PointerEvent& e) override {
    Fetch(); corner = -1;
    if (HasShape() && selected.info["live"]["radii"].size() == 4) for (int i = 0; i < 4; i++) { auto p = Marker(i), s = canvas->ToScreen(p.x,p.y); if (std::hypot(s.x - e.x,s.y - e.y) < 8) { corner = i; originalRadii = selected.radii; break; } }
    start = end = {float(x),float(y)}; dragging = true;
  }
  void Drag(double x, double y, const PointerEvent& e) override {
    if (!dragging) return; end = {float(x),float(y)};
    if (corner >= 0) {
      double dx = corner == 1 || corner == 2 ? selected.rect[0] + selected.rect[2] - x : x - selected.rect[0];
      double dy = corner >= 2 ? selected.rect[1] + selected.rect[3] - y : y - selected.rect[1];
      double radius = std::clamp((dx + dy) / 2,0.0,std::min(selected.rect[2],selected.rect[3]) / 2);
      selected.radii = originalRadii;
      if (e.mods.alt) selected.radii[size_t(corner)] = radius; else selected.radii.fill(radius);
      Geometry(false); UpdateControls(); return;
    }
    if (e.mods.shift && kind != "line") { float s = std::max(std::abs(end.x - start.x),std::abs(end.y - start.y)); end = {start.x + std::copysign(s,end.x - start.x),start.y + std::copysign(s,end.y - start.y)}; }
    centered = e.mods.alt;
  }
  void Up(double x, double y, const PointerEvent& e) override {
    if (!dragging) return; Drag(x,y,e); dragging = false;
    if (corner >= 0) { FinishParameterPreviews(); corner = -1; return; }
    auto r = Box(); if (W(r) < 1 && H(r) < 1) return;
    JsonObject p; p.Str("kind",kind == "triangle" ? "path" : kind);
    if (kind == "line") p.Add("from",Pt(start.x,start.y)).Add("to",Pt(end.x,end.y)).Num("weight",std::max(1.0,creation.strokeWidth));
    else if (kind == "triangle") p.Str("name",UniqueLayerName(i18n::Tr(L"Triangle"),true)).Add("path","{\"subpaths\":[{\"closed\":true,\"knots\":[" + Pt(MidX(r),r.top) + "," + Pt(r.right,r.bottom) + "," + Pt(r.left,r.bottom) + "]}]}");
    else p.Add("rect","[" + JsonNum(r.left) + "," + JsonNum(r.top) + "," + JsonNum(W(r)) + "," + JsonNum(H(r)) + "]");
    if (kind == "roundedRect") p.Num("radii",std::min(creation.radius,double(std::min(W(r),H(r))) / 2));
    Style style = creation; if (!style.picked) style.fill = Color(Json::Parse(JsonQuote(Hex(sessionInfo["foreground"]))),style.fill);
    PaintParams(p,style);
    if (creation.operation && ActiveLayer() && (*ActiveLayer())["kind"].Str() == "Shape") {
      const char* ops[] = {"combine","combine","subtract","intersect","exclude"}; p.Num("addTo",double((*ActiveLayer())["id"].Int())).Str("op",ops[std::clamp(creation.operation,0,4)]);
    }
    Exec("shape.create",p.Text());
  }
  void Cancel() override { if (corner >= 0) { if (shapePreview) shapePreview->Finish(false,previewParams); shapePreview.reset(); pending = false; selected.radii = originalRadii; } dragging = false; corner = -1; }
  void Deactivate() override { Cancel(); }
  bool Options(Container& dock, std::vector<Widget*>& out) override {
    bool current = HasShape(); AddStyleControls(dock,out,current);
    auto* width = dock.Add<NumberField>(L"W",current ? selected.rect[2] : creation.width,0,100000,1,L" px"); out.push_back(width);
    auto* height = dock.Add<NumberField>(L"H",current ? selected.rect[3] : creation.height,0,100000,1,L" px"); out.push_back(height);
    width->onChange = [current](double v) { creation.width = v; if (current && v > 0) { selected.rect[2] = v; Geometry(false); } };
    height->onChange = [current](double v) { creation.height = v; if (current && v > 0) { selected.rect[3] = v; Geometry(false); } };
    width->onCommit = height->onCommit = [](double) { FinishParameterPreviews(); };
    if (kind == "roundedRect") { auto* radius = dock.Add<NumberField>(L"Radius",current ? selected.radii[0] : creation.radius,0,10000,1,L" px"); radius->onChange = [current](double v) { creation.radius = v; if (current) { selected.radii.fill(v); Geometry(false); } }; radius->onCommit = [](double) { FinishParameterPreviews(); }; out.push_back(radius); }
    auto* op = dock.Add<SelectList>(std::vector<std::wstring>{L"New Layer",L"Combine",L"Subtract",L"Intersect",L"Exclude"},creation.operation); op->onChange = [](int i) { creation.operation = i; }; out.push_back(op); return true;
  }
  void Overlay() override {
    if (dragging && corner < 0) { auto r = Box(); auto a = canvas->ToScreen(r.left,r.top), b = canvas->ToScreen(r.right,r.bottom); StrokeRound(R(std::min(a.x,b.x),std::min(a.y,b.y),std::max(a.x,b.x),std::max(a.y,b.y)),kind == "roundedRect" ? float(creation.radius * canvas->Zoom()) : 0,P.accent,1.2f); }
    if (HasShape() && selected.info["live"]["radii"].size() == 4) for (int i = 0; i < 4; i++) { auto p = Marker(i), s = canvas->ToScreen(p.x,p.y); FillEllipse(s.x,s.y,4,4,P.text); StrokeEllipse(s.x,s.y,5,5,P.accent,1); }
  }
 private:
  std::string kind;
  D2D_POINT_2F start{}, end{};
  bool dragging = false, centered = false; int corner = -1; std::array<double,4> originalRadii{};
  Rect Box() const {
    float w = creation.width > 0 ? float(creation.width) : std::abs(end.x - start.x), h = creation.height > 0 ? float(creation.height) : std::abs(end.y - start.y);
    if (centered) { if (creation.width <= 0) w *= 2; if (creation.height <= 0) h *= 2; }
    float x = centered ? start.x - w/2 : end.x < start.x ? start.x - w : start.x, y = centered ? start.y - h/2 : end.y < start.y ? start.y - h : start.y;
    return RWH(x,y,w,h);
  }
  D2D_POINT_2F Marker(int i) const { double r = std::max(selected.radii[size_t(i)],8.0 / std::max(.01f,canvas->Zoom())); return {float(i == 1 || i == 2 ? selected.rect[0] + selected.rect[2] - r : selected.rect[0] + r),float(i >= 2 ? selected.rect[1] + selected.rect[3] - r : selected.rect[1] + r)}; }
};
void RefreshShapeOptions() { if (canvas && dynamic_cast<ShapeTool*>(canvas->tool) && !Mode()) RebuildOptions(); }
}

CanvasTool* NewShapeTool(const std::string& kind) { return new ShapeTool(kind); }
void FinishParameterPreviews() {
  requestedRevision = ~0ull;
  if (pending && !shapePreview) shapePreview = std::make_shared<LivePreview>("shape.edit");
  pending = false;
  if (shapePreview) { shapePreview->Finish(true,previewParams); shapePreview.reset(); }
  FinishGradientPreview();
}
void FlushParameterPreviews() {
  if (pending && !jobs::Busy() && (!shapePreview || !shapePreview->Busy())) {
    pending = false; if (!shapePreview) shapePreview = std::make_shared<LivePreview>("shape.edit"); shapePreview->Show(previewParams);
  }
  FlushGradientPreview();
}
void SyncShapeProperties() { Fetch(); }
ScrollArea* BuildShapeProperties(Container& dock) {
  properties = dock.Add<ScrollArea>(); properties->visible = false;
  auto number = [](const wchar_t* label, double v, double max, std::function<void(double)> change) {
    auto* n = properties->Add<NumberField>(label,v,0,max,1,L" px"); n->onChange = std::move(change); n->onCommit = [](double) { FinishParameterPreviews(); }; rows.push_back(n); return n;
  };
  propWidth = number(L"W",0,100000,[](double v) { if (v > 0) { selected.rect[2] = v; Geometry(false); } });
  propHeight = number(L"H",0,100000,[](double v) { if (v > 0) { selected.rect[3] = v; Geometry(false); } });
  propRadius = number(L"Radius",0,10000,[](double v) { selected.radii.fill(v); Geometry(false); });
  size_t start = rows.size(); AddStyleControls(*properties,rows,true);
  propFill = static_cast<ColorField*>(rows[start]); propFilled = static_cast<Toggle*>(rows[start + 1]); propStroke = static_cast<ColorField*>(rows[start + 2]); propOutlined = static_cast<Toggle*>(rows[start + 3]);
  propStrokeWidth = static_cast<NumberField*>(rows[start + 4]); propDash = static_cast<SelectList*>(rows[start + 5]); propAlign = static_cast<SelectList*>(rows[start + 6]);
  return properties;
}
void LayoutShapeProperties(const Rect& rect) {
  if (!properties) return; properties->rect = rect;
  float y = rect.top; for (auto* w : rows) { w->rect = R(rect.left + 3,y,rect.right - 8,y + 30); y += 36; }
  properties->contentHeight = y - rect.top; properties->Clamp();
}
