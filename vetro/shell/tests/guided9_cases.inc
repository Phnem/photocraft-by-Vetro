void GuidedCanvas(){
  for(int depth:{8,16,32}){
    NewDocument(depth);Command("shape.create",R"({"kind":"rect","rect":[20,20,100,80],"fill":"#ff0000","creationMode":"pixels"})");auto original=Sample(20,20);auto history=pc_history_len(engine);
    RunCommand("image.guidedUpright",L"Guided Upright");CanvasSettle();Check(GuidedWorkspaceState()["active"].Truthy(),"Guided Upright workspace missing");Stroke(20,40,120,60);CanvasSettle();Check(GuidedWorkspaceState()["params"]["lines"].size()==1,"horizon guide not drawn");
    auto* preview=dynamic_cast<Toggle*>(GuidedWorkspacePanel()->body->children[1].get());preview->Set(true);preview->onChange(true);CanvasSettle();Check(GuidedWorkspaceState()["preview"].Truthy(),"guided geometry did not preview");Key(VK_ESCAPE);Check(Sample(20,20)==original&&pc_history_len(engine)==history,"Guided cancellation changed history/pixels");
    RunCommand("image.guidedUpright",L"Guided Upright");CanvasSettle();Stroke(20,40,120,60);CanvasSettle();Key('Z',{true});CanvasSettle();Check(GuidedWorkspaceState()["params"]["lines"].size()==0,"guided undo escaped to document");Key('Z',{true,true});CanvasSettle();
    if(depth==8&&!interactionScreenshots.empty()){Layout(true);Capture(interactionScreenshots/L"milestone9-guided-upright.png",false);}Key(VK_RETURN);Check(pc_history_len(engine)==history+1,"Guided apply is not a single step");Key('Z',{true});Check(Sample(20,20)==original,"Guided apply did not undo once");
  }
}
