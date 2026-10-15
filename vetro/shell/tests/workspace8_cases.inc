namespace {
void EditorSettle() {
  for(int i=0;i<70;i++){if(Mode())Mode()->Tick();jobs::Pump();Sleep(2);}
  Drain();
}
void SelectMaskWorkspace(const std::filesystem::path& root) {
  for(int depth:{8,16,32}) {
    NewDocument(depth);Rectangle();Command("layer.rasterize.layer");
    Command("select.rect",R"({"x":20,"y":20,"width":30,"height":30})");
    auto before=JsonText(docInfo["selectionBounds"]);
    RunCommand("select.refineEdge",L"Select and Mask");EditorSettle();
    Check(WorkspaceEditorState()["active"].Truthy(),"Select and Mask workspace missing");
    auto* panel=WorkspaceEditorPanel();Check(panel&&panel->visible,"Select and Mask controls missing");
    auto* radius=dynamic_cast<NumberField*>(panel->body->children[5].get());
    Check(radius!=nullptr,"refine radius control missing");radius->onCommit(4);EditorSettle();
    Stroke(47,20,47,45);EditorSettle();Check(WorkspaceEditorState()["strokes"].Int()==1,"edge brush stroke missing");
    Check(WorkspaceEditorState()["preview"].Truthy(),"Select and Mask did not preview through the engine");
    auto* views=dynamic_cast<SelectList*>(panel->body->children[1].get());
    for(int view=0;view<5;view++){views->onChange(view);EditorSettle();Check(canvas->tool->ShowsSelection()==(view==1),"view mode did not control selection ants");}
    views->onChange(2);EditorSettle();if(depth==8){Layout(true);Capture(root/L"milestone8-select-mask.png",false);}
    Key(VK_ESCAPE);Check(!WorkspaceEditorState()["active"].Truthy()&&JsonText(docInfo["selectionBounds"])==before,"Select and Mask cancellation changed selection");
    RunCommand("select.refineEdge",L"Select and Mask");EditorSettle();
    auto* output=dynamic_cast<SelectList*>(WorkspaceEditorPanel()->body->children[14].get());
    Check(output!=nullptr,"Select and Mask output missing");output->onChange(1);Key(VK_RETURN);
    Check(!(*ActiveLayer())["mask"].IsNull(),"Select and Mask output did not create layer mask");
    Key('Z',{true});Check((*ActiveLayer())["mask"].IsNull(),"Select and Mask output did not undo once");
  }
}
void LiquifyUndo(const std::filesystem::path& root) {
  NewDocument();Rectangle();Command("layer.rasterize.layer");
  RunCommand("filter.liquify",L"Liquify");
  Stroke(40,30,65,30);EditorSettle();
  Check(LiquifyState()["strokes"].Int()==1,"Liquify stroke missing");
  Key('Z',{true});EditorSettle();Check(LiquifyState()["strokes"].Int()==0&&LiquifyState()["redo"].Int()==1,"Liquify Ctrl+Z did not undo locally");
  Key('Z',{true,true});EditorSettle();Check(LiquifyState()["strokes"].Int()==1,"Liquify Ctrl+Shift+Z did not redo");
  Key('Z',{true});EditorSettle();Stroke(40,35,60,35);EditorSettle();Check(LiquifyState()["redo"].Int()==0,"new Liquify stroke did not clear redo");
  Layout(true);Capture(root/L"milestone9-liquify.png",false);Key(VK_ESCAPE);Check(!Mode(),"Liquify cancellation left editor active");
}
void ScreenAndQuickMask(const std::filesystem::path& root) {
  NewDocument();Rectangle();Tool("brush");
  Command("select.rect",R"({"x":10,"y":10,"width":80,"height":80})");
  Key('Q'); Check(!docInfo["channels"]["quickMask"].IsNull(),"Q did not enter Quick Mask");
  Command("tools.setColors",R"({"foreground":"#000000"})");
  brush.size=20;brush.hardness=100;SetBrushSettings();Drain();Stroke(40,40,40,40);
  Layout(true);Capture(root/L"milestone8-quick-mask.png",false);
  Key('Q');Check(docInfo["channels"]["quickMask"].IsNull(),"Q did not exit Quick Mask");
  Snapshot snap(pc_snapshot(engine));
  Check(pc_selection_contains(snap.p,15,15)==1,"Quick Mask lost unaffected selection");
  Check(pc_selection_contains(snap.p,40,40)==0,"painting Quick Mask did not change selection");
  bool extras=ExtrasVisible();Key('H',{true});Check(ExtrasVisible()!=extras,"Ctrl+H did not hide extras");
  Key('H',{true});Check(ExtrasVisible()==extras,"Ctrl+H did not restore extras");
  auto* panel=WorkspacePanel("color");panel->visible=true;
  auto* off=WorkspacePanel("patterns");off->visible=false;
  Key(VK_TAB);Check(!toolsDock->visible&&!layersDock->visible&&!panel->visible,"Tab left panels visible");
  Key(VK_TAB);Check(toolsDock->visible&&layersDock->visible&&panel->visible&&!off->visible,"Tab did not preserve individual visibility");
  panel->visible=false;
  auto style=GetWindowLongPtr(hwnd,GWL_STYLE);
  Key('F');Check(screenMode==1&&menuDock->visible,"F did not enter full screen with menu");
  Key('F');Check(screenMode==2&&!menuDock->visible&&!documentTabs->visible,"full screen did not hide chrome");
  Key('F');Check(screenMode==0&&GetWindowLongPtr(hwnd,GWL_STYLE)==style&&menuDock->visible,"F did not restore standard window");
  ui::SetBounds(R(0,0,1584,961));Layout(true);
}
void ContextTasks(const std::filesystem::path& root) {
  NewDocument();Rectangle();Layout(true);canvas->Fit(false);Refresh();
  Check(contextTaskBar&&contextTaskBar->context=="image"&&contextTaskBar->children.size()==2,"empty-selection task bar incorrect");
  Command("select.rect",R"({"x":10,"y":10,"width":30,"height":20})");
  Check(contextTaskBar->context=="selection"&&contextTaskBar->children.size()==4,"selection task bar incorrect");
  auto* invert=dynamic_cast<TextButton*>(contextTaskBar->children[1].get());invert->onClick();Drain();
  Snapshot snap(pc_snapshot(engine));Check(pc_selection_contains(snap.p,15,15)==0&&pc_selection_contains(snap.p,100,100)==1,"context invert did not apply");
  Command("select.deselect");Command("type.create",R"({"x":20,"y":70,"text":"Context","size":31})");
  Drain();Check(contextTaskBar->context=="text"&&contextTaskBar->size&&contextTaskBar->size->value==31,"text context did not read size");
  contextTaskBar->size->onCommit(45);Drain();Check(Command("type.info")["runs"][0]["style"]["size_pt"].Num()==45,"context font size did not apply");
  Layout(true);Capture(root/L"milestone8-context-text.png",false);
}
}
