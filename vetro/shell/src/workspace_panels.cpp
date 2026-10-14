// Native panels share engine commands with the upstream UI; only presentation lives here.
#include "workspace_panels.h"
#include "color_picker.h"
#include "icons_phosphor.h"
#include <commdlg.h>
#include <set>
#include <array>
#include <thread>
#include <mutex>

void AskText(double,double,std::wstring,std::wstring,std::function<void(const std::wstring&)>);
namespace {
std::map<std::string,ToolPanel*> panels;
std::map<std::string,Json> views;
std::map<std::string,std::string> searches,selected;
std::map<std::string,std::string> selectedGroups;
std::map<std::string,std::set<std::string>> collapsed,emptyGroups;
std::map<uint64_t,std::string> selectedPaths;
std::map<uint64_t,int> historySources;
std::map<uint64_t,uint64_t> snapshotSources;
std::map<std::string,Microsoft::WRL::ComPtr<ID2D1Bitmap1>> thumbs;
std::set<std::string> thumbPending,loading;
std::unique_ptr<Menu> context;
uint64_t cacheDocument=0,cacheRevision=~0ull;
Json histogram,samplerInfo,cursorInfo;
Microsoft::WRL::ComPtr<ID2D1Bitmap1> navigation;
bool analysisLoading=false,cursorLoading=false;
struct AnalysisResult {uint64_t doc=0,revision=0;bool histogram=false,navigator=false;Json values;std::vector<uint8_t> pixels;std::array<uint32_t,2> size{};};
std::mutex analysisMutex;
std::shared_ptr<AnalysisResult> analysisResult;
std::jthread analysisThread;
uint64_t analysisDocument=0,analysisRevision=~0ull;
int histChannel=0,lastX=-1,lastY=-1;
double sampleAt=0;
ColorSpectrum* colorSpectrum=nullptr;
std::string foreground;

uint64_t Doc(){return uint64_t(docInfo["id"].Int());}
D2D1_COLOR_F Color(const Json& j) {
  if(j.kind==Json::Array)return Rgba(float(j[0].Num()),float(j[1].Num()),float(j[2].Num()),1);
  std::string s=j.Str();if(s=="foreground"||s=="background")return Color(sessionInfo[s.c_str()]);
  unsigned r=0,g=0,b=0;if(s.size()==7 && s[0]=='#')sscanf_s(s.c_str()+1,"%2x%2x%2x",&r,&g,&b);
  return Rgba(r/255.f,g/255.f,b/255.f,1);
}
std::string Hex(D2D1_COLOR_F c){char b[16];snprintf(b,sizeof b,"#%02x%02x%02x",int(c.r*255+.5f),int(c.g*255+.5f),int(c.b*255+.5f));return b;}
void Name(std::wstring title,std::string initial,std::function<void(std::string)> done){AskText(canvas->DocW()/2,canvas->DocH()/2,i18n::Tr(title),W(initial),[done](const std::wstring& v){if(!v.empty())done(Narrow(v));});}
// Queries bypass Exec's document refresh: their completion never recursively schedules itself.
void Query(const std::string& id,const std::string& params,std::function<void(Json)> done) {
  auto result=std::make_shared<std::string>();auto ok=std::make_shared<bool>(false);
  jobs::Run([id,params,result,ok]{char* r=nullptr;*ok=pc_execute(engine,id.c_str(),params.c_str(),&r)==1;*result=TakeString(r);},[result,ok,done]{done(*ok?Json::Parse(*result):Json{});ui::RequestFrame();});
}
void Thumbnail(const std::string& key,const std::string& params) {
  if(thumbs.count(key)||thumbPending.count(key))return;
  thumbPending.insert(key);auto pixels=std::make_shared<std::vector<uint8_t>>(48*48*4);auto ok=std::make_shared<bool>(false);auto document=Doc(),revision=uint64_t(docInfo["revision"].Int());
  jobs::Run([pixels,ok,params]{*ok=pc_panel_thumb(engine,params.c_str(),48,pixels->data(),pixels->size(),48*4)==1;},[pixels,ok,key,document,revision]{thumbPending.erase(key);if(*ok && document==Doc() && revision==uint64_t(docInfo["revision"].Int()))thumbs[key]=MakeBitmap(48,48,pixels->data(),48*4);ui::RequestFrame();});
}
void Button(ToolPanel* p,std::wstring label,std::function<void()> action,bool enabled=true){auto* b=p->Row<TextButton>(30,std::move(label),TextButton::Secondary,std::move(action));b->enabled=enabled;}
std::wstring ChooseFile(bool save,const std::string& key) {
  wchar_t path[32768]{};const wchar_t* filter=key=="swatches"?L"Adobe Swatches (*.aco;*.ase)\0*.aco;*.ase\0\0":key=="gradients"?L"Photoshop Gradients (*.grd)\0*.grd\0\0":L"Photoshop Patterns (*.pat)\0*.pat\0\0";
  OPENFILENAMEW f{};f.lStructSize=sizeof f;f.hwndOwner=hwnd;f.lpstrFilter=filter;f.lpstrFile=path;f.nMaxFile=32768;f.Flags=OFN_NOCHANGEDIR|OFN_EXPLORER|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);f.lpstrDefExt=key=="swatches"?L"aco":key=="gradients"?L"grd":L"pat";
  return (save?GetSaveFileNameW(&f):GetOpenFileNameW(&f))?path:L"";
}
void BuildAssets(std::string,const Json&);
void LoadAssets(const std::string& key) {
  if(loading.count(key))return;loading.insert(key);
  Query(key=="swatches"?"swatches.list":key=="gradients"?"gradient.presets.list":"pattern.list","{}",[key](Json j){loading.erase(key);views[key]=j;BuildAssets(key,j);});
}
std::pair<std::string,std::string> PatternGroup(const std::string& name){auto at=name.find('/');return at==std::string::npos?std::pair{std::string("Patterns"),name}:std::pair{name.substr(0,at),name.substr(at+1)};}
void EditAsset(const std::string& key,const Json& item,const std::string& group,const std::string& action,const std::string& value="") {
  JsonObject p;
  if(key=="swatches") {
    p.Str("swatch",item["name"].Str()).Str("group",group);if(action=="rename")p.Str("name",value);if(action=="move")p.Str("to",value);
    Exec("swatches."+action,p.Text());
  } else if(key=="gradients") {
    p.Str("preset",item["name"].Str()).Str("group",group).Str("action",action);if(action=="rename")p.Str("name",value);if(action=="move")p.Str("to",value);
    Exec("gradient.presets.edit",p.Text());
  } else {
    p.Str("pattern",item["id"].Str());
    if(action!="delete") {auto [oldGroup,name]=PatternGroup(item["name"].Str());p.Str("name",action=="rename"?(oldGroup=="Patterns"?value:oldGroup+"/"+value):value+"/"+name);}
    Exec(action=="delete"?"pattern.delete":"pattern.rename",p.Text());
  }
}
void GroupMenu(const std::string& key,const std::string& group,const Rect& from) {
  std::vector<MenuItem> rows;
  rows.push_back(ActionItem(L"Rename Group",[key,group]{Name(L"Rename Group",group,[key,group](std::string name){
    selectedGroups[key]=name;
    if(key=="patterns") {for(auto& item:views[key].a)if(PatternGroup(item["name"].Str()).first==group)EditAsset(key,item,group,"move",name);emptyGroups[key].erase(group);emptyGroups[key].insert(name);LoadAssets(key);}
    else {JsonObject p;p.Str("group",group).Str("name",name);if(key=="gradients")p.Str("action","renameGroup");Exec(key=="swatches"?"swatches.renameGroup":"gradient.presets.edit",p.Text());}
  });}));
  rows.push_back(ActionItem(L"Export Group…",[key,group]{auto path=ChooseFile(true,key);if(!path.empty())ExportPanelAssets(key,path,group);}));
  rows.push_back(ActionItem(L"Delete Group",[key,group]{
    if(selectedGroups[key]==group)selectedGroups[key].clear();
    if(key=="patterns") {for(auto& item:views[key].a)if(PatternGroup(item["name"].Str()).first==group)EditAsset(key,item,group,"delete");emptyGroups[key].erase(group);LoadAssets(key);}
    else{JsonObject p;p.Str("group",group);if(key=="gradients")p.Str("action","deleteGroup");Exec(key=="swatches"?"swatches.deleteGroup":"gradient.presets.edit",p.Text());}
  }));
  context=std::make_unique<Menu>(std::move(rows));context->Open(from,Side::Below);
}
class AssetRow : public Widget {
 public:
  std::string key,group;Json item;
  AssetRow(std::string k,std::string g,Json j):key(std::move(k)),group(std::move(g)),item(std::move(j)){}
  void Paint() override {
    if(parent && (rect.bottom<parent->rect.top || rect.top>parent->rect.bottom))return;
    std::string id=item["id"].Str().empty()?group+"/"+item["name"].Str():item["id"].Str();
    FillRound(rect,8,selected[key]==id?Fade(P.accent,.18f):Fade(P.hover,hover.v));Rect preview=RWH(rect.left+6,rect.top+4,48,H(rect)-8);Checkerboard(preview,5);
    if(key=="swatches")FillRound(preview,4,Color(item["hex"]));
    else if(key=="gradients") {
      auto stops=item["stops"];for(int x=0;x<48;x++) {double t=(x+.5)/48;auto color=Color(stops.size()?stops[stops.size()-1][1]:Json{});
        for(size_t i=1;i<stops.size();i++)if(t<=stops[i][0].Num()){auto a=stops[i-1],b=stops[i];color=Mix(Color(a[1]),Color(b[1]),float(std::clamp((t-a[0].Num())/std::max(.00001,b[0].Num()-a[0].Num()),0.,1.)));break;}
        FillRect(RWH(preview.left+x,preview.top,1,H(preview)),color);
      }
    } else {
      std::string cache="pattern/"+item["id"].Str();Thumbnail(cache,JsonObject().Str("kind","pattern").Str("id",item["id"].Str()).Text());
      if(thumbs.count(cache))DrawBitmap(thumbs[cache].Get(),preview,1,nullptr,false);
    }
    auto name=key=="patterns"?PatternGroup(item["name"].Str()).second:item["name"].Str();Text(W(name),R(rect.left+64,rect.top,rect.right-8,rect.bottom),F_Row,P.text);
  }
  void OnUp(const PointerEvent& e,bool inside) override {
    Widget::OnUp(e,inside);if(!inside)return;
    std::string id=item["id"].Str().empty()?group+"/"+item["name"].Str():item["id"].Str();selected[key]=id;selectedGroups[key]=group;
    if(e.button==1) {
      auto k=key,g=group;auto j=item;
      std::vector<MenuItem> rows{ActionItem(L"Rename…",[k,g,j]{Name(L"Name",k=="patterns"?PatternGroup(j["name"].Str()).second:j["name"].Str(),[k,g,j](std::string v){EditAsset(k,j,g,"rename",v);});}),ActionItem(L"Move to Group…",[k,g,j]{Name(L"Group",g,[k,g,j](std::string v){EditAsset(k,j,g,"move",v);});}),ActionItem(L"Delete",[k,g,j]{EditAsset(k,j,g,"delete");})};
      context=std::make_unique<Menu>(std::move(rows));context->Open(rect,Side::Below);return;
    }
    if(key=="swatches")Exec("swatches.use",JsonObject().Str("swatch",item["name"].Str()).Str("group",group).Str("target",e.mods.alt?"background":"foreground").Text());
    else if(key=="gradients")Exec(e.clicks>1?"gradient.presets.apply":"gradient.presets.select",JsonObject().Str("preset",item["name"].Str()).Str("group",group).Text());
    else Exec("layer.newFillLayer.pattern",JsonObject().Str("pattern",item["id"].Str()).Text());
  }
};
class GroupRow : public TextButton {
 public:
  std::string key,group;
  GroupRow(std::string k,std::string g):TextButton(W(g),Quiet,nullptr),key(std::move(k)),group(std::move(g)){onClick=[this]{selectedGroups[key]=group;if(!collapsed[key].erase(group))collapsed[key].insert(group);BuildAssets(key,views[key]);};}
  void Paint() override{label=(collapsed[key].count(group)?L"▸ ":L"▾ ")+W(group);TextButton::Paint();}
  void OnUp(const PointerEvent& e,bool inside) override{if(inside&&e.button==1){GroupMenu(key,group,rect);return;}TextButton::OnUp(e,inside);}
};
void BuildAssets(std::string key,const Json& data) {
  auto* p=panels[key];if(!p)return;p->Reset();
  auto* search=p->Row<TextField>(30,i18n::Tr(L"Search"),W(searches[key]));search->onChange=[key](const std::wstring& text){searches[key]=Narrow(text);BuildAssets(key,views[key]);};
  Button(p,L"New…",[key]{Name(key=="swatches"?L"New Swatch":key=="gradients"?L"New Gradient":L"Define Pattern","",[key](std::string name){JsonObject p;auto group=selectedGroups[key];p.Str("name",key=="patterns"&&!group.empty()&&group!="Patterns"?group+"/"+name:name);if(!group.empty()&&key!="patterns")p.Str("group",group);Exec(key=="swatches"?"swatches.add":key=="gradients"?"gradient.presets.new":"edit.definePattern",p.Text());});},key!="patterns"||!docInfo.IsNull());
  Button(p,L"New Group…",[key]{Name(L"New Group","",[key](std::string name){selectedGroups[key]=name;if(key=="patterns"){emptyGroups[key].insert(name);BuildAssets(key,views[key]);}else{JsonObject p;p.Str("name",name);if(key=="gradients")p.Str("action","newGroup");Exec(key=="swatches"?"swatches.newGroup":"gradient.presets.edit",p.Text());}});});
  Button(p,L"Import…",[key]{auto path=ChooseFile(false,key);if(!path.empty())ImportPanelAssets(key,path);});
  Button(p,L"Export…",[key]{auto path=ChooseFile(true,key);if(!path.empty())ExportPanelAssets(key,path);});
  std::map<std::string,std::vector<Json>> groups;
  if(key=="patterns") {for(auto& item:data.a)groups[PatternGroup(item["name"].Str()).first].push_back(item);for(auto& g:emptyGroups[key])groups[g];}
  else for(auto& group:data["groups"].a){groups[group["name"].Str()];for(auto& item:group[key=="swatches"?"swatches":"presets"].a)groups[group["name"].Str()].push_back(item);}
  size_t shown=0;
  for(auto& [group,items]:groups) {
    std::vector<Json> matches;for(auto& item:items){auto name=W(item["name"].Str()),term=W(searches[key]);std::transform(name.begin(),name.end(),name.begin(),towlower);std::transform(term.begin(),term.end(),term.begin(),towlower);if(term.empty()||name.find(term)!=std::wstring::npos)matches.push_back(item);}
    if(matches.empty() && !searches[key].empty())continue;
    p->Row<GroupRow>(30,key,group);if(collapsed[key].count(group)&&searches[key].empty())continue;
    for(auto& item:matches){if(shown++>=1000)break;p->Row<AssetRow>(48,key,group,item);}
  }
  if(!shown)p->Row<Label>(30,L"No matching presets",F_Label);
  if(shown>=1000)p->Row<Label>(30,L"Use Search to narrow the list",F_Small);
  p->Arrange();ui::RequestFrame();
}

class ChannelRow : public Container {
 public:
  std::string source,cache;Json parameters;bool targeted=false;
  ChannelRow(std::string channel,std::wstring name,bool eye,bool target,std::string thumbnail):source(std::move(channel)),cache(std::move(thumbnail)),targeted(target) {
    auto* b=Add<IconButton>(PhEye,L"Channel Visibility",[this]{Exec("channel.setVisible",JsonObject().Add("channel",source).Text());});b->SetSelected(eye);
    Add<Label>(std::move(name),F_Row);
  }
  void Layout() override{children[0]->rect=RWH(rect.left,rect.top+6,28,32);children[1]->rect=R(rect.left+90,rect.top,rect.right-5,rect.bottom);}
  void Paint() override{Layout();FillRound(rect,8,targeted?Fade(P.accent,.18f):Fade(P.hover,hover.v));Rect r=RWH(rect.left+34,rect.top+4,48,36);Checkerboard(r,4);Thumbnail(cache,JsonText(parameters));if(thumbs.count(cache))DrawBitmap(thumbs[cache].Get(),r,1,nullptr,false);Container::Paint();}
  bool Interactive() const override{return true;}
  void OnUp(const PointerEvent& e,bool inside) override {
    Widget::OnUp(e,inside);if(!inside)return;
    if(e.mods.ctrl){Exec("select.loadSelection",JsonObject().Add("channel",source).Text());return;}
    if(e.button==1){auto s=source;std::vector<MenuItem> rows{
      ActionItem(L"Duplicate Channel…",[s]{Name(L"Duplicate Channel","",[s](std::string n){Exec("channel.duplicate",JsonObject().Add("channel",s).Str("name",n).Text());});}),
      ActionItem(L"Load Selection",[s]{Exec("select.loadSelection",JsonObject().Add("channel",s).Text());}),
      ActionItem(L"Rename…",[s]{Name(L"Name","",[s](std::string n){Exec("channel.rename",JsonObject().Add("channel",s).Str("name",n).Text());});},nullptr,source.size()&&source[0]!='"'),
      ActionItem(L"Delete Channel",[s]{Exec("channel.delete",JsonObject().Add("channel",s).Text());},nullptr,source.size()&&source[0]!='"')};context=std::make_unique<Menu>(std::move(rows));context->Open(rect,Side::Below);return;}
    Exec("channel.target",JsonObject().Add("channel",source).Text());
  }
};
void BuildChannels() {
  auto* p=panels["channels"];p->Reset();if(docInfo.IsNull()){p->Row<Label>(30,L"No document",F_Label);return;}
  auto j=docInfo["channels"],target=j["target"];std::string kind=target["kind"].Str();
  auto add=[&](std::string source,std::wstring name,bool visible,bool active,Json params){auto* row=p->Row<ChannelRow>(44,source,std::move(name),visible,active,"channel/"+std::to_string(Doc())+"/"+source);row->parameters=params;};
  add(JsonQuote("composite"),W(j["composite"].Str()),j["compositeVisible"].Truthy(),kind=="composite",Json{});
  for(size_t i=0;i<j["colors"].size();i++) {auto c=j["colors"][i];std::string name=c["name"].Str(),lower=name;std::transform(lower.begin(),lower.end(),lower.begin(),tolower);add(JsonQuote(lower),i18n::Tr(W(name)),c["visible"].Truthy(),kind=="color"&&target["index"].Int()==int(i),Json::Parse(JsonObject().Num("component",double(i)).Text()));}
  for(auto& c:j["alpha"].a)add(JsonNum(c["index"].Num()),W(c["name"].Str()),c["visible"].Truthy(),kind=="alpha"&&target["index"].Int()==c["index"].Int(),Json::Parse(JsonObject().Num("alpha",c["index"].Num()).Text()));
  Button(p,L"New Channel…",[]{Name(L"New Channel","",[](std::string n){Exec("channel.new",JsonObject().Str("name",n).Text());});});
  Button(p,L"Save Selection as Channel",[]{Exec("select.saveSelection");},docInfo["hasSelection"].Truthy());
  Button(p,L"Load Selection",[]{Exec("select.loadSelection",R"({"channel":"targeted"})");});
  std::string source=kind=="alpha"?JsonNum(target["index"].Num()):kind=="color"?JsonQuote(j["colors"][size_t(target["index"].Int())]["name"].Str()):JsonQuote("composite");
  Button(p,L"Duplicate Channel…",[source]{Name(L"Duplicate Channel","",[source](std::string n){Exec("channel.duplicate",JsonObject().Add("channel",source).Str("name",n).Text());});});
  Button(p,L"Delete Channel",[]{Exec("channel.delete");},kind=="alpha");
}
void BuildPaths(Json data) {
  views["paths"]=data;auto* p=panels["paths"];p->Reset();auto doc=Doc();
  std::string name=selectedPaths[doc];bool exists=name=="work"?!data["workPath"].IsNull():name=="layer"?!data["layerPath"].IsNull():std::any_of(data["paths"].a.begin(),data["paths"].a.end(),[&](const Json& p){return p["name"].Str()==name;});
  if(!exists){name=!data["workPath"].IsNull()?"work":data["paths"].size()?data["paths"][0]["name"].Str():!data["layerPath"].IsNull()?"layer":"";selectedPaths[doc]=name;}
  auto row=[&](std::string name,std::wstring caption){auto* b=p->Row<TextButton>(36,std::move(caption),selectedPaths[doc]==name?TextButton::Primary:TextButton::Secondary,[doc,name]{selectedPaths[doc]=name;BuildPaths(views["paths"]);});b->tooltip=L"Path";};
  if(!data["workPath"].IsNull())row("work",L"Work Path");
  if(!data["layerPath"].IsNull())row("layer",W(data["layerPath"]["name"].Str()));
  for(auto& path:data["paths"].a)row(path["name"].Str(),W(path["name"].Str()));
  Button(p,L"New Path…",[]{Name(L"New Path","",[](std::string n){Exec("path.set",JsonObject().Str("name",n).Add("path",R"({"subpaths":[]})").Text());});},!docInfo.IsNull());
  Button(p,L"Save / Rename Path…",[name]{Name(L"Save Path",name=="work"?"":name,[name](std::string n){Exec("path.rename",JsonObject().Str("name",name).Str("to",n).Text());});},!name.empty()&&name!="layer");
  Button(p,L"Fill Path",[name]{Exec("path.fill",JsonObject().Str("name",name).Text());},!name.empty()&&ActiveLayer());
  Button(p,L"Stroke Path",[name]{Exec("path.stroke",JsonObject().Str("name",name).Text());},!name.empty()&&ActiveLayer());
  Button(p,L"Load Path as Selection",[name]{Exec("path.toSelection",JsonObject().Str("name",name).Text());},!name.empty());
  Button(p,L"Make Work Path",[]{Exec("select.toWorkPath");},docInfo["hasSelection"].Truthy());
  Button(p,L"Delete Path",[name]{Exec("path.delete",JsonObject().Str("name",name).Text());},!name.empty()&&name!="layer");
}

class Navigation : public Widget {
 public:
  Rect Image() const {float scale=std::min(W(rect)/std::max(1.f,float(canvas->DocW())),H(rect)/std::max(1.f,float(canvas->DocH())));return RWH(MidX(rect)-canvas->DocW()*scale/2,MidY(rect)-canvas->DocH()*scale/2,canvas->DocW()*scale,canvas->DocH()*scale);}
  void Paint() override {
    auto r=Image();Checkerboard(r,5);if(navigation)DrawBitmap(navigation.Get(),r,1,nullptr,false);if(!canvas->HasDocument())return;
    PushClip(r);std::vector<D2D_POINT_2F> frame;
    for(auto p:{D2D1::Point2F(canvas->free.left,canvas->free.top),D2D1::Point2F(canvas->free.right,canvas->free.top),D2D1::Point2F(canvas->free.right,canvas->free.bottom),D2D1::Point2F(canvas->free.left,canvas->free.bottom)}){auto d=canvas->ToDoc(p.x,p.y);frame.push_back({r.left+d.x/canvas->DocW()*W(r),r.top+d.y/canvas->DocH()*H(r)});}
    if(auto g=Polyline(frame,true)){Dc()->DrawGeometry(g.Get(),Ink(Rgba(0,0,0,1)),3);Dc()->DrawGeometry(g.Get(),Ink(P.accent),1.5f);}PopClip();
  }
  void OnDown(const PointerEvent& e) override{OnDrag(e);}
  void OnDrag(const PointerEvent& e) override{if(!canvas->HasDocument())return;auto r=Image();auto point=canvas->ToScreen(std::clamp((e.x-r.left)/std::max(1.f,W(r)),0.f,1.f)*canvas->DocW(),std::clamp((e.y-r.top)/std::max(1.f,H(r)),0.f,1.f)*canvas->DocH());canvas->PanBy(MidX(canvas->free)-point.x,MidY(canvas->free)-point.y);ui::RequestFrame();}
};
class Histogram : public Widget {
 public:
  void Paint() override {
    Rect plot=R(rect.left+3,rect.top+4,rect.right-3,rect.bottom-34);FillRound(plot,5,P.tintFallback);uint64_t peak=1;
    auto channels=histogram["channels"];int count=int(channels.size());auto show=[&](int i){return histChannel==0?i<count-1:histChannel==1?i==count-1:i==histChannel-2;};
    for(int i=0;i<count;i++)if(show(i))for(auto& v:channels[i].a)peak=std::max(peak,uint64_t(v.Int()));
    D2D1_COLOR_F colors[]={Rgba(1,.3f,.3f,.65f),Rgba(.3f,1,.4f,.65f),Rgba(.3f,.5f,1,.65f),P.dim,P.text};
    if(histogram["mode"].Str()=="CMYK"){colors[0]=Rgba(0,1,1,.7f);colors[1]=Rgba(1,0,1,.7f);colors[2]=Rgba(1,1,0,.7f);}
    for(int i=0;i<count;i++)if(show(i))for(int x=0;x<256;x++){float y=plot.bottom-float(channels[i][x].Num()/double(peak))*H(plot);Line(plot.left+W(plot)*x/255,plot.bottom,plot.left+W(plot)*x/255,y,colors[std::min(i,4)],std::max(1.f,W(plot)/256));}
    Text(i18n::Tr(L"Pixels")+L": "+W(JsonNum(histogram["samples"].Num())),R(rect.left,rect.bottom-28,rect.right,rect.bottom),F_Small,P.dim);
  }
};
class Info : public Widget {
 public:
  void Paint() override {
    float y=rect.top;auto line=[&](std::wstring text){Text(text,RWH(rect.left,y,W(rect),24),F_Small,P.text);y+=27;};
    if(cursorInfo.IsNull()){line(L"X: —   Y: —");line(L"RGB: —");}
    else {line(L"X: "+W(JsonNum(cursorInfo["x"].Num()))+L"   Y: "+W(JsonNum(cursorInfo["y"].Num()))+L" px");
    auto c=cursorInfo["rgba"];line(L"RGB: "+W(JsonNum(std::round(c[0].Num()*255)))+L", "+W(JsonNum(std::round(c[1].Num()*255)))+L", "+W(JsonNum(std::round(c[2].Num()*255))));}
    auto s=docInfo["selectionBounds"];line(i18n::Tr(L"Selection")+L": "+W(JsonNum(s[2].Num()))+L" × "+W(JsonNum(s[3].Num()))+L" px");
    for(auto& sampler:samplerInfo["samplers"].a)line(L"#"+W(JsonNum(sampler["number"].Num()))+L"  "+W(JsonText(sampler["position"]))+L"  RGB "+W(JsonText(sampler["rgb"])));
  }
};
void ReadAnalysis() {
  if(analysisLoading||docInfo.IsNull())return;
  auto doc=Doc(),rev=uint64_t(docInfo["revision"].Int());
  bool wantHist=panels["histogram"]->visible,wantNav=panels["navigator"]->visible;
  if(!wantHist&&!wantNav)return;
  if(doc==analysisDocument&&rev==analysisRevision && (!wantHist||!histogram.IsNull())&&(!wantNav||navigation))return;
  analysisLoading=true;auto snap=std::make_shared<Snapshot>(pc_snapshot(engine));auto width=canvas->DocW(),height=canvas->DocH();HWND window=hwnd;
  analysisThread=std::jthread([snap,doc,rev,wantHist,wantNav,width,height,window]{
    auto result=std::make_shared<AnalysisResult>();result->doc=doc;result->revision=rev;result->histogram=wantHist;result->navigator=wantNav;
    if(wantHist)result->values=Json::Parse(TakeString(pc_histogram(snap->p)));
    if(wantNav){result->pixels.resize(280*200*4);pc_render_view(snap->p,0,0,width,height,280,200,result->pixels.data(),result->pixels.size(),280*4,&result->size[0],&result->size[1]);}
    {std::lock_guard<std::mutex> lock(analysisMutex);analysisResult=std::move(result);}PostMessage(window,WM_WORKSPACE_PANELS,0,0);
  });
}
void TakeAnalysis() {
  std::shared_ptr<AnalysisResult> result;{std::lock_guard<std::mutex> lock(analysisMutex);result=std::exchange(analysisResult,{});}if(!result)return;
  analysisLoading=false;if(result->doc!=Doc()||result->revision!=uint64_t(docInfo["revision"].Int()))return;
  analysisDocument=result->doc;analysisRevision=result->revision;
  if(result->histogram){histogram=std::move(result->values);auto* choice=static_cast<SelectList*>(panels["histogram"]->body->children[0].get());choice->options={i18n::Tr(W(histogram["mode"].Str())),i18n::Tr(L"Luminosity")};for(auto& name:histogram["channelNames"].a)choice->options.push_back(i18n::Tr(W(name.Str())));histChannel=std::clamp(histChannel,0,int(choice->options.size())-1);choice->index=histChannel;}
  if(result->navigator&&result->size[0]&&result->size[1])navigation=MakeBitmap(result->size[0],result->size[1],result->pixels.data(),280*4);ui::RequestFrame();
}
class SnapshotRow : public TextButton {
 public:
  uint64_t id;
  SnapshotRow(Json snapshot):TextButton(W(snapshot["name"].Str()),Secondary,nullptr),id(uint64_t(snapshot["id"].Int())){onClick=[this]{Exec("history.selectSnapshot",JsonObject().Num("snapshot",double(id)).Text());};}
  void Paint() override{TextButton::Paint();if(snapshotSources[Doc()]==id)IconAt(PhPaintBrush,rect.left+12,MidY(rect),13,P.accent);}
  void OnUp(const PointerEvent& e,bool inside) override {
    if(inside&&e.button==0&&e.x<rect.left+25){snapshotSources[Doc()]=id;ui::RequestFrame();return;}
    if(inside && e.button==1){auto snapshot=id;std::vector<MenuItem> rows{
      ActionItem(L"Rename Snapshot…",[snapshot]{Name(L"Name","",[snapshot](std::string n){Exec("history.renameSnapshot",JsonObject().Num("snapshot",double(snapshot)).Str("name",n).Text());});}),
      ActionItem(L"Delete Snapshot",[snapshot]{Exec("history.deleteSnapshot",JsonObject().Num("snapshot",double(snapshot)).Text());})};context=std::make_unique<Menu>(std::move(rows));context->Open(rect,Side::Below);return;}
    TextButton::OnUp(e,inside);
  }
};
class CameraAction : public TextButton {
 public:
  CameraAction():TextButton(L"Create Snapshot",Secondary,[]{Exec("history.newSnapshot",JsonObject().Str("name",Narrow(i18n::Tr(L"Snapshot"))+" "+std::to_string(docInfo["snapshots"].size()+1)).Text());}){enabled=!docInfo.IsNull();}
  void OnUp(const PointerEvent& e,bool inside) override{if(inside&&enabled&&e.mods.alt){Name(L"New Snapshot","",[](std::string n){Exec("history.newSnapshot",JsonObject().Str("name",n).Text());});return;}TextButton::OnUp(e,inside);}
  void Paint() override {
    TextButton::Paint();
    // Clean-room camera outline on the same 256 grid and 16-unit stroke as our tool icons.
    IconAt(L"M32,72H72L88,48H168L184,72H224V208H32Z M168,136A40,40,0,1,1,88,136A40,40,0,1,1,168,136",rect.left+18,MidY(rect),16,P.dim,true);
  }
};
}

void InitializeWorkspacePanels() {
  int index=0;for(auto [key,title]:{std::pair{"color",L"Color"},{"swatches",L"Swatches"},{"gradients",L"Gradients"},{"patterns",L"Patterns"},{"channels",L"Channels"},{"paths",L"Paths"},{"navigator",L"Navigator"},{"histogram",L"Histogram"},{"info",L"Info"}}){
    auto* p=new ToolPanel(title);panels[key]=p;int position=index++;p->home=[position](const Rect& w,D2D1_SIZE_F s){return RWH(w.left+105+float(position%3)*34,w.top+150+float(position%3)*24,s.width,s.height);};ui::AddLayer(p);
  }
  auto* p=panels["color"];colorSpectrum=p->Row<ColorSpectrum>(220);colorSpectrum->change=[](D2D1_COLOR_F c,bool commit){if(commit)Exec("tools.setColors",JsonObject().Str("foreground",Hex(c)).Text());};
  Button(p,L"Foreground…",[]{OpenColorPicker(Color(sessionInfo["foreground"]),[](D2D1_COLOR_F c){Exec("tools.setColors",JsonObject().Str("foreground",Hex(c)).Text());},panels["color"]->rect);});
  Button(p,L"Background…",[]{OpenColorPicker(Color(sessionInfo["background"]),[](D2D1_COLOR_F c){Exec("tools.setColors",JsonObject().Str("background",Hex(c)).Text());},panels["color"]->rect);});
  Button(p,L"Add to Swatches",[]{Exec("swatches.add",JsonObject().Str("name",Narrow(i18n::Tr(L"Color Swatch"))).Text());});
  panels["navigator"]->Row<Navigation>(220);auto* zoom=panels["navigator"]->Row<Slider>(50,L"Zoom",1,3200,100,1,L"%");zoom->logScale=true;zoom->onChange=[](double v){canvas->ZoomTo(float(v/100),true);};
  auto* channel=panels["histogram"]->Row<SelectList>(30,std::vector<std::wstring>{L"RGB",i18n::Tr(L"Luminosity"),i18n::Tr(L"Red"),i18n::Tr(L"Green"),i18n::Tr(L"Blue")},0);channel->onChange=[](int i){histChannel=i;ui::RequestFrame();};panels["histogram"]->Row<Histogram>(240);
  panels["info"]->Row<Info>(450);
}
void LayoutWorkspacePanels(bool snap){for(auto& [key,p]:panels){if(key=="navigator")static_cast<Slider*>(p->body->children[1].get())->Set(canvas->Zoom()*100);if(p->visible)p->Place(ui::Bounds(),snap);}}
ToolPanel* WorkspacePanel(const std::string& key){auto it=panels.find(key);return it==panels.end()?nullptr:it->second;}
std::string PanelKey(const std::string& id){std::string key=id.starts_with("window.panel.")?id.substr(13):id.starts_with("window.")?id.substr(7):"";return panels.count(key)?key:"";}
bool IsWorkspacePanelCommand(const std::string& id){static const std::set<std::string> keys{"color","swatches","gradients","patterns","channels","paths","navigator","histogram","info"};auto key=id.starts_with("window.panel.")?id.substr(13):id.starts_with("window.")?id.substr(7):"";return keys.count(key)>0;}
bool WorkspacePanelChecked(const std::string& id){auto key=PanelKey(id);return !key.empty()&&panels[key]->visible;}
bool WorkspacePanelCommand(const std::string& id){auto key=PanelKey(id);if(key.empty())return false;auto* p=panels[key];p->visible=!p->visible;if(p->visible){SyncWorkspacePanels();p->Place(ui::Bounds(),true);}ui::RequestFrame();return true;}
void SyncWorkspacePanels() {
  if(panels.empty())return;
  auto doc=Doc(),rev=uint64_t(docInfo["revision"].Int());if(doc!=cacheDocument||rev!=cacheRevision){thumbs.clear();navigation.Reset();histogram=Json{};lastX=lastY=-1;cacheDocument=doc;cacheRevision=rev;}
  std::string fg=JsonText(sessionInfo["foreground"]);if(fg!=foreground){foreground=fg;if(colorSpectrum)colorSpectrum->Set(Color(sessionInfo["foreground"]));}
  for(auto key:{"swatches","gradients","patterns"})if(panels[key]->visible)LoadAssets(key);
  if(panels["channels"]->visible)BuildChannels();
  if(panels["paths"]->visible&&!loading.count("paths")){if(docInfo.IsNull())BuildPaths(Json{});else{loading.insert("paths");Query("path.list","{}",[doc](Json data){loading.erase("paths");if(doc==Doc())BuildPaths(data);});}}
  if(panels["info"]->visible&&!docInfo.IsNull())Query("view.colorSamplers.list","{}",[doc](Json data){if(doc==Doc())samplerInfo=data;});
  if(docInfo.IsNull()){navigation.Reset();histogram=Json{};cursorInfo=Json{};samplerInfo=Json{};}else ReadAnalysis();
}
void TickWorkspacePanels() {
  if(panels.empty())return;TakeAnalysis();if(jobs::Busy())return;ReadAnalysis();
  if(!panels["info"]->visible||cursorLoading||docInfo.IsNull()||ui::NowMs()-sampleAt<80)return;
  auto p=canvas->ToDoc(pointerX,pointerY);int x=int(std::floor(p.x)),y=int(std::floor(p.y));if(x==lastX&&y==lastY)return;lastX=x;lastY=y;sampleAt=ui::NowMs();
  if(x<0||y<0||x>=int(canvas->DocW())||y>=int(canvas->DocH())){cursorInfo=Json{};return;}
  cursorLoading=true;auto doc=Doc();auto snap=std::make_shared<Snapshot>(pc_snapshot(engine));auto rgba=std::make_shared<std::array<float,4>>();
  jobs::Run([snap,rgba,x,y]{pc_sample(snap->p,x,y,rgba->data());},[doc,rgba,x,y]{cursorLoading=false;if(doc==Doc())cursorInfo=Json::Parse(JsonObject().Num("x",x).Num("y",y).Add("rgba","["+JsonNum((*rgba)[0])+","+JsonNum((*rgba)[1])+","+JsonNum((*rgba)[2])+","+JsonNum((*rgba)[3])+"]").Text());ui::RequestFrame();});
}
Json WorkspacePanelState(const std::string& key){if(key=="histogram")return histogram;if(key=="navigator")return Json::Parse(JsonObject().Bool("preview",bool(navigation)).Num("document",double(analysisDocument)).Text());if(key=="info")return Json::Parse(JsonObject().Add("cursor",JsonText(cursorInfo)).Add("samplers",JsonText(samplerInfo)).Text());return views[key];}
void ImportPanelAssets(const std::string& key,const std::wstring& path){Exec(key=="swatches"?"swatches.import":key=="gradients"?"gradient.presets.importGrd":"pattern.import",JsonObject().Str("path",Narrow(path)).Text());}
void ExportPanelAssets(const std::string& key,const std::wstring& path,const std::string& group){
  JsonObject p;p.Str("path",Narrow(path));if(!group.empty())p.Str("group",group);
  if(key=="gradients") {auto params=p.Text();auto error=std::make_shared<std::string>();jobs::Run([params,error]{auto result=TakeString(pc_export_gradients(engine,params.c_str()));if(result.empty())*error=LastError();},[error]{if(!error->empty())ui::Toast(W(*error),true);});return;}
  if(key=="patterns"&&!group.empty()){std::string ids="[";for(auto& item:views[key].a)if(PatternGroup(item["name"].Str()).first==group)ids+=(ids.size()>1?",":"")+JsonQuote(item["id"].Str());p.Add("patterns",ids+"]");}
  Exec(key=="swatches"?"swatches.export":"pattern.export",p.Text());
}
void BuildHistoryExtras(Container& list){
  if(snapshotSources[Doc()]&&!std::any_of(docInfo["snapshots"].a.begin(),docInfo["snapshots"].a.end(),[](const Json& v){return uint64_t(v["id"].Int())==snapshotSources[Doc()];}))snapshotSources.erase(Doc());
  list.Add<CameraAction>();
  list.Add<TextButton>(L"New Document from State",TextButton::Secondary,[]{Exec("history.newDocument");})->enabled=!docInfo.IsNull();
  for(auto& snapshot:docInfo["snapshots"].a)list.Add<SnapshotRow>(snapshot);
}
void PaintHistorySource(const Rect& r,int state){if(!snapshotSources[Doc()]&&historySources[Doc()]==state)IconAt(PhPaintBrush,r.left+12,MidY(r),13,P.accent);}
bool HistorySourceClick(const PointerEvent& e,const Rect& r,int state){if(e.x>r.left+25)return false;snapshotSources.erase(Doc());historySources[Doc()]=state;ui::RequestFrame();return true;}
void AddHistoryBrushSource(JsonObject& p){if(snapshotSources[Doc()])p.Num("snapshot",double(snapshotSources[Doc()]));else p.Num("state",std::clamp(historySources[Doc()],0,std::max(0,int(docInfo["history"].size())-1)));}
std::string SelectedPanelPath(){return selectedPaths[Doc()];}
bool WorkspacePanelsBusy(){return analysisLoading;}
