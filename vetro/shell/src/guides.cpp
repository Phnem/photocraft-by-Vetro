#include "guides.h"
#include "i18n.h"
#include <array>
#include <set>
#include <tuple>

namespace {
bool rulers = false, guides = true, grid = false, smart = true, snapping = false, locked = false, extras = true;
bool toGuides = true, toGrid = true, toLayers = true, toDocument = true;
std::string toolKey;
Rect viewport{}, topBar{}, leftBar{};
Json targets;
std::array<double, 4> moveBounds{};
bool moveBoundsValid = false;
struct Alignment { bool vertical; double pos, from, to; };
std::vector<Alignment> alignments;
std::unique_ptr<Menu> unitMenu;
std::unique_ptr<Popover> guideDialog;
bool dragging = false, vertical = false;
int guideIndex = -1;
uint64_t dragDocument = 0;
double guidePosition = 0;

D2D1_COLOR_F Colour(const Json& s, D2D1_COLOR_F fallback) {
  auto str = s.Str();
  if (str.size() != 7 || str[0] != '#') return fallback;
  unsigned r=0,g=0,b=0;
  if (sscanf_s(str.c_str()+1,"%2x%2x%2x",&r,&g,&b) != 3) return fallback;
  return Rgba(float(r)/255,float(g)/255,float(b)/255);
}
void DocLine(bool v, double p, double from, double to, D2D1_COLOR_F colour, float width=1) {
  auto a = canvas->ToScreen(v ? p : from, v ? from : p), b = canvas->ToScreen(v ? p : to, v ? to : p);
  Line(a.x,a.y,b.x,b.y,colour,width);
}
void CaptureTargets(bool moving) {
  targets = Json{}; alignments.clear();
  if (!snapping || jobs::Busy() || !canvas->HasDocument()) return;
  auto params = JsonObject().Bool("guides",toGuides && guides).Bool("grid",toGrid && grid)
    .Bool("layers",toLayers).Bool("document",toDocument).Bool("selection",!moving).Bool("excludeSelected",moving).Text();
  targets = Json::Parse(TakeString(pc_snap_targets(engine,params.c_str())));
}
struct Match { bool found=false; double delta=0, pos=0, from=0, to=0; bool guide=false; };
Match Nearest(int axis, double value, double tolerance) {
  Match best;
  auto consider = [&](double p, double from, double to, bool guide) {
    double delta=p-value;
    if (std::abs(delta)>tolerance) return;
    if (!best.found || std::abs(delta)<std::abs(best.delta)-1e-9 ||
        (std::abs(std::abs(delta)-std::abs(best.delta))<1e-9 && guide && !best.guide))
      best = {true,delta,p,from,to,guide};
  };
  for (auto& t : targets[axis==0?"x":"y"].a)
    consider(t["pos"].Num(),t["from"].Num(),t["to"].Num(),t["kind"].Str()=="guide");
  double step=targets["grid"][axis].Num();
  if (step>0 && std::isfinite(value/step)) consider(std::round(value/step)*step,0,axis==0?canvas->DocH():canvas->DocW(),false);
  return best;
}
D2D_POINT_2F SnapPoint(D2D_POINT_2F p, Mods mods) {
  alignments.clear();
  if (!snapping || mods.ctrl || mods.shift || mods.alt) return p;
  double tol=8/std::max(.01,double(canvas->Zoom()));
  for (int axis=0;axis<2;axis++) {
    auto match=Nearest(axis,axis==0?p.x:p.y,tol);
    if (!match.found) continue;
    if(axis==0) p.x=float(match.pos); else p.y=float(match.pos);
    alignments.push_back({axis==0,match.pos,match.from,match.to});
  }
  return p;
}
std::pair<bool,int> GuideAt(float x,float y) {
  if (!guides || !extras || locked || !Inside(viewport,x,y)) return {false,-1};
  auto d=canvas->ToDoc(x,y); double tol=5/std::max(.01,double(canvas->Zoom()));
  std::pair<bool,int> found{false,-1};
  for (bool v : {true,false}) {
    auto& values=docInfo["guides"][v?"vertical":"horizontal"];
    for(size_t i=0;i<values.size();i++) {
      double distance=std::abs(values[i].Num()-(v?d.x:d.y));
      if(distance<=tol) { tol=distance; found={v,int(i)}; }
    }
  }
  return found;
}
void UnitsMenu(Rect anchor) {
  std::vector<MenuItem> rows;
  for(auto [name,id] : {std::pair{L"Pixels","pixels"}, {L"Inches","inches"}, {L"Centimeters","cm"},
                        {L"Millimeters","mm"}, {L"Points","points"}, {L"Picas","picas"}, {L"Percent","percent"}}) {
    auto row=ActionItem(name,[id] { Exec("prefs.set",JsonObject().Str("path","unitsAndRulers.rulers").Str("value",id).Text()); });
    row.checked=docInfo["viewPreferences"]["units"].Str()==id; rows.push_back(std::move(row));
  }
  unitMenu=std::make_unique<Menu>(std::move(rows)); unitMenu->Open(anchor,Side::Below);
}
class Guides : public Widget {
 public:
  Widget* HitTest(float x,float y) override {
    if(!visible || !canvas->HasDocument()) return nullptr;
    if(rulers && (Inside(topBar,x,y)||Inside(leftBar,x,y))) return this;
    if(toolKey=="move" && GuideAt(x,y).second>=0) return this;
    return nullptr;
  }
  void OnDown(const PointerEvent& e) override {
    bool top=rulers && Inside(topBar,e.x,e.y), left=rulers && Inside(leftBar,e.x,e.y);
    if(e.button==1 && (top||left)) { UnitsMenu(RWH(e.x,e.y,1,1)); return; }
    if(e.button!=0 || locked || jobs::Busy()) return;
    auto hit=GuideAt(e.x,e.y);
    if(!top&&!left&&hit.second<0) return;
    vertical=left || (!top && hit.first); guideIndex=top||left?-1:hit.second;
    dragDocument=uint64_t(docInfo["id"].Int()); dragging=true;
    CaptureTargets(false); OnDrag(e);
  }
  void OnDrag(const PointerEvent& e) override {
    if(!dragging) return;
    auto d=canvas->ToDoc(e.x,e.y);
    // A guide must never snap back to its own old position.
    if(guideIndex<0) d=SnapPoint(d,e.mods);
    guidePosition=vertical?d.x:d.y; ui::RequestFrame();
  }
  void OnUp(const PointerEvent& e,bool inside) override {
    Widget::OnUp(e,inside); if(!dragging) return;
    OnDrag(e); dragging=false; alignments.clear();
    if(dragDocument!=uint64_t(docInfo["id"].Int())) return;
    bool remove=!Inside(viewport,e.x,e.y) || guidePosition<0 || guidePosition>(vertical?canvas->DocW():canvas->DocH());
    JsonObject p; p.Str("orientation",vertical?"vertical":"horizontal").Num("position",guidePosition);
    if(guideIndex>=0) p.Num("index",guideIndex);
    if(remove && guideIndex<0) return;
    Exec(remove?"view.deleteGuide":guideIndex<0?"view.newGuide":"view.moveGuide",p.Text());
  }
  void Ruler(bool horizontal,Rect r) {
    FillRound(r,3,P.tintFallback);
    auto first=canvas->ToDoc(horizontal?r.left:r.right,horizontal?r.bottom:r.top);
    auto last=canvas->ToDoc(r.right,r.bottom);
    double a=horizontal?first.x:first.y, b=horizontal?last.x:last.y;
    double unit=docInfo["viewPreferences"]["unitPixels"][horizontal?0:1].Num(1);
    if(unit<=0||!std::isfinite(unit)||std::abs(b-a)<1e-6) return;
    double desired=70*std::abs(b-a)/std::max(1.f,horizontal?W(r):H(r))/unit;
    double power=std::pow(10.0,std::floor(std::log10(std::max(1e-9,desired))));
    double step=(desired/power<=1?1:desired/power<=2?2:desired/power<=5?5:10)*power;
    double lo=std::min(a,b)/unit,hi=std::max(a,b)/unit;
    PushClip(r);
    double tick=std::ceil(lo/(step/5))*(step/5);
    for(int n=0;n<1000 && tick<=hi;n++,tick+=step/5) {
      float t=float((tick*unit-a)/(b-a));
      float s=(horizontal?r.left:r.top)+t*(horizontal?W(r):H(r));
      bool major=std::abs(tick/step-std::round(tick/step))<1e-5;
      if(horizontal) Line(s,r.bottom-(major?9:4),s,r.bottom,P.dim);
      else Line(r.right-(major?9:4),s,r.right,s,P.dim);
      if(major) {
        wchar_t text[64]; swprintf_s(text,L"%.4g",std::abs(tick)<step*1e-6?0:tick);
        Text(text,horizontal?RWH(s+3,r.top,62,16):RWH(r.left+2,s+2,W(r)-3,16),F_Small,P.dim);
      }
    }
    PopClip();
  }
  void Paint() override {
    if(!canvas->HasDocument()) return;
    if(rulers) { Ruler(true,topBar); Ruler(false,leftBar); }
    PushClip(viewport);
    auto prefs=docInfo["viewPreferences"];
    if(extras&&grid) {
      auto color=Colour(prefs["gridColor"],P.faint);
      for(int axis=0;axis<2;axis++) {
        double major=prefs["gridMajor"][axis].Num(72),step=major/std::max(1.0,prefs["subdivisions"].Num(4));
        if(step*canvas->Zoom()<4) step=major;
        if(step<=0||step*canvas->Zoom()<4) continue;
        double lo=1e30,hi=-1e30;
        for(auto p:{canvas->ToDoc(viewport.left,viewport.top),canvas->ToDoc(viewport.right,viewport.top),canvas->ToDoc(viewport.right,viewport.bottom),canvas->ToDoc(viewport.left,viewport.bottom)}) {
          double v=axis==0?p.x:p.y;lo=std::min(lo,v);hi=std::max(hi,v);
        }
        lo=std::max(0.0,lo);hi=std::min(double(axis==0?canvas->DocW():canvas->DocH()),hi);
        double position=std::ceil(lo/step)*step;
        for(int i=0;i<2048 && position<=hi;i++,position+=step)
          DocLine(axis==0,position,0,axis==0?canvas->DocH():canvas->DocW(),Fade(color,.35f));
      }
    }
    if(extras&&guides) for(bool v:{true,false}) for(auto& position:docInfo["guides"][v?"vertical":"horizontal"].a) {
      auto color=Colour(prefs["guideColor"],P.accent);
      for(auto& c:docInfo["guides"]["colors"].a) if(c["vertical"].Truthy()==v && c["position"].Num()==position.Num())
        color=Rgba(float(c["rgb"][0].Num()/255),float(c["rgb"][1].Num()/255),float(c["rgb"][2].Num()/255));
      DocLine(v,position.Num(),0,v?canvas->DocH():canvas->DocW(),color);
    }
    if(dragging) DocLine(vertical,guidePosition,0,vertical?canvas->DocH():canvas->DocW(),P.accent,1.5f);
    if(extras&&smart) for(auto& a:alignments) DocLine(a.vertical,a.pos,a.from,a.to,Colour(prefs["smartGuideColor"],P.accent),1.4f);
    PopClip();
  }
} *overlay=nullptr;
class GuideDialog : public Popover {
 public:
  bool layout;
  std::vector<NumberField*> fields;
  SelectList* orientation=nullptr;
  Toggle* clear=nullptr;
  explicit GuideDialog(bool isLayout):layout(isLayout) {
    modal=true;
    Add<Label>(layout?L"New Guide Layout":L"New Guide",F_Title);
    if(layout) {
      for(auto [name,value,lo,hi]:{std::tuple{L"Columns",2.,0.,100.},{L"Rows",0.,0.,100.},
          {L"Column Gutter",0.,0.,10000.},{L"Row Gutter",0.,0.,10000.},{L"Margin",0.,0.,10000.}})
        fields.push_back(Add<NumberField>(name,value,lo,hi,0));
      clear=Add<Toggle>(L"Clear existing guides",false);
    } else {
      orientation=Add<SelectList>(std::vector<std::wstring>{L"Horizontal",L"Vertical"},0);
      fields.push_back(Add<NumberField>(L"Position",0,-100000,100000,2,W(docInfo["viewPreferences"]["units"].Str())));
    }
    Add<TextButton>(L"Cancel",TextButton::Secondary,[this]{Close();});
    Add<TextButton>(L"Apply",TextButton::Primary,[this]{Apply();});
    Add<TextButton>(L"×",TextButton::Quiet,[this]{Close();});
  }
  D2D1_SIZE_F Natural() override {return {360,layout?350.f:220.f};}
  void Arrange() override {
    children[0]->rect=R(target.left+22,target.top+16,target.right-46,target.top+45);
    float y=target.top+60;
    for(size_t i=1;i+3<children.size();i++) {children[i]->rect=R(target.left+22,y,target.right-22,y+30);y+=36;}
    size_t count=children.size();
    children[count-3]->rect=RWH(target.right-218,target.bottom-50,92,32);
    children[count-2]->rect=RWH(target.right-114,target.bottom-50,92,32);
    children[count-1]->rect=RWH(target.right-40,target.top+12,28,28);
  }
  void PaintContent() override {}
  void Apply() {
    JsonObject p;
    if(layout) {
      p.Num("columns",fields[0]->value).Num("rows",fields[1]->value).Num("gutter",fields[2]->value)
        .Num("rowGutter",fields[3]->value).Num("margin",fields[4]->value).Bool("clearExisting",clear->on);
    } else p.Str("orientation",orientation->index==1?"vertical":"horizontal")
      .Str("position",JsonNum(fields[0]->value)+" "+docInfo["viewPreferences"]["units"].Str());
    std::weak_ptr<int> alive=lifetime;
    Exec(layout?"view.newGuideLayout":"view.newGuide",p.Text(),[this,alive](bool ok,const std::string&){if(ok&&!alive.expired())Close();});
  }
};
} // namespace

void InitializeGuides() {
  overlay=new Guides(); ui::AddLayer(overlay);
  canvas->onToolPoint=[](D2D_POINT_2F p,const PointerEvent& e,int phase) {
    bool supported=toolKey.find("Marquee")!=std::string::npos || toolKey=="rectangle" || toolKey=="ellipse" || toolKey=="roundedRectangle" || toolKey=="polygon" || toolKey=="star" || toolKey=="line" || toolKey=="pen" || toolKey=="crop";
    if(!supported || Mode()) return p;
    if(phase==0) CaptureTargets(false);
    p=SnapPoint(p,e.mods);
    if(phase==2) alignments.clear();
    return p;
  };
}
void GuideToolChanged(const std::string& key) { toolKey=key; targets=Json{};alignments.clear(); }
void LayoutGuides(Rect& free) {
  Rect bounds=ui::Bounds();
  free.left=std::clamp(free.left,bounds.left,std::max(bounds.left,bounds.right-1));
  free.top=std::clamp(free.top,bounds.top,std::max(bounds.top,bounds.bottom-1));
  free.right=std::max(free.left+1,std::min(free.right,bounds.right));
  free.bottom=std::max(free.top+1,std::min(free.bottom,bounds.bottom));
  viewport=free;
  if(rulers&&canvas->HasDocument()) {
    float width=std::min(38.f,std::max(0.f,W(free)/5)),height=std::min(24.f,std::max(0.f,H(free)/5));
    topBar=R(free.left+width,free.top,free.right,free.top+height);
    leftBar=R(free.left,free.top+height,free.left+width,free.bottom);
    free.left+=width;free.top+=height;viewport=free;
  } else topBar=leftBar=Rect{};
  if(overlay) overlay->rect=ui::Bounds();
}
void SyncGuidePreferences() { rulers=docInfo["viewPreferences"]["showRulers"].Truthy(); }
bool IsGuideCommand(const std::string& id) {
  static const std::set<std::string> ids{"view.rulers","view.show.guides","view.show.grid","view.show.smartGuides","view.snap","view.lockGuides","view.snapTo.guides","view.snapTo.grid","view.snapTo.layers","view.snapTo.documentBounds","view.snapTo.all"};
  return ids.count(id)>0;
}
bool GuideCommandChecked(const std::string& id) {
  if(id=="view.rulers")return rulers; if(id=="view.show.guides")return guides;
  if(id=="view.show.grid")return grid; if(id=="view.show.smartGuides")return smart;
  if(id=="view.snap")return snapping; if(id=="view.lockGuides")return locked;
  if(id=="view.snapTo.guides")return toGuides;if(id=="view.snapTo.grid")return toGrid;
  if(id=="view.snapTo.layers")return toLayers;if(id=="view.snapTo.documentBounds")return toDocument;
  return id=="view.snapTo.all"&&toGuides&&toGrid&&toLayers&&toDocument;
}
bool GuideCommand(const std::string& id) {
  if(id=="view.newGuideLayout"||id=="view.newGuide") {
    if(!canvas->HasDocument())return true;
    guideDialog=std::make_unique<GuideDialog>(id=="view.newGuideLayout");guideDialog->Open(ui::Bounds(),Side::Center);return true;
  }
  if(!IsGuideCommand(id)) return false;
  if(id=="view.rulers") { rulers=!rulers; Exec("prefs.set",JsonObject().Str("path","unitsAndRulers.showRulers").Bool("value",rulers).Text()); }
  if(id=="view.show.guides")guides=!guides;if(id=="view.show.grid")grid=!grid;
  if(id=="view.show.smartGuides")smart=!smart;if(id=="view.snap")snapping=!snapping;
  if(id=="view.lockGuides")locked=!locked;if(id=="view.snapTo.guides")toGuides=!toGuides;
  if(id=="view.snapTo.grid")toGrid=!toGrid;if(id=="view.snapTo.layers")toLayers=!toLayers;
  if(id=="view.snapTo.documentBounds")toDocument=!toDocument;
  if(id=="view.snapTo.all")toGuides=toGrid=toLayers=toDocument=!GuideCommandChecked(id);
  ui::RequestFrame();return true;
}
bool GuideKey(UINT vk,Mods m) {
  if(vk==VK_ESCAPE&&dragging) {dragging=false;alignments.clear();ui::RequestFrame();return true;}
  if(!m.ctrl) return false;
  if(vk=='R'&&!m.shift&&!m.alt)return GuideCommand("view.rulers");
  if(vk==VK_OEM_1)return GuideCommand(m.alt?"view.lockGuides":m.shift?"view.snap":"view.show.guides");
  if(vk==VK_OEM_7&&!m.shift&&!m.alt)return GuideCommand("view.show.grid");
  return false;
}
Json GuideState() {
  return Json::Parse(JsonObject().Bool("rulers",rulers).Bool("guides",guides).Bool("grid",grid).Bool("snap",snapping).Bool("locked",locked).Bool("dragging",dragging).Num("position",guidePosition).Text());
}
Widget* GuideOverlay(){return overlay;}
void EndViewSnap(){alignments.clear();}
void BeginMoveSnap() {
  CaptureTargets(true);moveBoundsValid=false;
  std::function<void(const Json&,int)> visit=[&](const Json& list,int depth) {
    if(depth>128) return;
    for(auto& layer:list.a) {
      auto b=layer["bounds"];
      if(layer["selected"].Truthy()&&b.size()==4) {
        std::array<double,4> r{b[0].Num(),b[1].Num(),b[0].Num()+b[2].Num(),b[1].Num()+b[3].Num()};
        if(!moveBoundsValid)moveBounds=r;
        else for(int i=0;i<4;i++)moveBounds[i]=i<2?std::min(moveBounds[i],r[i]):std::max(moveBounds[i],r[i]);
        moveBoundsValid=true;
      }
      visit(layer["children"],depth+1);
    }
  };visit(docInfo["layers"],0);
}
D2D_POINT_2F SnapMoveOffset(double dx,double dy,Mods mods) {
  alignments.clear();
  if(!snapping||!moveBoundsValid||mods.ctrl||mods.shift) return {float(dx),float(dy)};
  double tol=8/std::max(.01,double(canvas->Zoom()));
  for(int axis=0;axis<2;axis++) {
    double offset=axis==0?dx:dy;
    Match best;
    for(double value:{moveBounds[axis],(moveBounds[axis]+moveBounds[axis+2])/2,moveBounds[axis+2]}) {
      auto match=Nearest(axis,value+offset,tol);
      if(match.found&&(!best.found||std::abs(match.delta)<std::abs(best.delta)))best=match;
    }
    if(best.found) {
      if(axis==0)dx+=best.delta;else dy+=best.delta;
      alignments.push_back({axis==0,best.pos,std::min(best.from,moveBounds[1-axis]+(axis==0?dy:dx)),std::max(best.to,moveBounds[3-axis]+(axis==0?dy:dx))});
    }
  }
  return {float(dx),float(dy)};
}
