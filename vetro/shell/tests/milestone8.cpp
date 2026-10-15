#define VETRO_MILESTONE2
#include "milestone1.cpp"
#include "../src/workspace_editors.h"
#include "workspace8_cases.inc"
int main(int argc, char** argv) {

  int status = 0;
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  SetConsoleOutputCP(CP_UTF8);
  WNDCLASSW wc{};
  wc.lpfnWndProc = Proc;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.lpszClassName = L"VetroWorkspaceTests";
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
    auto root=std::filesystem::path(argc>1?Widen(argv[1]):L".");std::filesystem::create_directories(root);
    ScreenAndQuickMask(root);ContextTasks(root);SelectMaskWorkspace(root);LiquifyUndo(root);
    NewDocument();Rectangle();Layout(true);canvas->Fit(false);Capture(root/L"milestone8-render-proof.png",true);
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

