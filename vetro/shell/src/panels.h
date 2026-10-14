#pragma once
#include "shell.h"
#include "i18n.h"

// Tool panels share one scrollable layout and the same controls as the options bar.
class ToolPanel : public Dock {
 public:
  std::wstring title;
  ScrollArea* body;
  std::vector<float> heights;
  TextButton* close;
  explicit ToolPanel(std::wstring name) : title(std::move(name)) {
    visible = false;
    body = Add<ScrollArea>();
    close = Add<TextButton>(L"×", TextButton::Quiet, [this] { visible = false; });
    home = [](const Rect& w, D2D1_SIZE_F s) { return RWH(w.left + 110, w.top + 160, s.width, s.height); };
  }
  D2D1_SIZE_F Natural() override { return {340, std::clamp(H(ui::Bounds()) - 230, 240.f, 580.f)}; }
  void Arrange() override {
    close->rect = R(rect.right - 42, rect.top + 6, rect.right - 8, rect.top + 38);
    body->rect = R(rect.left + 12, rect.top + 48, rect.right - 12, rect.bottom - 12);
    float y0 = body->rect.top - body->Scroll();
    for (size_t i = 0; i < body->children.size(); i++) {
      float h = i < heights.size() ? heights[i] : 30;
      body->children[i]->rect = R(body->rect.left, y0, body->rect.right - 6, y0 + h);
      y0 += h + 6;
    }
    body->contentHeight = y0 + body->Scroll() - body->rect.top;
    body->Clamp();
  }
  void PaintBody() override {
    Text(i18n::Tr(title), R(rect.left + 16, rect.top + 6, rect.right - 48, rect.top + 38), F_Section, P.text);
    Line(rect.left + 12, rect.top + 44, rect.right - 12, rect.top + 44, P.sep);
  }
  void Reset() {
    body->Clear();
    heights.clear();
  }
  template <class T, class... A>
  T* Row(float height, A&&... args) {
    heights.push_back(height);
    return body->Add<T>(std::forward<A>(args)...);
  }
};

void InitializeToolPanels();
void LayoutToolPanels(bool snap);
bool ToolPanelCommand(const std::string& id);
ToolPanel* BrushPanel(bool settings);
void OpenBrushPalette(const PointerEvent& e);
void ReadBrushState(const Json& result);
void InitializeLayerControls();
void LayoutLayerControls(bool snap);
bool LayerPanelCommand(const std::string&);
void SyncLayerControls();
bool LayerMatches(const Json&);
void AddLayerAccessories(Container&, const Json&);
float LayerAccessoryWidth(const Json&);
void PlaceLayerAccessories(Container&, const Rect& thumb);
void PaintLayerIndicators(const Json&, const Rect&);
void AddLayerDetails(Container&, const Json&);
void ClearLayerDetails();
float LayerDetailHeight(Widget*);
void SyncAdjustmentProperties();
ScrollArea* BuildAdjustmentProperties(Container&);
void LayoutAdjustmentProperties(const Rect&);
void FlushAdjustmentPreview();
void FinishAdjustmentPreview();
void RebuildLayerFilter();
void InitializeRetouchPanel();
void LayoutRetouchPanel(bool snap);
bool RetouchPanelCommand(const std::string&);
void RefreshCloneSource();
Json CloneSourceState();
void DrawCloneOverlay(double x, double y, const std::string& sample, bool stroking);
CanvasTool* NewPatchTool(bool move);
CanvasTool* NewTypeTool(bool vertical);
bool TextEditing();
bool TextEditorKey(UINT vk, Mods mods);
bool TextEditorChar(wchar_t c);
void FinishTextEditing(bool restoreTool = true);
void InitializeTextPanels();
void LayoutTextPanels(bool snap);
bool TextPanelCommand(const std::string& id);
void SyncTextPanels();
ToolPanel* TextPanel(bool paragraph);
Json TextEditorState();
void EditTypeLayer(uint64_t layer, bool selectAll, std::function<void()> restore = nullptr);
