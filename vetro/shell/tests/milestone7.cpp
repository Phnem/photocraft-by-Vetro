#define VETRO_MILESTONE2
#include "milestone1.cpp"
#include "../src/color_picker.h"
namespace {
void PickerChecks(const std::filesystem::path& root) {
  for(int depth:{8,16,32}) {
    NewDocument(depth);Command("tools.setColors",R"({"foreground":"#123456","background":"#abcdef"})");
    auto initial=JsonText(sessionInfo);auto history=pc_history_len(engine);
    OpenColorPicker(Rgba(.1f,.2f,.3f,1),[](D2D1_COLOR_F c){char b[16];snprintf(b,sizeof b,"#%02x%02x%02x",int(c.r*255+.5f),int(c.g*255+.5f),int(c.b*255+.5f));Exec("tools.setColors",JsonObject().Str("foreground",b).Text());},ui::Bounds());
    auto* dialog=ui::ActivePopover();Check(dialog!=nullptr,"custom color picker missing");
    auto* hex=dynamic_cast<TextField*>(dialog->children[15].get());Check(hex!=nullptr,"HEX field missing");hex->onChange(L"ff0000");
    Check(ColorPickerState()["hex"].Str()=="#ff0000","HEX did not update picker");
    Check(ColorPickerState()["outOfGamut"].Truthy(),"CMYK gamut warning missing");
    Check(JsonText(sessionInfo)==initial,"unconfirmed picker changed tool colors");
    auto* web=dynamic_cast<Toggle*>(dialog->children[16].get());web->onChange(true);hex->onChange(L"7b850c");
    Check(ColorPickerState()["hex"].Str()=="#669900","web-only quantization wrong");
    auto* l=dynamic_cast<NumberField*>(dialog->children[8].get());Check(l!=nullptr,"Lab fields missing");l->onCommit(50);
    Check(ColorPickerState()["rgba"].size()==4,"Lab conversion missing");
    auto* cmyk=dynamic_cast<NumberField*>(dialog->children[11].get());cmyk->onCommit(25);
    Check(ColorPickerState()["cmyk"].size()==4,"CMYK conversion missing");
    Key(VK_ESCAPE);ui::Animate(1);Check(pc_history_len(engine)==history && JsonText(sessionInfo)==initial,"picker cancellation changed document or colors");
  }
  bool applied=false;D2D1_COLOR_F picked{};
  OpenColorPicker(Rgba(0,0,0,1),[&](D2D1_COLOR_F c){applied=true;picked=c;},ui::Bounds());
  auto* dialog=ui::ActivePopover();auto* hex=dynamic_cast<TextField*>(dialog->children[15].get());hex->onChange(L"336699");
  Layout(true);ui::Animate(1);Capture(root/L"milestone7-picker-ru.png",false);
  Key(VK_RETURN);ui::Animate(1);Check(applied && std::abs(picked.g-.4f)<.001,"picker confirmation did not return target color");
  ColorField field(L"Color",Rgba(0,0,0,1),false);bool changed=false;field.onChange=[&](D2D1_COLOR_F){changed=true;};field.OnUp({},true);
  Check(ui::ActivePopover()!=nullptr,"shared color field still uses system picker");Key(VK_ESCAPE);Check(!changed && !field.set,"cancelled field changed its target");ui::Animate(1);
}
}
#include "panels7_cases.inc"
int main(int argc, char** argv) {

  int status = 0;
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  SetConsoleOutputCP(CP_UTF8);
  WNDCLASSW wc{};
  wc.lpfnWndProc = Proc;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.lpszClassName = L"VetroPanelTests";
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
    if(false) {}
    else {
    auto root=std::filesystem::path(argc>1?Widen(argv[1]):L".");std::filesystem::create_directories(root); PickerChecks(root); AssetPanels(root); ChannelsAndPaths(root); HistoryAndAnalysis(root);
    NewDocument();Rectangle();Layout(true);canvas->Fit(false);Capture(root/L"milestone7-render-proof.png",true);


    }
    std::cout << checks << " panel checks passed\n";
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

