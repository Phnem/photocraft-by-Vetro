// Integration tests of the real shell handlers, widgets, worker queue and engine DLL.
// The window stays hidden. No user's instance, files or saved preferences are touched.
#include "../src/app.cpp"
#include <wincodec.h>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
struct ThreadKeys {
  BYTE original[256]{};
  ThreadKeys(std::initializer_list<UINT> held) {
    GetKeyboardState(original);
    BYTE state[256]{};
    for (UINT key : held) if (key < 256) state[key] = 0x80;
    SetKeyboardState(state);
  }
  ~ThreadKeys() { SetKeyboardState(original); }
};
void Check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
  checks++;
}
void Drain() {
  double end = ui::NowMs() + 15000;
  while (jobs::Busy() && ui::NowMs() < end) { jobs::Pump(); Sleep(1); }
  Check(!jobs::Busy(), "engine worker timed out");
  Refresh();
}
Json Command(const std::string& id, const std::string& params = "{}") {
  bool ok = false;
  std::string text;
  Exec(id, params, [&](bool success, const std::string& result) { ok = success; text = result; }, false);
  Drain();
  if (!ok) throw std::runtime_error(id + ": " + text);
  return Json::Parse(text);
}
void NewDocument(int depth = 8) {
  FinishNudge(); Drain();
  Command("file.new", "{\"width\":200,\"height\":160,\"background\":\"transparent\",\"depth\":" + std::to_string(depth) + "}");
  canvas->Reload(); Refresh();
  canvas->free = R(100, 100, 1100, 850);
  canvas->RotateTo(0, false); canvas->Fit(false);
  layersFocused = false; layerDrag = {}; visibilityPainting = false;
  springTool = nullptr; springKey = 0; springRestore = false;
  percentKeys.Reset(); brush = {}; hasBrushAnchor = false;
}
void Tool(const std::string& key) {
  for (auto& section : sections) for (auto& slot : section) for (auto& t : slot)
    if (t.key == key) { SelectTool(&t); Drain(); return; }
  throw std::runtime_error("missing tool " + key);
}
void Key(UINT vk, Mods m = {}) { Check(HandleKey(vk, m), "key was not handled"); Drain(); }
PointerEvent Pointer(double x, double y, Mods mods = {}, int clicks = 1) {
  auto screen = canvas->ToScreen(x, y);
  PointerEvent e; e.x = screen.x; e.y = screen.y; e.mods = mods; e.clicks = clicks; e.timeMs = ui::NowMs();
  pointerX = e.x; pointerY = e.y;
  return e;
}
void Stroke(double x0, double y0, double x1, double y1, Mods mods = {}) {
  auto down = Pointer(x0, y0, mods); canvas->OnDown(down);
  auto up = Pointer(x1, y1, mods); up.timeMs += 30;
  canvas->OnDrag(up); canvas->OnUp(up, true); Drain();
}
std::array<float, 4> Sample(int x, int y) {
  Snapshot snap(pc_snapshot(engine)); std::array<float, 4> rgba{};
  Check(pc_sample(snap.p, x, y, rgba.data()) == 1, "sampling failed");
  return rgba;
}
int64_t Rectangle(const char* name = "Patch") {
  auto result = Command("shape.create", std::string("{\"kind\":\"rect\",\"rect\":[20,20,30,30],\"fill\":\"#ff0000\",\"name\":\"") + name + "\"}");
  return result["layer"].Int(docInfo["activeLayer"].Int());
}
LayerRow* Row(int64_t id) {
  Layout(true); ui::Animate(1.f / 60);
  for (auto* r : layerRows) if (r->id == uint64_t(id)) return r;
  std::string rows;
  for (auto* r : layerRows) rows += std::to_string(r->id) + ":" + Narrow(r->kind) + ":" + (r->expanded ? "open " : "closed ");
  throw std::runtime_error("layer row " + std::to_string(id) + " is missing among " + rows + "; " + JsonText(docInfo["layers"]));
}
void Eye(int64_t id, bool alt = false) {
  auto* r = Row(id); auto box = r->eye->rect;
  PointerEvent e; e.x = MidX(box); e.y = MidY(box); e.mods.alt = alt;
  r->eye->OnDown(e); Drain();
  r->eye->OnUp(e, true); Drain();
}
void Capture(const std::filesystem::path& path, bool content = true) {
  double deadline = ui::NowMs() + 20000;
  while (canvas->Busy() && ui::NowMs() < deadline) { Frame(); Sleep(3); }
  Check(!canvas->Busy(), "canvas render timed out");
  for (int i = 0; i < 90; i++) { Frame(); Sleep(2); }
  if (content && GetEnvironmentVariableW(L"VETRO_TEST_GPU", nullptr, 0)) Check(canvas->OnGpu(), "the GPU test must actually use the shared GPU texture");
  Microsoft::WRL::ComPtr<ID2D1Image> image;
  Dc()->GetTarget(&image);
  Microsoft::WRL::ComPtr<ID2D1Bitmap1> source, cpu;
  Check(SUCCEEDED(image.As(&source)), "framebuffer is not a bitmap");
  auto size = source->GetPixelSize();
  auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, source->GetPixelFormat());
  Check(SUCCEEDED(Dc()->CreateBitmap(size, nullptr, 0, &props, &cpu)), "CPU readback allocation failed");
  Check(SUCCEEDED(cpu->CopyFromBitmap(nullptr, source.Get(), nullptr)), "framebuffer copy failed");
  D2D1_MAPPED_RECT mapped{};
  Check(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ, &mapped)), "framebuffer mapping failed");
  auto pixel = canvas->ToScreen(40, 40);
  auto offset = size_t(std::lround(pixel.y)) * mapped.pitch + size_t(std::lround(pixel.x)) * 4;
  if (content) {
    Check(pixel.x >= 0 && pixel.y >= 0 && pixel.x < size.width && pixel.y < size.height, "test pixel is outside framebuffer");
    Check(mapped.bits[offset + 2] > 150 && mapped.bits[offset + 1] < 40 && mapped.bits[offset] < 40,
          "the canvas must display the latest red rectangle, not an older render");
  }
  Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
  HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
  Microsoft::WRL::ComPtr<IWICStream> stream;
  Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
  Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
  if (SUCCEEDED(hr)) hr = factory->CreateStream(&stream);
  if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
  if (SUCCEEDED(hr)) hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
  if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
  if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, nullptr);
  if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
  if (SUCCEEDED(hr)) hr = frame->SetSize(size.width, size.height);
  WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
  if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
  if (SUCCEEDED(hr)) hr = frame->WritePixels(size.height, mapped.pitch, mapped.pitch * size.height, mapped.bits);
  if (SUCCEEDED(hr)) hr = frame->Commit();
  if (SUCCEEDED(hr)) hr = encoder->Commit();
  cpu->Unmap();
  Check(SUCCEEDED(hr), "PNG encoding failed");
}

void Rules() {
  gestures::PercentKeys keys;
  Check(keys.Push(3, false, 0) == 30 && keys.Push(7, false, 100) == 37, "two digits must set exact opacity");
  Check(keys.Push(0, false, 200) == 100 && keys.Push(5, false, 250) == 5, "05 must set five percent");
  Check(keys.Push(0, false, 300) == 100 && keys.Push(0, false, 400) == 100, "00 must set 100 percent");
  keys.Reset(); Check(keys.Push(2, false, 0) == 20 && keys.Push(8, true, 50) == 80, "flow and opacity must not combine");
  Check(keys.Push(4, true, 1000) == 40, "expired digit must start a new pair");
  auto [x, y] = gestures::Snap45(10, 7);
  Check(std::fabs(x - y) < .001, "Shift must snap diagonally");
  gestures::Marquee box; box.Begin(50, 50); box.Move(70, 60, false, true, false);
  Check(box.left == 30 && box.right == 70 && box.top == 40 && box.bottom == 60, "Alt must expand from the center");
  box.Move(80, 80, false, true, true);
  Check(box.right - box.left == 40 && box.bottom - box.top == 20 && box.ax == 60 && box.ay == 70, "Space must reposition without resizing");
  box.Move(90, 80, true, true, false);
  Check(box.right - box.left == box.bottom - box.top, "Shift+Alt must stay square");
}
void Nudges() {
  for (int depth : {8, 16, 32}) {
    NewDocument(depth); Rectangle(); Tool("move");
    auto before = pc_history_len(engine);
    Key(VK_RIGHT); Key(VK_RIGHT, {false, true, false}); Key(VK_DOWN);
    FinishNudge(); Drain();
    const Json& b = (*ActiveLayer())["bounds"];
    Check(b[0].Int() == 31 && b[1].Int() == 21, "arrow layer translation is wrong");
    Check(pc_history_len(engine) == before + 1, "arrow repeats must make one history step");
    Undo(); Drain(); Check((*ActiveLayer())["bounds"][0].Int() == 20, "Undo must restore the whole arrow burst");
    Command("select.rect", "{\"x\":70,\"y\":70,\"width\":20,\"height\":10}");
    Tool("rectMarquee"); before = pc_history_len(engine);
    Key(VK_LEFT); Key(VK_UP, {false, true, false}); FinishNudge(); Drain();
    Check(docInfo["selectionBounds"][0].Int() == 69 && docInfo["selectionBounds"][1].Int() == 60, "selection arrows must move its outline");
    Check((*ActiveLayer())["bounds"][0].Int() == 20, "selection nudge moved layer pixels");
    Check(pc_history_len(engine) == before + 1, "selection repeats must make one history step");
    Undo(); Drain();
    Tool("brush"); Key(VK_RIGHT, {true, false, false}); FinishNudge(); Drain();
    Check((*ActiveLayer())["bounds"][0].Int() == 21, "Ctrl+arrow must move layer from the Brush tool");
    std::cout << "VT-24 depth " << depth << " passed\n";
  }
}
void MoveGestures() {
  NewDocument(); int64_t original = Rectangle("Original"); Tool("move"); moveAutoSelect = true;
  auto down = Pointer(30, 30, {false, false, true}); canvas->OnDown(down); Drain();
  auto end = Pointer(50, 44, {false, true, true}); canvas->OnDrag(end); Drain(); canvas->OnUp(end, true); Drain();
  Check(docInfo["layers"].size() == 3, "Alt drag must duplicate exactly one layer");
  Check(docInfo["activeLayer"].Int() != original, "Alt drag must select its copy");
  Check((*ActiveLayer())["bounds"][0].Int() - 20 == (*ActiveLayer())["bounds"][1].Int() - 20, "Shift drag must constrain to 45 degrees");
  const Json* l = FindLayer(docInfo["layers"], original);
  Check(l && (*l)["bounds"][0].Int() == 20, "Alt drag changed the original");
  auto e = Pointer(30, 30); CanvasContext(30, 30, e); Drain();
  Check(contextMenu && !contextMenu->items.empty(), "Move right click must list layers");
  ui::ClosePopovers();
  Command("layer.select", "{\"layer\":" + std::to_string(original) + "}");
  Command("layer.new.groupFromLayers"); int64_t group = docInfo["activeLayer"].Int();
  moveSelectGroup = true; down = Pointer(30, 30); canvas->OnDown(down); Drain(); canvas->OnUp(down, true); Drain();
  Check(docInfo["activeLayer"].Int() == group, "Auto-Select Group must select the containing group");
  moveSelectGroup = false;
  auto before = pc_history_len(engine);
  down = Pointer(30, 30, {false, false, true}); canvas->OnDown(down); Drain();
  end = Pointer(60, 30, {false, false, true}); canvas->OnDrag(end); Drain(); canvas->CancelInput(); Drain();
  Check(pc_history_len(engine) == before, "cancelling an Alt drag must remove the copy and its preview");
  moveControls = true;
  const Json& bounds = (*ActiveLayer())["bounds"];
  down = Pointer(bounds[0].Num(), bounds[1].Num()); canvas->OnDown(down);
  Check(Mode() != nullptr, "a Move control handle must enter Free Transform");
  canvas->OnUp(down, true); Key(VK_ESCAPE); Drain(); moveControls = false;
  std::cout << "VT-25 passed\n";
}
void BrushKeys() {
  NewDocument(); Command("layer.new.layer"); Tool("brush");
  Command("tools.setColors", "{\"foreground\":\"#ff0000\"}");
  Key('3'); Key('7'); Check(brush.opacity == 37, "brush digits did not set 37 percent");
  Key('2', {false, true, false}); Key('5', {false, true, false}); Check(brush.flow == 25, "Shift digits did not set flow");
  auto settings = Json::Parse(TakeString(pc_session_info(engine)))["brush"];
  Check(std::fabs(settings["opacity"].Num() - .37) < .001 && settings["flow"].Num() == .25, "brush keyboard settings were not sent to engine");
  Key(VK_OEM_4, {false, true, false}); Check(brush.hardness == 55, "Shift+[ must reduce hardness by 25");
  brush.opacity = brush.flow = brush.hardness = 100; brush.size = 8; SetBrushSettings(); Drain();
  Stroke(20, 90, 20, 90); Stroke(100, 90, 100, 90, {false, true, false});
  Check(Sample(60, 90)[3] > .9f, "Shift click must draw a straight line from the previous point");
  Key(VK_OEM_3); Stroke(60, 90, 60, 90); Proc(hwnd, WM_KEYUP, VK_OEM_3, 0);
  Check(!temporaryErase && Sample(60, 90)[3] < .1f, "holding tilde must erase with the current brush");
  auto history = pc_history_len(engine); Stroke(30, 90, 30, 90, {false, false, true});
  Check(pc_history_len(engine) == history, "Alt eyedropper must not paint or add history");
  auto start = Pointer(20, 120); canvas->OnDown(start);
  Key('5'); Check(brushSettingsPending && canvas->Drawing(), "brush settings during a stroke must be deferred");
  auto end = Pointer(100, 120); canvas->OnDrag(end); canvas->OnUp(end, true); Drain();
  Check(Sample(60, 120)[3] > .9f, "changing opacity mid-stroke must not discard that stroke");
  SetBrushSettings(); Drain();
  Key(VK_OEM_3); canvas->spaceHand = true; Proc(hwnd, WM_KILLFOCUS, 0, 0); Drain();
  Check(!temporaryErase && !canvas->spaceHand, "focus loss must clear temporary modifiers");
  std::cout << "VT-26 passed\n";
}
void Selections() {
  NewDocument(); Tool("rectMarquee");
  auto down = Pointer(80, 70); canvas->OnDown(down);
  auto end = Pointer(100, 80, {false, false, true}); canvas->OnDrag(end); canvas->OnUp(end, true); Drain();
  Check(docInfo["selectionBounds"][0].Int() == 60 && docInfo["selectionBounds"][2].Int() == 40, "Alt after press must create selection from center");
  down = Pointer(40, 40); canvas->OnDown(down);
  end = Pointer(70, 60); canvas->OnDrag(end);
  {
    ThreadKeys space({VK_SPACE});
    auto moved = Pointer(80, 80); canvas->OnDrag(moved);
    auto* marquee = dynamic_cast<MarqueeTool*>(canvas->tool);
    Check(marquee && marquee->x0 == 50 && marquee->y0 == 60 && marquee->x1 - marquee->x0 == 30,
          "Space during a marquee drag must shift the frame in document coordinates");
  }
  end = Pointer(80, 80); canvas->OnUp(end, true); Drain();
  Tool("polygonLasso");
  for (auto p : {std::pair{30., 30.}, std::pair{80., 30.}, std::pair{80., 70.}}) {
    auto e = Pointer(p.first, p.second); canvas->OnDown(e); canvas->OnUp(e, true);
  }
  auto* polygon = dynamic_cast<PolygonLassoTool*>(canvas->tool);
  Check(polygon && polygon->points.size() == 3, "Polygonal Lasso must retain clicked vertices");
  Key(VK_BACK); Check(polygon->points.size() == 2, "Backspace must remove one polygon vertex");
  auto e = Pointer(80, 70, {}, 2); canvas->OnDown(e); canvas->OnUp(e, true); Drain();
  Check(polygon->points.empty() && docInfo["hasSelection"].Truthy(), "double click must close Polygonal Lasso");
  Check(shape.kind == SelectionShape::PolyK, "Polygonal Lasso must show the actual polygon outline");
  std::cout << "VT-27 passed\n";
}
void LayerPanel() {
  NewDocument(); int64_t a = Rectangle("A"); Command("layer.new.groupFromLayers"); int64_t group = docInfo["activeLayer"].Int();
  auto* row = Row(group); row->disclosure->onClick(); Drain();
  Check(!FindLayer(docInfo["layers"], group)->operator[]("expanded").Truthy(), "group arrow must collapse its children");
  row = Row(group); row->disclosure->onClick(); Drain(); Check(Row(a) != nullptr, "group arrow must expand its children");
  Command("layer.new.groupFromLayers"); int64_t outer = docInfo["activeLayer"].Int();
  {
    ThreadKeys alt({VK_MENU}); row = Row(outer); row->disclosure->onClick(); Drain();
    Check(!FindLayer(docInfo["layers"], group)->operator[]("expanded").Truthy(), "Alt group arrow must collapse nested groups");
    row = Row(outer); row->disclosure->onClick(); Drain();
  }
  Check(Row(a) != nullptr, "Alt group expansion must reveal descendants");
  int64_t b = Rectangle("B"); Eye(b, true);
  Check(!FindLayer(docInfo["layers"], group)->operator[]("visible").Truthy(), "Alt eye must hide other branches");
  Eye(b, true); Check(FindLayer(docInfo["layers"], group)->operator[]("visible").Truthy(), "second Alt eye must restore original visibility");
  auto* first = Row(b); auto* second = Row(outer);
  PointerEvent swipe; swipe.x = MidX(first->eye->rect); swipe.y = MidY(first->eye->rect);
  first->eye->OnDown(swipe); Drain();
  swipe.x = MidX(second->eye->rect); swipe.y = MidY(second->eye->rect); first->eye->OnDrag(swipe); Drain();
  first->eye->OnUp(swipe, false); Drain();
  Check(!FindLayer(docInfo["layers"], b)->operator[]("visible").Truthy() && !FindLayer(docInfo["layers"], outer)->operator[]("visible").Truthy(),
        "eye swipe must apply the same visibility to each crossed row");
  Eye(b); Eye(outer);
  Command("layer.select", "{\"layer\":" + std::to_string(b) + "}");
  row = Row(a); Rect thumb = row->Thumb(); PointerEvent e; e.x = MidX(thumb); e.y = MidY(thumb); e.mods.ctrl = true;
  row->OnDown(e); Drain();
  Check(docInfo["hasSelection"].Truthy() && docInfo["activeLayer"].Int() == b, "Ctrl thumbnail must load transparency without selecting the layer");
  Command("select.rect", "{\"x\":100,\"y\":100,\"width\":10,\"height\":10}");
  row = Row(a); thumb = row->Thumb(); e.x = MidX(thumb); e.y = MidY(thumb); e.mods.shift = true; row->OnDown(e); Drain();
  Check(docInfo["selectionBounds"][2].Int() >= 90, "Ctrl+Shift thumbnail must add transparency to selection");
  row = Row(a); thumb = row->Thumb(); e.x = MidX(thumb); e.y = MidY(thumb); e.mods.shift = false; e.mods.alt = true; row->OnDown(e); Drain();
  Check(docInfo["selectionBounds"][0].Int() == 100, "Ctrl+Alt thumbnail must subtract transparency from selection");
  layersFocused = true; Key(VK_DELETE); Check(!FindLayer(docInfo["layers"], b), "Delete in Layers must delete layer even with a pixel selection");
  Undo(); Drain();
  Command("select.deselect"); row = Row(b); e = {}; e.x = MidX(row->rect); e.y = MidY(row->rect); row->OnDown(e);
  e.x = MidX(newLayerAction->rect); e.y = MidY(newLayerAction->rect); row->OnDrag(e); row->OnUp(e, false); Drain();
  Check(docInfo["layers"].size() == 4, "row drop on New Layer must duplicate it");
  int64_t copy = docInfo["activeLayer"].Int(); row = Row(copy); e = {}; e.x = MidX(row->rect); e.y = MidY(row->rect); row->OnDown(e);
  e.x = MidX(deleteLayerAction->rect); e.y = MidY(deleteLayerAction->rect); row->OnDrag(e); row->OnUp(e, false); Drain();
  Check(!FindLayer(docInfo["layers"], copy), "row drop on trash must delete it");
  Command("layer.select", "{\"layer\":" + std::to_string(b) + "}"); Command("layer.smartObjects.convertToSmartObject");
  row = Row(docInfo["activeLayer"].Int()); thumb = row->Thumb(); e = {}; e.x = MidX(thumb); e.y = MidY(thumb); e.clicks = 2;
  auto document = docInfo["id"].Int(); row->OnDown(e); Drain();
  Check(docInfo["id"].Int() != document, "smart thumbnail double click must open contents");
  std::cout << "VT-28 passed\n";
}
void Navigation() {
  NewDocument(); Tool("brush"); ToolDef* original = current;
  Key('H'); springAt -= 300; Proc(hwnd, WM_KEYUP, 'H', 0); Drain();
  Check(current == original, "holding a tool letter must restore the previous tool on release");
  Key('Z'); ReleaseToolKey('Z'); Check(current->key == "zoom", "a short tool-key tap must keep the new tool");
  float before = canvas->Zoom(); Stroke(80, 70, 100, 70); Check(canvas->Zoom() > before, "Zoom tool drag must smoothly magnify");
  auto e = Pointer(80, 70); before = canvas->Zoom(); canvas->OnWheel(120, e); Check(canvas->Zoom() == before, "plain wheel must scroll instead of zooming");
  e.mods.alt = true; canvas->OnWheel(120, e); Check(canvas->Zoom() > before, "Alt wheel must zoom");
  Tool("brush");
  { ThreadKeys ctrlSpace({VK_CONTROL, VK_SPACE}); Proc(hwnd, WM_KEYDOWN, VK_SPACE, 0); }
  Check(canvas->spaceZoom == 1 && !canvas->spaceHand, "Ctrl+Space must route to temporary Zoom instead of Hand");
  before = canvas->Zoom(); e = Pointer(80, 70); canvas->OnDown(e); canvas->OnUp(e, true);
  Check(canvas->Zoom() > before && current->key == "brush", "temporary zoom must preserve the current tool"); canvas->spaceZoom = 0;
  Proc(hwnd, WM_KEYUP, VK_SPACE, 0);
  { ThreadKeys altSpace({VK_MENU, VK_SPACE}); Proc(hwnd, WM_SYSKEYDOWN, VK_SPACE, 0); }
  Check(canvas->spaceZoom == -1, "Alt+Space must route to temporary Zoom Out");
  before = canvas->Zoom(); e = Pointer(80, 70); canvas->OnDown(e); canvas->OnUp(e, true);
  Check(canvas->Zoom() < before, "Alt+Space click must zoom out"); Proc(hwnd, WM_SYSKEYUP, VK_SPACE, 0);
  Check(!canvas->spaceHand && !canvas->spaceZoom, "Space release must clear temporary navigation");
  Tool("rotateView"); Check(current->letter == 'R', "R must address Rotate View"); canvas->RotateTo(37, false);
  auto s = canvas->ToScreen(40, 60); auto p = canvas->ToDoc(s.x, s.y);
  Check(std::fabs(p.x - 40) < .001 && std::fabs(p.y - 60) < .001, "rotated view coordinate mapping must round-trip");
  Check(canvas->DocOnScreen().right > canvas->DocOnScreen().left, "rotated page bounds must remain ordered");
  Tool("zoom");
  for (size_t i = 0; i < toolButtons.size(); i++) if (SlotTool(i).key == "zoom") {
    PointerEvent doubleClick; doubleClick.clicks = 2; toolButtons[i]->OnDown(doubleClick); toolButtons[i]->OnUp(doubleClick, true);
    Check(std::fabs(canvas->Zoom() - 1.f / GfxDpi()) < .001f, "double clicking Zoom button must select 100 percent");
  }
  Tool("hand");
  for (size_t i = 0; i < toolButtons.size(); i++) if (SlotTool(i).key == "hand") {
    PointerEvent doubleClick; doubleClick.clicks = 2; toolButtons[i]->OnDown(doubleClick); toolButtons[i]->OnUp(doubleClick, true);
    Check(canvas->Zoom() != 1.f / GfxDpi() && canvas->Rotation() == 0, "double clicking Hand button must fit the document");
  }
  std::cout << "VT-29 passed\n";
}
void Localisation(const std::filesystem::path& screenshots) {
  i18n::Load("ru"); NewDocument();
  Check((*ActiveLayer())["name"].Str() == "Слой 1", "initial document layer must have a Russian name");
  Command("layer.new.layer"); Check((*ActiveLayer())["name"].Str() == "Слой 2", "new raster layer must have a Russian name");
  Command("shape.create", "{\"kind\":\"rect\",\"rect\":[20,20,60,40],\"fill\":\"#ff0000\"}");
  Check((*ActiveLayer())["name"].Str() == "Прямоугольник 1", "new shape must have a Russian name");
  Command("layer.duplicate"); Check((*ActiveLayer())["name"].Str() == "Прямоугольник 1 копия", "copy name must be localized");
  Command("layer.duplicate"); Check((*ActiveLayer())["name"].Str().find("copy") == std::string::npos, "nested copies must remain localized");
  for (auto key : {L"Use global light", L"Anti alias", L"Fill opacity", L"Blend clipped layers as group", L"Texture scale", L"Contour name"})
    Check(i18n::Tr(key) != key, "Layer Style label has no Russian translation");
  std::set<std::wstring> untranslated;
  for (const auto* effect : {"blendingOptions", "dropShadow", "innerShadow", "outerGlow", "innerGlow", "bevelEmboss", "satin", "colorOverlay", "gradientOverlay", "patternOverlay", "stroke"}) {
    for (const auto& field : ParseParams(paramsDoc[std::string("layer.layerStyle.") + effect])) {
      if (field.key == "layer" || field.key == "add") continue;
      if (i18n::Tr(field.label) == field.label) untranslated.insert(field.label);
      for (const auto& choice : field.choices) {
        auto label = HumanKey(choice);
        if (label != L"Normal" && i18n::Tr(label) == label) untranslated.insert(label);
      }
    }
  }
  if (!untranslated.empty()) {
    std::string labels;
    for (auto& label : untranslated) labels += Narrow(label) + "; ";
    throw std::runtime_error("untranslated Layer Style text: " + labels);
  }
  if (!screenshots.empty()) {
    std::filesystem::create_directories(screenshots);
    Tool("move"); moveControls = true; RebuildOptions(); Layout(true); canvas->Fit(false); Capture(screenshots / L"milestone1-move-ru.png");
    canvas->RotateTo(37, false); canvas->Fit(false); Capture(screenshots / L"milestone1-rotate-ru.png");
    canvas->RotateTo(0, false); canvas->Fit(false);
    auto* dialog = ActiveLayer() ? Row(docInfo["activeLayer"].Int()) : nullptr;
    if (dialog) RunCommand("layer.layerStyle.dropShadow", L"Drop Shadow");
    Capture(screenshots / L"milestone1-layer-style-ru.png");
  }
  std::cout << "VT-30 passed\n";
}
}  // namespace

#ifndef VETRO_MILESTONE2
int main(int argc, char** argv) {
  int status = 0;
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); SetConsoleOutputCP(CP_UTF8);
  WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = GetModuleHandle(nullptr); wc.lpszClassName = L"VetroInteractionTests";
  RegisterClassW(&wc);
  hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, wc.lpszClassName, L"PhotoCraft test", WS_OVERLAPPEDWINDOW,
                        0, 0, 1600, 1000, nullptr, nullptr, wc.hInstance, nullptr);
  ui::Init(hwnd); ui::SetBounds(R(0, 0, 1584, 961));
  if (!GfxCreate(hwnd, 1)) { std::cerr << "graphics initialization failed\n"; return 1; }
  engine = pc_new(); jobs::Init(hwnd, WM_JOBS); LoadCatalog();
  canvas = new Canvas(engine); ui::AddLayer(canvas); ui::AddLayer(new SelectionOverlay());
  if (GetEnvironmentVariableW(L"VETRO_TEST_GPU", nullptr, 0)) {
    uint32_t low = 0; int32_t high = 0;
    if (GfxAdapterLuid(&low, &high)) canvas->UseGpu(low, high);
  }
  canvas->onContext = CanvasContext; canvas->onBrushDrag = BrushDrag;
  canvas->onSample = [](double x, double y, const PointerEvent&) { if (!IsBrushTool()) return false; PickColour(x, y, false); return true; };
  BuildTools(); BuildDocks(); BuildShortcuts();
  try {
    Rules(); Nudges(); MoveGestures(); BrushKeys(); Selections(); LayerPanel(); Navigation();
    Localisation(argc > 1 ? std::filesystem::path(Widen(argv[1])) : std::filesystem::path{});
    std::cout << checks << " checks passed\n";
  } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; status = 1; }
  FinishNudge(); Drain(); jobs::Shutdown(); delete canvas; canvas = nullptr; pc_free(engine); engine = nullptr;
  GfxDestroy(); DestroyWindow(hwnd); CoUninitialize();
  return status;
}
#endif
