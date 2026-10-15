namespace {
ScrollArea* AppBody(){auto* dialog=ApplicationDialog();Check(dialog&&dialog->open,"application dialog missing");auto* body=dynamic_cast<ScrollArea*>(dialog->children[0].get());Check(body!=nullptr,"application form missing");return body;}
template<class T>T* AppField(const std::wstring& label){for(auto& widget:AppBody()->children)if(auto* field=dynamic_cast<T*>(widget.get());field&&field->label==label)return field;throw std::runtime_error("missing application field: "+Narrow(label));}
TextButton* AppButton(const std::wstring& label){return AppField<TextButton>(label);}
void AnimateApplication(){for(int i=0;i<150;i++){ui::Animate(.02f);jobs::Pump();Sleep(2);}Drain();}
void ApplicationSheets(const std::filesystem::path& root){
  auto saved=root/L"application-preferences.json";InitializeApplicationPreferences(saved.wstring());Drain();
  auto count=sessionInfo["documents"].size();RunCommand("file.new",L"New…");Drain();
  Check(ApplicationDialogState()["kind"].Str()=="new","File New did not open its workspace");
  auto* category=AppField<SelectList>(i18n::Tr(L"Category"));
  for(int i=0;i<4;i++){category->index=i;category->onChange(i);Check(ApplicationDialogState()["presets"].Int()>=2,"new-document preset category empty");}
  Key(VK_ESCAPE);Check(sessionInfo["documents"].size()==count,"cancel New created a document");
  RunCommand("file.new",L"New…");Drain();auto* width=AppField<NumberField>(L"Width");auto* height=AppField<NumberField>(L"Height");width->onCommit(80);height->onCommit(60);
  AppField<SelectList>(i18n::Tr(L"Bit Depth"))->index=1;AppField<SelectList>(i18n::Tr(L"Background"))->index=2;
  AppField<TextField>(i18n::Tr(L"Preset Name"))->value=L"Vetro 80×60";AppButton(L"Save Preset")->onClick();Drain();
  category=AppField<SelectList>(i18n::Tr(L"Category"));category->index=4;category->onChange(4);Check(ApplicationDialogState()["presets"].Int()>=1,"custom document preset not listed");
  Layout(true);Capture(root/L"milestone8-new.png",false);Key(VK_RETURN);
  Check(docInfo["width"].Int()==80&&docInfo["height"].Int()==60&&docInfo["depth"].Int()==16,"new preset dimensions/depth lost");
  Check(std::filesystem::exists(saved),"custom presets were not persisted");
  Rectangle();auto source=ActiveDocumentId();auto history=pc_history_len(engine);
  RunCommand("file.export.exportAs",L"Export As…");Drain();
  width=AppField<NumberField>(L"Width");width->onCommit(40);AnimateApplication();
  Check(ApplicationDialogState()["preview"].Truthy(),"Export As has no encoded preview");
  Layout(true);Capture(root/L"milestone8-export.png",false);
  auto png=root/L"native-export.png";fileDialogOverride=[png](bool save,const std::wstring&){Check(save,"export did not ask for destination");return png.wstring();};Key(VK_RETURN);fileDialogOverride={};
  Check(ActiveDocumentId()==source&&pc_history_len(engine)==history,"export edited the source document");
  char* result=nullptr;Check(pc_open(engine,Narrow(png.wstring()).c_str(),&result)==1,"exported PNG did not reopen");TakeString(result);Refresh();
  Check(docInfo["width"].Int()==40&&docInfo["height"].Int()==30,"export ignored requested size/aspect ratio");
  RunCommand("edit.preferences.guidesGridAndSlices",L"Preferences…");Drain();
  Check(ApplicationDialogState()["sections"].Int()==18&&ApplicationDialogState()["section"].Str()=="guidesGridAndSlices","preferences sections missing");
  auto* subdivisions=AppField<NumberField>(i18n::Tr(HumanKey("subdivisions")));auto before=subdivisions->value;subdivisions->Set(7);subdivisions->onCommit(7);Key(VK_ESCAPE);
  Check(Command("prefs.get",R"({"path":"guidesGridAndSlices.subdivisions"})").Num()==before,"cancel Preferences applied changes");
  RunCommand("edit.preferences.guidesGridAndSlices",L"Preferences…");Drain();subdivisions=AppField<NumberField>(i18n::Tr(HumanKey("subdivisions")));subdivisions->Set(7);subdivisions->onCommit(7);
  Layout(true);Capture(root/L"milestone8-preferences.png",false);Key(VK_RETURN);
  Check(Command("prefs.get",R"({"path":"guidesGridAndSlices.subdivisions"})").Int()==7,"Preferences did not apply validated field");
  auto chooseUndo=[] {
    auto* body=AppBody();auto* choices=dynamic_cast<SelectList*>(body->children[1].get());Check(choices!=nullptr,"shortcut command list missing");
    auto label=i18n::Tr(L"Edit")+L" › "+i18n::TrId("edit.undo",L"Undo");auto it=std::find(choices->options.begin(),choices->options.end(),label);
    Check(it!=choices->options.end(),"Undo missing from shortcut editor");int index=int(it-choices->options.begin());choices->index=index;choices->onChange(index);
  };
  auto original=Command("edit.keyboardShortcuts",R"({"list":true})");
  Key('K',{true,true,true});chooseUndo();auto* key=AppField<TextField>(i18n::Tr(L"Shortcut"));key->onChange(L"F9");Drain();Key(VK_ESCAPE);
  Check(JsonText(Command("edit.keyboardShortcuts")["overrides"])==JsonText(original["overrides"]),"cancel shortcut editor changed bindings");
  Key('K',{true,true,true});chooseUndo();key=AppField<TextField>(i18n::Tr(L"Shortcut"));key->onChange(L"F9");Drain();
  Layout(true);Capture(root/L"milestone8-shortcuts.png",false);Key(VK_RETURN);
  NewDocument();Rectangle("A");Rectangle("B");auto layers=docInfo["layers"].size();Key(VK_F9);Check(docInfo["layers"].size()==layers-1,"custom F9 did not execute Undo");
  Key('Z',{true});Check(docInfo["layers"].size()==layers-1,"overridden Ctrl+Z still ran its old command");
  auto kys=root/L"native-shortcuts.kys";{std::ofstream f(kys,std::ios::binary);f<<R"(<photoshop-keyboard-shortcuts><command kind="static" name="Undo"><shortcut>Ctrl+Alt+U</shortcut></command></photoshop-keyboard-shortcuts>)";}
  Key('K',{true,true,true});ImportShortcutFile(kys.wstring());Drain();
  Check(ApplicationDialogState()["overrides"]["edit.undo"].Str()=="Cmd+Alt+U","Windows KYS shortcut did not normalize to the engine command key");Key(VK_RETURN);
  Rectangle("C");layers=docInfo["layers"].size();Key('U',{true,false,true});Check(docInfo["layers"].size()==layers-1,"imported KYS shortcut did not execute");
  Command("prefs.set",R"({"path":"guidesGridAndSlices.subdivisions","value":3})");InitializeApplicationPreferences(saved.wstring());Drain();
  Check(Command("prefs.get",R"({"path":"guidesGridAndSlices.subdivisions"})").Int()==7,"saved preferences did not restore");
  Check(Command("edit.keyboardShortcuts")["overrides"]["edit.undo"].Str()=="Cmd+Alt+U","saved shortcuts did not restore");
  Command("edit.keyboardShortcuts",R"({"reset":true})");ApplyShortcutMetadata(Command("edit.keyboardShortcuts",R"({"list":true})"));
}
}
