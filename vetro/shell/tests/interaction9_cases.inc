void DialogMenuDetails() {
  NewDocument();Rectangle();Command("layer.rasterize.layer");
  AskText(50,50,L"Name",L"untouched",[](const std::wstring&){throw std::runtime_error("close button committed a text dialog");});
  auto* dialog=ui::ActivePopover();Check(dialog&&dialog->titleClose,"dialog header has no close button");
  dialog->titleClose->onClick();Check(!dialog->open,"dialog close button did not cancel");ui::Animate(1);
  RunCommand("image.adjustments.curves",L"Curves…");dialog=ui::ActivePopover();
  Check(dialog&&dialog->titleClose,"editor header has no close button");dialog->titleClose->onClick();Drain();Check(!dialog->open,"editor close button did not cancel");ui::Animate(1);
  layerBlend->OnUp({},true);auto* menu=dynamic_cast<Menu*>(ui::ActivePopover());
  Check(menu!=nullptr,"blend popup missing");int separators=0,choices=0;
  for(auto& row:menu->items){if(row.separator)separators++;else choices++;}
  Check(separators==5&&choices==27,"blend groups lost modes or separators");ui::ClosePopovers();ui::Animate(1);
  Tool("rectMarquee");sel.mode=0;RebuildOptions();Segmented* modes=nullptr;
  for(auto* w:optionWidgets)if(auto* s=dynamic_cast<Segmented*>(w);s&&s->options.size()==4)modes=s;
  Check(modes!=nullptr,"selection operation buttons missing");
  {ThreadKeys held({VK_SHIFT});Check(modes->DisplayIndex()==1&&sel.mode==0,"Shift does not highlight Add temporarily");}
  {ThreadKeys held({VK_MENU});Check(modes->DisplayIndex()==2&&sel.mode==0,"Alt does not highlight Subtract temporarily");}
  {ThreadKeys held({VK_SHIFT,VK_MENU});Check(modes->DisplayIndex()==3&&sel.mode==0,"Shift+Alt does not highlight Intersect");}
  Check(modes->DisplayIndex()==0,"selection button did not restore after modifier release");
  auto* row=Row(uint64_t(docInfo["activeLayer"].Int()));LayerMenu(row,{});
  auto found=std::find_if(contextMenu->items.begin(),contextMenu->items.end(),[](auto& item){return item.label==L"Load Transparency into Mask";});
  Check(found!=contextMenu->items.end()&&found->enabled,"layer transparency mask action missing");
  auto action=found->action;ui::ClosePopovers();action();Drain();Check(!(*ActiveLayer())["mask"].IsNull(),"transparency mask command did not run");
  Key('Z',{true});Check((*ActiveLayer())["mask"].IsNull(),"transparency mask action did not undo once");
}
void MoveBoxAndTabs() {
  NewDocument();auto a=Rectangle("Touched A");
  auto b=uint64_t(Command("shape.create",R"({"kind":"rect","rect":[60,25,25,25],"fill":"#0000ff","name":"Touched B"})")["layer"].Int());
  auto c=uint64_t(Command("shape.create",R"({"kind":"rect","rect":[130,25,25,25],"fill":"#00ff00","name":"Untouched"})")["layer"].Int());
  Tool("move");moveAutoSelect=true;moveSelectGroup=false;moveControls=false;
  Stroke(10,10,65,35);
  Check(FindLayer(docInfo["layers"],int64_t(a))->operator[]("selected").Truthy(),"Move box missed first touched layer");
  Check(FindLayer(docInfo["layers"],int64_t(b))->operator[]("selected").Truthy(),"Move box missed partly touched layer");
  Check(!FindLayer(docInfo["layers"],int64_t(c))->operator[]("selected").Truthy(),"Move box selected an untouched layer");
  Stroke(120,10,160,60,{false,true});
  Check(FindLayer(docInfo["layers"],int64_t(a))->operator[]("selected").Truthy()&&FindLayer(docInfo["layers"],int64_t(c))->operator[]("selected").Truthy(),"Shift box did not add to selection");
  auto source=ActiveDocumentId();size_t sourceCount=docInfo["layers"].size();
  NewDocument();auto destination=ActiveDocumentId();auto count=docInfo["layers"].size();
  SwitchDocument(source);Drain();Layout(true);
  auto* row=Row(a);auto box=row->NameBox();PointerEvent e;e.x=MidX(box);e.y=MidY(box);row->OnDown(e);
  DocumentTab* target=nullptr;for(auto& child:documentTabs->children)if(auto* tab=dynamic_cast<DocumentTab*>(child.get());tab&&tab->id==destination)target=tab;
  Check(target!=nullptr,"destination document tab missing");e.x=MidX(target->rect);e.y=MidY(target->rect);row->OnDrag(e);
  Check(layerDrag.document==destination,"layer drag did not target the other document tab");row->OnUp(e,false);Drain();
  Check(ActiveDocumentId()==destination&&docInfo["layers"].size()==count+3,"tab drop did not copy all selected layers");
  Key('Z',{true});Check(docInfo["layers"].size()==count,"tab copy did not undo as one step");
  SwitchDocument(source);Drain();Check(docInfo["layers"].size()==sourceCount,"tab copy removed source layers");
}
