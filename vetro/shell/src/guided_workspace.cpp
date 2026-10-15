#include "guided_workspace.h"
namespace {
class GuidedTool : public CanvasTool {
 public:
  struct Guide{D2D_POINT_2F from,to;int orientation;};
  bool active=false,preview=false,dirty=false;
  int orientation=0,selected=-1,end=0;
  std::vector<Guide> lines;
  std::vector<std::vector<Guide>> undo,redo;
  std::unique_ptr<ToolPanel> panel;
  std::shared_ptr<LivePreview> live;
  std::string Params()const{std::string list="[";for(auto& g:lines){if(list.size()>1)list+=",";list+=JsonObject().Add("from",Pt(g.from.x,g.from.y)).Add("to",Pt(g.to.x,g.to.y)).Str("orientation",g.orientation?"vertical":"horizontal").Text();}return JsonObject().Add("lines",list+"]").Text();}
  void Remember(){undo.push_back(lines);if(undo.size()>100)undo.erase(undo.begin());redo.clear();}
  void Begin(){if(auto* previous=Mode()){previous->Deactivate();jobs::Run([]{},[this]{Begin();});return;}if(!pc_enabled(engine,"image.guidedUpright")){ui::Toast(L"Select a pixel layer first",true);return;}active=true;preview=false;dirty=false;selected=-1;orientation=0;lines.clear();undo.clear();redo.clear();live=std::make_shared<LivePreview>("image.guidedUpright");
    if(!panel){panel=std::make_unique<ToolPanel>(L"Guided Upright");panel->home=[](const Rect& w,D2D1_SIZE_F s){return RWH(w.right-s.width-24,w.top+130,s.width,s.height);};ui::AddLayer(panel.get());}panel->Reset();panel->visible=true;panel->close->onClick=[this]{Finish(false);};
    auto* direction=panel->Row<SelectList>(32,std::vector<std::wstring>{i18n::Tr(L"Horizontal"),i18n::Tr(L"Vertical")},0);direction->onChange=[this](int i){orientation=i;};
    auto* toggle=panel->Row<Toggle>(28,L"Preview",false);toggle->onChange=[this](bool v){preview=v;dirty=true;};
    panel->Row<TextButton>(32,L"Clear Guides",TextButton::Secondary,[this]{Remember();lines.clear();selected=-1;dirty=true;});
    panel->Row<Label>(22,L"Drag along the horizon or an edge",F_Label);panel->Row<Label>(22,L"Up to two guides in each direction",F_Label);panel->Row<Label>(22,L"Two converging guides fix perspective",F_Label);panel->Row<Label>(22,L"Turn Preview off to edit source guides",F_Label);
    panel->Row<TextButton>(34,L"Cancel",TextButton::Secondary,[this]{Finish(false);});panel->Row<TextButton>(34,L"OK",TextButton::Primary,[this]{Finish(true);});EnterMode(this);
  }
  void Finish(bool keep){if(!active)return;active=false;live->Finish(keep&&!lines.empty(),Params());live.reset();panel->visible=false;LeaveMode(this);}
  void Down(double x,double y,const PointerEvent&){if(preview){ui::Toast(L"Turn Preview off to edit source guides",true);return;}selected=-1;for(size_t i=0;i<lines.size();i++)for(int e=0;e<2;e++){auto p=e?lines[i].to:lines[i].from;if(std::hypot(p.x-x,p.y-y)*canvas->Zoom()<12){Remember();selected=int(i);end=e;return;}}
    if(std::count_if(lines.begin(),lines.end(),[this](const Guide& g){return g.orientation==orientation;})>=2){ui::Toast(L"At most two guides per direction",true);return;}Remember();lines.push_back({{float(x),float(y)},{float(x),float(y)},orientation});selected=int(lines.size())-1;end=1;}
  void Drag(double x,double y,const PointerEvent&)override{if(!preview&&selected>=0&&size_t(selected)<lines.size()){auto& g=lines[size_t(selected)];(end?g.to:g.from)={float(x),float(y)};ui::RequestFrame();}}
  void Up(double x,double y,const PointerEvent& e)override{Drag(x,y,e);if(selected>=0&&size_t(selected)<lines.size()){auto& g=lines[size_t(selected)];if(std::hypot(g.from.x-g.to.x,g.from.y-g.to.y)<1){lines.erase(lines.begin()+selected);selected=-1;}}dirty=true;}
  void Tick()override{if(!active)return;panel->Place(ui::Bounds(),false);if(dirty&&!jobs::Busy()&&!live->Busy()){dirty=false;if(preview&&!lines.empty())live->Show(Params());else live->Hide();}}
  void Overlay()override{if(preview)return;for(size_t i=0;i<lines.size();i++){auto a=canvas->ToScreen(lines[i].from.x,lines[i].from.y),b=canvas->ToScreen(lines[i].to.x,lines[i].to.y);auto color=lines[i].orientation?P.accent:P.text;Line(a.x,a.y,b.x,b.y,color,2);for(auto p:{a,b}){FillEllipse(p.x,p.y,4,4,color);StrokeEllipse(p.x,p.y,6,6,P.sunk,1.5f);}}}
  bool Key(UINT key,Mods m)override{if(key==VK_ESCAPE){Finish(false);return true;}if(key==VK_RETURN){Finish(true);return true;}if(key==VK_DELETE&&selected>=0&&size_t(selected)<lines.size()){Remember();lines.erase(lines.begin()+selected);selected=-1;dirty=true;return true;}if(m.ctrl&&(key=='Z'||key=='Y')){auto& from=key=='Y'||m.shift?redo:undo;auto& to=key=='Y'||m.shift?undo:redo;if(!from.empty()){to.push_back(lines);lines=from.back();from.pop_back();selected=-1;dirty=true;}return true;}return false;}
  bool Options(Container& dock,std::vector<Widget*>& out)override{out.push_back(dock.Add<Label>(i18n::Tr(L"Guided Upright")+L"  ·  "+i18n::Tr(L"Enter applies, Esc cancels"),F_Label));return true;}
  void Deactivate()override{Finish(false);}
};
GuidedTool guided;
}
bool GuidedWorkspaceCommand(const std::string& id){if(id!="image.guidedUpright")return false;guided.Begin();return true;}
Json GuidedWorkspaceState(){return Json::Parse(JsonObject().Bool("active",guided.active).Bool("preview",guided.live&&guided.live->Applied()).Add("params",guided.Params()).Text());}
ToolPanel* GuidedWorkspacePanel(){return guided.panel.get();}
