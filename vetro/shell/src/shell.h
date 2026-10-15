#pragma once
// What the parts of the shell share: the engine handle, the canvas, the views of the document, and
// the ways engine work is run. app.cpp owns all of it; tools.cpp (canvas tools) and editors.cpp
// (dialogs with their own surfaces) build on it.
#include "canvas.h"
#include "jobs.h"
#include "json.h"
#include "widgets.h"
#include <map>
#include <memory>

extern HWND hwnd;
extern PcBridge* engine;
extern Canvas* canvas;
extern Json docInfo, sessionInfo;
extern float pointerX, pointerY;
extern std::map<std::string, std::string> paramsDoc;

struct BrushState {
  double size = 30, hardness = 80, opacity = 100, flow = 100;
  double smoothing = 0;  // %: the brush trails the pointer on a string and catches up
};
extern BrushState brush;

std::wstring W(const std::string& s);
std::string UniqueLayerName(const std::wstring&, bool numbered);
std::string Pt(double x, double y);
std::string PointList(const std::vector<D2D_POINT_2F>& pts, bool pressure);
std::string Hex(const Json& rgba);
Rect ScreenRect(double x0, double y0, double x1, double y1);
Microsoft::WRL::ComPtr<ID2D1PathGeometry> Polyline(const std::vector<D2D_POINT_2F>& pts, bool close);
const Json* ActiveLayer();
void ShowSelectionOutline(const std::vector<D2D_POINT_2F>& polygon);  // the ants follow this shape

// Engine work, queued on the engine thread (jobs.h). `then` runs on the UI thread with the
// command's result (or its error), after the panels have caught up.
using Then = std::function<void(bool ok, const std::string& result)>;
void Exec(const std::string& id, const std::string& params = "{}", Then then = nullptr, bool toastError = true);
void Undo(int steps = 1);
void Refresh();
void RunCommand(const std::string& id, const std::wstring& label);
void RebuildOptions();
std::wstring CommandLabel(const std::string& id);  // its menu label, without the ellipsis

// Context-menu rows: an engine (or shell) command with its menu label, shortcut and availability;
// or any action.
MenuItem CommandItem(const std::string& id, std::wstring label = L"", const wchar_t* icon = nullptr);
MenuItem ActionItem(std::wstring label, std::function<void()> action, const wchar_t* icon = nullptr, bool enabled = true);

// A tool that takes over the canvas for a while (Free Transform, Liquify) and gives it back.
void EnterMode(CanvasTool* t);
void LeaveMode(CanvasTool* t);
CanvasTool* Mode();

// A live preview of one command: applied, and taken back with undo when the values move on. All of
// it runs on the engine thread in order, so a Finish after a Show acts on what the Show did.
class LivePreview {
 public:
  explicit LivePreview(std::string command);
  const std::string& Command() const { return cmd; }
  void Show(const std::string& params);  // undo the last preview, apply these
  void Hide();                           // undo the last preview
  // Keep (applying `params` unless the preview already shows them) or take it back.
  void Finish(bool keep, const std::string& params, std::function<void()> after = nullptr);
  bool Busy() const { return state->inFlight > 0; }
  bool Applied() const { return state->appliedUi; }
 private:
  struct State {
    bool applied = false, appliedUi = false;
    std::string params, error;
    int inFlight = 0;
  };
  std::string cmd;
  std::shared_ptr<State> state;
};

// tools.cpp: the canvas tools beyond the shell's simple ones.
CanvasTool* NewPenTool(int kind);           // 0 Pen, 1 Freeform Pen, 2 Curvature Pen
CanvasTool* NewPathSelectTool(bool direct);  // Path Selection, Direct Selection
CanvasTool* NewSourceTool(const std::string& command);  // Clone Stamp, Healing Brush (Alt sets the source)
void StartTransform(bool selection = false); // Free Transform or selection outline
bool TransformCommand(const std::string& id);
Json TransformState();
void MoveAlignmentOptions(Container&, std::vector<Widget*>&);
CanvasTool* NewShapeTool(const std::string& kind);
ScrollArea* BuildShapeProperties(Container&);
void SyncShapeProperties();
void LayoutShapeProperties(const Rect&);
void FinishParameterPreviews();
void FlushParameterPreviews();
CanvasTool* NewCropTool();
Json CropState();
CanvasTool* NewEyedropperTool(HCURSOR cursor);
void SampleColorAt(double x, double y, bool background);
CanvasTool* NewGradientTool();
void FinishGradientPreview();
void FlushGradientPreview();
void CloseGradientEditor();
Json GradientState();
extern const std::vector<std::wstring> BlendNames;
CanvasTool* NewPerspectiveCropTool();

// editors.cpp: commands with an editor of their own; true when `id` opened one.
bool OpenEditor(const std::string& id, const std::wstring& label, const Rect& from);
Json LiquifyState();
