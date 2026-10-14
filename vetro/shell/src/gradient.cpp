#include "shell.h"
#include "i18n.h"
#include <array>

namespace {
using Point = D2D_POINT_2F;
const char* styles[] = {"linear","radial","angle","reflected","diamond"};
struct Stop { double t = 0; std::string color; double opacity = 100; unsigned id = 0; };
struct Ramp {
  std::vector<Stop> colors{{0,"foreground",100,1},{1,"background",100,2}}, opacity{{0,"",100,3},{1,"",100,4}};
  unsigned next = 5;
  std::string Colors() const { std::string s = "["; for (auto& p : colors) { if (s.size() > 1) s += ','; s += "[" + JsonNum(p.t) + "," + JsonQuote(p.color) + "]"; } return s + "]"; }
  std::string Opacity() const { std::string s = "["; for (auto& p : opacity) { if (s.size() > 1) s += ','; s += "[" + JsonNum(p.t) + "," + JsonNum(p.opacity) + "]"; } return s + "]"; }
  void Read(const Json& j) {
    colors.clear(); opacity.clear();
    for (size_t i = 0; i < j["stops"].size() && i < 1024; i++) colors.push_back({j["stops"][i][0].Num(),j["stops"][i][1].Str(),100,next++});
    for (size_t i = 0; i < j["transparency"].size() && i < 1024; i++) opacity.push_back({j["transparency"][i][0].Num(),"",j["transparency"][i][1].Num(100),next++});
    if (colors.size() < 2) colors = {{0,"foreground",100,next++},{1,"background",100,next++}};
    if (opacity.size() < 2) opacity = {{0,"",100,next++},{1,"",100,next++}};
  }
};
Ramp ramp;
Point from{},to{};
uint64_t gradientLayer = 0, gradientDoc = 0, revision = ~0ull;
bool reverse = false, dither = true, classic = false;
int style = 0, blend = 0;
double opacity = 100;
std::shared_ptr<LivePreview> preview, layerPreview;
std::string lastParams, lastLayerParams;
Json presets;
bool loading = false;
bool dirty = false;
void RefreshGradientOptions();
bool HasGradient() { return gradientLayer && gradientDoc == uint64_t(docInfo["id"].Int()) && ActiveLayer() && uint64_t((*ActiveLayer())["id"].Int()) == gradientLayer; }
std::string Parameters(int geometry, bool stops) {
  JsonObject p; p.Num("layer",double(gradientLayer)).Str("style",styles[style]).Bool("reverse",reverse).Bool("dither",dither);
  if (geometry == 1) p.Add("from",Pt(from.x,from.y));
  if (geometry == 2) p.Add("to",Pt(to.x,to.y));
  if (stops) p.Add("stops",ramp.Colors()).Add("transparency",ramp.Opacity());
  return p.Text();
}
void Change(bool commit = false, int geometry = 0, bool stops = false) {
  if (!HasGradient()) return; lastParams = Parameters(geometry,stops);
  dirty = true;
  if (commit) FinishGradientPreview();
}
D2D1_COLOR_F Color(const std::string& hex) {
  if (hex == "foreground" || hex == "background") { const auto& c = sessionInfo[hex.c_str()]; return Rgba(float(c[0].Num()),float(c[1].Num()),float(c[2].Num()),1); }
  unsigned r = 0,g = 0,b = 0,a = 255;
  if (hex.size() >= 7 && hex[0] == '#') sscanf_s(hex.c_str() + 1,"%02x%02x%02x",&r,&g,&b);
  if (hex.size() >= 9) sscanf_s(hex.c_str() + 7,"%02x",&a);
  return Rgba(r/255.f,g/255.f,b/255.f,a/255.f);
}
std::string HexColor(D2D1_COLOR_F c) { char s[12]{}; snprintf(s,sizeof s,"#%02x%02x%02x",int(c.r*255+.5f),int(c.g*255+.5f),int(c.b*255+.5f)); return s; }
double SampleOpacity(double t) {
  for (size_t i = 1; i < ramp.opacity.size(); i++) if (t <= ramp.opacity[i].t) { auto& a = ramp.opacity[i-1]; auto& b = ramp.opacity[i]; double f = (t-a.t) / std::max(1e-6,b.t-a.t); return a.opacity + (b.opacity-a.opacity)*f; }
  return ramp.opacity.back().opacity;
}
D2D1_COLOR_F Sample(double t) {
  auto result = Color(ramp.colors.back().color);
  for (size_t i = 1; i < ramp.colors.size(); i++) if (t <= ramp.colors[i].t) { auto& a = ramp.colors[i-1]; auto& b = ramp.colors[i]; result = Mix(Color(a.color),Color(b.color),float((t-a.t)/std::max(1e-6,b.t-a.t))); break; }
  result.a *= float(SampleOpacity(t)/100); return result;
}
void Sort(std::vector<Stop>& stops) { std::stable_sort(stops.begin(),stops.end(),[](const Stop& a,const Stop& b) { return a.t < b.t; }); }
class RampControl : public Widget {
 public:
  bool alpha = false;
  unsigned selected = 1;
  std::function<void(bool)> change;
  std::vector<Stop>& Stops() { return alpha ? ramp.opacity : ramp.colors; }
  Stop* Active() { for (auto& s : Stops()) if (s.id == selected) return &s; return nullptr; }
  Rect Bar() const { return Inset(rect,10,15); }
  void Paint() override {
    auto bar = Bar(); Checkerboard(bar,6);
    auto antialias = Dc()->GetAntialiasMode(); Dc()->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    for (int i = 0; i < 120; i++) { auto c = Sample((i+.5)/120); FillRect(R(bar.left + W(bar)*i/120,bar.top,bar.left + W(bar)*(i+1)/120 + .2f,bar.bottom),c); }
    Dc()->SetAntialiasMode(antialias);
    for (int kind = 0; kind < 2; kind++) for (auto& p : kind ? ramp.opacity : ramp.colors) { float x = bar.left + float(p.t)*W(bar), y = kind ? bar.top-7 : bar.bottom+7; FillEllipse(x,y,p.id == selected && alpha == bool(kind) ? 5.f : 4.f,4,kind ? Rgba(float(p.opacity/100),float(p.opacity/100),float(p.opacity/100),1) : Color(p.color)); StrokeEllipse(x,y,5,5,P.text,.8f); }
  }
  void OnDown(const PointerEvent& e) override {
    auto bar = Bar(); alpha = e.y < MidY(rect); auto& stops = Stops(); selected = 0; float best = 9;
    for (auto& p : stops) { float d = std::abs(bar.left + float(p.t)*W(bar) - e.x); if (d < best) { best = d; selected = p.id; } }
    if (!selected && stops.size() < 1024) { double t = std::clamp(double((e.x-bar.left)/W(bar)),0.0,1.0); auto color = HexColor(Sample(t)); selected = ramp.next++; stops.push_back({t,color,SampleOpacity(t),selected}); Sort(stops); }
    dragging = true; if (change) change(false);
  }
  void OnDrag(const PointerEvent& e) override { if (!dragging) return; if (auto* p = Active()) p->t = std::clamp(double((e.x-Bar().left)/W(Bar())),0.0,1.0); Sort(Stops()); if (change) change(false); }
  void OnUp(const PointerEvent& e, bool inside) override { Widget::OnUp(e,inside); dragging = false; if (change) change(true); }
  void Delete() { auto& stops = Stops(); if (stops.size() <= 2) return; stops.erase(std::remove_if(stops.begin(),stops.end(),[this](const Stop& p) { return p.id == selected; }),stops.end()); selected = stops.front().id; if (change) change(true); }
 private: bool dragging = false;
};

class GradientEditor : public Popover {
 public:
  RampControl* control;
  NumberField *location,*alpha;
  ColorField* color;
  Ramp saved;
  GradientEditor() : saved(ramp) {
    modal = true;
    control = Add<RampControl>(); control->selected = ramp.colors.front().id; control->change = [this](bool) { Sync(); Change(false,false,true); };
    location = Add<NumberField>(L"Location",0,0,100,1,L"%"); location->onChange = [this](double t) { if (auto* s = control->Active()) s->t = t/100; Sort(control->Stops()); Change(false,false,true); };
    alpha = Add<NumberField>(L"Opacity",100,0,100,1,L"%"); alpha->onChange = [this](double v) { if (control->alpha) if (auto* s = control->Active()) s->opacity = v; Change(false,false,true); };
    color = Add<ColorField>(L"Color",Color(ramp.colors.front().color),true); color->labelWidth = 60; color->onChange = [this](D2D1_COLOR_F c) { if (auto* s = control->Active(); s && !control->alpha) s->color = HexColor(c); Change(false,false,true); };
    Add<TextButton>(L"Delete Stop",TextButton::Secondary,[this] { control->Delete(); });
    Add<TextButton>(L"Cancel",TextButton::Secondary,[this] { Finish(false); }); Add<TextButton>(L"Apply",TextButton::Primary,[this] { Finish(true); });
    Sync();
  }
  void Sync() { if (auto* s = control->Active()) { location->Set(s->t*100); alpha->Set(s->opacity); if (!control->alpha) color->color = Color(s->color); } alpha->enabled = control->alpha; color->enabled = !control->alpha; }
  D2D1_SIZE_F Natural() override { return {560,258}; }
  void Arrange() override {
    control->rect = R(target.left+16,target.top+48,target.right-16,target.top+125);
    location->rect = RWH(target.left+20,target.top+142,150,30); alpha->rect = RWH(target.left+180,target.top+142,210,30);
    color->rect = R(target.left+20,target.top+180,target.right-20,target.top+210);
    children[4]->rect = RWH(target.right-150,target.top+142,130,30); children[5]->rect = RWH(target.right-200,target.bottom-38,82,30); children[6]->rect = RWH(target.right-108,target.bottom-38,88,30);
  }
  void PaintContent() override { Text(L"Gradient Editor",R(target.left+20,target.top+12,target.right-20,target.top+42),F_Title,P.text); }
  bool OnKey(UINT vk, Mods) override { if (vk == VK_ESCAPE) { Finish(false); return true; } if (vk == VK_RETURN) { Finish(true); return true; } if (vk == VK_DELETE || vk == VK_BACK) { control->Delete(); return true; } return true; }
  void Finish(bool keep) {
    Close(); if (keep) {
      if (HasGradient()) FinishGradientPreview();
      else Exec("gradient.presets.select",JsonObject().Add("stops",ramp.Colors()).Add("transparency",ramp.Opacity()).Bool("applyToLayer",false).Text());
    } else { ramp = saved; dirty = false; if (preview) { preview->Finish(false,lastParams); preview.reset(); } }
  }
};
std::unique_ptr<GradientEditor> editor;
std::unique_ptr<Menu> presetMenu;
void OpenEditor(Rect anchor) { FinishGradientPreview(); editor = std::make_unique<GradientEditor>(); editor->Open(anchor,Side::Below); }
void PresetMenu(Rect anchor) {
  Exec("gradient.presets.list","{}",[anchor](bool ok,const std::string& result) {
    if (!ok) return; presets = Json::Parse(result); std::vector<MenuItem> rows;
    for (size_t g = 0; g < presets["groups"].size(); g++) {
      MenuItem group; group.label = W(presets["groups"][g]["name"].Str());
      const auto& list = presets["groups"][g]["presets"];
      for (size_t i = 0; i < list.size(); i++) { auto preset = list[i]; group.submenu.push_back(ActionItem(W(preset["name"].Str()),[preset] { FinishGradientPreview(); ramp.Read(preset); if (HasGradient()) Change(true,false,true); Exec("gradient.presets.select",JsonObject().Str("preset",preset["name"].Str()).Bool("applyToLayer",false).Text()); })); }
      rows.push_back(std::move(group));
    }
    presetMenu = std::make_unique<Menu>(std::move(rows)); presetMenu->Open(anchor,Side::Below);
  });
}
void ReadGradient(bool force = false) {
  if (preview || dirty || loading || docInfo.IsNull()) return;
  auto doc = uint64_t(docInfo["id"].Int()), layer = uint64_t(docInfo["activeLayer"].Int()), rev = uint64_t(docInfo["revision"].Int());
  if (!force && doc == gradientDoc && layer == gradientLayer && rev == revision) return;
  if (!pc_enabled(engine,"gradient.fill.get")) { gradientLayer = 0; gradientDoc = doc; revision = rev; return; }
  loading = true;
  Exec("gradient.fill.get",JsonObject().Num("layer",double(layer)).Text(),[doc,layer,rev](bool ok,const std::string& result) {
    loading = false;
    if (!ok || doc != uint64_t(docInfo["id"].Int()) || layer != uint64_t(docInfo["activeLayer"].Int())) return;
    auto j = Json::Parse(result); gradientLayer = layer; gradientDoc = doc; revision = rev;
    from = {float(j["from"][0].Num()),float(j["from"][1].Num())}; to = {float(j["to"][0].Num()),float(j["to"][1].Num())}; ramp.Read(j); reverse = j["reverse"].Truthy(); dither = j["dither"].Truthy(true);
    for (int i = 0; i < 5; i++) if (j["style"].Str() == styles[i]) style = i;
    opacity = ActiveLayer() ? (*ActiveLayer())["opacity"].Num(1) * 100 : 100;
    if (ActiveLayer()) for (size_t i = 0; i < BlendNames.size(); i++) if (Narrow(BlendNames[i]) == (*ActiveLayer())["blend"].Str()) blend = int(i);
    RefreshGradientOptions();
  },false);
}
class GradientTool : public CanvasTool {
 public:
  void Activate() override { ReadGradient(true); }
  void Tick() override { if (!dragging && !jobs::Busy()) ReadGradient(); }
  void Down(double x, double y, const PointerEvent& e) override {
    start = {float(x),float(y)}; beforeFrom = from; beforeTo = to; grab = -1; dragging = true;
    if (HasGradient() && !classic) {
      auto a = canvas->ToScreen(from.x,from.y),b = canvas->ToScreen(to.x,to.y);
      if (std::hypot(a.x-e.x,a.y-e.y) < 9) grab = 0; else if (std::hypot(b.x-e.x,b.y-e.y) < 9) grab = 1;
      else for (size_t i = 0; i < ramp.colors.size(); i++) { auto& stop = ramp.colors[i]; auto p = canvas->ToScreen(from.x + (to.x-from.x)*stop.t,from.y + (to.y-from.y)*stop.t); if (std::hypot(p.x-e.x,p.y-e.y) < 8) { grab = int(i)+2; beforeStop = stop.t; break; } }
    }
    if (grab < 0) from = to = start;
  }
  void Drag(double x, double y, const PointerEvent& e) override {
    if (!dragging) return; Point p{float(x),float(y)};
    if (e.mods.shift) { double dx = p.x - start.x,dy = p.y - start.y,r = std::hypot(dx,dy),a = std::round(std::atan2(dy,dx) / (.25 * 3.141592653589793)) * (.25 * 3.141592653589793); p = {float(start.x+r*std::cos(a)),float(start.y+r*std::sin(a))}; }
    if (grab < 0) to = p;
    else if (grab == 0) { from = p; Change(false,1); }
    else if (grab == 1) { to = p; Change(false,2); }
    else { auto d = Point{beforeTo.x-beforeFrom.x,beforeTo.y-beforeFrom.y}; double n = d.x*d.x+d.y*d.y; if (n > .01) ramp.colors[size_t(grab-2)].t = std::clamp(beforeStop + ((p.x-start.x)*d.x + (p.y-start.y)*d.y)/n,0.0,1.0); Change(false,false,true); }
  }
  void Up(double x, double y, const PointerEvent& e) override {
    if (!dragging) return; Drag(x,y,e); dragging = false;
    if (grab >= 0) { Sort(ramp.colors); FinishGradientPreview(); return; }
    if (std::hypot(to.x-from.x,to.y-from.y) < 1) { from = beforeFrom; to = beforeTo; return; }  // a plain click draws nothing and moves nothing
    JsonObject p; p.Add("from",Pt(from.x,from.y)).Add("to",Pt(to.x,to.y)).Str("style",styles[style]).Bool("reverse",reverse).Bool("dither",dither).Num("opacity",opacity).Str("mode",Narrow(BlendNames[size_t(blend)])).Add("stops",ramp.Colors()).Add("transparency",ramp.Opacity());
    if (!classic) p.Str("name",UniqueLayerName(i18n::Tr(L"Gradient Fill"),true));  // the bridge names the layer in the interface language
    Exec(classic ? "paint.gradient" : "gradient.fill.create",p.Text(),[](bool ok,const std::string&) { if (ok) ReadGradient(true); });
  }
  void Cancel() override { if (!dragging) return; if (grab >= 2 && size_t(grab-2) < ramp.colors.size()) ramp.colors[size_t(grab-2)].t = beforeStop; dragging = dirty = false; if (preview) { preview->Finish(false,lastParams); preview.reset(); } from = beforeFrom; to = beforeTo; }
  void Deactivate() override { FinishGradientPreview(); dragging = false; }
  bool Key(UINT vk, Mods) override { if (vk == VK_ESCAPE && dragging) { Cancel(); return true; } return false; }
  bool Options(Container& dock, std::vector<Widget*>& out) override {
    auto* presets = dock.Add<TextButton>(L"Presets…",TextButton::Secondary,nullptr); presets->onClick = [presets] { PresetMenu(presets->rect); }; out.push_back(presets);
    auto* edit = dock.Add<TextButton>(L"Edit Gradient…",TextButton::Secondary,nullptr); edit->onClick = [edit] { OpenEditor(edit->rect); }; out.push_back(edit);
    auto* kinds = dock.Add<SelectList>(std::vector<std::wstring>{L"Linear",L"Radial",L"Angle",L"Reflected",L"Diamond"},style); kinds->onChange = [](int i) { style = i; Change(true); }; out.push_back(kinds);
    auto* mode = dock.Add<SelectList>(BlendNames,blend); mode->onChange = [](int i) { blend = i; if (HasGradient()) Exec("layer.setProps",JsonObject().Num("layer",double(gradientLayer)).Str("blend",Narrow(BlendNames[size_t(blend)])).Text()); }; out.push_back(mode);
    auto* alpha = dock.Add<NumberField>(L"Opacity",opacity,0,100,1,L"%"); alpha->onChange = [](double v) { opacity = v; if (HasGradient()) { if (!layerPreview) layerPreview = std::make_shared<LivePreview>("layer.setProps"); lastLayerParams = JsonObject().Num("layer",double(gradientLayer)).Num("opacity",v / 100).Text(); layerPreview->Show(lastLayerParams); } }; alpha->onCommit = [](double) { FinishGradientPreview(); }; out.push_back(alpha);
    auto* rev = dock.Add<Toggle>(L"Reverse",reverse); rev->onChange = [](bool on) { reverse = on; Change(true); }; out.push_back(rev);
    auto* noise = dock.Add<Toggle>(L"Dither",dither); noise->onChange = [](bool on) { dither = on; Change(true); }; out.push_back(noise);
    auto* legacy = dock.Add<Toggle>(L"Classic Gradient",classic); legacy->onChange = [](bool on) { classic = on; }; out.push_back(legacy); return true;
  }
  void Overlay() override {
    if (!dragging && (!HasGradient() || classic)) return;
    auto a = canvas->ToScreen(from.x,from.y),b = canvas->ToScreen(to.x,to.y); Line(a.x,a.y,b.x,b.y,Rgba(0,0,0,.6f),3); Line(a.x,a.y,b.x,b.y,P.text,1.2f); FillEllipse(a.x,a.y,5,5,P.text); FillEllipse(b.x,b.y,5,5,P.accent);
    for (auto& s : ramp.colors) { float x = a.x + float(s.t)*(b.x-a.x),y = a.y + float(s.t)*(b.y-a.y); FillEllipse(x,y,4,4,Color(s.color)); StrokeEllipse(x,y,4.5f,4.5f,P.text,.8f); }
  }
 private: bool dragging = false; int grab = -1; double beforeStop = 0; Point start{},beforeFrom{},beforeTo{};
};
void RefreshGradientOptions() { if (canvas && dynamic_cast<GradientTool*>(canvas->tool) && !Mode()) RebuildOptions(); }
}
CanvasTool* NewGradientTool() { return new GradientTool(); }
void CloseGradientEditor() { if (editor && editor->open) editor->Finish(false); if (presetMenu && presetMenu->open) presetMenu->Close(); }
void FinishGradientPreview() { revision = ~0ull; if (dirty && !preview) preview = std::make_shared<LivePreview>("gradient.fill.set"); dirty = false; if (preview) { preview->Finish(true,lastParams); preview.reset(); } if (layerPreview) { layerPreview->Finish(true,lastLayerParams); layerPreview.reset(); } }
void FlushGradientPreview() { if (dirty && !jobs::Busy() && (!preview || !preview->Busy())) { dirty = false; if (!preview) preview = std::make_shared<LivePreview>("gradient.fill.set"); preview->Show(lastParams); } }
Json GradientState() { JsonObject p; p.Num("layer",double(gradientLayer)).Add("from",Pt(from.x,from.y)).Add("to",Pt(to.x,to.y)).Add("stops",ramp.Colors()).Add("transparency",ramp.Opacity()); return Json::Parse(p.Text()); }
