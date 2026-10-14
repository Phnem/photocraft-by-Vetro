#define VETRO_MILESTONE2
#include "milestone1.cpp"
#include <fstream>
#include <chrono>

namespace {
void Brushes() {
  NewDocument();
  Tool("brush");
  auto rows = BuildMenu("Window");
  bool paletteEntry = false, settingsEntry = false;
  for (auto& r : rows) {
    if (r.label == L"Brushes" && r.enabled) paletteEntry = true;
    if (r.label == L"Brush Settings" && r.enabled) settingsEntry = true;
  }
  Check(paletteEntry && settingsEntry, "brush panels are absent from Window menu");
  auto e = Pointer(80, 80);
  e.button = 1;
  CanvasContext(80, 80, e);
  Drain();
  auto* p = ui::ActivePopover();
  Check(p && dynamic_cast<Menu*>(p) == nullptr, "brush right click must open tip palette");
  auto* size = dynamic_cast<NumberField*>(p->children[0].get());
  Check(size && size->value == brush.size && size->min == .1 && size->max == 5000, "brush size bounds/default");
  size->Set(45);
  size->onChange(45);
  Drain();
  Check(Command("brush.get")["size"].Num() == 45, "palette size not sent");
  auto* list = dynamic_cast<ScrollArea*>(p->children[2].get());
  Check(list && list->children.size() > 3, "tip preset list empty");
  auto* row = list->children[1].get();
  row->OnUp(e, true);
  Drain();
  Check(Command("brush.get")["size"].Num() == brush.size, "preset size not synchronized into stroke settings");
  ui::ClosePopovers();
  auto settings = Command("brush.get");
  std::vector<uint8_t> px(48 * 48 * 4);
  Check(pc_brush_tip(JsonText(settings).c_str(), 48, px.data(), px.size(), 48 * 4) == 1, "actual tip preview failed");
  int transparent = 0, opaque = 0;
  for (size_t i = 3; i < px.size(); i += 4) {
    transparent += px[i] == 0;
    opaque += px[i] > 0;
  }
  Check(transparent && opaque, "tip thumbnail has no silhouette");
  Check(pc_brush_tip("{}", 300, px.data(), px.size(), 48 * 4) == 0, "tip preview size must be bounded");
  RunCommand("window.brushSettings", L"");
  Drain();
  Layout(true);
  RunCommand("window.brushSettings", L"");
}
void MasksAndLocks() {
  NewDocument();
  auto id = Rectangle();
  Command("layer.layerMask.revealAll");
  auto* row = Row(id);
  Check(row->children.size() >= 3, "mask thumbnail missing");
  auto* mask = row->children[2].get();
  PointerEvent e;
  e.x = MidX(mask->rect);
  e.y = MidY(mask->rect);
  e.mods.shift = true;
  mask->OnUp(e, true);
  Drain();
  Check(!(*ActiveLayer())["mask"]["enabled"].Truthy(), "Shift mask click must disable");
  row = Row(id);
  mask = row->children[2].get();
  e.mods = {};
  mask->OnUp(e, true);
  Drain();
  e.mods.alt = true;
  Row(id)->children[2]->OnUp(e, true);
  Drain();
  auto color = Sample(40, 40);
  Check(color[0] > .99 && color[1] > .99 && color[2] > .99, "Alt mask view must show white grayscale");
  Command("view.layerMask", "{\"mode\":\"off\"}");
  canvas->Reload();
  Drain();
  color = Sample(40, 40);
  Check(color[0] > .99 && color[1] < .01, "mask view changed source content");
  Command("layer.lockLayers", "{\"pixels\":true,\"position\":true}");
  Check((*ActiveLayer())["locks"]["pixels"].Truthy() && (*ActiveLayer())["locks"]["position"].Truthy(),
        "lock inspector missing");
  Command("layer.lockLayers", "{\"all\":false,\"pixels\":false,\"position\":false}");
  Command("layer.setLabelColor", "{\"color\":\"red\"}");
  Check((*ActiveLayer())["labelColor"].Str() == "red", "color label missing");
}
void Adjustment() {
  NewDocument();
  Rectangle();
  auto result = Command("layer.newAdjustmentLayer.brightnessContrast");
  Check(adjustmentProperties->visible, "adjustment properties did not open");
  auto* form = dynamic_cast<ParamForm*>(adjustmentProperties->children[0].get());
  Check(form && form->fields.size() > 1, "adjustment controls absent");
  uint64_t before = pc_history_len(engine);
  form->fields[0].def = 50;  // the widget remains the source of values
  auto* slider = dynamic_cast<Slider*>(form->children[0].get());
  Check(slider != nullptr, "brightness slider absent");
  slider->Set(40);
  slider->onChange(40);
  FlushParameterPreviews();
  Drain();
  slider->Set(60);
  slider->onChange(60);
  FlushParameterPreviews();
  Drain();
  FinishParameterPreviews();
  Drain();
  Check(pc_history_len(engine) == before + 1, "adjustment drag must be one history step");
  Check((*ActiveLayer())["adjustment"]["brightness"].Num() == 60, "adjustment live value not stored");
  Undo();
  Drain();
  Check((*ActiveLayer())["adjustment"]["brightness"].Num() == 0, "adjustment undo failed");
  Command("layer.newAdjustmentLayer.levels");
  auto lists = [] {
    std::vector<SelectList*> values;
    for (auto& w : adjustmentProperties->children)
      if (auto* s = dynamic_cast<SelectList*>(w.get())) values.push_back(s);
    return values;
  };
  Check(lists().size() == 2, "levels channels and presets absent");
  lists().front()->onChange(1);
  lists().back()->onChange(2);
  Drain();
  Check((*ActiveLayer())["adjustment"]["red"]["inBlack"].Num() == 25 &&
            (*ActiveLayer())["adjustment"]["inBlack"].Num() == 0,
        "levels preset must affect the selected channel");
  lists().back()->onChange(0);
  Drain();
  Check((*ActiveLayer())["adjustment"]["inBlack"].Num() == 0 &&
            (*ActiveLayer())["adjustment"]["red"]["inBlack"].Num() == 25,
        "composite default must preserve other channels");
  lists().front()->onChange(1);
  lists().back()->onChange(0);
  Drain();
  Check((*ActiveLayer())["adjustment"]["red"]["inBlack"].Num() == 0, "Default preset did not reset levels");
  std::string pencil = "[";
  for (int i = 0; i < 256; i++) {
    if (i) pencil += ',';
    pencil += std::to_string(255 - i);
  }
  pencil += ']';
  Command("layer.newAdjustmentLayer.curves", "{\"pencil\":true,\"points\":" + pencil + "}");
  CurveEditor* plotted = nullptr;
  for (auto& w : adjustmentProperties->children)
    if (auto* c = dynamic_cast<CurveEditor*>(w.get())) plotted = c;
  Check(plotted && plotted->points.size() <= 19 && std::abs(plotted->Eval(64) - 191) < 2,
        "pencil curves must show their fitted point curve");
  plotted->points[0].y = 200;
  plotted->onChange();
  FlushParameterPreviews();
  Drain();
  FinishParameterPreviews();
  Drain();
  Check(
      !(*ActiveLayer())["adjustment"]["pencil"].Truthy() && (*ActiveLayer())["adjustment"]["points"][0][1].Num() == 200,
      "editing a pencil curve must use the point parameter format");
  Command("layer.newAdjustmentLayer.colorBalance");
  NumberField* numeric = nullptr;
  for (auto& w : adjustmentProperties->children)
    if (auto* n = dynamic_cast<NumberField*>(w.get())) {
      numeric = n;
      break;
    }
  Check(numeric != nullptr, "color-balance channel controls missing");
  numeric->onChange(35);
  FlushParameterPreviews();
  Drain();
  FinishParameterPreviews();
  Drain();
  bool found = false;
  for (auto& kv : (*ActiveLayer())["adjustment"].o)
    if (kv.second.kind == Json::Array && kv.second[0].Num() == 35) found = true;
  Check(found, "numeric array adjustment was not stored");
}
void DetailsAndRetouch(int depth) {
  NewDocument(depth);
  auto id = Rectangle();
  Command("layer.layerStyle.dropShadow", "{\"distance\":3,\"size\":2}");
  auto* row = Row(id);
  Check((*ActiveLayer())["effects"].size() == 1, "effects inspector missing");
  TextButton* fx = nullptr;
  for (auto& c : row->children)
    if (auto* button = dynamic_cast<TextButton*>(c.get())) fx = button;
  Check(fx != nullptr, "fx badge missing");
  PointerEvent e;
  e.x = MidX(fx->rect);
  e.y = MidY(fx->rect);
  fx->OnUp(e, true);
  Drain();
  Layout(true);
  ui::Animate(.1f);
  Check(layerList->children.size() > layerRows.size(), "expanded effect rows missing");
  auto* effect = layerList->children[1].get();
  e.x = effect->rect.left + 46;
  e.y = MidY(effect->rect);
  effect->OnUp(e, true);
  Drain();
  Check(!(*ActiveLayer())["effects"][0]["visible"].Truthy(), "effect eye did not disable effect");
  auto target = Rectangle();
  Layout(true);
  ui::Animate(.1f);
  Frame();
  row = Row(id);
  fx = nullptr;
  for (auto& c : row->children)
    if (auto* button = dynamic_cast<TextButton*>(c.get())) fx = button;
  Check(fx != nullptr, "source fx badge lost after adding target");
  auto* targetRow = Row(target);
  Frame();
  // Hidden test docks can still be in their reveal delay; register this real row's painted bounds.
  targetRow->Paint();
  e.x = MidX(fx->rect);
  e.y = MidY(fx->rect);
  fx->OnDown(e);
  e.x = MidX(targetRow->rect);
  e.y = MidY(targetRow->rect);
  e.mods.alt = true;
  fx->OnDrag(e);
  fx->OnUp(e, false);
  Drain();
  if ((*ActiveLayer())["effects"].size() != 1)
    throw std::runtime_error("Alt fx drag: source=" + std::to_string(id) + " target=" + std::to_string(target) +
                             " active=" + std::to_string(docInfo["activeLayer"].Int()) + " at=" + JsonNum(e.x) + "," +
                             JsonNum(e.y) + " error=" + LastError() + " layers=" + JsonText(docInfo["layers"]));
  Check((*ActiveLayer())["effects"].size() == 1, "Alt fx drag failed to copy effects");
  Command("layer.select", JsonObject().Num("layer", double(id)).Text());
  Check((*ActiveLayer())["effects"].size() == 1, "Alt fx drag removed source effects");
  Command("layer.select", JsonObject().Num("layer", double(target)).Text());
  Command("layer.rasterize.layer");
  Command("filter.convertForSmartFilters");
  Command("filter.blur.gaussianBlur", "{\"radius\":2}");
  Command("filter.blur.gaussianBlur", "{\"radius\":4}");
  Check((*ActiveLayer())["smartFilters"].size() == 2, "smart filter inspector missing");
  Layout(true);
  ui::Animate(.1f);
  Frame();
  auto* filter = layerList->children[1].get();
  e.x = filter->rect.left + 46;
  e.y = MidY(filter->rect);
  filter->OnUp(e, true);
  Drain();
  Check(!(*ActiveLayer())["smartFilters"][1]["visible"].Truthy(), "smart filter eye used wrong index");
  filter = layerList->children[1].get();
  Layout(true);
  ui::Animate(.1f);
  e.x = filter->rect.right - 15;
  e.y = MidY(filter->rect);
  filter->OnUp(e, true);
  auto* dialog = dynamic_cast<ParamDialog*>(ui::ActivePopover());
  Check(dialog && dialog->form, "smart blend editor absent");
  for (auto& f : dialog->form->fields)
    if (f.key == "opacity") f.def = .25;
  for (auto& c : dialog->form->children)
    if (auto* slider = dynamic_cast<Slider*>(c.get())) {
      slider->Set(.25);
      slider->onChange(.25);
    }
  dialog->Finish(true);
  Drain();
  Check(std::abs((*ActiveLayer())["smartFilters"][1]["opacity"].Num() - .25) < .001,
        "smart blend changed wrong filter");
  Layout(true);
  ui::Animate(.1f);
  filter = layerList->children[1].get();
  Frame();
  auto* nextFilter = layerList->children[2].get();
  nextFilter->Paint();
  e.x = filter->rect.left + 85;
  e.y = MidY(filter->rect);
  e.mods = {};
  filter->OnDown(e);
  e.x = nextFilter->rect.left + 85;
  e.y = MidY(nextFilter->rect);
  filter->OnUp(e, false);
  Drain();
  Check(std::abs((*ActiveLayer())["smartFilters"][0]["opacity"].Num() - .25) < .001,
        "smart filter drag did not reorder the actual indexed filter");
  NewDocument(depth);
  Rectangle();
  Command("layer.rasterize.layer");
  Tool("cloneStamp");
  auto source = Pointer(35, 35, {false, false, true});
  canvas->OnDown(source);
  canvas->OnUp(source, true);
  Drain();
  uint64_t before = pc_history_len(engine);
  auto down = Pointer(120, 40);
  canvas->OnDown(down);
  auto live = Sample(120, 40);
  if (live[0] <= .9)
    throw std::runtime_error("clone live: " + JsonText(CloneSourceState()) + " sample " + JsonNum(live[0]) + "," +
                             JsonNum(live[3]) + " error " + LastError());
  auto up = Pointer(125, 40);
  canvas->OnDrag(up);
  canvas->OnUp(up, true);
  Drain();
  Check(pc_history_len(engine) == before + 1, "live clone must commit once");
  Check(Sample(120, 40)[0] > .9, "clone commit lost live pixels");
  std::vector<uint8_t> px(32 * 32 * 4);
  before = pc_history_len(engine);
  Check(pc_clone_preview(engine, "{\"at\":[125,40],\"sampleLayer\":\"current\"}", 109, 24, 141, 56, px.data(),
                         px.size(), 128) == 1,
        "source image overlay failed");
  Check(pc_history_len(engine) == before, "hover overlay changed history");
  Command("select.rect", "{\"x\":20,\"y\":20,\"width\":25,\"height\":25}");
  Tool("patch");
  before = pc_history_len(engine);
  Stroke(30, 30, 120, 40);
  Check(pc_history_len(engine) == before + 1, "patch must commit one step");
  Undo();
  Drain();
  Tool("contentAwareMove");
  before = pc_history_len(engine);
  Stroke(30, 30, 70, 70);
  Check(pc_history_len(engine) == before + 1, "content-aware move did not commit");
}
void RetouchBenchmark(const std::filesystem::path& root) {
  Command("file.new", "{\"width\":6000,\"height\":4000,\"background\":\"white\"}");
  canvas->Reload();
  Refresh();
  canvas->Fit(false);
  Command("tools.setColors", "{\"foreground\":\"#aa4444\"}");
  Command("paint.stroke", "{\"points\":[[100,100,1],[180,100,1]],\"size\":100,\"hardness\":1}");
  Command("cloneSource.set", "{\"source\":[140,100]}");
  std::ofstream report(root / "retouch-perf.txt");
  double sum = 0, worst = 0;
  uint64_t history = pc_history_len(engine);
  std::vector<uint8_t> pixels(128 * 128 * 4);
  for (int i = 0; i < 60; i++) {
    double x = 500 + i * 3, y = 400;
    std::string p = JsonObject().Add("at", Pt(x, y)).Str("sampleLayer", "all").Text();
    auto start = std::chrono::steady_clock::now();
    Check(pc_clone_preview(engine, p.c_str(), int(x) - 64, int(y) - 64, int(x) + 64, int(y) + 64, pixels.data(),
                           pixels.size(), 512) == 1,
          "large-document source overlay failed");
    double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    sum += elapsed;
    worst = std::max(worst, elapsed);
  }
  Check(pc_history_len(engine) == history, "overlay benchmark changed history");
  report << "24 MP, 128x128 hover patch, 60 positions: mean " << sum / 60 << " ms, worst " << worst << " ms\n";
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
  wc.lpszClassName = L"VetroBrushLayerTests";
  RegisterClassW(&wc);
  hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, wc.lpszClassName, L"Vetro brush and layer tests",
                         WS_OVERLAPPEDWINDOW, 0, 0, 1600, 1000, nullptr, nullptr, wc.hInstance, nullptr);
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
    bool benchmarkOnly = argc > 2 && std::string(argv[2]) == "--benchmark-only";
    if (!benchmarkOnly) {
      Brushes();
      MasksAndLocks();
      Adjustment();
      for (int depth : {8, 16, 32}) DetailsAndRetouch(depth);
    }
    if (argc > 1) {
      auto root = std::filesystem::path(Widen(argv[1]));
      std::filesystem::create_directories(root);
      i18n::Load("ru");
      if (!benchmarkOnly) {
        NewDocument();
        Rectangle();
        Command("layer.layerMask.revealAll");
        Command("layer.newAdjustmentLayer.curves");
        Tool("brush");
        Layout(true);
        canvas->Fit(false);
        Sleep(3000);
        Capture(root / L"milestone4-curves-masks-ru.png", false);
        RunCommand("window.brushes", L"");
        Drain();
        Layout(true);
        Sleep(3000);
        Capture(root / L"milestone4-brushes-ru.png", false);
        RunCommand("window.brushes", L"");
        CanvasContext(80, 80, Pointer(80, 80));
        Drain();
        Sleep(3000);
        Capture(root / L"milestone4-palette-ru.png", false);
        ui::ClosePopovers();
        NewDocument();
        Rectangle();
        Tool("brush");
        Layout(true);
        canvas->Fit(false);
        Capture(root / L"milestone4-render-proof.png", true);
      }
      if (benchmarkOnly) RetouchBenchmark(root);
    }
    std::cout << checks << " brush/layer checks passed\n";
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    status = 1;
  }
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
