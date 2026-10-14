#include "shell.h"

namespace {
int sizeIndex = 0, sourceIndex = 2;
bool ring = true;
D2D1_COLOR_F sampled{}, previous{};
const int sizes[] = {1,3,5,11,31,51,101};
const char* sources[] = {"current","currentAndBelow","all","allNoAdjustments","currentAndBelowNoAdjustments"};
D2D1_COLOR_F Color(const Json& j) { return Rgba(float(j[0].Num()),float(j[1].Num()),float(j[2].Num()),1); }
class Eyedropper : public CanvasTool {
 public:
  explicit Eyedropper(HCURSOR c) : cursor(c) {}
  void Down(double x, double y, const PointerEvent& e) override { previous = Color(sessionInfo[e.mods.alt ? "background" : "foreground"]); dragging = true; background = e.mods.alt; sample = {float(x),float(y)}; pending = true; Tick(); }
  void Drag(double x, double y, const PointerEvent&) override { sample = {float(x),float(y)}; pending = true; }
  void Up(double x, double y, const PointerEvent& e) override { Drag(x,y,e); dragging = false; Tick(); }
  void Tick() override { if (pending && !jobs::Busy()) { pending = false; SampleColorAt(sample.x,sample.y,background); } }
  void Deactivate() override { dragging = pending = false; }
  HCURSOR Cursor() override { return cursor; }
  bool Options(Container& dock, std::vector<Widget*>& out) override {
    auto* size = dock.Add<SelectList>(std::vector<std::wstring>{L"Point Sample",L"3 × 3",L"5 × 5",L"11 × 11",L"31 × 31",L"51 × 51",L"101 × 101"},sizeIndex);
    size->onChange = [](int i) { sizeIndex = std::clamp(i,0,6); }; out.push_back(size);
    auto* source = dock.Add<SelectList>(std::vector<std::wstring>{L"Current Layer",L"Current and Below",L"All Layers",L"All without Adjustments",L"Current and Below without Adjustments"},sourceIndex);
    source->onChange = [](int i) { sourceIndex = std::clamp(i,0,4); }; out.push_back(source);
    auto* show = dock.Add<Toggle>(L"Show Sampling Ring",ring); show->onChange = [](bool on) { ring = on; }; out.push_back(show); return true;
  }
  void Overlay() override {
    if (!dragging || !ring) return;
    float x = pointerX,y = pointerY;
    StrokeEllipse(x,y,28,28,Rgba(0,0,0,.7f),10);
    Dc()->PushAxisAlignedClip(R(x-36,y-36,x+36,y),D2D1_ANTIALIAS_MODE_ALIASED); StrokeEllipse(x,y,28,28,sampled,7); Dc()->PopAxisAlignedClip();
    Dc()->PushAxisAlignedClip(R(x-36,y,x+36,y+36),D2D1_ANTIALIAS_MODE_ALIASED); StrokeEllipse(x,y,28,28,previous,7); Dc()->PopAxisAlignedClip();
    StrokeEllipse(x,y,23,23,P.text,.8f); StrokeEllipse(x,y,33,33,P.text,.8f);
  }
 private:
  HCURSOR cursor; bool dragging = false, background = false, pending = false; D2D_POINT_2F sample{};
};
}
void SampleColorAt(double x, double y, bool background) {
  JsonObject p; p.Num("x",x).Num("y",y).Num("size",sizes[sizeIndex]).Str("sampleLayer",sources[sourceIndex]);
  Exec("document.sampleColor",p.Text(),[background](bool ok,const std::string& result) {
    if (!ok) return; auto c = Json::Parse(result); sampled = Color(c);
    Exec("tools.setColors",JsonObject().Str(background ? "background" : "foreground",Hex(c)).Text());
  });
}
CanvasTool* NewEyedropperTool(HCURSOR cursor) { return new Eyedropper(cursor); }
