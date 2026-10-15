void CanvasSettle(){for(int i=0;i<5;i++){if(canvas->tool)canvas->tool->Tick();Drain();}}
template<class T>T* CanvasField(const std::wstring& label){auto* p=CanvasWorkspacePanel();Check(p&&p->visible,"canvas workspace panel missing");for(auto& w:p->body->children)if(auto* field=dynamic_cast<T*>(w.get());field&&field->label==label)return field;throw std::runtime_error("missing canvas field: "+Narrow(label));}
void CanvasEditors(){
  for(int depth:{8,16,32}){
    NewDocument(depth);Command("shape.create",R"({"kind":"ellipse","rect":[20,20,80,80],"fill":"#ff0000","creationMode":"pixels"})");Tool("move");auto original=Sample(50,50);auto history=pc_history_len(engine);
    RunCommand("edit.puppetWarp",L"Puppet Warp");CanvasSettle();auto state=CanvasWorkspaceState();Check(state["active"].Truthy()&&state["geometry"]["triangles"].size()>0,"Puppet Warp alpha mesh missing");
    // No mesh vertex may fill the transparent corners of the layer's bounding rectangle.
    bool inner=false;for(auto& p:state["geometry"]["rest"].a)if(p[0].Num()>40&&p[0].Num()<80)inner=true;Check(inner,"alpha contour has no interior mesh vertices");
    Stroke(50,50,70,50);CanvasSettle();Check(CanvasWorkspaceState()["params"]["pins"].size()==1,"pin gesture did not create a pin");Check(CanvasWorkspaceState()["preview"].Truthy(),"Puppet Warp preview missing");
    Check(Sample(95,50)[3]>.5,"pin translation did not move pixels");Key('Z',{true});CanvasSettle();Check(CanvasWorkspaceState()["params"]["pins"].size()==0,"Puppet local undo escaped to document");Key('Z',{true,true});CanvasSettle();Check(CanvasWorkspaceState()["params"]["pins"].size()==1,"Puppet local redo missing");
    if(depth==8&&!interactionScreenshots.empty()){Layout(true);Capture(interactionScreenshots/L"milestone9-puppet.png",false);}Key(VK_ESCAPE);Check(Sample(50,50)==original&&pc_history_len(engine)==history,"cancel Puppet changed source history/pixels");
    RunCommand("edit.puppetWarp",L"Puppet Warp");CanvasSettle();Stroke(50,50,70,50);CanvasSettle();Key(VK_RETURN);Check(Sample(95,50)[3]>.5,"Puppet apply lost the edit");Key('Z',{true});Check(Sample(50,50)==original,"Puppet apply did not undo once");
    RunCommand("filter.vanishingPoint",L"Vanishing Point");CanvasSettle();
    for(auto p:{D2D_POINT_2F{10,10},{160,20},{150,130},{20,120}}){auto e=Pointer(p.x,p.y);canvas->OnDown(e);canvas->OnUp(e,true);Drain();}CanvasSettle();
    Check(CanvasWorkspaceState()["params"]["planes"].size()==1,"four clicks did not create perspective plane");
    auto* tool=CanvasField<SelectList>(i18n::Tr(L"Tool"));tool->onChange(3);
    auto e=Pointer(40,50,{false,false,true});canvas->OnDown(e);canvas->OnUp(e,true);Drain();Stroke(65,50,85,55);CanvasSettle();
    Check(CanvasWorkspaceState()["params"]["clone"].size()==1&&CanvasWorkspaceState()["preview"].Truthy(),"perspective clone did not preview");
    if(depth==8&&!interactionScreenshots.empty()){Layout(true);Capture(interactionScreenshots/L"milestone9-vanishing-point.png",false);}Key(VK_ESCAPE);Check(Sample(50,50)==original,"cancel Vanishing Point changed pixels");
    RunCommand("edit.contentAwareScale",L"Content-Aware Scale");CanvasSettle();auto* width=CanvasField<NumberField>(L"Width");width->onCommit(60);CanvasSettle();Check(CanvasWorkspaceState()["params"]["width"].Int()==60&&CanvasWorkspaceState()["preview"].Truthy(),"Content Aware Scale width did not preview");Key(VK_ESCAPE);Check(Sample(50,50)==original,"cancel Content Aware Scale changed pixels");
    RunCommand("edit.contentAwareScale",L"Content-Aware Scale");CanvasSettle();auto p=CanvasWorkspaceState()["params"];double right=p["x"].Num()+p["width"].Num(),bottom=p["y"].Num()+p["height"].Num();Stroke(right,bottom,right-10,bottom-5);CanvasSettle();Check(CanvasWorkspaceState()["params"]["width"].Num()<p["width"].Num(),"Content Aware Scale canvas handles did not resize");
    if(depth==8&&!interactionScreenshots.empty()){Layout(true);Capture(interactionScreenshots/L"milestone9-content-scale.png",false);}Key(VK_RETURN);Key('Z',{true});Check(Sample(50,50)==original,"Content Aware Scale did not undo once");
  }
  NewDocument();Command("shape.create",R"({"kind":"rect","rect":[0,0,200,160],"fill":"#ff0000","creationMode":"pixels"})");Command("select.rect",R"({"x":80,"y":60,"width":12,"height":12})");
  auto original=Sample(85,65);auto history=pc_history_len(engine);RunCommand("edit.contentAwareFill",L"Content-Aware Fill");CanvasSettle();Stroke(10,10,60,100);CanvasSettle();Check(CanvasWorkspaceState()["params"]["sampling"].Str()=="custom"&&CanvasWorkspaceState()["preview"].Truthy(),"sampling rectangle did not reach Fill preview");
  if(!interactionScreenshots.empty()){Layout(true);Capture(interactionScreenshots/L"milestone9-content-fill.png",false);}Key(VK_ESCAPE);Check(Sample(85,65)==original&&pc_history_len(engine)==history,"cancel Fill changed source/history");
}
