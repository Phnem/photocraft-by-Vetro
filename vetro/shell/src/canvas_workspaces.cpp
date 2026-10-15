#include "canvas_workspaces.h"
#include <array>

namespace {
using Point=D2D_POINT_2F;
std::string Array(const std::vector<std::string>& items){std::string s="[";for(auto& i:items){if(s.size()>1)s+=",";s+=i;}return s+"]";}
Point FromJson(const Json& p){return {float(p[0].Num()),float(p[1].Num())};}
Point Lerp(Point a,Point b,float t){return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};}
// Unit-square homography. Solving the eight constraints also detects degenerate quads.
bool Project(const std::array<Point,4>& q,double u,double v,Point& result,bool inverse=false){
  double a[8][9]{};const double uv[4][2]={{0,0},{1,0},{1,1},{0,1}};
  for(int i=0;i<4;i++){double x=inverse?q[i].x:uv[i][0],y=inverse?q[i].y:uv[i][1],X=inverse?uv[i][0]:q[i].x,Y=inverse?uv[i][1]:q[i].y;
    a[2*i][0]=x;a[2*i][1]=y;a[2*i][2]=1;a[2*i][6]=-x*X;a[2*i][7]=-y*X;a[2*i][8]=X;
    a[2*i+1][3]=x;a[2*i+1][4]=y;a[2*i+1][5]=1;a[2*i+1][6]=-x*Y;a[2*i+1][7]=-y*Y;a[2*i+1][8]=Y;}
  for(int k=0;k<8;k++){int best=k;for(int i=k+1;i<8;i++)if(std::abs(a[i][k])>std::abs(a[best][k]))best=i;if(std::abs(a[best][k])<1e-10)return false;
    for(int j=k;j<9;j++)std::swap(a[k][j],a[best][j]);double d=a[k][k];for(int j=k;j<9;j++)a[k][j]/=d;
    for(int i=0;i<8;i++)if(i!=k){double f=a[i][k];for(int j=k;j<9;j++)a[i][j]-=f*a[k][j];}}
  double d=a[6][8]*u+a[7][8]*v+1;if(std::abs(d)<1e-9)return false;
  double x=(a[0][8]*u+a[1][8]*v+a[2][8])/d,y=(a[3][8]*u+a[4][8]*v+a[5][8])/d;
  if(!std::isfinite(x)||!std::isfinite(y))return false;result={float(x),float(y)};return true;
}
bool Convex(const std::array<Point,4>& q){double sign=0;for(int i=0;i<4;i++){auto a=q[i],b=q[(i+1)%4],c=q[(i+2)%4];double k=(b.x-a.x)*(c.y-b.y)-(b.y-a.y)*(c.x-b.x);if(std::abs(k)<1e-4||sign*k<0)return false;sign=k;}return true;}
class CanvasWorkspace : public CanvasTool {
 public:
  enum Kind{Puppet,Vanishing,Scale,Fill};Kind kind=Puppet;
  bool active=false,dirty=false,geometryDirty=false,loading=false,meshVisible=true,preview=true,skin=false,all=false,mirror=false,scaleSamples=false,newLayer=true;
  int density=1,mode=1,selected=-1,drag=-1,operation=0,sampling=0,output=0,color=2,rotation=0;
  double expansion=2,amount=100,size=30,pasteWidth=.5,margin=20;
  uint64_t layer=0,epoch=0;
  Point start{},last{},source{};bool hasSource=false;
  Rect frame{},sample{};
  Json geometry;
  struct Pin{Point src,dst;double rotation=0;bool fixed=false;int depth=0;};
  std::vector<Pin> pins;
  std::vector<std::array<Point,4>> planes;
  std::vector<Point> corners,stroke;
  std::vector<std::string> pastes,clones,undo,redo;
  std::unique_ptr<ToolPanel> panel;
  std::shared_ptr<LivePreview> live;
  std::shared_ptr<PcSnapshot> snapshot;
  std::string Command()const{static const char* names[]={"edit.puppetWarp","filter.vanishingPoint","edit.contentAwareScale","edit.contentAwareFill"};return names[kind];}
  std::wstring Title()const{static const wchar_t* names[]={L"Puppet Warp",L"Vanishing Point",L"Content-Aware Scale",L"Content-Aware Fill"};return names[kind];}
  std::string Planes()const{std::vector<std::string> p;for(auto& q:planes)p.push_back(JsonObject().Add("corners",PointList(std::vector<Point>(q.begin(),q.end()),false)).Text());return Array(p);}
  std::string Params()const{
    JsonObject p;p.Num("layer",double(layer));
    if(kind==Puppet){std::vector<std::string> list;for(auto& pin:pins){JsonObject v;v.Add("src",Pt(pin.src.x,pin.src.y)).Add("dst",Pt(pin.dst.x,pin.dst.y)).Num("depth",pin.depth);if(pin.fixed)v.Num("rotate",pin.rotation);list.push_back(v.Text());}
      static const char* densities[]={"fewer","normal","more"};static const char* modes[]={"rigid","normal","distort"};p.Add("pins",Array(list)).Str("density",densities[density]).Str("mode",modes[mode]).Num("expansion",expansion);
    }else if(kind==Vanishing)p.Add("planes",Planes()).Add("paste",Array(pastes)).Add("clone",Array(clones)).Bool("newLayer",newLayer);
    else if(kind==Scale)p.Num("x",frame.left).Num("y",frame.top).Num("width",W(frame)).Num("height",H(frame)).Num("amount",amount).Bool("protectSkinTones",skin);
    else {static const char* samplings[]={"auto","rectangular","custom"};static const char* outputs[]={"current","new","duplicate"};static const char* colors[]={"none","high","default","veryHigh"};static const char* rotations[]={"none","low","medium","high","full"};
      p.Str("sampling",samplings[sampling]).Str("output",outputs[output]).Num("margin",margin).Str("colorAdaptation",colors[color]).Str("rotationAdaptation",rotations[rotation]).Bool("sampleAllLayers",all).Bool("mirror",mirror).Bool("scale",scaleSamples);if(sampling==2)p.Add("area","["+JsonNum(sample.left)+","+JsonNum(sample.top)+","+JsonNum(W(sample))+","+JsonNum(H(sample))+"]");}
    return p.Text();
  }
  std::string Save()const{std::vector<std::string> pp;for(auto& pin:pins)pp.push_back(JsonObject().Add("src",Pt(pin.src.x,pin.src.y)).Add("dst",Pt(pin.dst.x,pin.dst.y)).Num("rotate",pin.rotation).Bool("fixed",pin.fixed).Num("depth",pin.depth).Text());return JsonObject().Add("pins",Array(pp)).Add("planes",Planes()).Add("paste",Array(pastes)).Add("clone",Array(clones)).Add("frame","["+JsonNum(frame.left)+","+JsonNum(frame.top)+","+JsonNum(frame.right)+","+JsonNum(frame.bottom)+"]").Add("sample","["+JsonNum(sample.left)+","+JsonNum(sample.top)+","+JsonNum(sample.right)+","+JsonNum(sample.bottom)+"]").Num("sampling",sampling).Text();}
  void Restore(const std::string& text){auto s=Json::Parse(text);pins.clear();for(auto& p:s["pins"].a)pins.push_back({FromJson(p["src"]),FromJson(p["dst"]),p["rotate"].Num(),p["fixed"].Truthy(),int(p["depth"].Int())});planes.clear();for(auto& p:s["planes"].a){std::array<Point,4> q;for(int i=0;i<4;i++)q[i]=FromJson(p["corners"][i]);planes.push_back(q);}pastes.clear();for(auto& p:s["paste"].a)pastes.push_back(JsonText(p));clones.clear();for(auto& p:s["clone"].a)clones.push_back(JsonText(p));auto r=s["frame"],t=s["sample"];frame=R(float(r[0].Num()),float(r[1].Num()),float(r[2].Num()),float(r[3].Num()));sample=R(float(t[0].Num()),float(t[1].Num()),float(t[2].Num()),float(t[3].Num()));sampling=int(s["sampling"].Int());selected=-1;Changed();Build();}
  void Remember(){undo.push_back(Save());if(undo.size()>100)undo.erase(undo.begin());redo.clear();}
  void Changed(){dirty=true;geometryDirty=kind==Puppet;ui::RequestFrame();}
  SelectList* Choice(std::wstring label,std::vector<std::wstring> choices,int value,std::function<void(int)> change){for(auto& x:choices)x=i18n::Tr(x);auto* f=panel->Row<SelectList>(32,choices,value);f->label=i18n::Tr(label);f->labelWidth=125;f->onChange=std::move(change);return f;}
  NumberField* Number(std::wstring label,double value,double low,double high,std::function<void(double)> change){auto* f=panel->Row<NumberField>(30,label,value,low,high,1);f->onCommit=std::move(change);return f;}
  void ToggleField(std::wstring label,bool* value,bool changes=true){auto* f=panel->Row<Toggle>(28,label,*value);f->onChange=[this,value,changes](bool v){*value=v;if(changes)Changed();};}
  void Build(){panel->Reset();panel->title=Title();panel->close->onClick=[this]{Finish(false);};
    if(kind==Puppet){ToggleField(L"Show Mesh",&meshVisible,false);Choice(L"Density",{L"Fewer Points",L"Normal",L"More Points"},density,[this](int i){density=i;Changed();});Choice(L"Mode",{L"Rigid",L"Normal",L"Distort"},mode,[this](int i){mode=i;Changed();});Number(L"Expansion",expansion,-200,200,[this](double v){expansion=v;Changed();});
      bool pin=selected>=0&&size_t(selected)<pins.size();auto* rotate=Number(L"Rotation",pin?pins[size_t(selected)].rotation:0,-360,360,[this](double v){if(selected>=0&&size_t(selected)<pins.size()){Remember();auto& p=pins[size_t(selected)];p.rotation=v;p.fixed=true;Changed();}});rotate->enabled=pin;
      auto* depth=Number(L"Pin Depth",pin?pins[size_t(selected)].depth:0,-100,100,[this](double v){if(selected>=0&&size_t(selected)<pins.size()){Remember();pins[size_t(selected)].depth=int(v);Changed();}});depth->enabled=pin;
      panel->Row<Label>(22,L"Click to add a pin; drag to move",F_Label);panel->Row<Label>(22,L"Alt-click or Delete removes a pin",F_Label);
    }else if(kind==Vanishing){Choice(L"Tool",{L"Create Plane",L"Edit Plane",L"Paste",L"Clone Stamp"},operation,[this](int i){operation=i;corners.clear();});Number(L"Brush Size",size,1,2000,[this](double v){size=v;});Number(L"Paste Width",pasteWidth,.01,10,[this](double v){pasteWidth=v;});ToggleField(L"New Layer",&newLayer);
      panel->Row<TextButton>(32,L"Paste",TextButton::Secondary,[this]{Paste();});panel->Row<TextButton>(32,L"Delete Plane",TextButton::Secondary,[this]{if(selected>=0&&size_t(selected)<planes.size()){Remember();planes.erase(planes.begin()+selected);pastes.clear();clones.clear();selected=-1;Changed();}});
      panel->Row<Label>(22,L"Click four corners for a plane",F_Label);panel->Row<Label>(22,L"Drag corners to edit the plane",F_Label);panel->Row<Label>(22,L"Alt-click sets the clone source",F_Label);
    }else if(kind==Scale){Number(L"Width",W(frame),1,60000,[this](double v){Remember();frame.right=frame.left+float(v);Changed();});Number(L"Height",H(frame),1,60000,[this](double v){Remember();frame.bottom=frame.top+float(v);Changed();});Number(L"Amount",amount,0,100,[this](double v){amount=v;Changed();});ToggleField(L"Protect Skin Tones",&skin);panel->Row<Label>(22,L"Drag frame handles to resize",F_Label);
    }else {Choice(L"Sampling",{L"Auto",L"Rectangular",L"Custom"},sampling,[this](int i){sampling=i;Changed();});Number(L"Margin",margin,0,100000,[this](double v){margin=v;Changed();});Choice(L"Color Adaptation",{L"None",L"High",L"Default",L"Very High"},color,[this](int i){color=i;Changed();});Choice(L"Rotation Adaptation",{L"None",L"Low",L"Medium",L"High",L"Full"},rotation,[this](int i){rotation=i;Changed();});ToggleField(L"Scale",&scaleSamples);ToggleField(L"Mirror",&mirror);ToggleField(L"Sample All Layers",&all);Choice(L"Output To",{L"Current Layer",L"New Layer",L"Duplicate Layer"},output,[this](int i){output=i;Changed();});panel->Row<Label>(22,L"Drag to choose the sampling area",F_Label);panel->Row<Label>(22,L"The selection is the fill target",F_Label);}
    ToggleField(L"Preview",&preview);panel->Row<TextButton>(34,L"Cancel",TextButton::Secondary,[this]{Finish(false);});panel->Row<TextButton>(34,L"OK",TextButton::Primary,[this]{Finish(true);});
  }
  void Begin(Kind k){if(auto* previous=Mode()){previous->Deactivate();jobs::Run([]{},[this,k]{Begin(k);});return;}auto* l=ActiveLayer();if(!l||(*l)["kind"].Str()!="Pixel"){ui::Toast(L"Select a pixel layer first",true);return;}if(k==Fill&&!docInfo["hasSelection"].Truthy()){ui::Toast(L"Create a selection first",true);return;}
    kind=k;active=true;dirty=k==Fill;geometryDirty=k==Puppet;loading=false;layer=uint64_t(docInfo["activeLayer"].Int());++epoch;pins.clear();planes.clear();pastes.clear();clones.clear();corners.clear();stroke.clear();undo.clear();redo.clear();geometry=Json{};selected=-1;drag=-1;operation=0;sampling=0;hasSource=false;preview=true;
    auto b=(*l)["bounds"];frame=RWH(float(b[0].Num()),float(b[1].Num()),float(b[2].Num()),float(b[3].Num()));auto selection=docInfo["selectionBounds"];if(k==Scale&&selection.size()==4)frame=RWH(float(selection[0].Num()),float(selection[1].Num()),float(selection[2].Num()),float(selection[3].Num()));sample=RWH(0,0,float(docInfo["width"].Num()),float(docInfo["height"].Num()));last={MidX(sample),MidY(sample)};
    live=std::make_shared<LivePreview>(Command());snapshot.reset(pc_snapshot(engine),pc_snapshot_free);if(!panel){panel=std::make_unique<ToolPanel>(Title());panel->home=[](const Rect& w,D2D1_SIZE_F s){return RWH(w.right-s.width-24,w.top+130,s.width,s.height);};ui::AddLayer(panel.get());}panel->visible=true;Build();EnterMode(this);
  }
  void Finish(bool keep){if(!active)return;active=false;++epoch;if(live)live->Finish(keep&&Ready(),Params());live.reset();snapshot.reset();panel->visible=false;LeaveMode(this);}
  bool Ready()const{return kind==Puppet?!pins.empty():kind==Vanishing?!planes.empty()&&(!pastes.empty()||!clones.empty()):true;}
  void Tick()override{if(!active)return;panel->Place(ui::Bounds(),false);if(jobs::Busy()||loading||!live||live->Busy()||drag>=0||!stroke.empty())return;
    if(geometryDirty){geometryDirty=false;loading=true;auto token=epoch;auto snap=snapshot;auto params=Params();auto result=std::make_shared<std::string>();jobs::Run([snap,params,result]{*result=TakeString(pc_puppet_geometry(snap.get(),params.c_str()));},[this,token,result]{if(token!=epoch)return;loading=false;if(!result->empty())geometry=Json::Parse(*result);else ui::Toast(W(LastError()),true);ui::RequestFrame();});return;}
    if(dirty){dirty=false;if(preview&&Ready())live->Show(Params());else live->Hide();}
  }
  Point Rest(Point p)const{for(auto& triangle:geometry["triangles"].a){auto a=FromJson(geometry["vertices"][size_t(triangle[0].Int())]),b=FromJson(geometry["vertices"][size_t(triangle[1].Int())]),c=FromJson(geometry["vertices"][size_t(triangle[2].Int())]);double det=(b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);if(std::abs(det)<1e-9)continue;double u=((b.y-c.y)*(p.x-c.x)+(c.x-b.x)*(p.y-c.y))/det,v=((c.y-a.y)*(p.x-c.x)+(a.x-c.x)*(p.y-c.y))/det,w=1-u-v;if(u<-.001||v<-.001||w<-.001)continue;auto A=FromJson(geometry["rest"][size_t(triangle[0].Int())]),B=FromJson(geometry["rest"][size_t(triangle[1].Int())]),C=FromJson(geometry["rest"][size_t(triangle[2].Int())]);return {float(u*A.x+v*B.x+w*C.x),float(u*A.y+v*B.y+w*C.y)};}return {NAN,NAN};}
  int PlaneAt(Point p)const{for(size_t i=planes.size();i>0;i--){Point uv;if(Project(planes[i-1],p.x,p.y,uv,true)&&uv.x>=0&&uv.x<=1&&uv.y>=0&&uv.y<=1)return int(i-1);}return -1;}
  void Paste(){if(planes.empty()){ui::Toast(L"Create a perspective plane first",true);return;}auto token=epoch;ReadClipboardForCanvas([this,token]{if(!active||token!=epoch)return;Point uv;int plane=PlaneAt(last);if(plane<0)plane=std::clamp(selected,0,int(planes.size())-1);if(!Project(planes[size_t(plane)],last.x,last.y,uv,true))uv={.25f,.25f};Remember();pastes.push_back(JsonObject().Num("plane",plane).Add("at",Pt(std::clamp(uv.x,0.f,1.f),std::clamp(uv.y,0.f,1.f))).Num("width",pasteWidth).Text());Changed();});}
  void Down(double x,double y,const PointerEvent& e)override{last=start={float(x),float(y)};drag=-1;if(!active)return;
    if(kind==Puppet){for(size_t i=0;i<pins.size();i++)if(std::hypot(pins[i].dst.x-x,pins[i].dst.y-y)*canvas->Zoom()<12){Remember();selected=int(i);if(e.mods.alt){pins.erase(pins.begin()+i);selected=-1;Changed();}else drag=int(i);Build();return;}
      auto rest=Rest(start);if(!std::isfinite(rest.x)||pins.size()>=256)return;Remember();pins.push_back({rest,start});selected=drag=int(pins.size())-1;Changed();Build();
    }else if(kind==Vanishing){if(operation==0){corners.push_back(start);if(corners.size()==4){std::array<Point,4> q;std::copy(corners.begin(),corners.end(),q.begin());if(Convex(q)){Remember();planes.push_back(q);selected=int(planes.size())-1;operation=1;Changed();Build();}else ui::Toast(L"The plane must be a convex quadrilateral",true);corners.clear();}}
      else if(operation==1){for(size_t i=0;i<planes.size();i++)for(int j=0;j<4;j++)if(std::hypot(planes[i][j].x-x,planes[i][j].y-y)*canvas->Zoom()<12){Remember();selected=int(i);drag=j;return;}selected=PlaneAt(start);}
      else if(operation==2){selected=PlaneAt(start);Paste();}else if(e.mods.alt){source=start;hasSource=true;}else if(hasSource&&PlaneAt(start)>=0){Remember();stroke={start};}
    }else if(kind==Scale){Point handles[4]={{frame.left,frame.top},{frame.right,frame.top},{frame.right,frame.bottom},{frame.left,frame.bottom}};for(int i=0;i<4;i++)if(std::hypot(handles[i].x-x,handles[i].y-y)*canvas->Zoom()<14){Remember();drag=i;return;}}
    else{Remember();sampling=2;drag=0;sample=RWH(float(x),float(y),1,1);}
  }
  void Drag(double x,double y,const PointerEvent&)override{last={float(x),float(y)};
    if(kind==Puppet&&drag>=0&&size_t(drag)<pins.size()){pins[size_t(drag)].dst=last;Changed();}
    else if(kind==Vanishing){if(drag>=0&&selected>=0&&size_t(selected)<planes.size()){auto q=planes[size_t(selected)];q[size_t(drag)]=last;if(Convex(q)){planes[size_t(selected)]=q;Changed();}}else if(!stroke.empty()&&std::hypot(stroke.back().x-x,stroke.back().y-y)*canvas->Zoom()>1)stroke.push_back(last);}
    else if(kind==Scale&&drag>=0){if(drag==0||drag==3)frame.left=std::min(float(x),frame.right-1);else frame.right=std::max(float(x),frame.left+1);if(drag==0||drag==1)frame.top=std::min(float(y),frame.bottom-1);else frame.bottom=std::max(float(y),frame.top+1);frame.right=std::min(frame.right,frame.left+60000);frame.bottom=std::min(frame.bottom,frame.top+60000);Changed();}
    else if(kind==Fill&&drag>=0){sample=R(std::clamp(std::min(start.x,last.x),0.f,float(docInfo["width"].Num()-1)),std::clamp(std::min(start.y,last.y),0.f,float(docInfo["height"].Num()-1)),std::clamp(std::max(start.x,last.x),1.f,float(docInfo["width"].Num())),std::clamp(std::max(start.y,last.y),1.f,float(docInfo["height"].Num())));Changed();}ui::RequestFrame();
  }
  void Up(double x,double y,const PointerEvent& e)override{Drag(x,y,e);drag=-1;if(!stroke.empty()){clones.push_back(JsonObject().Add("source",Pt(source.x,source.y)).Add("points",PointList(stroke,false)).Num("size",size).Text());stroke.clear();Changed();}if(kind==Scale||kind==Fill)Build();}
  void Segment(Point a,Point b,D2D1_COLOR_F color,float width=1){auto A=canvas->ToScreen(a.x,a.y),B=canvas->ToScreen(b.x,b.y);Line(A.x,A.y,B.x,B.y,color,width);}
  void Dot(Point p,bool chosen=false){auto v=canvas->ToScreen(p.x,p.y);FillEllipse(v.x,v.y,5,5,chosen?P.accent:P.text);StrokeEllipse(v.x,v.y,7,7,P.sunk,2);}
  void Overlay()override{if(!active)return;
    if(kind==Puppet){if(meshVisible)for(auto& t:geometry["triangles"].a)for(int i=0;i<3;i++)Segment(FromJson(geometry["vertices"][size_t(t[size_t(i)].Int())]),FromJson(geometry["vertices"][size_t(t[size_t((i+1)%3)].Int())]),Rgba(.8f,.8f,.8f,.4f));for(size_t i=0;i<pins.size();i++)Dot(pins[i].dst,int(i)==selected);}
    else if(kind==Vanishing){for(size_t i=0;i<planes.size();i++){auto& q=planes[i];for(int j=0;j<=10;j++){Point a,b;if(Project(q,j/10.,0,a)&&Project(q,j/10.,1,b))Segment(a,b,P.accent);if(Project(q,0,j/10.,a)&&Project(q,1,j/10.,b))Segment(a,b,P.accent);}for(auto p:q)Dot(p,int(i)==selected);}for(size_t i=1;i<corners.size();i++)Segment(corners[i-1],corners[i],P.accent,2);for(auto p:corners)Dot(p);if(hasSource){auto p=canvas->ToScreen(source.x,source.y);Line(p.x-6,p.y,p.x+6,p.y,P.text,2);Line(p.x,p.y-6,p.x,p.y+6,P.text,2);}}
    else {Rect r=kind==Scale?frame:sample;Point q[4]={{r.left,r.top},{r.right,r.top},{r.right,r.bottom},{r.left,r.bottom}};for(int i=0;i<4;i++)Segment(q[i],q[(i+1)%4],kind==Fill?Rgba(.2f,1,.4f,.9f):P.accent,2);if(kind==Scale)for(auto p:q)Dot(p);}
  }
  bool Options(Container& dock,std::vector<Widget*>& out)override{out.push_back(dock.Add<Label>(i18n::Tr(Title())+L"  ·  "+i18n::Tr(L"Enter applies, Esc cancels"),F_Label));return true;}
  bool Key(UINT vk,Mods m)override{if(vk==VK_ESCAPE){Finish(false);return true;}if(vk==VK_RETURN){Finish(true);return true;}if(kind==Vanishing&&m.ctrl&&vk=='V'){Paste();return true;}if(vk==VK_DELETE&&kind==Puppet&&selected>=0&&size_t(selected)<pins.size()){Remember();pins.erase(pins.begin()+selected);selected=-1;Changed();Build();return true;}if(m.ctrl&&(vk=='Z'||vk=='Y')){bool forward=vk=='Y'||m.shift;auto& from=forward?redo:undo;auto& to=forward?undo:redo;if(!from.empty()){to.push_back(Save());auto s=from.back();from.pop_back();Restore(s);}return true;}return false;}
  void Deactivate()override{Finish(false);}
};
CanvasWorkspace workspace;
}
bool CanvasWorkspaceCommand(const std::string& id){if(id=="edit.puppetWarp")workspace.Begin(CanvasWorkspace::Puppet);else if(id=="filter.vanishingPoint")workspace.Begin(CanvasWorkspace::Vanishing);else if(id=="edit.contentAwareScale")workspace.Begin(CanvasWorkspace::Scale);else if(id=="edit.contentAwareFill")workspace.Begin(CanvasWorkspace::Fill);else return false;return true;}
ToolPanel* CanvasWorkspacePanel(){return workspace.panel.get();}
Json CanvasWorkspaceState(){return Json::Parse(JsonObject().Bool("active",workspace.active).Str("command",workspace.Command()).Add("params",workspace.Params()).Add("geometry",JsonText(workspace.geometry)).Num("corners",double(workspace.corners.size())).Bool("preview",workspace.live&&workspace.live->Applied()).Text());}
