#pragma once
#include "widgets.h"
#include <functional>

// A bounded MRU of file paths (the engine's document.recent commands are creation presets).
class RecentDocuments {
 public:
  explicit RecentDocuments(std::wstring key = L"Software\\PhotoCraft by Vetro") : key(std::move(key)) {}
  std::vector<std::wstring> paths;
  void Load();
  bool Add(const std::wstring& path);
  bool Clear();
 private:
  std::wstring key;
  bool Save() const;
};

class DocumentTab : public Container {
 public:
  uint64_t id;
  std::wstring name, path;
  bool dirty = false;
  float width = 180;
  Spring selected{0, motion::SlideK, motion::SlideC};
  IconButton* close = nullptr;
  std::function<void(uint64_t)> activate, dismiss;
  std::function<void(uint64_t, Rect)> context;
  std::function<void(uint64_t, float)> reorder;
  explicit DocumentTab(uint64_t document);
  void Layout() override;
  void Paint() override;
  bool Animate(float dt) override;
  bool Interactive() const override { return true; }
  void OnDown(const PointerEvent& e) override;
  void OnDrag(const PointerEvent& e) override;
  void OnUp(const PointerEvent& e, bool inside) override;
 private:
  float downX = 0;
  bool dragging = false, right = false;
};

class DocumentTabs : public Container {
 public:
  bool Interactive() const override { return true; }
  std::function<void(uint64_t)> activate, dismiss;
  std::function<void(uint64_t, Rect)> context;
  std::function<void(uint64_t, size_t)> reorder;
  void Sync(const Json& session);
  void Layout() override;
  void Paint() override;
  Widget* HitTest(float x, float y) override;
  bool OnWheel(float delta, const PointerEvent&) override;
  void RevealActive();
 private:
  float offset = 0, total = 0;
  uint64_t active = 0;
  std::string signature;
  void Scroll(float dx);
};

class RecentFileButton : public TextButton {
 public:
  std::wstring path;
  RecentFileButton(std::wstring file, std::function<void()> open);
  void Paint() override;
};

class StartScreen : public Container {
 public:
  bool Interactive() const override { return true; }
  std::function<void()> create, open;
  std::function<void(const std::wstring&)> openPath;
  void Sync(const std::vector<std::wstring>& recent);
  void Layout() override;
 private:
  float offset = 0, contentHeight = 0;
  std::vector<std::wstring> last;
  bool built = false;
 public:
  void Paint() override;
  Widget* HitTest(float x, float y) override;
  bool OnWheel(float delta, const PointerEvent&) override;
};
