#include "workspace_editors.h"
#include <array>

namespace {
class SelectMask : public CanvasTool {
 public:
  bool active=false,dirty=false,loading=false;
  int view=2,source=0,output=0;
  double radius=0,smooth=0,feather=0,contrast=0,shift=0,amount=100,size=30,opacity=50;
  bool smart=false,decontaminate=false,all=false,subtract=false;
  uint64_t epoch=0,overlayRevision=~0ull;
  int overlayView=-1;
  double overlayOpacity=-1;
  std::unique_ptr<ToolPanel> panel;
  std::shared_ptr<LivePreview> live;
  std::vector<D2D_POINT_2F> points;
  std::vector<std::string> strokes;
  Microsoft::WRL::ComPtr<ID2D1Bitmap1> overlay;
  static constexpr UINT Side=768;
  void Begin() {
    if(active)return;
    auto* layer=ActiveLayer();bool mask=layer&&!(*layer)["mask"].IsNull();
    if(!docInfo["hasSelection"].Truthy()&&!mask) {ui::Toast(L"Create a selection or layer mask first",true);return;}
    active=true;dirty=true;loading=false;++epoch;overlay.Reset();overlayRevision=~0ull;
    source=docInfo["hasSelection"].Truthy()?0:1;output=source==1?1:0;
    strokes.clear();points.clear();
    live=std::make_shared<LivePreview>("select.refineEdge");
    if(!panel) {panel=std::make_unique<ToolPanel>(L"Select and Mask");ui::AddLayer(panel.get());}
    else panel->Reset();
    panel->visible=true;panel->close->onClick=[this]{Finish(false);};
    panel->home=[](const Rect& w,D2D1_SIZE_F s){return RWH(w.right-s.width-24,w.top+130,s.width,s.height);};
    auto list=[this](std::wstring title,std::vector<std::wstring> names,int value,std::function<void(int)> change) {
      for(auto& name:names)name=i18n::Tr(name);
      auto* field=panel->Row<SelectList>(32,std::move(names),value);field->label=i18n::Tr(title);field->labelWidth=110;
      field->onChange=std::move(change);return field;
    };
    auto* src=list(L"Source",{L"Selection",L"Layer Mask"},source,[this](int i){source=i;dirty=true;});
    src->enabled=mask&&docInfo["hasSelection"].Truthy();
    list(L"View",{L"Onion Skin",L"Marching Ants",L"Overlay",L"On Black",L"On White"},view,[this](int i){view=i;overlayView=-1;});
    auto number=[this](std::wstring title,double* value,double lo,double hi){
      auto* f=panel->Row<NumberField>(30,title,*value,lo,hi,1);f->onCommit=[this,value](double v){*value=v;dirty=true;};return f;
    };
    auto* op=number(L"Opacity",&opacity,0,100);op->onCommit=[this](double v){opacity=v;overlayOpacity=-1;};
    number(L"Brush Size",&size,.5,5000);
    auto* sub=panel->Row<Toggle>(28,L"Subtract",subtract);sub->onChange=[this](bool v){subtract=v;};
    number(L"Radius",&radius,0,250);
    auto* sr=panel->Row<Toggle>(28,L"Smart Radius",smart);sr->onChange=[this](bool v){smart=v;dirty=true;};
    number(L"Smooth",&smooth,0,100);number(L"Feather",&feather,0,1000);number(L"Contrast",&contrast,0,100);number(L"Shift Edge",&shift,-100,100);
    auto* dc=panel->Row<Toggle>(28,L"Decontaminate Colors",decontaminate);dc->onChange=[this](bool v){decontaminate=v;dirty=true;};number(L"Amount",&amount,0,100);
    auto* sample=panel->Row<Toggle>(28,L"Sample All Layers",all);sample->onChange=[this](bool v){all=v;dirty=true;};
    list(L"Output To",{L"Selection",L"Layer Mask",L"New Layer",L"New Layer with Layer Mask",L"New Document",L"New Document with Layer Mask"},output,[this](int i){output=i;});
    panel->Row<TextButton>(34,L"Cancel",TextButton::Secondary,[this]{Finish(false);});
    panel->Row<TextButton>(34,L"OK",TextButton::Primary,[this]{Finish(true);});
    EnterMode(this);
  }
  std::string Params(bool preview) const {
    static const char* outputs[]={"selection","layerMask","newLayer","newLayerWithMask","newDocument","newDocumentWithMask"};
    std::string path="[";for(auto& s:strokes){if(path.size()>1)path+=",";path+=s;}path+="]";
    return JsonObject().Str("source",source==0?"selection":"layerMask").Str("output",preview?"selection":outputs[output]).Num("radius",radius).Bool("smartRadius",smart).Num("smooth",smooth).Num("feather",feather).Num("contrast",contrast).Num("shiftEdge",shift).Bool("decontaminate",decontaminate).Num("amount",amount).Bool("sampleAllLayers",all).Add("refineStrokes",path).Text();
  }
  void Finish(bool keep) {
    if(!active)return;active=false;++epoch;
    if(live)live->Finish(keep,Params(false));
    live.reset();panel->visible=false;overlay.Reset();LeaveMode(this);
  }
  void Down(double x,double y,const PointerEvent&) override {if(active)points={{float(x),float(y)}};}
  void Drag(double x,double y,const PointerEvent&) override {if(!points.empty()&&std::hypot(points.back().x-x,points.back().y-y)*canvas->Zoom()>=1)points.push_back({float(x),float(y)});}
  void Up(double x,double y,const PointerEvent& e) override {
    if(points.empty())return;Drag(x,y,e);
    strokes.push_back(JsonObject().Add("points",PointList(points,false)).Num("size",size).Str("mode",subtract||e.mods.alt?"subtract":"add").Text());
    points.clear();dirty=true;
  }
  void Tick() override {
    if(!active)return;
    panel->Place(ui::Bounds(),false);
    if(dirty&&points.empty()&&!jobs::Busy()&&!live->Busy()) {dirty=false;live->Show(Params(true));return;}
    uint64_t rev=uint64_t(docInfo["revision"].Int());
    if(view==1||loading||jobs::Busy()||dirty||live->Busy()||(overlayRevision==rev&&overlayView==view&&overlayOpacity==opacity))return;
    overlayRevision=rev;overlayView=view;overlayOpacity=opacity;loading=true;
    auto token=epoch;auto pixels=std::make_shared<std::vector<uint8_t>>(Side*Side*4);auto ok=std::make_shared<bool>(false);
    auto params=JsonObject().Str("kind","selectionOverlay").Num("view",view).Num("opacity",opacity).Text();
    jobs::Run([pixels,ok,params]{*ok=pc_panel_thumb(engine,params.c_str(),Side,pixels->data(),pixels->size(),Side*4)==1;},[this,token,pixels,ok]{if(token!=epoch)return;loading=false;if(*ok)overlay=MakeBitmap(Side,Side,pixels->data(),Side*4);ui::RequestFrame();});
  }
  void Overlay() override {
    if(!active)return;
    if(view!=1&&overlay) {
      float dw=float(canvas->DocW()),dh=float(canvas->DocH());
      float scale=std::min(1.f,std::min(float(Side)/dw,float(Side)/dh)),w=std::round(dw*scale),h=std::round(dh*scale);
      Rect src=RWH(std::floor((Side-w)/2),std::floor((Side-h)/2),w,h),dst=RWH(0,0,dw,dh);
      auto o=canvas->ToScreen(0,0),x=canvas->ToScreen(dw,0),y=canvas->ToScreen(0,dh);
      D2D1_MATRIX_3X2_F before;Dc()->GetTransform(&before);
      auto transform=D2D1::Matrix3x2F((x.x-o.x)/dw,(x.y-o.y)/dw,(y.x-o.x)/dh,(y.y-o.y)/dh,o.x,o.y);
      Dc()->SetTransform(transform* *D2D1::Matrix3x2F::ReinterpretBaseType(&before));DrawBitmap(overlay.Get(),dst,1,&src,false);Dc()->SetTransform(before);
    }
    if(pointerX>=0&&canvas->PointerOver())StrokeEllipse(pointerX,pointerY,float(size*canvas->Zoom()/2),float(size*canvas->Zoom()/2),P.text,1);
  }
  bool ShowsSelection() const override{return view==1;}
  bool Options(Container& dock,std::vector<Widget*>& out) override {out.push_back(dock.Add<Label>(L"Select and Mask  ·  refine the edge  ·  Enter applies, Esc cancels",F_Label));return true;}
  bool Key(UINT vk,Mods) override {if(vk==VK_RETURN){Finish(true);return true;}if(vk==VK_ESCAPE){Finish(false);return true;}return false;}
  void Deactivate() override {Finish(false);}
};
SelectMask selectMask;
}
bool WorkspaceEditorCommand(const std::string& id) {if(id=="select.refineEdge"||id=="select.selectAndMask"){selectMask.Begin();return true;}return false;}
ToolPanel* WorkspaceEditorPanel(){return selectMask.panel.get();}
Json WorkspaceEditorState(){return Json::Parse(JsonObject().Bool("active",selectMask.active).Str("kind","selectMask").Num("strokes",double(selectMask.strokes.size())).Bool("preview",selectMask.live&&selectMask.live->Applied()).Add("params",selectMask.Params(false)).Text());}
