// The real Win32 clipboard is isolated in a private window station. The desktop and the
// clipboard of the user's session are never read or modified. The shell itself stays hidden.
#define VETRO_MILESTONE2
#include "milestone1.cpp"
#include <fstream>
#include <chrono>

namespace {
std::wstring recentTestKey = L"Software\\PhotoCraft by Vetro\\Tests\\Milestone2-" + std::to_wstring(GetCurrentProcessId());
struct PrivateDesktop {
  HWINSTA originalStation = GetProcessWindowStation(), station = nullptr;
  HDESK originalDesktop = GetThreadDesktop(GetCurrentThreadId()), desktop = nullptr;
  PrivateDesktop() {
    station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
    if (!station || !SetProcessWindowStation(station)) throw std::runtime_error("private clipboard station failed");
    desktop = CreateDesktopW(L"VetroTests", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desktop || !SetThreadDesktop(desktop)) throw std::runtime_error("private test desktop failed");
  }
  ~PrivateDesktop() {
    SetThreadDesktop(originalDesktop); SetProcessWindowStation(originalStation);
    if (desktop) CloseDesktop(desktop); if (station) CloseWindowStation(station);
  }
};
void Format(UINT f, const std::vector<uint8_t>& bytes) {
  Check(OpenClipboard(hwnd), "cannot open private clipboard");
  Check(EmptyClipboard(), "cannot empty private clipboard");
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes.size()); Check(h != nullptr, "clipboard allocation failed");
  void* p = GlobalLock(h); Check(p != nullptr, "clipboard allocation not accessible");
  memcpy(p, bytes.data(), bytes.size()); GlobalUnlock(h);
  bool ok = SetClipboardData(f, h) != nullptr; CloseClipboard();
  if (!ok) GlobalFree(h); Check(ok, "setting clipboard format failed");
  ownClipboardSequence = 0;
}
void EmptyDocuments() {
  FinishNudge(); Drain();
  while (sessionInfo["documents"].size()) Command("file.close");
}
void Codecs() {
  clipboard::Image im{3, 2, {255, 0, 0, 255, 7, 80, 160, 128, 1, 2, 3, 0, 20, 30, 40, 255, 150, 160, 170, 1, 10, 90, 200, 200}}, read;
  auto dib = clipboard::EncodeDib(im), png = clipboard::EncodePng(im);
  Check(clipboard::DecodeDib(dib, read) && read.rgba == im.rgba && read.width == 3 && read.height == 2, "DIBV5 straight alpha round trip failed");
  Check(clipboard::DecodePng(png, read) && read.rgba == im.rgba, "PNG straight alpha round trip failed");
  auto bottom = dib; LONG height = 2; memcpy(bottom.data() + 8, &height, 4);
  std::swap_ranges(bottom.begin() + 124, bottom.begin() + 136, bottom.begin() + 136);
  Check(clipboard::DecodeDib(bottom, read) && read.rgba == im.rgba, "bottom-up DIB orientation failed");
  BITMAPINFOHEADER h{}; h.biSize = 40; h.biWidth = 1; h.biHeight = -2; h.biPlanes = 1; h.biBitCount = 24;
  std::vector<uint8_t> legacy(48); memcpy(legacy.data(), &h, 40); legacy[42] = 255; legacy[45] = 255;
  Check(clipboard::DecodeDib(legacy, read) && read.rgba == std::vector<uint8_t>({255, 0, 0, 255, 0, 255, 0, 255}), "24-bit DIB padding failed");
  h.biBitCount = 32; memcpy(legacy.data(), &h, 40);
  Check(clipboard::DecodeDib(legacy, read) && read.rgba[3] == 255 && read.rgba[7] == 255, "legacy DIB reserved byte must not make screenshots transparent");
  h.biWidth = 2; h.biHeight = 1; h.biBitCount = 8; h.biClrUsed = 2;
  legacy.assign(52, 0); memcpy(legacy.data(), &h, 40); legacy[42] = 255; legacy[45] = 255; legacy[48] = 0; legacy[49] = 1;
  Check(clipboard::DecodeDib(legacy, read) && read.rgba == std::vector<uint8_t>({255, 0, 0, 255, 0, 255, 0, 255}), "indexed DIB failed");
  h.biBitCount = 16; h.biClrUsed = 0; h.biCompression = BI_BITFIELDS;
  legacy.assign(56, 0); memcpy(legacy.data(), &h, 40); uint32_t masks[] = {0xf800, 0x7e0, 0x1f}; memcpy(legacy.data() + 40, masks, 12);
  legacy[53] = 0xf8; legacy[54] = 0xe0; legacy[55] = 0x07;
  Check(clipboard::DecodeDib(legacy, read) && read.rgba == std::vector<uint8_t>({255, 0, 0, 255, 0, 255, 0, 255}), "16-bit bitfields failed");
  for (size_t length : {size_t(0), size_t(39), size_t(123), dib.size() - 1}) {
    auto bad = dib; bad.resize(length); Check(!clipboard::DecodeDib(bad, read), "truncated DIB was accepted");
  }
  for (auto [offset, value] : std::vector<std::pair<size_t, uint32_t>>{{0, UINT32_MAX}, {4, 0}, {4, UINT32_MAX}, {8, 0x80000000}, {40, 0xff00}, {44, 3}, {52, 0xff0000}, {116, UINT32_MAX}}) {
    auto bad = dib; memcpy(bad.data() + offset, &value, 4);
    if (clipboard::DecodeDib(bad, read)) throw std::runtime_error("malformed DIB was accepted at offset " + std::to_string(offset));  }
  Check(!clipboard::DecodePng({1, 2, 3}, read), "malformed PNG was accepted");
  Format(RegisterClipboardFormatW(L"PNG"), png);
  std::wstring error;
  Check(clipboard::Available() && clipboard::Read(hwnd, read, error) == clipboard::Result::Image && read.rgba == im.rgba, "PNG-only Windows clipboard failed");
  Format(CF_DIBV5, dib);
  Check(clipboard::Read(hwnd, read, error) == clipboard::Result::Image && read.rgba == im.rgba, "DIBV5-only Windows clipboard failed");
  Check(clipboard::Write(hwnd, im, error), "Windows clipboard write failed");
  Check(IsClipboardFormatAvailable(CF_DIBV5) && IsClipboardFormatAvailable(RegisterClipboardFormatW(L"PNG")), "copy must publish both PNG and DIBV5");
  Check(clipboard::Read(hwnd, read, error) == clipboard::Result::Image && read.rgba == im.rgba, "Windows clipboard round trip failed");
}
void ClipboardCommands() {
  EmptyDocuments();
  clipboard::Image im{2, 1, {255, 0, 0, 128, 0, 255, 0, 255}};
  Check(!pc_clipboard_set(engine, 0, 1, nullptr, 0) && !pc_clipboard_set(engine, UINT32_MAX, 2, nullptr, 0), "C ABI must reject invalid image dimensions before dereferencing pointers");
  Check(!pc_clipboard_set(engine, 2, 1, im.rgba.data(), 4), "C ABI must reject truncated input buffers");
  Check(pc_clipboard_set(engine, 2, 1, im.rgba.data(), im.rgba.size()), "C ABI must accept exact input buffers");
  std::array<uint8_t, 4> sentinel{1, 2, 3, 4};
  Check(!pc_clipboard_read(engine, sentinel.data(), sentinel.size()) && sentinel == std::array<uint8_t, 4>{1, 2, 3, 4}, "C ABI must refuse short output buffers without writing any pixels");
  Check(!pc_clipboard_read(engine, nullptr, 8), "C ABI must reject null output buffers");
  Format(RegisterClipboardFormatW(L"PNG"), clipboard::EncodePng(im));
  Key('V', {true});
  Check(docInfo["width"].Int() == 2 && docInfo["height"].Int() == 1 && sessionInfo["documents"].size() == 1, "Paste without document must create clipboard-sized document");
  Check(sessionInfo["documents"][0]["dirty"].Truthy(), "clipboard document must be dirty");
  RunCommand("file.newFromClipboard", L""); Drain(); Check(sessionInfo["documents"].size() == 2, "New from Clipboard must create another document");
  for (int depth : {8, 16, 32}) {
    NewDocument(depth); Format(CF_DIBV5, clipboard::EncodeDib(im));
    Key('V', {true}); Check(docInfo["layers"].size() == 2 && docInfo["depth"].Int() == depth, "external paste must add one layer at document depth");
    Undo(); Drain(); Check(docInfo["layers"].size() == 1, "Undo must remove the pasted layer");
    Command("shape.create", "{\"kind\":\"rect\",\"rect\":[30,30,70,70],\"fill\":\"#ff0000\"}");
    Command("select.rect", "{\"x\":30,\"y\":30,\"width\":40,\"height\":40}");
    Key('C', {true}); clipboard::Image copied; std::wstring error;
    Check(clipboard::Read(hwnd, copied, error) == clipboard::Result::Image && copied.width == 40 && copied.height == 40 && copied.rgba[0] == 255, "Ctrl+C must export selected layer pixels");
    Check(ownClipboardSequence == GetClipboardSequenceNumber(), "copy must remember its clipboard ownership");
    Key('V', {true}); Check(docInfo["depth"].Int() == depth && docInfo["layers"].size() == 3, "our own paste must retain engine depth");
    Key('C', {true, true}); Check(clipboard::Read(hwnd, copied, error) == clipboard::Result::Image && copied.rgba[0] == 255, "Ctrl+Shift+C must export merged selection");
  }
  NewDocument(); Command("edit.fill", "{\"color\":\"#0000ff\"}");
  Command("layer.new.layer"); Command("select.rect", "{\"x\":40,\"y\":40,\"width\":20,\"height\":20}");
  Command("edit.fill", "{\"color\":\"#ff0000\"}"); Command("select.rect", "{\"x\":30,\"y\":30,\"width\":40,\"height\":40}");
  Key('C', {true}); clipboard::Image single, merged; std::wstring error;
  Check(clipboard::Read(hwnd, single, error) == clipboard::Result::Image && single.width == 20 && single.height == 20 && single.rgba[0] == 255 && single.rgba[2] == 0, "Copy must contain only the selected layer's content");
  Key('C', {true, true});
  Check(clipboard::Read(hwnd, merged, error) == clipboard::Result::Image && merged.rgba[2] == 255 && merged.rgba[3] == 255, "Copy Merged must include visible lower-layer pixels");
  Command("edit.purge.clipboard"); Key('V', {true});
  Check(docInfo["layers"].size() == 3, "Paste must re-import our Windows image if the engine clipboard was purged");
  size_t before = docInfo["layers"].size(); Format(CF_UNICODETEXT, {65, 0, 0, 0}); Key('V', {true});
  uint32_t w = 0, h = 0;
  Check(!pc_clipboard_size(engine, &w, &h) && docInfo["layers"].size() == before, "text clipboard must not paste an old image");
  Format(CF_DIBV5, {1, 2, 3}); Key('V', {true}); Check(docInfo["layers"].size() == before, "malformed clipboard must leave document unchanged");
}
void DropMessage(const std::filesystem::path& file, float x, float y, bool shift = false) {
  std::wstring paths = file.wstring(); paths.push_back(0); paths.push_back(0);
  size_t bytes = sizeof(DROPFILES) + paths.size() * sizeof(wchar_t);
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes); Check(h != nullptr, "drop allocation failed");
  auto* p = static_cast<DROPFILES*>(GlobalLock(h)); p->pFiles = sizeof(DROPFILES); p->pt = {LONG(std::lround(x * dpiScale)), LONG(std::lround(y * dpiScale))}; p->fWide = TRUE;
  memcpy(reinterpret_cast<uint8_t*>(p) + sizeof(DROPFILES), paths.data(), paths.size() * sizeof(wchar_t)); GlobalUnlock(h);
  ThreadKeys keys(shift ? std::initializer_list<UINT>{VK_SHIFT} : std::initializer_list<UINT>{});
  Proc(hwnd, WM_DROPFILES, WPARAM(h), 0); Drain();
}
void FilesAndTabs(const std::filesystem::path& root) {
  EmptyDocuments(); clipboard::Image im{10, 6, std::vector<uint8_t>(10 * 6 * 4, 255)};
  auto bytes = clipboard::EncodePng(im); auto file = root / L"пример снимка.png";
  { std::ofstream out(file, std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size())); }
  DropMessage(file, 400, 300); Check(sessionInfo["documents"].size() == 1 && docInfo["width"].Int() == 10, "dropping on an empty window must open the image");
  Check(recentDocuments.paths.front() == file.wstring(), "successful open must be remembered with Unicode path");
  for (int depth : {8, 16, 32}) {
    NewDocument(depth); Layout(true); canvas->RotateTo(37, false); canvas->ZoomTo(2, false); canvas->PanBy(15, -8);
    auto p = canvas->ToScreen(80, 70); DropMessage(file, p.x, p.y);
    Check(ActiveLayer() && (*ActiveLayer())["kind"].Str() == "Smart Object", "canvas drop must place an embedded smart object");
    auto bounds = (*ActiveLayer())["bounds"];
    Check(std::abs(bounds[0].Num() - 75) <= 1 && std::abs(bounds[1].Num() - 67) <= 1, "drop must use the rotated and zoomed document coordinate");
    Undo(); Drain(); Check(docInfo["layers"].size() == 1, "placed file must be one undo step");
    DropMessage(file, p.x, p.y, true); bounds = (*ActiveLayer())["bounds"];
    Check(bounds[0].Int() == 95 && bounds[1].Int() == 77, "Shift drop must center in document");
  }
  auto original = ActiveDocumentId(); auto count = sessionInfo["documents"].size();
  NewDocument(); auto last = ActiveDocumentId();
  Key(VK_TAB, {true, true}); Check(ActiveDocumentId() == original, "Ctrl+Shift+Tab must switch to previous document");
  Key(VK_TAB, {true}); Check(ActiveDocumentId() == last, "Ctrl+Tab must switch forward");
  Key(VK_TAB, {true}); Check(sessionInfo["active"].Int() == 0, "Ctrl+Tab must wrap");
  SwitchDocument(last); Drain(); Layout(true);
  Check(documentTabs->children.size() == count + 1 && !startScreen->visible, "tabs must correspond to open documents");
  auto* tab = static_cast<DocumentTab*>(documentTabs->children.back().get());
  PointerEvent e; e.x = tab->rect.left + 10; e.y = MidY(tab->rect); e.button = 1; tab->OnDown(e);
  Check(tabMenu && tabMenu->items.size() == 3 && !tabMenu->items[2].enabled, "tab context must offer Close, Close Others, Reveal with path availability");
  ui::ClosePopovers();
  documentTabs->reorder(last, 0); Drain(); Check(sessionInfo["active"].Int() == 0 && ActiveDocumentId() == last, "reordering tabs must preserve active document");
  SwitchDocument(original); Drain(); Check(ActiveDocumentId() == original, "switch by id must survive reorder");
  {
    Layout(true);
    auto ids = [] { std::vector<uint64_t> v; for (auto& c : documentTabs->children) v.push_back(static_cast<DocumentTab*>(c.get())->id); return v; };
    auto before = ids(); Check(before.size() >= 2, "tab drag test needs two tabs");
    auto* first = static_cast<DocumentTab*>(documentTabs->children[0].get());
    first->reorder(before[0], MidX(documentTabs->children[0]->rect) + 2); Drain(); Layout(true);
    Check(ids() == before, "a tab dragged short of its neighbour's middle must not move");
    first = static_cast<DocumentTab*>(documentTabs->children[0].get());
    first->reorder(before[0], MidX(documentTabs->children[1]->rect) + 2); Drain(); Layout(true);
    auto after = ids(); Check(after[0] == before[1] && after[1] == before[0], "a tab dragged right past its neighbour must land right after it, not one place further");
    auto* moved = static_cast<DocumentTab*>(documentTabs->children[1].get());
    moved->reorder(before[0], MidX(documentTabs->children[0]->rect) - 2); Drain(); Layout(true);
    Check(ids() == before, "a tab dragged left before its neighbour must return to its place");
    moved = static_cast<DocumentTab*>(documentTabs->children[0].get());
    moved->reorder(before[0], documentTabs->rect.right); Drain(); Layout(true);
    Check(ids().back() == before[0], "a tab dropped past the last tab must become the last");
    documentTabs->reorder(before[0], 0); Drain(); Layout(true);
  }
  for (int i = 0; i < 12; i++) NewDocument(); Layout(true);
  auto* selectedTab = static_cast<DocumentTab*>(documentTabs->children.back().get());
  Check(selectedTab->rect.right <= documentTabs->rect.right + 1, "active tab must scroll into view");
  documentTabs->OnWheel(3, {}); auto offset = documentTabs->children.front()->rect.left;
  Check(offset <= documentTabs->rect.left, "tab overflow must scroll without leaving a gap");
}
void CloseAndRecent(const std::filesystem::path& root) {
  EmptyDocuments(); NewDocument(); Rectangle();
  auto id = ActiveDocumentId(); size_t count = sessionInfo["documents"].size();
  closePromptOverride = [](const std::wstring&) { return IDCANCEL; };
  CloseDocument(id); Drain(); Check(sessionInfo["documents"].size() == count && ActiveDocumentId() == id, "Cancel must keep dirty document open");
  closePromptOverride = [](const std::wstring&) { return IDYES; };
  fileDialogOverride = [](bool, const std::wstring&) { return std::wstring{}; };
  CloseDocument(id); Drain(); Check(ActiveDocumentId() == id, "cancelling Save As must keep the tab open");
  fileDialogOverride = [root](bool, const std::wstring&) { return (root / L"пример снимка.png" / L"bad.pcraft").wstring(); };
  CloseDocument(id); Drain(); Check(ActiveDocumentId() == id && sessionInfo["documents"].size() == count, "failed save must keep dirty tab open");
  auto saved = root / L"сохранённый документ.pcraft";
  fileDialogOverride = [saved](bool, const std::wstring&) { return saved.wstring(); };
  CloseDocument(id); Drain(); Check(docInfo.IsNull() && std::filesystem::exists(saved), "save-before-close must wait for completed save");
  OpenPath(saved.wstring()); Drain(); Check(!sessionInfo["documents"][0]["dirty"].Truthy(), "saved document must reopen cleanly");
  NewDocument(); Rectangle(); auto keep = ActiveDocumentId();
  closePromptOverride = [](const std::wstring&) { return IDNO; };
  NewDocument(); Rectangle(); std::vector<uint64_t> other;
  const auto& docs = sessionInfo["documents"];
  for (size_t n = 0; n < docs.size(); n++) if (uint64_t(docs[n]["id"].Int()) != keep) other.push_back(uint64_t(docs[n]["id"].Int()));
  CloseDocuments(other, [keep](bool ok) { if (ok) SwitchDocument(keep); }); Drain();
  Check(sessionInfo["documents"].size() == 1 && ActiveDocumentId() == keep, "Close Others must retain requested document despite changing indices");
  closePromptOverride = [](const std::wstring&) { return IDCANCEL; };
  Check(!CloseRequested(), "window close must wait for document decision"); Drain();
  Check(!closingDocuments && sessionInfo["documents"].size() == 1, "cancelled window close must leave application usable");
  closePromptOverride = [](const std::wstring&) { return IDNO; }; CloseDocument(keep); Drain();
  Check(startScreen->visible && !documentTabs->visible, "empty session must display recent start screen");
  Layout(true);
  auto* recentButton = dynamic_cast<RecentFileButton*>(startScreen->children[5].get());
  Check(recentButton != nullptr, "start screen must contain recent file buttons");
  PointerEvent click; click.x = MidX(recentButton->rect); click.y = MidY(recentButton->rect);
  ui::PointerDown(click); ui::PointerUp(click); Drain();
  Check(docInfo["path"].Str() == Narrow(saved.wstring()), "clicking a recent file must open it");
  CloseDocument(ActiveDocumentId()); Drain();
  auto rows = RecentMenu(); Check(rows.size() >= 3 && rows.back().label == L"Clear recent documents", "recent menu must contain paths and clear action");
  RecentDocuments restored(recentTestKey); restored.Load();
  Check(restored.paths == recentDocuments.paths, "recent paths must survive a new store instance");
  auto previous = restored.paths.size(); restored.Add(saved.wstring()); restored.Add(saved.wstring());
  Check(restored.paths.size() == previous && restored.paths.front() == saved.wstring(), "recent list must deduplicate and put latest first");
  for (int n = 0; n < 25; n++) restored.Add((root / (L"recent-" + std::to_wstring(n) + L".png")).wstring());
  Check(restored.paths.size() == 20, "recent list must be bounded");
  auto before = sessionInfo["documents"].size(); OpenPath((root / L"does-not-exist.png").wstring()); Drain();
  Check(sessionInfo["documents"].size() == before && recentDocuments.paths.front() == saved.wstring(), "failed opens must not enter recent files");
  fileDialogOverride = nullptr; closePromptOverride = nullptr;
}
void ClipboardPerformance(const std::filesystem::path& root) {
  clipboard::Image im{6000, 4000, std::vector<uint8_t>(6000ull * 4000 * 4)};
  uint32_t state = 1;
  for (size_t n = 0; n < im.rgba.size(); n += 4) {
    state = state * 1664525 + 1013904223;
    im.rgba[n] = uint8_t(state >> 24); im.rgba[n + 1] = uint8_t(state >> 16); im.rgba[n + 2] = uint8_t(state >> 8); im.rgba[n + 3] = 255;
  }
  std::shared_ptr<clipboard::PreparedImage> prepared;
  auto start = std::chrono::steady_clock::now();
  jobs::Run([&] { prepared = clipboard::Prepare(im); });
  size_t frames = 0; double longestFrame = 0;
  while (jobs::Busy()) {
    auto before = ui::NowMs(); Frame(); longestFrame = std::max(longestFrame, ui::NowMs() - before); frames++; jobs::Pump();
  }
  auto encode = std::chrono::steady_clock::now();
  std::wstring error; Check(clipboard::Publish(hwnd, prepared, error), "24 MP clipboard publish failed");
  auto publish = std::chrono::steady_clock::now();
  clipboard::Image read;
  clipboard::Result result = clipboard::Result::Error;
  jobs::Run([&] { result = clipboard::Read(hwnd, read, error); });
  while (jobs::Busy()) { Frame(); jobs::Pump(); }
  auto end = std::chrono::steady_clock::now();
  Check(result == clipboard::Result::Image && read.rgba == im.rgba && read.width == im.width && read.height == im.height, "24 MP clipboard pixels must round trip exactly");
  Check(frames > 2, "large clipboard encoding must leave the UI thread free to draw frames");
  auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
  std::ofstream report(root / L"clipboard-perf.txt");
  report << "24 MP deterministic RGB noise (6000 x 4000), release build\n"
         << "worker encode + HGLOBAL prepare: " << ms(start, encode) << " ms\n"
         << "UI clipboard handle publication: " << ms(encode, publish) << " ms\n"
         << "worker PNG clipboard read/decode: " << ms(publish, end) << " ms\n"
         << "UI frames during encoding: " << frames << ", longest frame: " << longestFrame << " ms\n";
  std::cout << "24 MP clipboard round trip passed; encoding " << ms(start, encode) << " ms, UI publication " << ms(encode, publish) << " ms, decode " << ms(publish, end) << " ms\n";
}
}  // namespace

int main(int argc, char** argv) {
  int status = 0;
  PrivateDesktop isolated;
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); SetConsoleOutputCP(CP_UTF8);
  WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = GetModuleHandle(nullptr); wc.lpszClassName = L"VetroSystemTests";
  RegisterClassW(&wc);
  hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, wc.lpszClassName, L"PhotoCraft system test", WS_OVERLAPPEDWINDOW, 0, 0, 1600, 1000, nullptr, nullptr, wc.hInstance, nullptr);
  ui::Init(hwnd); ui::SetBounds(R(0, 0, 1584, 961));
  if (!GfxCreate(hwnd, 1)) { std::cerr << "graphics initialization failed on private test desktop\n"; return 1; }
  engine = pc_new(); jobs::Init(hwnd, WM_JOBS); LoadCatalog();
  canvas = new Canvas(engine); ui::AddLayer(canvas); ui::AddLayer(new SelectionOverlay());
  if (GetEnvironmentVariableW(L"VETRO_TEST_GPU", nullptr, 0)) { uint32_t low = 0; int32_t high = 0; if (GfxAdapterLuid(&low, &high)) canvas->UseGpu(low, high); }
  BuildTools(); BuildDocks(); BuildShortcuts(); SelectTool(&sections[2][1][0]); Drain(); ui::SetGate(InputAllowed);
  recentDocuments = RecentDocuments(recentTestKey);
  std::filesystem::path root = argc > 1 ? std::filesystem::path(Widen(argv[1])) : std::filesystem::temp_directory_path() / L"vetro-milestone2";
  root = std::filesystem::absolute(root); std::filesystem::create_directories(root);
  try {
    Codecs(); ClipboardCommands(); FilesAndTabs(root); CloseAndRecent(root);
    i18n::Load("ru"); Sleep(3000); Refresh(); Layout(true); Capture(root / L"milestone2-start-ru.png", false);
    OpenPath((root / L"сохранённый документ.pcraft").wstring()); Drain();
    NewDocument(); Command("shape.create", "{\"kind\":\"rect\",\"rect\":[30,30,70,70],\"fill\":\"#ff0000\"}");
    Tool("move"); moveControls = true; RebuildOptions(); Layout(true); canvas->Fit(false); Capture(root / L"milestone2-tabs-ru.png");
    auto* tab = static_cast<DocumentTab*>(documentTabs->children.back().get()); documentTabs->context(tab->id, tab->rect);
    Capture(root / L"milestone2-tab-menu-ru.png"); ui::ClosePopovers();
    auto fileMenu = BuildMenu("File"); bool recent = false; for (auto& row : fileMenu) recent |= row.label == L"Recent documents" && !row.submenu.empty();
    Check(recent, "File menu must include Recent Documents submenu");
    if (argc > 2 && std::string(argv[2]) == "--benchmark") ClipboardPerformance(root);
    std::cout << checks << " system checks passed\n";
  } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; status = 1; }
  recentDocuments.Clear(); RegDeleteKeyW(HKEY_CURRENT_USER, recentTestKey.c_str());
  FinishNudge(); Drain(); jobs::Shutdown(); delete canvas; canvas = nullptr; pc_free(engine); engine = nullptr;
  GfxDestroy(); DestroyWindow(hwnd); CoUninitialize(); return status;
}
