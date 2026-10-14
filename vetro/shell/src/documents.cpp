#include "documents.h"
#include "icons_phosphor.h"
#include "i18n.h"
#include <filesystem>

void RecentDocuments::Load() {
  paths.clear(); DWORD bytes = 0, type = 0;
  if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), L"RecentDocuments", RRF_RT_REG_MULTI_SZ, &type, nullptr, &bytes) != ERROR_SUCCESS || bytes > 131072 || bytes < 4) return;
  std::vector<wchar_t> data(bytes / sizeof(wchar_t) + 2, 0);
  if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), L"RecentDocuments", RRF_RT_REG_MULTI_SZ, nullptr, data.data(), &bytes) != ERROR_SUCCESS) return;
  size_t start = 0;
  while (start < data.size() && data[start] && paths.size() < 20) {
    size_t end = start; while (end < data.size() && data[end]) end++;
    if (end == data.size()) break;
    paths.emplace_back(data.data() + start, end - start); start = end + 1;
  }
}
bool RecentDocuments::Save() const {
  std::vector<wchar_t> data;
  for (const auto& p : paths) { data.insert(data.end(), p.begin(), p.end()); data.push_back(0); }
  data.push_back(0); if (data.size() == 1) data.push_back(0);
  return RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), L"RecentDocuments", REG_MULTI_SZ, data.data(), DWORD(data.size() * sizeof(wchar_t))) == ERROR_SUCCESS;
}
bool RecentDocuments::Add(const std::wstring& file) {
  std::error_code ec;
  auto path = std::filesystem::absolute(std::filesystem::path(file), ec).lexically_normal().wstring();
  if (ec || path.empty() || path.size() > 3000 || path.find(L'\0') != std::wstring::npos) return false;
  paths.erase(std::remove_if(paths.begin(), paths.end(), [&](const auto& p) { return _wcsicmp(path.c_str(), p.c_str()) == 0; }), paths.end());
  paths.insert(paths.begin(), std::move(path)); if (paths.size() > 20) paths.resize(20); return Save();
}
bool RecentDocuments::Clear() { paths.clear(); return Save(); }

DocumentTab::DocumentTab(uint64_t document) : id(document) {
  close = Add<IconButton>(PhX, L"Close document", [this] { if (dismiss) dismiss(id); }); close->iconSize = 14;
}
void DocumentTab::Layout() { close->rect = R(rect.right - 30, rect.top + 4, rect.right - 4, rect.bottom - 4); }
void DocumentTab::Paint() {
  float a = std::clamp(selected.v, 0.f, 1.f);
  FillRound(rect, 7, Fade(P.hover, .4f * a + .2f * hover.v));
  if (a > .01f) Line(rect.left + 12, rect.bottom - 2, rect.right - 12, rect.bottom - 2, Fade(P.accent, a), 2);
  Text(name + (dirty ? L" *" : L""), R(rect.left + 12, rect.top, close->rect.left - 4, rect.bottom), F_Row, a > .5f ? P.text : P.faint);
  Container::Paint();
}
bool DocumentTab::Animate(float dt) { return selected.Step(dt) | Container::Animate(dt); }
void DocumentTab::OnDown(const PointerEvent& e) {
  downX = e.x; dragging = false; right = e.button == 1;
  if (right) { if (context) context(id, rect); }
  else if (activate) activate(id);
}
void DocumentTab::OnDrag(const PointerEvent& e) { if (!right && std::abs(e.x - downX) > 8) dragging = true; }
void DocumentTab::OnUp(const PointerEvent& e, bool inside) {
  if (dragging && reorder) reorder(id, e.x);
  dragging = false; Widget::OnUp(e, inside);
}
void DocumentTabs::Sync(const Json& s) {
  const auto& docs = s["documents"];
  auto i = s["active"].Int(-1); uint64_t next = i >= 0 ? uint64_t(docs[size_t(i)]["id"].Int()) : 0;
  std::string sig;
  for (size_t n = 0; n < docs.size(); n++) sig += std::to_string(docs[n]["id"].Int()) + "/";
  if (sig != signature) {
    signature = sig; Clear();
    for (size_t n = 0; n < docs.size(); n++) {
      auto* tab = Add<DocumentTab>(uint64_t(docs[n]["id"].Int()));
      tab->activate = activate; tab->dismiss = dismiss; tab->context = context;
      tab->reorder = [this](uint64_t id, float x) {
        if (!reorder || children.empty()) return;
        // The slot (0..count) in the current order the tab was dropped into; `document.move` takes
        // the final index, which is one less when the tab moves right past its own place.
        size_t slot = children.size(), from = children.size();
        for (size_t n = 0; n < children.size(); n++) {
          if (static_cast<DocumentTab*>(children[n].get())->id == id) from = n;
          if (slot == children.size() && x < MidX(children[n]->rect)) slot = n;
        }
        if (from == children.size()) return;
        reorder(id, slot > from ? slot - 1 : slot);
      };
    }
  }
  for (size_t n = 0; n < children.size(); n++) {
    auto* t = static_cast<DocumentTab*>(children[n].get());
    t->name = Widen(docs[n]["name"].Str()); t->path = Widen(docs[n]["path"].Str()); t->dirty = docs[n]["dirty"].Truthy();
    t->tooltip = t->path.empty() ? t->name : t->path;
    t->width = std::clamp(Measure(t->name + (t->dirty ? L" *" : L""), F_Row) + 56, 120.f, 250.f);
    t->selected.To(t->id == next ? 1.f : 0.f);
  }
  visible = !children.empty(); Layout();
  if (next != active) { active = next; RevealActive(); }
}
void DocumentTabs::Layout() {
  total = 0; for (auto& t : children) total += static_cast<DocumentTab*>(t.get())->width + 4;
  offset = std::clamp(offset, 0.f, std::max(0.f, total - W(rect)));
  float x = rect.left - offset;
  for (auto& t : children) { float w = static_cast<DocumentTab*>(t.get())->width; t->rect = RWH(x, rect.top, w, H(rect)); t->Layout(); x += w + 4; }
}
void DocumentTabs::RevealActive() {
  for (auto& t : children) if (static_cast<DocumentTab*>(t.get())->id == active) {
    if (t->rect.left < rect.left) offset -= rect.left - t->rect.left;
    else if (t->rect.right > rect.right) offset += t->rect.right - rect.right;
    Layout(); break;
  }
}
void DocumentTabs::Scroll(float dx) { offset += dx; Layout(); ui::RequestFrame(); }
bool DocumentTabs::OnWheel(float delta, const PointerEvent&) { Scroll(-delta * 120); return true; }
void DocumentTabs::Paint() {
  Glass(rect, 8, 1, GlassKind::Dock);
  Dc()->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_ALIASED); Container::Paint(); Dc()->PopAxisAlignedClip();
}
Widget* DocumentTabs::HitTest(float x, float y) { return Inside(rect, x, y) ? Container::HitTest(x, y) : nullptr; }

RecentFileButton::RecentFileButton(std::wstring file, std::function<void()> open) : TextButton(L"", Quiet, std::move(open)), path(std::move(file)) {
  label = std::filesystem::path(path).filename().wstring(); tooltip = path;
}
void RecentFileButton::Paint() {
  FillRound(rect, tok::RadiusControl, Fade(P.hover, .5f * hover.v));
  Text(label, R(rect.left + 12, rect.top + 3, rect.right - 12, rect.top + 29), F_Row, P.text);
  Text(path, R(rect.left + 12, rect.top + 28, rect.right - 12, rect.bottom - 3), F_Small, P.faint);
}
void StartScreen::Sync(const std::vector<std::wstring>& recent) {
  if (built && last == recent) return;
  built = true; last = recent; offset = 0; Clear();
  Add<Label>(L"PhotoCraft by Vetro", F_Title);
  Add<Label>(L"Create a document or open an image", F_Row);
  Add<TextButton>(L"New document…", TextButton::Primary, create);
  Add<TextButton>(L"Open…", TextButton::Secondary, open);
  Add<Label>(L"Recent documents", F_Title);
  if (recent.empty()) Add<Label>(L"Opened files will appear here", F_Row);
  else for (auto& path : recent) Add<RecentFileButton>(path, [this, path] { if (openPath) openPath(path); });
  Layout();
}
void StartScreen::Layout() {
  float width = std::max(0.f, std::min(600.f, W(rect) - 48));
  float x = MidX(rect) - width / 2, y = rect.top + 30 - offset;
  for (size_t i = 0; i < children.size(); i++) {
    float height = i < 5 ? 38.f : 60.f;
    children[i]->rect = RWH(x, y, width, height);
    if (i == 2) { children[i]->rect.right = x + width / 2 - 6; continue; }
    if (i == 3) children[i]->rect.left = x + width / 2 + 6;
    y += height + (i == 3 ? 28.f : 8.f);
  }
  contentHeight = y - rect.top + offset; offset = std::clamp(offset, 0.f, std::max(0.f, contentHeight - H(rect)));
}
void StartScreen::Paint() { Dc()->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_ALIASED); Container::Paint(); Dc()->PopAxisAlignedClip(); }
Widget* StartScreen::HitTest(float x, float y) { return Inside(rect, x, y) ? Container::HitTest(x, y) : nullptr; }
bool StartScreen::OnWheel(float delta, const PointerEvent&) { offset = std::clamp(offset - delta * 100, 0.f, std::max(0.f, contentHeight - H(rect))); Layout(); ui::RequestFrame(); return true; }
