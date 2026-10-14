#define VETRO_MILESTONE2
#include "milestone1.cpp"
namespace {
void QueuedInput() {
  NewDocument(); Rectangle(); Tool("move");
  size_t layers=docInfo["layers"].size();
  jobs::Run([] { Sleep(120); });
  Check(jobs::Busy(),"slow job must be in flight");
  Check(HandleKey(VK_DELETE,{}),"busy Delete was not accepted");
  Check(HandleKey('Z',{true}),"busy Undo was not accepted");
  Check(HandleKey('Y',{true}),"busy Redo was not accepted");
  Check(pendingInput.size()==3,"keys were dropped instead of queued");
  Check(StatusText().find(L"3")!=std::wstring::npos,"queue count missing in capsule");
  Drain(); Check(pendingInput.empty(),"input queue did not drain");
  Check(docInfo["layers"].size()==layers-1,"Delete/Undo/Redo did not retain order");
  jobs::Run([] { Sleep(120); });
  RunCommand("layer.new.layer",L"New Layer"); RunCommand("layer.new.layer",L"New Layer");
  Check(pendingInput.size()==2,"busy menu actions were dropped");
  Drain(); Check(docInfo["layers"].size()==layers+1,"queued menu commands did not run");
  auto* button=new TextButton(L"Test",TextButton::Secondary,[] { RunCommand("layer.new.layer",L"New Layer"); });
  button->rect=RWH(700,400,80,36); ui::AddLayer(button);
  PointerEvent e; e.x=730;e.y=416;
  jobs::Run([] { Sleep(120); });
  ui::PointerDown(e);ui::PointerUp(e);
  Check(pendingInput.size()==1,"busy pointer click was dropped");
  Drain(); Check(docInfo["layers"].size()==layers+2,"queued pointer click did not run");
  button->visible=false;
}
void ClickText(double x, double y) {
  auto e = Pointer(x, y); canvas->OnDown(e); canvas->OnUp(e, true); Drain();
}
void EmptyText(int depth) {
  NewDocument(depth); Tool("type");
  size_t count = docInfo["layers"].size();
  ClickText(20,50); Check(TextEditing(), "empty text editor did not start");
  Key(VK_ESCAPE);
  Check(docInfo["layers"].size() == count, "click/Esc leaves an empty text layer");
  ClickText(20,50); TextEditorChar(L'A');
  Key('A', {true}); Key(VK_DELETE); Key(VK_RETURN, {true});
  Check(docInfo["layers"].size() == count, "delete all/commit leaves an empty text layer");
  ClickText(20,50);
  auto* number = dynamic_cast<NumberField*>(TextPanel(false)->body->children[1].get());
  Check(number != nullptr,"size field missing"); number->onCommit(30); Drain();
  Check(TextEditing(), "styling a new empty text layer must preserve the editor");
  TextEditorChar(L'B'); Key(VK_ESCAPE);
  Check(docInfo["layers"].size() == count + 1,"non-empty text layer removed");
  auto id = uint64_t(docInfo["activeLayer"].Int());
  Tool("move"); Layout(true);
  auto* row = Row(id); auto thumb = row->Thumb();
  PointerEvent e; e.x=MidX(thumb); e.y=MidY(thumb); e.clicks=2;
  row->OnDown(e); Drain();
  Check(TextEditing() && current->key == "type", "double click did not edit text");
  auto before = TextEditorState()["blinkAt"].Num(); Sleep(2); Key(VK_RIGHT);
  Check(TextEditorState()["blinkAt"].Num() > before,"navigation does not reset caret blink");
  TextEditorChar(L'C');
  Check(TextEditorState()["blinkAt"].Num() >= before,"typing does not reset caret blink");
  Key(VK_ESCAPE); Check(current->key == "move","previous tool not restored");
  row=Row(id);thumb=row->Thumb();e.x=MidX(thumb);e.y=MidY(thumb);row->OnDown(e);Drain();
  Check(TextEditing(),"second double-click did not edit text");
  SelectList* smoothing=nullptr;
  for(auto* w:optionWidgets) if(auto* list=dynamic_cast<SelectList*>(w);list&&list->options.size()==5&&list->options[0]==L"None")smoothing=list;
  Check(smoothing!=nullptr,"text antialias control missing");smoothing->onChange(1);Drain();
  Check(TextEditing() && current->key=="type","changing antialias must preserve temporary text editing");
  NewDocument();
  Check(current->key=="move","automatic text completion did not restore previous tool");
}
void PatchSelection(int depth) {
  NewDocument(depth); Rectangle(); Command("layer.rasterize.layer");
  Command("select.rect",R"({"x":20,"y":20,"width":80,"height":70})");
  Command("select.rect",R"({"x":40,"y":40,"width":20,"height":20,"mode":"subtract"})");
  Snapshot s(pc_snapshot(engine));
  Check(pc_selection_contains(s.p,25,25)==1,"selected pixel not contained");
  Check(pc_selection_contains(s.p,50,50)==0,"selection hole treated as selected");
  Check(pc_selection_contains(s.p,-1,20)==0,"outside canvas treated as selected");
  Tool("patch");
  auto before = JsonText(docInfo["selectionBounds"]);
  auto e=Pointer(120,100); canvas->OnDown(e);
  for (auto point : {std::pair{160.,100.}, {160.,140.}, {120.,140.}}) canvas->OnDrag(Pointer(point.first,point.second));
  canvas->OnUp(Pointer(120,100),true); Drain();
  Check(JsonText(docInfo["selectionBounds"]) != before,"outside drag moved patch instead of new selection");
  before = JsonText(docInfo["selectionBounds"]);
  Stroke(130,110,135,115);
  Check(JsonText(docInfo["selectionBounds"]) == before,"inside drag replaced patch selection");
  Command("select.rect",R"({"x":20,"y":20,"width":80,"height":70})");
  Command("select.rect",R"({"x":40,"y":40,"width":20,"height":20,"mode":"subtract"})");
  canvas->OnDown(Pointer(45,45)); canvas->OnDrag(Pointer(55,45)); canvas->OnDrag(Pointer(55,55));
  canvas->OnUp(Pointer(45,55),true); Drain();
  Check(docInfo["selectionBounds"][0].Num() >= 44,"drag in a selection hole must draw a new area");
}
void TextControls() {
  NewDocument(); i18n::Load("ru");
  Command("type.create",R"({"x":20,"y":60,"text":"Centered","size":24,"align":"center"})");
  Command("type.setStyle",R"({"kerning":"optical","caps":"all","direction":"rtl","hyphenate":true,"autoLeading":150})");
  TextPanelCommand("window.paragraph"); TextPanelCommand("window.character"); Drain();
  auto& para=TextPanel(true)->body->children; auto& chr=TextPanel(false)->body->children;
  Check(dynamic_cast<SelectList*>(para[0].get())->index==1,"paragraph alignment does not read layer");
  Check(dynamic_cast<SelectList*>(para[7].get())->index==2,"paragraph direction does not read layer");
  Check(dynamic_cast<Toggle*>(para[8].get())->on,"hyphenation does not read layer");
  Check(dynamic_cast<NumberField*>(para[6].get())->value==150,"auto leading percentage incorrect");
  Check(dynamic_cast<SelectList*>(chr[8].get())->index==1,"kerning mode does not read layer");
  Check(dynamic_cast<SelectList*>(chr.back().get())->index==2,"caps do not read layer");
  auto* kern = dynamic_cast<NumberField*>(chr[4].get());
  kern->onCommit(125); Drain();
  Check(Command("type.info")["runs"][0]["style"]["kern"].Num()==125,"manual kerning field sends wrong parameter");
  for(auto key:{"firstLineIndent","startIndent","endIndent","spaceBefore","spaceAfter","autoLeading"})
    Check(i18n::Tr(HumanKey(key))!=HumanKey(key),"paragraph label untranslated");
  Command("type.create",R"({"x":50,"y":90,"text":"Right","align":"right"})");
  Check(dynamic_cast<SelectList*>(para[0].get())->index==2,"open paragraph panel did not follow layer change");
  Command("type.setStyle",R"({"align":"left","direction":"ltr"})");
  Check(dynamic_cast<SelectList*>(para[0].get())->index==0,"open panel did not follow a style change");
  auto mixed=Command("type.create",R"({"x":10,"y":40,"text":"left\nright","align":"left"})")["layer"].Int();
  Command("type.setStyle",R"({"range":[5,10],"align":"right","kerning":"optical"})");
  Tool("type");EditTypeLayer(uint64_t(mixed),false);Drain();
  Key(VK_END,{true});canvas->tool->Tick();Drain();
  Check(dynamic_cast<SelectList*>(para[0].get())->index==2,"paragraph panel did not follow caret to second paragraph");
  Check(dynamic_cast<SelectList*>(chr[8].get())->index==1,"character panel did not follow caret to styled run");
  Key(VK_HOME,{true});canvas->tool->Tick();Drain();
  Check(dynamic_cast<SelectList*>(para[0].get())->index==0,"paragraph panel did not follow caret to first paragraph");
  Key(VK_ESCAPE);
  TextPanel(true)->visible=false; TextPanel(false)->visible=false;
}
void BrushControls() {
  NewDocument(); Tool("brush"); ToolPanelCommand("window.brushes"); Drain();
  auto* panel=BrushPanel(false);
  auto* groups=dynamic_cast<SelectList*>(panel->body->children[1].get());
  Check(groups && groups->index==0,"all groups is not selected");
  for(auto& name:groups->options) Check(!name.empty(),"empty brush group row");
  panel->visible=false;
  ToolPanelCommand("window.brushSettings"); Drain();
  auto selectSection=[](int i) { auto* field=dynamic_cast<SelectList*>(BrushPanel(true)->body->children[0].get()); field->onChange(i); Drain(); };
  for (int i=0;i<12;i++) { selectSection(i); Check(BrushPanel(true)->body->children.size()>1,"brush section has no controls"); }
  selectSection(1);
  int disabled=0;
  for(auto& c:BrushPanel(true)->body->children)
    if(auto* n=dynamic_cast<NumberField*>(c.get()); n && n->label==L"Minimum") { Check(!n->enabled,"minimum active while control off"); disabled++; }
  Check(disabled>0,"no dependent brush minimum controls");
  SelectList* control=nullptr;
  for(auto& c:BrushPanel(true)->body->children) if(auto* l=dynamic_cast<SelectList*>(c.get());l && l->label==L"Control") {control=l;break;}
  Check(control!=nullptr,"brush controller missing"); control->onChange(2); Drain();
  bool enabled=false;
  for(auto& c:BrushPanel(true)->body->children) if(auto* n=dynamic_cast<NumberField*>(c.get());n && n->label==L"Minimum") enabled |= n->enabled;
  Check(enabled,"minimum does not enable when controller changes");
  BrushPanel(true)->visible=false;
}
void LayerState() {
  NewDocument(); for(int i=0;i<25;i++) Command("layer.new.layer");
  auto a=ActiveDocumentId(); Layout(true); layerList->offset.Reset(300); layerList->Clamp();
  float scroll=layerList->offset.target; Check(scroll>10,"list not scrollable");
  NewDocument(); Layout(true); SwitchDocument(a); Drain(); Layout(true); ui::Animate(.5f);
  Check(std::abs(layerList->offset.target-scroll)<1,"layer scroll not restored by document");
  const char* keys[]={"transparency","pixels","position","artboard","all"};
  for(size_t i=0;i<5;i++) {
    auto* b=dynamic_cast<IconButton*>(layerLocks->children[i].get()); Check(b && b->enabled,"lock button unavailable");
    b->onClick(); Drain(); Check((*ActiveLayer())["locks"][keys[i]].Truthy(),"lock button not applied");
    b->onClick(); Drain(); Check(!(*ActiveLayer())["locks"][keys[i]].Truthy(),"lock button not cleared");
  }
}
void EffectState() {
  NewDocument();auto first=Rectangle();Command("layer.layerStyle.dropShadow","{}");
  auto expand=[](uint64_t id) {
    auto* row=Row(id);for(auto& c:row->children)if(auto* b=dynamic_cast<TextButton*>(c.get());b&&b->label==L"fx"){b->onClick();Drain();return;}
    throw std::runtime_error("fx badge missing");
  };
  expand(first);auto document=ActiveDocumentId();
  Check(layerList->children.size()>layerRows.size(),"effect did not expand");
  NewDocument();Rectangle();Command("layer.layerStyle.dropShadow","{}");
  Check(layerList->children.size()==layerRows.size(),"effect expansion leaked between documents");
  SwitchDocument(document);Drain();
  Check(layerList->children.size()>layerRows.size(),"effect expansion not restored for document");
}
void TranslatedNames() {
  i18n::Load("ru"); NewDocument();
  Check(docInfo["name"].Str().starts_with("Без имени"),"untitled document untranslated");
  for(auto id:{"layer.newAdjustmentLayer.curves","layer.newAdjustmentLayer.levels","layer.newFillLayer.solidColor"}) {
    Command(id); Check((*ActiveLayer())["name"].Str().find_first_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")==std::string::npos,"created layer name untranslated");
  }
  Check(i18n::Tr(L"Search")==L"Поиск","Search untranslated");
}
}
int main(int argc, char** argv) {

  int status = 0;
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  SetConsoleOutputCP(CP_UTF8);
  WNDCLASSW wc{};
  wc.lpfnWndProc = Proc;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.lpszClassName = L"VetroMilestone9Tests";
  RegisterClassW(&wc);
  hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, wc.lpszClassName, L"Vetro text tests", WS_OVERLAPPEDWINDOW, 0, 0,
                         1600, 1000, nullptr, nullptr, wc.hInstance, nullptr);
  ui::Init(hwnd);
  ui::SetBounds(R(0, 0, 1584, 961));
  if (!GfxCreate(hwnd, 1)) return 1;
  engine = pc_new();
  jobs::Init(hwnd, WM_JOBS);
  LoadCatalog();
  canvas = new Canvas(engine);
  ui::AddLayer(canvas);
  ui::AddLayer(new SelectionOverlay());
  if (GetEnvironmentVariableW(L"VETRO_TEST_GPU", nullptr, 0)) {
    uint32_t low = 0;
    int32_t high = 0;
    if (GfxAdapterLuid(&low, &high)) canvas->UseGpu(low, high);
  }
  BuildTools();
  BuildDocks();
  BuildShortcuts();
  ui::SetGate(InputAllowed);
  try {
    i18n::Load("ru");
    for (int depth : {8, 16, 32}) { EmptyText(depth); PatchSelection(depth); }
    TextControls();
    BrushControls();
    LayerState();
    EffectState();
    TranslatedNames();
    QueuedInput();
    if (argc > 1) {
      auto root = std::filesystem::path(Widen(argv[1]));
      std::filesystem::create_directories(root);
      NewDocument(); Command("type.create",R"({"x":20,"y":60,"text":"По центру","align":"center"})");
      TextPanelCommand("window.paragraph"); Drain();
      TextPanel(true)->visible = true;
      Layout(true);
      Capture(root / L"milestone9-paragraph-ru.png", false);
      TextPanel(true)->visible = false;
      BrushPanel(true)->visible = true;
      Layout(true);
      Capture(root / L"milestone9-brush-ru.png", false);
      BrushPanel(true)->visible = false;
      Capture(root / L"milestone9-layers-ru.png", false);
      NewDocument(); Rectangle(); Layout(true);canvas->Fit(false);
      Capture(root / L"milestone9-render-proof.png",true);
    }
    std::cout << checks << " regression checks passed\n";
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    status = 1;
  }
  FinishTextEditing();
  FinishParameterPreviews();
  FinishNudge();
  Drain();
  jobs::Shutdown();
  delete canvas;
  canvas = nullptr;
  pc_free(engine);
  engine = nullptr;
  GfxDestroy();
  DestroyWindow(hwnd);
  CoUninitialize();
  return status;
}
