#define VETRO_MILESTONE2
#include "milestone1.cpp"
#include <chrono>
#include <fstream>
namespace {
void RulersAndGuides() {
  NewDocument(); Tool("move");
  if(!GuideState()["rulers"].Truthy()) Key('R',{true});
  Layout(true); canvas->Fit(false);
  Check(GuideState()["rulers"].Truthy(),"Ctrl+R did not show rulers");
  auto free=canvas->free;
  PointerEvent e; e.x=free.left-15; e.y=free.top+30;
  auto* overlay=GuideOverlay(); Check(overlay->HitTest(e.x,e.y)==overlay,"left ruler not hittable");
  overlay->OnDown(e); overlay->OnDrag(Pointer(50,60)); overlay->OnUp(Pointer(50,60),true); Drain();
  Check(docInfo["guides"]["vertical"].size()==1,"ruler did not create vertical guide");
  Check(std::abs(docInfo["guides"]["vertical"][0].Num()-50)<.01,"guide coordinate wrong");
  auto history=pc_history_len(engine);
  overlay->OnDown(Pointer(50,60)); overlay->OnDrag(Pointer(80,60)); overlay->OnUp(Pointer(80,60),true); Drain();
  Check(pc_history_len(engine)==history+1,"moving a guide must make one history step");
  Check(std::abs(docInfo["guides"]["vertical"][0].Num()-80)<.01,"guide did not move");
  Key('Z',{true}); Check(std::abs(docInfo["guides"]["vertical"][0].Num()-50)<.01,"guide undo failed");
  Key(VK_OEM_1,{true,false,true});
  auto p=Pointer(50,60); Check(overlay->HitTest(p.x,p.y)==nullptr,"locked guide is hittable");
  Key(VK_OEM_1,{true,false,true});
  overlay->OnDown(p); Key(VK_ESCAPE); Check(!GuideState()["dragging"].Truthy(),"Escape did not cancel guide drag");
  overlay->OnDown(p); e.x=free.left-20;e.y=free.top+40;overlay->OnUp(e,true);Drain();
  Check(docInfo["guides"]["vertical"].size()==0,"drag outside did not delete guide");
  e.x=free.left+60;e.y=free.top-10;overlay->OnDown(e);overlay->OnUp(Pointer(60,40),true);Drain();
  Check(docInfo["guides"]["horizontal"].size()==1,"top ruler did not create horizontal guide");
  RunCommand("view.newGuideLayout",L"");
  auto* layout=ui::ActivePopover(); Check(layout!=nullptr,"guide layout dialog missing");
  auto* columns=dynamic_cast<NumberField*>(layout->children[1].get());
  auto* rows=dynamic_cast<NumberField*>(layout->children[2].get());
  Check(columns && rows,"guide layout omits column/row counts"); columns->Set(2);rows->Set(2);
  dynamic_cast<Toggle*>(layout->children[6].get())->Set(true);
  dynamic_cast<TextButton*>(layout->children[8].get())->onClick(); Drain();ui::Animate(1);
  Check(docInfo["guides"]["horizontal"].size()>=2,"layout dialog does not create rows");
  Check(docInfo["guides"]["vertical"].size()>=2,"guide layout not reflected in shell");
  Command("prefs.set",R"({"path":"unitsAndRulers.rulers","value":"cm"})");
  Check(docInfo["viewPreferences"]["units"].Str()=="cm","ruler units did not refresh");
  Check(docInfo["viewPreferences"]["unitPixels"][0].Num()>1,"ruler unit conversion missing");
  RunCommand("view.newGuide",L""); auto* dialog=ui::ActivePopover();
  dynamic_cast<SelectList*>(dialog->children[1].get())->index=1;
  dynamic_cast<NumberField*>(dialog->children[2].get())->Set(1);
  dynamic_cast<TextButton*>(dialog->children[4].get())->onClick();Drain();ui::Animate(1);
  Check(std::abs(docInfo["guides"]["vertical"].a.back().Num()-72/2.54)<.001,"new guide dialog did not convert ruler units");
  Key(VK_OEM_1,{true}); Check(!GuideState()["guides"].Truthy(),"Ctrl+; did not hide guides"); Key(VK_OEM_1,{true});
  e.x=free.left+60;e.y=free.top-10;e.button=1; overlay->OnDown(e);
  auto* menu=dynamic_cast<Menu*>(ui::ActivePopover()); Check(menu && menu->items.size()==7,"ruler unit menu missing");ui::ClosePopovers();ui::Animate(1);
  canvas->RotateTo(30,false); p=Pointer(docInfo["guides"]["vertical"][1].Num(),60);
  Check(overlay->HitTest(p.x,p.y)==overlay,"rotated guide hit test wrong"); canvas->RotateTo(0,false);
}
void Snapping() {
  NewDocument(); Tool("rectMarquee");
  Command("view.newGuide",R"({"orientation":"vertical","position":50})");
  Command("view.newGuide",R"({"orientation":"horizontal","position":40})");
  if(!GuideState()["snap"].Truthy()) Key(VK_OEM_1,{true,true});
  Layout(true); canvas->Fit(false); canvas->ZoomTo(1,false);
  Stroke(52,42,121,111);
  Check(docInfo["selectionBounds"][0].Num()==50 && docInfo["selectionBounds"][1].Num()==40,"marquee point did not snap to guides");
  Command("select.deselect");
  Command("prefs.set",R"({"values":{"guidesGridAndSlices.gridlineEvery":40,"guidesGridAndSlices.gridUnit":"pixels","guidesGridAndSlices.subdivisions":4}})");
  if(!GuideState()["grid"].Truthy()) Key(VK_OEM_7,{true});
  Stroke(72,72,123,123);
  Check(docInfo["selectionBounds"][0].Num()==70 && docInfo["selectionBounds"][1].Num()==70,"grid subdivision snapping wrong");
  Command("select.deselect"); if(GuideState()["grid"].Truthy()) Key(VK_OEM_7,{true});
  Rectangle(); Tool("move"); moveAutoSelect=false;
  auto old=(*ActiveLayer())["bounds"]; double dx=50-(old[0].Num()+old[2].Num());
  BeginMoveSnap(); auto offset=SnapMoveOffset(dx+2,0,{});
  Check(std::abs(offset.x-dx)<.01,"moving bounds edge did not snap to guide");
  auto raw=SnapMoveOffset(dx+2,0,{true}); Check(std::abs(raw.x-(dx+2))<.01,"Ctrl did not disable snapping");
  Stroke(30,40,30+dx+2,40);
  auto moved=(*ActiveLayer())["bounds"];
  Check(std::abs(moved[0].Num()+moved[2].Num()-50)<.01,"real Move gesture did not snap layer edge");
  moveAutoSelect=true;
  Command("prefs.set",R"({"values":{"guidesGridAndSlices.gridStyle":"dots","guidesGridAndSlices.guideStyle":"dashedLines"}})");
  Check(docInfo["viewPreferences"]["gridStyle"].Str()=="dots" && docInfo["viewPreferences"]["guideStyle"].Str()=="dashedLines","line styles not exposed");
  // A transform corner uses the same cached targets; cancelling keeps the source intact.
  NewDocument(); Rectangle(); Tool("move");
  Command("view.newGuide",R"({"orientation":"vertical","position":110})");
  Layout(true);canvas->ZoomTo(1,false);StartTransform();
  auto* transform=Mode();Check(transform!=nullptr,"transform did not start");
  transform->Down(50,50,Pointer(50,50));
  transform->Drag(108,100,Pointer(108,100));
  auto state=TransformState();
  Check(std::abs(state["quad"][2][0].Num()-110)<.01,"transform handle did not snap to guide");
  Key(VK_ESCAPE);Check(!Mode(),"transform cancel failed after snapping");
  if(GuideState()["snap"].Truthy()) Key(VK_OEM_1,{true,true});
}
void SnapBenchmark(const std::filesystem::path& root) {
  Command("file.new",R"({"width":6000,"height":4000,"background":"transparent"})");
  for(int i=0;i<32;i++) {
    auto p=JsonObject().Str("kind","rect").Add("rect","["+std::to_string(i*150)+",100,100,100]").Text();
    char* result=nullptr; Check(pc_execute(engine,"shape.create",p.c_str(),&result)==1,"benchmark shape creation failed");TakeString(result);
  }
  infoRevision=~0ull;Refresh();Tool("move");
  if(!GuideState()["snap"].Truthy())GuideCommand("view.snap");
  auto started=std::chrono::steady_clock::now();BeginMoveSnap();
  double capture=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
  started=std::chrono::steady_clock::now();
  for(int i=0;i<1000;i++)SnapMoveOffset(i%500,i%300,{});
  double queries=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
  std::filesystem::create_directories(root);
  std::ofstream report(root / L"snap-perf.txt");
  report<<"6000x4000, 32 vector layers\nCapture targets: "<<capture<<" ms\n1000 pointer queries: "<<queries<<" ms; mean "<<queries/1000<<" ms\n";
  std::cout<<"snap capture "<<capture<<" ms; query mean "<<queries/1000<<" ms\n";
}
void CanvasProperties() {
  NewDocument(); Command("select.deselectLayers");
  Check(!ActiveLayer() && documentProperties->visible,"document properties hidden without layer selection");
  Check(documentFields[0]->value==200 && documentFields[1]->value==160,"canvas dimensions not displayed");
  auto history=pc_history_len(engine); documentFields[2]->onCommit(300); Drain();
  Check(docInfo["resolution"].Num()==300 && docInfo["width"].Num()==200,"resolution changed pixel size");
  Check(pc_history_len(engine)==history+1,"resolution edit must be undoable");
  documentFields[0]->onCommit(240); Drain(); Check(docInfo["width"].Num()==240,"width property not applied");
  if(!GuideState()["grid"].Truthy()) documentGrid->onChange(true);
  Check(GuideState()["grid"].Truthy(),"document grid toggle not connected");
  Key(VK_OEM_7,{true});Layout(true);
  Check(documentGrid->on==GuideState()["grid"].Truthy(),"document grid toggle did not follow keyboard change");
  if(!GuideState()["grid"].Truthy())Key(VK_OEM_7,{true});
  auto* action=dynamic_cast<TextButton*>(documentProperties->children.back().get());action->onClick();
  Check(ui::AnyPopoverOpen(),"guide layout quick action did not open dialog");
  history=pc_history_len(engine); Key(VK_ESCAPE);ui::Animate(1);
  Check(!ui::AnyPopoverOpen() && pc_history_len(engine)==history,"cancelled guide layout changed document");
  Command("layer.new.layer");Check(!documentProperties->visible,"canvas fields remain visible on selected layer");
  Command("select.deselectLayers");
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
  wc.lpszClassName = L"VetroViewTests";
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
    if(argc>2 && std::string(argv[2])=="--snap-benchmark-only") SnapBenchmark(std::filesystem::path(Widen(argv[1])));
    else {
    RulersAndGuides();
    Snapping();
    CanvasProperties();
    for(auto size:{std::pair{5120.f,1440.f},{7680.f,2160.f},{300.f,200.f},{1584.f,961.f}}) {
      ui::SetBounds(RWH(0,0,size.first,size.second));Layout(true);
      Check(W(canvas->free)>0 && H(canvas->free)>0,"ruler layout produced an inverted viewport");
      Frame();
    }
    if (argc > 1) {
      auto root=std::filesystem::path(Widen(argv[1])); std::filesystem::create_directories(root);
      i18n::Load("ru");
      Command("view.newGuideLayout",R"({"columns":3,"rows":3})");
      Layout(true); canvas->Fit(false);ui::Animate(1);
      Capture(root / L"milestone6-guides-properties-ru.png",false);
      if(GuideState()["grid"].Truthy())GuideCommand("view.show.grid");
      NewDocument(); Rectangle(); Layout(true);canvas->Fit(false);
      Capture(root / L"milestone6-render-proof.png",true);
    }
    }
    std::cout << checks << " view checks passed\n";
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
