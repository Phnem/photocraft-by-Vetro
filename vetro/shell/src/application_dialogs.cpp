#include "application_dialogs.h"
#include <filesystem>
#include <fstream>
#include <shlobj.h>

UINT KeyCode(const std::string& key);
void UpdateShortcutCatalog(const Json& metadata);
void PrepareDocumentChange();
namespace {
std::wstring preferencesFile;
std::unique_ptr<Popover> applicationDialog;
struct KeyBinding {UINT key=0;bool ctrl=false,shift=false,alt=false;std::string command;};
std::vector<KeyBinding> customKeys,blockedKeys;
KeyBinding ParseKey(std::string text,std::string command={}) {
  KeyBinding b;b.command=std::move(command);
  for(;;){auto at=text.find('+');if(at==std::string::npos||at==0||at+1==text.size())break;auto mod=text.substr(0,at);if(mod=="Ctrl"||mod=="Cmd"||mod=="Control")b.ctrl=true;else if(mod=="Shift")b.shift=true;else if(mod=="Alt"||mod=="Opt")b.alt=true;else break;text=text.substr(at+1);}
  b.key=KeyCode(text);return b;
}
bool Match(const KeyBinding& b,UINT key,Mods mods){return b.key&&b.key==key&&b.ctrl==mods.ctrl&&b.shift==mods.shift&&b.alt==mods.alt;}
std::wstring ReadTextFile(const std::wstring& path,size_t limit){std::ifstream f(std::filesystem::path(path),std::ios::binary);if(!f)return {};f.seekg(0,std::ios::end);auto n=f.tellg();if(n<0||uint64_t(n)>limit)return {};f.seekg(0);std::string s(size_t(n),'\0');f.read(s.data(),n);return f?W(s):L"";}
bool SavePreferencesNow(){
  if(preferencesFile.empty())return true;
  auto all=Json::Parse(TakeString(pc_preferences_info(engine)));auto text=JsonText(all["saved"]);
  auto path=std::filesystem::path(preferencesFile);std::error_code ec;std::filesystem::create_directories(path.parent_path(),ec);
  auto temp=path;temp+=L".tmp-"+std::to_wstring(GetCurrentProcessId());std::ofstream file(temp,std::ios::binary|std::ios::trunc);file.write(text.data(),std::streamsize(text.size()));file.close();
  if(!file||!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(temp.c_str());return false;}return true;
}
D2D1_COLOR_F PreferenceColor(const std::string& text){unsigned rgb=0;if(text.size()==7&&text[0]=='#'&&sscanf_s(text.c_str()+1,"%x",&rgb)==1)return Rgba(float((rgb>>16)&255)/255,float((rgb>>8)&255)/255,float(rgb&255)/255,1);return P.accent;}
std::vector<std::wstring> Translated(std::initializer_list<const wchar_t*> names){std::vector<std::wstring> out;for(auto* name:names)out.push_back(i18n::Tr(name));return out;}
class Sheet : public Popover {
 public:
  std::wstring title;
  ScrollArea* body;
  TextButton *cancel,*apply;
  std::vector<float> heights;
  explicit Sheet(std::wstring t,std::wstring button=L"OK"):title(std::move(t)){
    modal=true;body=Add<ScrollArea>();cancel=Add<TextButton>(L"Cancel",TextButton::Secondary,[this]{Close();});apply=Add<TextButton>(button,TextButton::Primary,[this]{Apply();});
  }
  virtual void Load(){}
  virtual void Apply()=0;
  virtual Json State()const{return {};}
  D2D1_SIZE_F Natural() override{return {std::max(260.f,std::min(680.f,W(ui::Bounds())-24)),std::max(180.f,std::min(740.f,H(ui::Bounds())-24))};}
  template<class T,class...A>T* Row(float height,A&&...args){heights.push_back(height);return body->Add<T>(std::forward<A>(args)...);}
  void Reset(){body->Clear();heights.clear();}
  void Arrange() override {
    body->rect=R(target.left+22,target.top+62,target.right-22,target.bottom-76);float y=body->rect.top-body->Scroll();
    for(size_t i=0;i<body->children.size();i++){float h=i<heights.size()?heights[i]:32;body->children[i]->rect=R(body->rect.left,y,body->rect.right-7,y+h);if(auto* list=dynamic_cast<SelectList*>(body->children[i].get());list&&list->labelWidth>0)list->labelWidth=std::min(260.f,W(list->rect)*.48f);if(auto* color=dynamic_cast<ColorField*>(body->children[i].get()))color->labelWidth=std::min(260.f,W(color->rect)*.48f);y+=h+8;}
    body->contentHeight=y+body->Scroll()-body->rect.top;body->Clamp();
    cancel->rect=RWH(target.right-222,target.bottom-56,96,36);apply->rect=RWH(target.right-118,target.bottom-56,96,36);
  }
  void PaintContent() override{Text(title,R(target.left+22,target.top+15,target.right-48,target.top+48),F_Title,P.text);Line(target.left+22,target.bottom-68,target.right-22,target.bottom-68,P.sep);}
  bool OnKey(UINT vk,Mods mods) override{if(vk==VK_RETURN){Apply();return true;}return Popover::OnKey(vk,mods);}
};
class DocumentPreview : public Widget {
 public:
  double width=1,height=1;
  Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
  UINT side=384;
  void Paint() override{
    FillRound(rect,12,P.sunk);float scale=std::min((W(rect)-24)/float(width),(H(rect)-24)/float(height));
    auto destination=RWH(MidX(rect)-float(width)*scale/2,MidY(rect)-float(height)*scale/2,float(width)*scale,float(height)*scale);
    Checkerboard(destination,6);
    if(bitmap){float k=std::min(1.,double(side)/std::max(width,height));float w=float(std::round(width*k)),h=float(std::round(height*k));auto source=RWH(std::floor((side-w)/2),std::floor((side-h)/2),w,h);DrawBitmap(bitmap.Get(),destination,1,&source,false);}
    else {FillRect(destination,Rgba(1,1,1,.85f));Text(W(JsonNum(width))+L" × "+W(JsonNum(height))+L" px",destination,F_Label,Rgba(.1f,.1f,.1f,1),Align::Center);}
    StrokeRound(destination,0,P.faint,1);
  }
  bool Interactive()const override{return false;}
};
class NewDocumentSheet : public Sheet {
 public:
  TextField *name,*presetName;
  SelectList *category,*presets,*mode,*depth,*background;
  NumberField *width,*height,*resolution;
  DocumentPreview* preview;
  Json custom;
  std::vector<Json> settings;
  NewDocumentSheet():Sheet(L"New Document",L"Create"){
    name=Row<TextField>(32,i18n::Tr(L"Name"),L"");
    category=Row<SelectList>(32,Translated({L"Photo",L"Print",L"Web",L"Mobile",L"Saved Presets"}),0);category->label=i18n::Tr(L"Category");category->labelWidth=150;
    presets=Row<SelectList>(32,std::vector<std::wstring>{},0);presets->label=i18n::Tr(L"Preset");presets->labelWidth=150;
    preview=Row<DocumentPreview>(140);
    width=Row<NumberField>(30,L"Width",1800,1,300000,0,L" px");height=Row<NumberField>(30,L"Height",1200,1,300000,0,L" px");resolution=Row<NumberField>(30,L"Resolution",300,1,30000,1,L" ppi");
    mode=Row<SelectList>(32,Translated({L"RGB",L"Grayscale",L"CMYK",L"Lab"}),0);mode->label=i18n::Tr(L"Color Mode");mode->labelWidth=150;
    depth=Row<SelectList>(32,std::vector<std::wstring>{L"8",L"16",L"32"},0);depth->label=i18n::Tr(L"Bit Depth");depth->labelWidth=150;
    background=Row<SelectList>(32,Translated({L"White",L"Black",L"Transparent"}),0);background->label=i18n::Tr(L"Background");background->labelWidth=150;
    presetName=Row<TextField>(32,i18n::Tr(L"Preset Name"),L"");
    Row<TextButton>(32,L"Save Preset",TextButton::Secondary,[this]{SavePreset();});
    Row<TextButton>(32,L"Delete Preset",TextButton::Secondary,[this]{DeletePreset();});
    category->onChange=[this](int){FillPresets();};presets->onChange=[this](int i){UsePreset(i);};
    width->onCommit=[this](double v){width->Set(v);UpdatePreview();};height->onCommit=[this](double v){height->Set(v);UpdatePreview();};FillPresets();
  }
  void Load() override{auto token=std::weak_ptr<int>(lifetime);Exec("document.presets.list","{}",[this,token](bool ok,const std::string& result){if(token.expired()||!open||!ok)return;custom=Json::Parse(result)["presets"];if(category->index==4)FillPresets();});}
  void UpdatePreview(){preview->width=width->value;preview->height=height->value;ui::RequestFrame();}
  std::string Params(bool saved=false)const{
    static const char* modes[]={"rgb","gray","cmyk","lab"};static const char* backgrounds[]={"white","black","transparent"};static const int depths[]={8,16,32};
    JsonObject p;p.Num("width",width->value).Num("height",height->value).Num("resolution",resolution->value).Str("mode",modes[mode->index]).Num("depth",depths[depth->index]).Str("background",backgrounds[background->index]);if(saved)p.Str("unit","px").Str("resolutionUnit","in");else if(!name->value.empty())p.Str("name",Narrow(name->value));return p.Text();
  }
  void FillPresets(){
    settings.clear();presets->options.clear();
    auto add=[this](const wchar_t* label,int w,int h,int ppi){presets->options.push_back(i18n::Tr(label));settings.push_back(Json::Parse(JsonObject().Num("width",w).Num("height",h).Num("resolution",ppi).Str("mode","rgb").Num("depth",8).Str("background","white").Text()));};
    switch(category->index){case 0:add(L"Photo 4 × 6",1800,1200,300);add(L"Photo 5 × 7",2100,1500,300);break;case 1:add(L"A4",2480,3508,300);add(L"A3",3508,4961,300);break;case 2:add(L"Full HD",1920,1080,72);add(L"HD",1280,720,72);break;case 3:add(L"Mobile Portrait",1080,1920,72);add(L"Mobile High Resolution",1440,3200,72);break;default:for(auto& item:custom.a){presets->options.push_back(W(item["name"].Str()));settings.push_back(item["settings"]);}break;}
    presets->index=0;presets->enabled=!settings.empty();if(!settings.empty())UsePreset(0);
  }
  void UsePreset(int i){if(i<0||size_t(i)>=settings.size())return;auto value=settings[size_t(i)];width->Set(value["width"].Num());height->Set(value["height"].Num());resolution->Set(value["resolution"].Num(72));auto m=value["mode"].Str();mode->index=m=="gray"?1:m=="cmyk"?2:m=="lab"?3:0;depth->index=value["depth"].Int(8)==16?1:value["depth"].Int(8)==32?2:0;auto bg=value["background"].Str();background->index=bg=="black"?1:bg=="transparent"?2:0;UpdatePreview();}
  void SavePreset(){if(presetName->value.empty()){ui::Toast(L"Enter a preset name",true);return;}auto token=std::weak_ptr<int>(lifetime);Exec("document.presets.save",JsonObject().Str("name",Narrow(presetName->value)).Add("settings",Params(true)).Text(),[this,token](bool ok,const std::string&){if(!ok)return;PersistApplicationPreferences();if(!token.expired()&&open)Load();});}
  void DeletePreset(){if(category->index!=4||presets->options.empty())return;auto token=std::weak_ptr<int>(lifetime);Exec("document.presets.delete",JsonObject().Str("name",Narrow(presets->options[size_t(presets->index)])).Text(),[this,token](bool ok,const std::string&){if(ok){PersistApplicationPreferences();if(!token.expired()&&open)Load();}});}
  void Apply() override{auto recent=Params(true);auto params=Params();Close();PrepareDocumentChange();Exec("file.new",params,[recent](bool ok,const std::string&){if(ok)Exec("document.recent.add",JsonObject().Add("settings",recent).Text(),[](bool good,const std::string&){if(good)PersistApplicationPreferences();});});}
  Json State()const override{return Json::Parse(JsonObject().Str("kind","new").Add("params",Params()).Num("category",category->index).Num("presets",double(settings.size())).Text());}
};
class ExportSheet : public Sheet {
 public:
  bool onlyLayer=false,dirty=true,loading=false;
  double aspect=1,changedAt=0;
  DocumentPreview* preview;
  SelectList* format;
  NumberField *width,*height,*quality;
  Toggle *linked,*transparency;
  ExportSheet(bool layer):Sheet(L"Export As",L"Export"),onlyLayer(layer){
    auto* selected=ActiveLayer();auto bounds=selected?(*selected)["bounds"]:Json{};
    double w=layer?bounds[2].Num(1):docInfo["width"].Num(1),h=layer?bounds[3].Num(1):docInfo["height"].Num(1);w=std::max(1.,w);h=std::max(1.,h);aspect=w/h;
    preview=Row<DocumentPreview>(260);preview->width=w;preview->height=h;
    format=Row<SelectList>(32,std::vector<std::wstring>{L"PNG",L"JPEG",L"GIF"},0);format->label=i18n::Tr(L"Format");format->labelWidth=150;
    width=Row<NumberField>(30,L"Width",w,1,300000,0,L" px");height=Row<NumberField>(30,L"Height",h,1,300000,0,L" px");
    linked=Row<Toggle>(28,L"Keep Aspect Ratio",true);quality=Row<NumberField>(30,L"Quality",85,0,100,0,L"%");quality->enabled=false;
    transparency=Row<Toggle>(28,L"Transparency",true);
    format->onChange=[this](int){quality->enabled=format->index==1;transparency->enabled=format->index!=1;Changed();};
    width->onCommit=[this](double v){width->Set(v);if(linked->on)height->Set(std::round(v/aspect));Changed();};
    height->onCommit=[this](double v){height->Set(v);if(linked->on)width->Set(std::round(v*aspect));Changed();};
    quality->onCommit=[this](double v){quality->Set(v);Changed();};linked->onChange=[this](bool on){if(on)aspect=width->value/height->value;};transparency->onChange=[this](bool){Changed();};
  }
  void Changed(){dirty=true;changedAt=ui::NowMs();preview->width=width->value;preview->height=height->value;ui::RequestFrame();}
  std::string Params(const std::wstring& path=L"")const{const char* formats[]={"png24","jpeg","gif"};JsonObject p;p.Str("format",formats[std::clamp(format->index,0,2)]).Num("width",width->value).Num("height",height->value).Num("quality",quality->value).Bool("transparency",transparency->on).Bool("onlyLayer",onlyLayer).Bool("convertToSrgb",true);if(!path.empty())p.Str("path",Narrow(path));return p.Text();}
  bool Animate(float dt)override {
    bool moving=Sheet::Animate(dt);if(!open||!dirty||loading)return moving;
    if(jobs::Busy()||ui::NowMs()-changedAt<180)return true;
    dirty=false;loading=true;auto token=std::weak_ptr<int>(lifetime);auto params=Json::Parse(Params());params.o.emplace_back("kind",Json::Parse(JsonQuote("exportPreview")));
    auto text=JsonText(params);auto pixels=std::make_shared<std::vector<uint8_t>>(384*384*4);auto ok=std::make_shared<bool>(false);
    jobs::Run([text,pixels,ok]{*ok=pc_panel_thumb(engine,text.c_str(),384,pixels->data(),pixels->size(),384*4)==1;},[this,token,pixels,ok]{if(token.expired())return;loading=false;if(*ok&&open)preview->bitmap=MakeBitmap(384,384,pixels->data(),384*4);ui::RequestFrame();});return true;
  }
  void Apply()override{
    const wchar_t* extensions[]={L".png",L".jpg",L".gif"};auto path=FileDialog(true,W(docInfo["name"].Str())+extensions[format->index]);if(path.empty())return;
    auto ext=std::filesystem::path(path).extension().wstring();std::transform(ext.begin(),ext.end(),ext.begin(),towlower);
    if(ext==L".png")format->index=0;else if(ext==L".jpg"||ext==L".jpeg")format->index=1;else if(ext==L".gif")format->index=2;else {ui::Toast(L"Choose PNG, JPEG or GIF",true);return;}
    auto params=Params(path);Close();Exec(onlyLayer?"vetro.exportLayer":"file.export.saveForWebLegacy",params,[](bool ok,const std::string&){if(ok){ui::Toast(L"Export complete");PersistApplicationPreferences();}});
  }
  Json State()const override{return Json::Parse(JsonObject().Str("kind","export").Add("params",Params()).Bool("preview",bool(preview->bitmap)).Text());}
};
class PreferencesSheet : public Sheet {
 public:
  Json metadata;
  std::string section;
  int sectionIndex=0;
  std::map<std::string,Json> changes;
  explicit PreferencesSheet(std::string initial):Sheet(L"Preferences"),section(std::move(initial)){}
  void Load()override {
    auto result=std::make_shared<std::string>();auto token=std::weak_ptr<int>(lifetime);
    jobs::Run([result]{*result=TakeString(pc_preferences_info(engine));},[this,token,result]{if(token.expired()||!open)return;metadata=Json::Parse(*result);for(size_t i=0;i<metadata["sections"].size();i++)if(metadata["sections"][i]["id"].Str()==section)sectionIndex=int(i);Build();});
  }
  void Build(){
    Reset();body->offset.Reset(0);std::vector<std::wstring> sections;for(auto& s:metadata["sections"].a)sections.push_back(i18n::Tr(W(s["title"].Str())));
    auto* choice=Row<SelectList>(34,sections,sectionIndex);choice->onChange=[this](int index){sectionIndex=index;Build();};
    if(size_t(sectionIndex)>=metadata["sections"].size())return;auto selected=metadata["sections"][size_t(sectionIndex)];section=selected["id"].Str();
    for(auto field:selected["fields"].a){
      auto path=field["path"].Str();auto value=changes.count(path)?changes[path]:field["value"];auto label=i18n::Tr(HumanKey(field["key"].Str()));
      auto save=[this,path](Json v){changes[path]=std::move(v);};
      if(field["choices"].size()){
        std::vector<std::wstring> names;int index=0;for(size_t i=0;i<field["choices"].size();i++){auto name=field["choices"][i].Str();names.push_back(i18n::Tr(HumanKey(name)));if(name==value.Str())index=int(i);}
        auto* list=Row<SelectList>(32,names,index);list->label=label;list->labelWidth=260;auto options=field["choices"];list->onChange=[save,options](int i){save(options[size_t(i)]);};
      }else if(value.kind==Json::Bool){auto* toggle=Row<Toggle>(28,label,value.Truthy());toggle->onChange=[save](bool on){save(Json::Parse(on?"true":"false"));};}
      else if(value.kind==Json::Number){auto* number=Row<NumberField>(30,label,value.Num(),field["min"].Num(),field["max"].Num(),std::floor(value.Num())==value.Num()?0:2);number->onCommit=[save](double v){save(Json::Parse(JsonNum(v)));};}
      else if(field["color"].Truthy()){auto* color=Row<ColorField>(32,label,PreferenceColor(value.Str()),true);color->labelWidth=260;color->onChange=[save](D2D1_COLOR_F c){char value[16];snprintf(value,sizeof value,"#%02x%02x%02x",int(c.r*255+.5f),int(c.g*255+.5f),int(c.b*255+.5f));save(Json::Parse(JsonQuote(value)));};}
      else {auto* text=Row<TextField>(32,label,W(value.Str()));text->onChange=[save](const std::wstring& v){save(Json::Parse(JsonQuote(Narrow(v))));};}
    }
    if(selected["fields"].size()==0)Row<Label>(32,L"No editable options in this section",F_Label);
    Arrange();
  }
  void Apply()override{
    JsonObject values;for(auto& [path,value]:changes)values.Add(path.c_str(),JsonText(value));auto token=std::weak_ptr<int>(lifetime);
    if(changes.empty()){Close();return;}
    Exec("prefs.set",JsonObject().Add("values",values.Text()).Text(),[this,token](bool ok,const std::string&){if(!ok)return;if(!token.expired())Close();PersistApplicationPreferences();Exec("edit.keyboardShortcuts",R"({"list":true})",[](bool good,const std::string& result){if(good)ApplyShortcutMetadata(Json::Parse(result));});});
  }
  Json State()const override{return Json::Parse(JsonObject().Str("kind","preferences").Str("section",section).Num("sections",double(metadata["sections"].size())).Num("changes",double(changes.size())).Text());}
};
std::string KeyName(UINT key){
  if((key>='A'&&key<='Z')||(key>='0'&&key<='9'))return std::string(1,char(key));if(key>=VK_F1&&key<=VK_F24)return "F"+std::to_string(key-VK_F1+1);
  switch(key){case VK_DELETE:return "Delete";case VK_BACK:return "Backspace";case VK_RETURN:return "Enter";case VK_TAB:return "Tab";case VK_SPACE:return "Space";case VK_LEFT:return "Left";case VK_RIGHT:return "Right";case VK_UP:return "Up";case VK_DOWN:return "Down";case VK_OEM_PLUS:return "+";case VK_OEM_MINUS:return "-";case VK_OEM_4:return "[";case VK_OEM_6:return "]";case VK_OEM_1:return ";";case VK_OEM_7:return "'";default:return {};}
}
class ShortcutCapture : public Popover {
 public:
  std::function<void(std::string)> accept;
  explicit ShortcutCapture(std::function<void(std::string)> done):accept(std::move(done)){modal=true;}
  D2D1_SIZE_F Natural()override{return {380,116};}
  void PaintContent()override{Text(L"Press a shortcut",R(target.left+20,target.top+16,target.right-42,target.top+48),F_Title,P.text);Text(L"Esc cancels",R(target.left+20,target.top+60,target.right-20,target.bottom-16),F_Label,P.dim);}
  bool OnKey(UINT key,Mods mods)override{if(key==VK_ESCAPE){Close();return true;}auto name=KeyName(key);if(name.empty())return true;std::string value;if(mods.ctrl)value+="Ctrl+";if(mods.alt)value+="Alt+";if(mods.shift)value+="Shift+";value+=name;Close();accept(value);return true;}
};
std::unique_ptr<ShortcutCapture> shortcutCapture;
class ShortcutsSheet : public Sheet {
 public:
  Json metadata,overrides;
  TextField *search,*key;
  SelectList* commands;
  Label* summary;
  std::vector<std::string> ids;
  std::string selected;
  bool loading=false;
  ShortcutsSheet():Sheet(L"Keyboard Shortcuts"){
    search=Row<TextField>(32,i18n::Tr(L"Search"),L"");commands=Row<SelectList>(34,std::vector<std::wstring>{},0);
    key=Row<TextField>(32,i18n::Tr(L"Shortcut"),L"");
    Row<TextButton>(32,L"Record Shortcut",TextButton::Secondary,[this]{auto token=std::weak_ptr<int>(lifetime);shortcutCapture=std::make_unique<ShortcutCapture>([this,token](std::string value){if(!token.expired()&&open)SetKey(value);});shortcutCapture->Open(target,Side::Center);});
    Row<TextButton>(32,L"Clear Shortcut",TextButton::Secondary,[this]{SetKey("");});
    Row<TextButton>(32,L"Restore Default",TextButton::Secondary,[this]{if(!selected.empty())Preview(JsonObject().Add("reset","["+JsonQuote(selected)+"]").Text());});
    Row<TextButton>(32,L"Import Photoshop Shortcuts…",TextButton::Secondary,[]{auto path=FileDialog(false,L".kys");if(!path.empty())ImportShortcutFile(path);});
    summary=Row<Label>(60,L"",F_Label);
    search->onChange=[this](const std::wstring&){Filter();};commands->onChange=[this](int i){if(i>=0&&size_t(i)<ids.size()){selected=ids[size_t(i)];ShowKey();}};key->onChange=[this](const std::wstring& value){SetKey(Narrow(value));};
  }
  void Load()override{loading=true;auto token=std::weak_ptr<int>(lifetime);Exec("edit.keyboardShortcuts",R"({"list":true})",[this,token](bool ok,const std::string& result){if(token.expired()||!open)return;loading=false;if(ok){metadata=Json::Parse(result);overrides=metadata["overrides"];Filter();}});}
  void Filter(){
    commands->options.clear();ids.clear();auto term=search->value;std::transform(term.begin(),term.end(),term.begin(),towlower);
    for(auto& item:metadata["commands"].a){if(item["hold"].Truthy()||item["menu"].size()==0)continue;auto id=item["id"].Str();std::wstring label;
      for(auto& part:item["menu"].a){if(!label.empty())label+=L" › ";label+=i18n::Tr(W(part.Str()));}
      label+=L" › "+i18n::TrId(id,W(item["label"].Str()));auto lower=label;std::transform(lower.begin(),lower.end(),lower.begin(),towlower);
      if(!term.empty()&&lower.find(term)==std::wstring::npos)continue;ids.push_back(id);commands->options.push_back(label);
    }
    auto where=std::find(ids.begin(),ids.end(),selected);commands->index=where==ids.end()?0:int(where-ids.begin());selected=ids.empty()?"":ids[size_t(commands->index)];commands->enabled=!ids.empty();key->enabled=!ids.empty();ShowKey();
  }
  void ShowKey(){key->value=L"";for(auto& item:metadata["commands"].a)if(item["id"].Str()==selected){auto value=item["shortcut"].Str();if(value.starts_with("Cmd+"))value.replace(0,3,"Ctrl");key->value=W(value);break;}}
  void Preview(const std::string& change){
    loading=true;apply->enabled=false;auto token=std::weak_ptr<int>(lifetime);auto params=JsonObject().Add("overrides",JsonText(overrides)).Add("change",change).Text();
    Exec("vetro.shortcuts.preview",params,[this,token](bool ok,const std::string& result){if(token.expired()||!open)return;loading=false;apply->enabled=true;if(!ok)return;metadata=Json::Parse(result);overrides=metadata["overrides"];Filter();if(!metadata["import"].IsNull())summary->text=i18n::Tr(L"Imported shortcuts")+L": "+std::to_wstring(metadata["import"]["imported"].Int())+L"  ·  "+i18n::Tr(L"Unrecognized")+L": "+std::to_wstring(metadata["import"]["unknown"].size());});
  }
  void SetKey(const std::string& value){if(selected.empty())return;Preview(JsonObject().Add("set","{"+JsonQuote(selected)+":"+JsonQuote(value)+"}").Text());}
  void Import(const std::wstring& path){auto xml=ReadTextFile(path,4*1024*1024);if(xml.empty()){ui::Toast(L"Could not read shortcut file",true);return;}Preview(JsonObject().Str("importKys",Narrow(xml)).Text());}
  void Apply()override{if(loading)return;auto token=std::weak_ptr<int>(lifetime);Exec("vetro.shortcuts.apply",JsonObject().Add("overrides",JsonText(overrides)).Text(),[this,token](bool ok,const std::string& result){if(!ok)return;ApplyShortcutMetadata(Json::Parse(result));PersistApplicationPreferences();if(!token.expired())Close();});}
  Json State()const override{return Json::Parse(JsonObject().Str("kind","shortcuts").Str("selected",selected).Num("commands",double(ids.size())).Add("overrides",JsonText(overrides)).Bool("loading",loading).Text());}
};
}

void ApplyShortcutMetadata(const Json& metadata){
  customKeys.clear();blockedKeys.clear();
  for(auto& item:metadata["commands"].a){auto id=item["id"].Str();if(item["hold"].Truthy()||metadata["overrides"][id.c_str()].IsNull())continue;auto value=item["shortcut"].Str();if(!value.empty())customKeys.push_back(ParseKey(value,id));auto original=item["default"].Str();if(!original.empty()&&original!=value)blockedKeys.push_back(ParseKey(original));}
  UpdateShortcutCatalog(metadata);
}
bool CustomShortcutKey(UINT vk,Mods mods){for(auto& binding:customKeys)if(Match(binding,vk,mods)){RunCommand(binding.command,CommandLabel(binding.command));return true;}for(auto& binding:blockedKeys)if(Match(binding,vk,mods))return true;return false;}
void InitializeApplicationPreferences(const std::wstring& file){
  preferencesFile=file;
  if(preferencesFile.empty()){PWSTR root=nullptr;if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&root))){preferencesFile=(std::filesystem::path(root)/L"PhotoCraft by Vetro"/L"preferences.json").wstring();CoTaskMemFree(root);}}
  auto saved=ReadTextFile(preferencesFile,16*1024*1024);if(!saved.empty()&&pc_preferences_restore(engine,Narrow(saved).c_str())!=1)ui::Toast(W(LastError()),true);
  Exec("edit.keyboardShortcuts",R"({"list":true})",[](bool ok,const std::string& result){if(ok)ApplyShortcutMetadata(Json::Parse(result));});
}
void PersistApplicationPreferences(){if(preferencesFile.empty())return;auto ok=std::make_shared<bool>(false);jobs::Run([ok]{*ok=SavePreferencesNow();},[ok]{if(!*ok)ui::Toast(L"Could not save preferences",true);});}
void ImportShortcutFile(const std::wstring& path){
  if(auto* dialog=dynamic_cast<ShortcutsSheet*>(applicationDialog.get());dialog&&dialog->open){dialog->Import(path);return;}
  auto xml=ReadTextFile(path,4*1024*1024);if(xml.empty()){ui::Toast(L"Could not read shortcut file",true);return;}
  Exec("vetro.shortcuts.apply",JsonObject().Add("change",JsonObject().Str("importKys",Narrow(xml)).Text()).Text(),[](bool ok,const std::string& result){if(ok){ApplyShortcutMetadata(Json::Parse(result));PersistApplicationPreferences();}});
}
bool ApplicationDialogCommand(const std::string& id){
  std::unique_ptr<Sheet> dialog;
  if(id=="file.new")dialog=std::make_unique<NewDocumentSheet>();
  else if(id=="file.export.exportAs"||id=="layer.exportAs")dialog=std::make_unique<ExportSheet>(id=="layer.exportAs");
  else if(id.starts_with("edit.preferences."))dialog=std::make_unique<PreferencesSheet>(id.substr(17));
  else if(id=="edit.keyboardShortcuts")dialog=std::make_unique<ShortcutsSheet>();
  else return false;
  if(applicationDialog&&applicationDialog->open)applicationDialog->OnKey(VK_ESCAPE,{});
  dialog->Open(ui::Bounds(),Side::Center);dialog->Load();applicationDialog=std::move(dialog);return true;
}
Popover* ApplicationDialog(){return applicationDialog.get();}
Json ApplicationDialogState(){auto* sheet=dynamic_cast<Sheet*>(applicationDialog.get());return sheet?sheet->State():Json{};}
