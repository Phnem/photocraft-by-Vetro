#define VETRO_MILESTONE2
#include "milestone1.cpp"
namespace {
struct TextDesktop {
  HWINSTA oldStation = GetProcessWindowStation(), station = nullptr;
  HDESK oldDesktop = GetThreadDesktop(GetCurrentThreadId()), desktop = nullptr;
  TextDesktop() {
    station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
    if (!station || !SetProcessWindowStation(station)) throw std::runtime_error("private clipboard station failed");
    desktop = CreateDesktopW(L"VetroTextTests", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desktop || !SetThreadDesktop(desktop)) throw std::runtime_error("private desktop failed");
  }
  ~TextDesktop() {
    SetThreadDesktop(oldDesktop);
    SetProcessWindowStation(oldStation);
    if (desktop) CloseDesktop(desktop);
    if (station) CloseWindowStation(station);
  }
};
void TextFlush() {
  if (canvas->tool) canvas->tool->Tick();
  Drain();
  if (canvas->tool) canvas->tool->Tick();
  Drain();
}
void SelectionPixels() {
  TextFlush();
  for (int i = 0; i < 8; i++) {
    jobs::Pump();
    Frame();
    Sleep(2);
  }
  auto state = TextEditorState();
  auto cluster = state["geometry"]["clusters"][0]["poly"];
  Check(cluster.size() == 4, "selection quadrilateral missing");
  double x = 0, y = 0;
  for (auto& p : cluster.a) {
    x += p[0].Num() / 4;
    y += p[1].Num() / 4;
  }
  auto at = canvas->ToScreen(x, y);
  Microsoft::WRL::ComPtr<ID2D1Image> target;
  Dc()->GetTarget(&target);
  Microsoft::WRL::ComPtr<ID2D1Bitmap1> source, cpu;
  Check(SUCCEEDED(target.As(&source)), "selection framebuffer unavailable");
  auto size = source->GetPixelSize();
  auto props =
      D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, source->GetPixelFormat());
  Check(SUCCEEDED(Dc()->CreateBitmap(size, nullptr, 0, &props, &cpu)), "selection readback allocation failed");
  Check(SUCCEEDED(cpu->CopyFromBitmap(nullptr, source.Get(), nullptr)), "selection readback copy failed");
  D2D1_MAPPED_RECT map{};
  Check(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ, &map)), "selection framebuffer map failed");
  size_t offset = size_t(std::lround(at.y)) * map.pitch + size_t(std::lround(at.x)) * 4;
  bool visible = map.bits[offset + 2] > map.bits[offset + 1] + 15;
  cpu->Unmap();
  Check(visible, "selection must actually fill the canvas with its tint");
}
void Type(const std::wstring& s) {
  for (auto c : s) Check(TextEditorChar(c), "character not routed to canvas text");
  TextFlush();
}
void Click(double x, double y) {
  auto e = Pointer(x, y);
  canvas->OnDown(e);
  canvas->OnUp(e, true);
  Drain();
  TextFlush();
}
void TextEntry(int depth) {
  NewDocument(depth);
  Tool("type");
  Click(20, 50);
  Check(TextEditing(), "click did not start text editor");
  uint64_t history = pc_history_len(engine);
  Type(L"Привет 🙂");
  Key(VK_RETURN);
  Type(L"second line");
  Check(TextEditorState()["text"].Str() == "Привет 🙂\nsecond line", "Unicode/newline input lost");
  Check(TextEditorState()["geometry"]["carets"].size() == 21, "scalar caret geometry count mismatch");
  Key('A', {true, false, false});
  SelectionPixels();
  Key('C', {true, false, false});
  Key(VK_END, {true, false, false});
  Key('V', {true, false, false});
  TextFlush();
  Check(TextEditorState()["text"].Str() == "Привет 🙂\nsecond lineПривет 🙂\nsecond line",
        "canvas Unicode clipboard paste failed");
  Key('Z', {true, false, false});
  TextFlush();
  Check(TextEditorState()["text"].Str() == "Привет 🙂\nsecond line", "local typing undo failed");
  Key(VK_HOME);
  Check(TextEditorState()["caret"].Int() == 9, "Home must reach line start");
  Key(VK_END);
  Check(TextEditorState()["caret"].Int() == 20, "End must reach line end");
  Key(VK_RETURN, {true, false, false});
  Check(!TextEditing(), "Ctrl Enter must commit");
  Check(pc_history_len(engine) == history + 1, "typing must commit one history entry");
  auto info = Command("type.info");
  Check(info["text"].Str() == "Привет 🙂\nsecond line", "committed text differs");
  Undo();
  Drain();
  Check(Command("type.info")["text"].Str().empty(), "typing Undo failed");
  Undo(-1);
  Drain();
  auto geometry = Json::Parse(TakeString(pc_text_geometry(engine, uint64_t(docInfo["activeLayer"].Int()))));
  Check(geometry["clusters"].size() > 5, "engine geometry clusters missing");
  auto line = geometry["carets"][2];
  double x = (line[0][0].Num() + line[1][0].Num()) / 2, y = (line[0][1].Num() + line[1][1].Num()) / 2;
  Click(x, y);
  Check(TextEditing(), "click existing text did not edit");
  Key('A', {true, false, false});
  Type(L"Changed");
  Key(VK_ESCAPE);
  Check(Command("type.info")["text"].Str() == "Changed", "Escape must finish editing");
  Tool("type");
  Stroke(20, 70, 180, 145);
  Check(TextEditing(), "paragraph drag did not start editing");
  Type(L"A paragraph of text that wraps inside its rectangle.");
  Key(VK_RETURN, {true, false, false});
  info = Command("type.info");
  Check(!info["shape"]["Box"].IsNull() && info["lines"].size() > 1, "paragraph box did not wrap");
}
void PendingTextOnDocumentChange() {
  NewDocument();
  Tool("type");
  Click(20, 50);
  auto history = pc_history_len(engine);
  Type(L"Pending");
  Check(TextEditorChar(L'!'), "pending final character not accepted");
  PrepareDocumentChange();
  Drain();
  Check(!TextEditing(), "document change must finish the editor");
  Check(Command("type.info")["text"].Str() == "Pending!", "document change lost unflushed characters");
  Check(pc_history_len(engine) == history + 1, "finishing pending text must commit once");
}
void MixedStyle() {
  NewDocument();
  auto id = Command("type.create", "{\"x\":20,\"y\":60,\"text\":\"ABcd\",\"size\":24}")["layer"].Int();
  Command("type.setStyle", "{\"range\":[0,2],\"fauxBold\":true}");
  Tool("type");
  auto geometry = Json::Parse(TakeString(pc_text_geometry(engine, uint64_t(id))));
  auto line = geometry["carets"][2];
  Click((line[0][0].Num() + line[1][0].Num()) / 2, (line[0][1].Num() + line[1][1].Num()) / 2);
  Type(L"!");
  Key(VK_ESCAPE);
  auto info = Command("type.info");
  Check(info["text"].Str() == "AB!cd", "insertion at exact caret failed");
  Check(info["runs"].size() > 1 && info["runs"][0]["style"]["faux_bold"].Truthy(),
        "text insertion lost existing style runs");
  Tool("verticalType");
  Click(150, 30);
  Type(L"縦書き");
  Key(VK_ESCAPE);
  Check(Command("type.info")["orientation"].Str() == "Vertical", "vertical type orientation lost");
}
}  // namespace
int main(int argc, char** argv) {
  TextDesktop privateDesktop;
  int status = 0;
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  SetConsoleOutputCP(CP_UTF8);
  WNDCLASSW wc{};
  wc.lpfnWndProc = Proc;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.lpszClassName = L"VetroTextTests";
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
    for (int depth : {8, 16, 32}) TextEntry(depth);
    MixedStyle();
    PendingTextOnDocumentChange();
    if (argc > 1) {
      auto root = std::filesystem::path(Widen(argv[1]));
      std::filesystem::create_directories(root);
      i18n::Load("ru");
      NewDocument();
      Tool("type");
      Click(15, 65);
      Type(L"Текст на холсте");
      Key(VK_RETURN);
      Type(L"PhotoCraft by Vetro");
      Key('A', {true, false, false});
      Layout(true);
      canvas->Fit(false);
      Sleep(3000);
      Capture(root / L"milestone5-text-selection-ru.png", false);
      TextPanelCommand("window.character");
      Layout(true);
      Sleep(3000);
      Capture(root / L"milestone5-character-ru.png", false);
      TextPanelCommand("window.character");
      Key(VK_ESCAPE);
      NewDocument();
      Rectangle();
      Tool("type");
      Click(90, 110);
      Type(L"GPU text");
      Key(VK_ESCAPE);
      Layout(true);
      canvas->Fit(false);
      Capture(root / L"milestone5-render-proof.png", true);
    }
    std::cout << checks << " text checks passed\n";
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
