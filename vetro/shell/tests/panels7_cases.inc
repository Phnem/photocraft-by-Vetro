namespace {
const std::vector<std::string> panelKeys{"color","swatches","gradients","patterns","channels","paths","navigator","histogram","info"};
void PanelPump() {
  double end=ui::NowMs()+15000;
  while((jobs::Busy()||WorkspacePanelsBusy())&&ui::NowMs()<end){jobs::Pump();TickWorkspacePanels();Sleep(1);}
  Check(!jobs::Busy()&&!WorkspacePanelsBusy(),"panel query timed out");Layout(true);ui::Animate(1);
}
void OnlyPanel(const std::string& key) {
  for(auto& k:panelKeys)WorkspacePanel(k)->visible=false;
  RunCommand("window."+key,L"");PanelPump();
  Check(WorkspacePanel(key)->visible && WorkspacePanelChecked("window.panel."+key),"panel command or checked state missing");
}
TextButton* PanelButton(const std::string& key,const std::wstring& label) {
  for(auto& child:WorkspacePanel(key)->body->children)if(auto* b=dynamic_cast<TextButton*>(child.get());b&&b->label==label)return b;
  throw std::runtime_error("panel button missing: "+Narrow(label));
}
void PanelClick(const std::string& key,const std::wstring& label){auto* b=PanelButton(key,label);Check(b->enabled,"panel action unexpectedly disabled");b->onClick();Drain();PanelPump();}
void PanelName(const std::wstring& name){Check(typePopover&&typePopover->open,"name dialog missing");typePopover->text=name;Key(VK_RETURN);ui::ClosePopovers();ui::Animate(1);PanelPump();}
Widget* FirstAsset(const std::string& key){for(auto& child:WorkspacePanel(key)->body->children)if(!dynamic_cast<TextField*>(child.get())&&!dynamic_cast<TextButton*>(child.get())&&!dynamic_cast<Label*>(child.get()))return child.get();throw std::runtime_error("asset rows missing");}
void AssetPanels(const std::filesystem::path& root) {
  NewDocument();Rectangle();
  for(auto& key:panelKeys){Check(ShellCommand("window.panel."+key),"native panel remains unavailable in menus");}
  OnlyPanel("color");auto before=Hex(sessionInfo["foreground"]);auto* spectrum=dynamic_cast<ColorSpectrum*>(WorkspacePanel("color")->body->children[0].get());
  Check(spectrum!=nullptr,"Color panel lacks spectrum");PointerEvent e;e.x=spectrum->rect.left+W(spectrum->rect)*.4f;e.y=spectrum->rect.top+H(spectrum->rect)*.25f;
  spectrum->OnDown(e);Check(Hex(sessionInfo["foreground"])==before,"Color panel commits before release");spectrum->OnUp(e,true);Drain();PanelPump();Check(Hex(sessionInfo["foreground"])!=before,"Color panel did not set foreground");
  OnlyPanel("swatches");Check(WorkspacePanelState("swatches")["groups"].size()>=5,"default swatch groups missing");
  Check(static_cast<TextField*>(WorkspacePanel("swatches")->body->children[0].get())->label==i18n::Tr(L"Search"),"asset Search caption is not localized");
  Command("tools.setColors",R"({"foreground":"#112233","background":"#ffffff"})");PanelPump();
  auto* asset=FirstAsset("swatches");e={};e.x=MidX(asset->rect);e.y=MidY(asset->rect);asset->OnUp(e,true);Drain();PanelPump();Check(Hex(sessionInfo["foreground"])!="#112233","swatch click did not set foreground");
  before=Hex(sessionInfo["foreground"]);asset=FirstAsset("swatches");e.mods.alt=true;asset->OnUp(e,true);Drain();PanelPump();Check(Hex(sessionInfo["foreground"])==before && Hex(sessionInfo["background"])==before,"Alt swatch click did not isolate background");
  PanelClick("swatches",L"New Group…");PanelName(L"QA Swatches");PanelClick("swatches",L"New…");PanelName(L"QA Orange");
  auto swatches=WorkspacePanelState("swatches");bool found=false;for(auto& group:swatches["groups"].a)if(group["name"].Str()=="QA Swatches")found=group["swatches"].size()==1;Check(found,"new swatch did not enter chosen group");
  auto size=WorkspacePanel("swatches")->body->children.size();auto* group=PanelButton("swatches",L"QA Swatches");group->onClick();Check(WorkspacePanel("swatches")->body->children.size()<size,"group did not collapse");
  for(auto ext:{L"aco",L"ase"}){auto path=root/(std::wstring(L"swatches.")+ext);ExportPanelAssets("swatches",path.wstring(),"QA Swatches");PanelPump();Check(std::filesystem::file_size(path)>0,"swatches export empty");ImportPanelAssets("swatches",path.wstring());Drain();PanelPump();}
  Capture(root/L"milestone7-swatches-ru.png",false);
  OnlyPanel("gradients");Check(WorkspacePanelState("gradients")["groups"].size()>0,"gradient groups missing");
  PanelClick("gradients",L"New Group…");PanelName(L"QA Gradients");PanelClick("gradients",L"New…");PanelName(L"QA Ramp");
  auto grd=root/L"gradients.grd";ExportPanelAssets("gradients",grd.wstring(),"QA Gradients");PanelPump();Check(std::filesystem::file_size(grd)>0,"GRD export empty");ImportPanelAssets("gradients",grd.wstring());Drain();PanelPump();
  auto layer=docInfo["activeLayer"].Int();asset=FirstAsset("gradients");e={};e.x=MidX(asset->rect);e.y=MidY(asset->rect);e.clicks=2;asset->OnUp(e,true);Drain();PanelPump();Check(docInfo["activeLayer"].Int()!=layer,"gradient double-click did not create fill layer");
  Capture(root/L"milestone7-gradients-ru.png",false);
  OnlyPanel("patterns");PanelClick("patterns",L"New Group…");PanelName(L"QA Patterns");PanelClick("patterns",L"New…");PanelName(L"QA Tile");
  auto pats=WorkspacePanelState("patterns");Check(pats.size()>0,"pattern definition absent from panel");bool grouped=false;for(auto& p:pats.a)grouped|=p["name"].Str()=="QA Patterns/QA Tile";Check(grouped,"pattern group not retained in name");
  auto pat=root/L"patterns.pat";ExportPanelAssets("patterns",pat.wstring(),"QA Patterns");PanelPump();Check(std::filesystem::file_size(pat)>0,"PAT export empty");ImportPanelAssets("patterns",pat.wstring());Drain();PanelPump();
  layer=docInfo["activeLayer"].Int();asset=FirstAsset("patterns");e={};e.x=MidX(asset->rect);e.y=MidY(asset->rect);asset->OnUp(e,true);Drain();PanelPump();Check(docInfo["activeLayer"].Int()!=layer,"pattern click did not create fill layer");
  Capture(root/L"milestone7-patterns-ru.png",false);
  for(auto& k:panelKeys)WorkspacePanel(k)->visible=false;
}
void ChannelsAndPaths(const std::filesystem::path& root) {
  for(int depth:{8,16,32}) {
    NewDocument(depth);Command("select.rect",R"({"x":20,"y":30,"width":60,"height":40})");Command("select.saveSelection",R"({"name":"QA Alpha"})");
    OnlyPanel("channels");auto* p=WorkspacePanel("channels");Check(p->body->children.size()>=10,"channel rows or actions missing");
    auto* row=dynamic_cast<Container*>(p->body->children[4].get());Check(row!=nullptr,"alpha channel row missing");PointerEvent e;e.x=MidX(row->rect);e.y=MidY(row->rect);row->OnUp(e,true);Drain();PanelPump();
    Check(docInfo["channels"]["target"]["kind"].Str()=="alpha","channel targeting failed");
    Snapshot display(pc_snapshot(engine));std::array<uint8_t,4> pixel{};uint32_t w=0,h=0;
    Check(pc_render_view(display.p,25,35,26,36,1,1,pixel.data(),pixel.size(),4,&w,&h)==1&&pixel[0]>250&&pixel[1]>250,"targeted alpha channel not shown on canvas");
    Check(Sample(25,35)[3]==0,"channel display changed source sampling");
    bool eyeWas=docInfo["channels"]["alpha"][0]["visible"].Truthy();row=dynamic_cast<Container*>(p->body->children[4].get());auto* eye=dynamic_cast<IconButton*>(row->children[0].get());eye->onClick();Drain();PanelPump();Check(docInfo["channels"]["alpha"][0]["visible"].Truthy()!=eyeWas,"channel eye did not toggle");
    Command("select.deselect");PanelPump();row=dynamic_cast<Container*>(p->body->children[4].get());e.mods.ctrl=true;row->OnUp(e,true);Drain();PanelPump();Check(docInfo["selectionBounds"][2].Num()==60,"Ctrl-channel did not load selection");
    row=dynamic_cast<Container*>(p->body->children[4].get());e.mods={};e.button=1;row->OnUp(e,true);auto* menu=dynamic_cast<Menu*>(ui::ActivePopover());Check(menu&&menu->items.size()>=4,"channel context actions missing");menu->items[0].action();PanelName(L"QA Copy");Check(docInfo["channels"]["alpha"].size()==2,"duplicate channel failed");
    Command("channel.target",R"({"channel":1})");PanelPump();PanelClick("channels",L"Delete Channel");Check(docInfo["channels"]["alpha"].size()==1,"channel deletion failed");
    if(depth==16)Capture(root/L"milestone7-channels-ru.png",false);
    OnlyPanel("paths");Command("channel.target",R"({"channel":"composite"})");Command("select.deselect");Command("tools.setColors",R"({"foreground":"#ff0000"})");
    Command("path.set",R"({"path":{"subpaths":[{"closed":true,"knots":[[20,30],[80,30],[80,70],[20,70]]}]}})");PanelPump();
    PanelClick("paths",L"Load Path as Selection");Check(docInfo["selectionBounds"][2].Num()==60,"path selection wrong");Command("select.deselect");
    PanelClick("paths",L"Save / Rename Path…");PanelName(L"QA Path");Check(SelectedPanelPath()=="QA Path","saved path not selected");
    PanelClick("paths",L"Fill Path");Check(Sample(40,40)[0]>.9f,"Fill Path button did not paint");
    Command("tools.setColors",R"({"foreground":"#00ff00"})");PanelClick("paths",L"Stroke Path");Check(ui::ActivePopover()&&ui::ActivePopover()->modal,"Stroke Path button did not open its tool dialog");Key(VK_RETURN);Check(Sample(20,30)[1]>.1f,"confirmed Stroke Path did not paint");
    Tool("directSelection");canvas->tool->Tick();Drain();Check(!SelectedPanelPath().empty(),"path selection lost on tool change");
    if(depth==16)Capture(root/L"milestone7-paths-ru.png",false);
    PanelClick("paths",L"Delete Path");Check(WorkspacePanelState("paths")["paths"].size()==0,"Delete Path failed");
    PanelClick("paths",L"New Path…");PanelName(L"QA Empty");Check(WorkspacePanelState("paths")["paths"].size()==1,"New Path failed");
  }
  for(auto& k:panelKeys)WorkspacePanel(k)->visible=false;
}
void HistoryAndAnalysis(const std::filesystem::path& root) {
  for(int depth:{8,16,32}) {
    NewDocument(depth);Command("edit.fill",R"({"color":"#ff0000"})");
    auto redState=int(docInfo["history"].size())-1;
    auto* camera=dynamic_cast<TextButton*>(historyList->children[0].get());Check(camera!=nullptr,"snapshot action missing");camera->onClick();Drain();PanelPump();Check(docInfo["snapshots"].size()==1,"camera did not create snapshot");
    Command("edit.fill",R"({"color":"#0000ff"})");Layout(true);auto* snapshot=historyList->children[2].get();PointerEvent e;e.x=snapshot->rect.left+10;e.y=MidY(snapshot->rect);snapshot->OnUp(e,true);
    Tool("historyBrush");auto count=pc_history_len(engine);Stroke(40,40,42,40);Check(Sample(41,40)[0]>.9f,"history brush did not use snapshot source");Check(pc_history_len(engine)==count+1,"snapshot brush did not commit once");
    Layout(true);for(auto& child:historyList->children)if(auto* row=dynamic_cast<HistoryRow*>(child.get());row&&row->index==redState){e.x=row->rect.left+10;e.y=MidY(row->rect);row->OnUp(e,true);break;}
    JsonObject p;AddHistoryBrushSource(p);Check(Json::Parse(p.Text())["state"].Int()==redState,"state source not selected");
    Layout(true);snapshot=historyList->children[2].get();e.button=1;e.x=MidX(snapshot->rect);snapshot->OnUp(e,true);auto* menu=dynamic_cast<Menu*>(ui::ActivePopover());Check(menu&&menu->items.size()==2,"snapshot context missing");menu->items[0].action();PanelName(L"Renamed Snapshot");Check(docInfo["snapshots"][0]["name"].Str()=="Renamed Snapshot","snapshot rename failed");
    Layout(true);snapshot=historyList->children[2].get();e={};e.x=MidX(snapshot->rect);snapshot->OnUp(e,true);Drain();Check(Sample(5,5)[0]>.9f,"snapshot restore failed");
    if(depth==16){layersTabs->index=1;layersTabs->onChange(1);Capture(root/L"milestone7-history-ru.png",true);}
    auto docs=sessionInfo["documents"].size();auto* newDoc=dynamic_cast<TextButton*>(historyList->children[1].get());newDoc->onClick();Drain();Check(sessionInfo["documents"].size()==docs+1,"New Document from State failed");
    layersTabs->index=0;layersTabs->onChange(0);
    OnlyPanel("histogram");Command("edit.fill",R"({"color":"#336699"})");PanelPump();auto h=WorkspacePanelState("histogram");Check(h["samples"].Num()==32000,"histogram is not exact full-canvas count");Check(h["channels"][0][51].Num()==32000 && h["channels"][1][102].Num()==32000,"histogram channel values wrong");
    if(depth==16)Capture(root/L"milestone7-histogram-ru.png",false);
    OnlyPanel("navigator");PanelPump();Check(WorkspacePanelState("navigator")["preview"].Truthy(),"navigator preview missing");canvas->ZoomTo(10,false);Layout(true);auto* nav=WorkspacePanel("navigator")->body->children[0].get();e={};e.x=MidX(nav->rect);e.y=MidY(nav->rect);nav->OnDown(e);for(int i=0;i<120;i++)ui::Animate(1.f/60);auto center=canvas->ToDoc(MidX(canvas->free),MidY(canvas->free));Check(std::abs(center.x-100)<1 && std::abs(center.y-80)<1,"navigator did not center document");
    if(depth==16)Capture(root/L"milestone7-navigator-ru.png",false);
    OnlyPanel("info");Pointer(25,35);TickWorkspacePanels();PanelPump();auto info=WorkspacePanelState("info");Check(info["cursor"]["x"].Num()==25&&info["cursor"]["y"].Num()==35,"Info coordinates stale");Check(std::abs(info["cursor"]["rgba"][1].Num()-.4)<.01,"Info RGB wrong");
    Command("view.colorSamplers.add",R"({"x":5,"y":6})");PanelPump();Check(WorkspacePanelState("info")["samplers"]["samplers"].size()==1,"Info sampler list missing");
    if(depth==16)Capture(root/L"milestone7-info-ru.png",false);
    for(auto& k:panelKeys)WorkspacePanel(k)->visible=false;
  }
  NewDocument();OnlyPanel("histogram");PanelPump();Check(WorkspacePanelState("histogram")["samples"].Num()==0,"transparent pixels counted in histogram");
  for(auto [mode,count]:{std::pair{"cmyk",4},std::pair{"lab",3},std::pair{"grayscale",1}}){NewDocument();Command("image.mode."+std::string(mode));OnlyPanel("histogram");PanelPump();auto hist=WorkspacePanelState("histogram");Check(hist["channelNames"].size()==size_t(count)&&hist["channels"].size()==size_t(count+1),"histogram channel list does not follow document mode");}
  for(auto& k:panelKeys)WorkspacePanel(k)->visible=false;
}
}
