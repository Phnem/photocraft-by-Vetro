// Reproductions of defects reported against the original PhotoCraft (storytold/photocraft) and of
// suspected defects of milestones 4-5, run against the real shell handlers. Not part of ctest: each
// probe prints REPRODUCED or "not reproduced" with the evidence, and never fails the run. A probe
// that reproduces becomes a regression check when its ticket is fixed.
#define VETRO_MILESTONE2
#include "milestone1.cpp"
#include <chrono>

namespace {
void Report(const char* id, bool reproduced, const std::string& note) {
  std::cout << id << ' ' << (reproduced ? "REPRODUCED" : "not reproduced") << " - " << note << '\n';
}
template <class F> void Probe(const char* id, F f) {
  try {
    f();
  } catch (const std::exception& e) {
    std::cout << id << " PROBE-ERROR - " << e.what() << '\n';
  }
}
double Ms(std::chrono::steady_clock::time_point since) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - since).count();
}
NumberField* Field(const std::wstring& label) {
  for (auto* w : optionWidgets)
    if (auto* n = dynamic_cast<NumberField*>(w); n && n->label == label) return n;
  throw std::runtime_error("numeric field missing: " + Narrow(label));
}
void Set(const std::wstring& label, double v) {
  auto* n = Field(label);
  n->Set(v);
  n->onChange(v);
  if (Mode()) Mode()->Tick();
  FlushParameterPreviews();
  Drain();
}
Json Quad() { return TransformState()["quad"]; }
void Patch() { Command("shape.create", R"({"kind":"rect","rect":[20,30,60,40],"fill":"#ff0000"})"); }

void PasteWhileBusy() {  // #5526
  NewDocument();
  Rectangle("Source");
  auto a = ActiveDocumentId();
  NewDocument();
  auto b = ActiveDocumentId();
  auto before = docInfo["layers"].size();
  SwitchDocument(a);
  Drain();
  HandleKey('C', {true});
  Drain();
  SwitchDocument(b);
  Drain();
  Check(ActiveDocumentId() == b, "second document is not active");
  jobs::Run([] { Sleep(400); }, [] {}, L"Rendering");  // the engine is busy, as after switching to a large PSD
  HandleKey('V', {true});
  Drain();
  bool dropped = docInfo["layers"].size() == before;
  Report("#5526", dropped, std::string("Ctrl+V pressed while the engine was busy ") + (dropped ? "was dropped (no layer pasted, no retry)" : "pasted"));
}
void LayerScroll() {  // #5522
  NewDocument();
  for (int i = 0; i < 25; i++) Command("layer.new.layer");
  auto a = ActiveDocumentId();
  Layout(true);
  layerList->offset.Reset(300);
  layerList->Clamp();
  Layout(true);
  float before = layerList->offset.target;
  NewDocument();
  Layout(true);
  ui::Animate(.5f);
  SwitchDocument(a);
  Drain();
  Layout(true);
  ui::Animate(.5f);
  float after = layerList->offset.target;
  if (before <= 10) throw std::runtime_error("layer list was not scrollable in the test window: " + JsonNum(before));
  Report("#5522", after < before - 1, "scroll before the tab switch " + JsonNum(before) + ", after coming back " + JsonNum(after));
}
void BlendDuringTransform() {  // #5513
  NewDocument();
  Rectangle();
  Command("layer.setProps", R"({"blend":"Multiply"})");
  StartTransform();
  Set(L"X", Field(L"X")->value + 10);
  std::string during = (*ActiveLayer())["blend"].Str();
  Key(VK_ESCAPE);
  Report("#5513", during != "Multiply", "blend of the layer during the transform preview: " + during);
}
void ReferencePointScale() {  // #5510
  NewDocument();
  Patch();
  StartTransform();
  auto e = Pointer(30, 40, {false, false, true});  // Alt+click inside the frame moves the reference point
  canvas->OnDown(e);
  canvas->OnUp(e, true);
  Drain();
  Set(L"W", 200);
  auto q = Quad();
  // Scaling by 2 about (30,40): the corner (20,30) must land on (10,20).
  bool fixed = std::abs(q[0][0].Num() - 10) < .01 && std::abs(q[0][1].Num() - 20) < .01;
  Key(VK_ESCAPE);
  Report("#5510", !fixed, "corner (20,30) after scaling 200% about the reference point (30,40), expected [10,20]: " + JsonText(q[0]));
}
void SelectionFollowsTransform() {  // #5297
  NewDocument();
  Command("select.rect", R"({"x":40,"y":40,"width":20,"height":10})");
  Command("edit.fill", R"({"color":"#ff0000"})");
  auto before = docInfo["selectionBounds"];
  StartTransform();
  Set(L"X", Field(L"X")->value + 30);
  Key(VK_RETURN);
  auto after = docInfo["selectionBounds"];
  Report("#5297", JsonText(before) == JsonText(after), "selection bounds before " + JsonText(before) + ", after moving the selected pixels by 30 px: " + JsonText(after));
}
void EmptyTextLayer() {  // V1
  NewDocument();
  Tool("type");
  auto layers = docInfo["layers"].size();
  auto e = Pointer(20, 50);
  canvas->OnDown(e);
  canvas->OnUp(e, true);
  Drain();
  Key(VK_ESCAPE);
  Drain();
  Report("V1 empty text layer", docInfo["layers"].size() == layers + 1 && Command("type.info")["text"].Str().empty(),
         "layers " + std::to_string(layers) + " -> " + std::to_string(docInfo["layers"].size()) + " after click + Esc");
}
void SmartFilterBlend() {  // V3
  NewDocument();
  Rectangle();
  Command("layer.rasterize.layer");
  Command("filter.convertForSmartFilters");
  Command("filter.blur.gaussianBlur", R"({"radius":2})");
  auto id = docInfo["activeLayer"].Int();
  Command("layer.smartFilter.blendingOptions", JsonObject().Num("layer", double(id)).Num("index", 0).Str("blend", "softLight").Text());
  std::string was = (*ActiveLayer())["smartFilters"][0]["blend"].Str();
  Layout(true);
  ui::Animate(.1f);
  Frame();
  auto* filter = layerList->children[1].get();
  PointerEvent e;
  e.x = filter->rect.right - 15;
  e.y = MidY(filter->rect);
  filter->OnUp(e, true);
  auto* dialog = dynamic_cast<ParamDialog*>(ui::ActivePopover());
  Check(dialog != nullptr, "smart filter blend dialog did not open");
  for (auto& c : dialog->form->children)
    if (auto* s = dynamic_cast<Slider*>(c.get())) {
      s->Set(.25);
      s->onChange(.25);
    }
  dialog->Finish(true);
  Drain();
  std::string now = (*ActiveLayer())["smartFilters"][0]["blend"].Str();
  Report("V3 smart filter blend", was != now, "blend before editing only the opacity: '" + was + "', after: '" + now + "'");
}
void BlendNameMatch() {  // V9: the shell compares its own mode names with the engine's
  NewDocument();
  Rectangle();
  std::string wrong;
  int count = 0;
  for (auto& name : BlendNames) {
    Command("layer.setProps", JsonObject().Str("blend", Narrow(name)).Text());
    std::string engineName = (*ActiveLayer())["blend"].Str();
    if (engineName != Narrow(name)) {
      count++;
      wrong += Narrow(name) + "→" + engineName + "; ";
    }
  }
  Report("V9 blend names", count > 0, std::to_string(count) + " of " + std::to_string(BlendNames.size()) + " modes differ between BlendNames and the engine: " + wrong);
}
void BrushGroups() {  // V10: the group list of the Brushes panel
  auto* panel = BrushPanel(false);
  bool wasVisible = panel->visible;
  if (!wasVisible) ToolPanelCommand("window.brushes");
  Drain();
  auto* groups = dynamic_cast<SelectList*>(panel->body->children[1].get());
  size_t empty = 0;
  if (groups) for (auto& label : groups->options) empty += label.empty();
  Report("V10 brush groups", !groups || empty > 0, std::to_string(empty) + " empty visible group rows");
  panel->visible = wasVisible;
}
void PickerCosts() {  // V4, V5
  NewDocument();
  Tool("type");
  Layout(true);
  size_t families = Command("type.fonts")["families"].size();
  TextButton* font = nullptr;
  for (auto* w : optionWidgets)
    if (auto* b = dynamic_cast<TextButton*>(w)) {
      font = b;
      break;
    }
  Check(font != nullptr, "font button missing");
  auto start = std::chrono::steady_clock::now();
  font->onClick();
  Drain();
  for (int i = 0; i < 3; i++) Frame();
  double fonts = Ms(start);
  ui::ClosePopovers();
  Report("V4 font picker", fonts > 500, std::to_string(families) + " families, open + 3 frames: " + JsonNum(fonts) + " ms (every row renders its preview on first paint)");
  Tool("brush");
  size_t presets = Command("brush.presets.list", R"({"full":true})")["presets"].size();
  auto e = Pointer(80, 80);
  e.button = 1;
  start = std::chrono::steady_clock::now();
  CanvasContext(80, 80, e);
  Drain();
  for (int i = 0; i < 3; i++) Frame();
  double brushes = Ms(start);
  ui::ClosePopovers();
  Report("V5 brush palette", brushes > 500, std::to_string(presets) + " presets, open + 3 frames: " + JsonNum(brushes) + " ms");
}
void PatchNewSelection() {  // V8
  NewDocument();
  Rectangle();
  Command("layer.rasterize.layer");
  Command("select.rect", R"({"x":20,"y":20,"width":25,"height":25})");
  Tool("patch");
  auto history = pc_history_len(engine);
  auto before = docInfo["selectionBounds"];
  Stroke(100, 100, 130, 130);
  bool patched = pc_history_len(engine) > history && JsonText(before) == JsonText(docInfo["selectionBounds"]);
  Report("V8 patch outside the selection", patched, std::string("dragging from outside the selection ") + (patched ? "moved the patch" : "drew a new selection"));
}
void ParagraphPanel(const std::filesystem::path& root) {  // V2
  NewDocument();
  Command("type.create", R"({"x":20,"y":60,"text":"Centered","size":24,"align":"center"})");
  Tool("type");
  TextPanelCommand("window.paragraph");
  Layout(true);
  Drain();
  Capture(root / L"probe-paragraph-panel.png", false);
  TextPanelCommand("window.paragraph");
}
}  // namespace

int main(int argc, char** argv) {
  int status = 0;
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  SetConsoleOutputCP(CP_UTF8);
  WNDCLASSW wc{};
  wc.lpfnWndProc = Proc;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.lpszClassName = L"VetroIssueProbe";
  RegisterClassW(&wc);
  hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, wc.lpszClassName, L"Vetro issue probe", WS_OVERLAPPEDWINDOW, 0, 0, 1600, 1000, nullptr, nullptr, wc.hInstance, nullptr);
  ui::Init(hwnd);
  ui::SetBounds(R(0, 0, 1584, 961));
  if (!GfxCreate(hwnd, 1)) return 1;
  engine = pc_new();
  jobs::Init(hwnd, WM_JOBS);
  LoadCatalog();
  canvas = new Canvas(engine);
  ui::AddLayer(canvas);
  ui::AddLayer(new SelectionOverlay());
  BuildTools();
  BuildDocks();
  BuildShortcuts();
  ui::SetGate(InputAllowed);
  std::filesystem::path root = argc > 1 ? std::filesystem::path(Widen(argv[1])) : std::filesystem::path(L".");
  std::filesystem::create_directories(root);
  try {
    i18n::Load("ru");
    Probe("#5526", PasteWhileBusy);
    Probe("#5522", LayerScroll);
    Probe("#5513", BlendDuringTransform);
    Probe("#5510", ReferencePointScale);
    Probe("#5297", SelectionFollowsTransform);
    Probe("V1", EmptyTextLayer);
    Probe("V3", SmartFilterBlend);
    Probe("V9", BlendNameMatch);
    Probe("V10", BrushGroups);
    Probe("V4/V5", PickerCosts);
    Probe("V8", PatchNewSelection);
    Probe("V2", [&] { ParagraphPanel(root); });
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
