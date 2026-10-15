#pragma once
// The canvas: the document as the backdrop of the whole interface (design.md §5: content is the
// background, never a card). Pixels come from the engine's own compositor through the bridge.
//
// The GPU path comes first: the engine's wgpu compositor (Direct3D 12) draws the whole document
// at full resolution into a texture shared with this Direct3D 11 device, and edits redraw only the
// rect they damaged. Direct2D shows that texture scaled to the view, through the colour table.
//
// The CPU path is the fallback (no shared adapter, a document too large for one texture, or one
// the GPU compositor doesn't cover yet). Two levels are kept. The base holds the whole document
// reduced by an integer factor k, so a fit view never waits. When the view needs more pixels than
// the base has, a detail level renders the visible part at the screen's resolution. Both sit on
// integer k grids, which is what makes a damaged area re-render to exactly the pixels a full render
// would give: an engine edit or a live brush dab updates only the tiles it touched.
#include "bridge.h"
#include "widgets.h"
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

struct DocRect {
  int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  bool Empty() const { return x1 <= x0 || y1 <= y0; }
  int32_t W() const { return x1 - x0; }
  int32_t H() const { return y1 - y0; }
};

// What the active tool does with the pointer on the canvas, in document pixels.
struct CanvasTool {
  virtual ~CanvasTool() = default;
  virtual void Down(double x, double y, const PointerEvent& e) = 0;
  virtual void Drag(double x, double y, const PointerEvent& e) = 0;
  virtual void Up(double x, double y, const PointerEvent& e) = 0;
  virtual void Overlay() {}  // drawn over the document, under the docks
  virtual bool ShowsSelection() const { return true; }
  virtual HCURSOR Cursor() { return LoadCursor(nullptr, IDC_CROSS); }
  virtual bool HidesCursor() { return false; }
  virtual void Activate() {}                     // it became the current tool
  virtual void Tick() {}                         // once a frame, before painting (queued previews)
  // The options dock: what this tool adds after the document pill (null: the shell's defaults).
  virtual bool Options(Container& dock, std::vector<Widget*>& out) { (void)dock; (void)out; return false; }
  virtual bool Key(UINT, Mods) { return false; }  // Enter, Esc, Delete... while the tool is current
  virtual void Deactivate() {}                    // another tool takes over
  virtual void Cancel() { Deactivate(); }
  virtual bool SpaceRepositions() const { return false; }
  // A tool with a brush diameter of its own (Liquify): Alt+wheel and Alt+right-drag size it, and
  // BrushSized() tells it to show the new value. Null: the shared brush, if the tool paints.
  virtual double* BrushSize() { return nullptr; }
  virtual void BrushSized() {}
  // A right click at (x, y): the rows of this tool's context menu (none: the canvas's own).
  virtual void Context(double x, double y, const PointerEvent& e, std::vector<MenuItem>& rows) { (void)x; (void)y; (void)e; (void)rows; }
};

class Canvas : public Widget {
 public:
  explicit Canvas(PcBridge* engine);
  ~Canvas() override;

  // The GPU path, on the adapter the window draws with (call once, after Gfx is up).
  void UseGpu(uint32_t luidLow, int32_t luidHigh);
  bool OnGpu() const { return mode == Mode::Gpu && gpuShown; }
  std::wstring PathName() const;  // "GPU", "CPU" (and why) for the status line

  // The document.
  void Reload();                  // a new active document (or none)
  void Sync();                    // after any engine call: re-render what changed
  void Damage(const DocRect& r);  // a live stroke touched this
  bool HasDocument() const { return docW > 0; }
  uint32_t DocW() const { return docW; }
  uint32_t DocH() const { return docH; }

  // The view.
  Rect free{};  // the part of the window the docks leave
  void Fit(bool animate);
  void ActualPixels(bool animate);
  void ZoomBy(float factor, float x, float y);
  void ZoomTo(float z, bool animate);
  void PanBy(float dx, float dy);
  void RotateTo(float degrees, bool animate = true);
  float Rotation() const { return angle.target; }
  float Zoom() const { return zoom.target; }
  D2D_POINT_2F ToDoc(float x, float y) const;
  D2D_POINT_2F ToScreen(double u, double v) const;
  Rect DocOnScreen() const;

  // Frame.
  bool SceneDirty() const { return sceneDirty; }
  void Invalidate() { sceneDirty = true; }  // something about how it is shown changed
  void PaintScene();             // backdrop + document, into the scene pass
  void Paint() override;         // tool overlays, in the interface pass
  bool Animate(float dt) override;
  void TakeResults();            // upload what the render thread finished
  bool Busy();

  CanvasTool* tool = nullptr;
  bool spaceHand = false;  // Space held: the hand, whatever the tool
  int spaceZoom = 0;  // Ctrl+Space / Alt+Space: temporary zoom, +1 / -1
  bool Drawing() const { return toolActive; }
  void CancelInput();
  // A right click on the document (document pixels): the shell opens a context menu. Tools never
  // see the right button as a press.
  std::function<void(double x, double y, const PointerEvent& e)> onContext;
  std::function<bool(double x, double y, const PointerEvent& e)> onSample;
  // View snapping is pure shell geometry captured once at gesture start (0 down, 1 drag, 2 up).
  std::function<D2D_POINT_2F(D2D_POINT_2F, const PointerEvent&, int)> onToolPoint;
  // Alt+wheel (notches) and Alt+right-drag (screen offset from the press; phase 0 press, 1 move,
  // 2 release) size the brush, as in Photoshop. False: the tool has no brush to size.
  std::function<bool(float notches)> onBrushWheel;
  std::function<bool(float dx, float dy, int phase)> onBrushDrag;
  void OnDown(const PointerEvent& e) override;
  void OnDrag(const PointerEvent& e) override;
  void OnUp(const PointerEvent& e, bool inside) override;
  bool OnWheel(float delta, const PointerEvent& e) override;
  HCURSOR Cursor();
  bool PointerOver() const { return hover.target > .5f; }

 private:
  struct Level {
    int k = 1;
    DocRect rect;  // on the k grid
    uint32_t w = 0, h = 0;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
  };
  struct Job {
    std::shared_ptr<Snapshot> snap;
    int level = 0;
    int k = 1;
    DocRect rect;
    uint64_t seq = 0;
  };
  struct Result {
    int level = 0, k = 1;
    DocRect rect;
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> px;
    uint64_t seq = 0;
  };

  // The GPU path. The PcGpu lives on the render thread; the UI thread only sees results.
  enum class Mode { Cpu, Gpu };
  struct GpuJob {
    std::shared_ptr<Snapshot> snap;
    DocRect region;
    bool attach = false;  // a new canvas texture of the document's size first
    uint32_t w = 0, h = 0;
    uint64_t seq = 0;
  };
  struct GpuResult {
    int status = 0;  // pc_gpu_render: 1 done, 2 not for this document, 0 error
    void* handle = nullptr;
    uint32_t w = 0, h = 0;
    uint64_t seq = 0;
    std::string why;
    std::shared_ptr<Snapshot> snap;  // what it drew (the CPU path starts from it on a fallback)
  };
  Mode mode = Mode::Cpu;
  bool gpuWanted = false, gpuBroken = false, gpuShown = false, gpuInit = false;
  uint32_t luidLow = 0;
  int32_t luidHigh = 0;
  PcGpu* gpu = nullptr;  // render thread only
  std::optional<GpuJob> gpuPending;
  std::vector<GpuResult> gpuDone;
  uint64_t gpuSeq = 0, gpuShowFrom = 0;
  Microsoft::WRL::ComPtr<ID2D1Bitmap1> gpuBitmap;
  uint32_t gpuW = 0, gpuH = 0;
  std::string gpuWhy;
  bool GpuFits() const;
  void RequestGpu(const DocRect& region, bool attach, std::shared_ptr<Snapshot> snap = nullptr);
  void UseCpu(const std::string& why, std::shared_ptr<Snapshot> snap);

  PcBridge* engine;
  uint32_t docW = 0, docH = 0;
  uint64_t revision = 0;
  Level levels[2];               // 0 base, 1 detail
  uint64_t wanted[2] = {0, 0};   // the job each level waits for
  uint64_t shown[2] = {0, 0};    // the last accepted result (edits supersede older jobs)
  Spring zoom{1, motion::ZoomK, motion::ZoomC};
  Spring angle{0, motion::ZoomK, motion::ZoomC};
  PixelSpring cx{0, motion::ZoomK, motion::ZoomC}, cy{0, motion::ZoomK, motion::ZoomC};
  Spring appear{0, motion::PanelK * .7f, motion::PanelC};
  bool sceneDirty = true;
  bool panning = false;
  int zooming = 0;
  float zoomX = 0, zoomY = 0, zoomLastX = 0;
  bool zoomDragged = false, sampling = false;
  bool sizing = false;  // Alt+right-drag in progress
  float sizeX0 = 0, sizeY0 = 0;
  float panX = 0, panY = 0;
  bool toolActive = false;
  double settledAt = 0;

  std::thread worker;
  std::mutex mutex;
  std::condition_variable cv;
  std::optional<Job> pending[2];
  std::vector<Result> done;
  bool quit = false;
  uint64_t seq = 0;
  int working = 0;

  void Work();
  void Request(int level, int k, DocRect rect, std::shared_ptr<Snapshot> snap = nullptr);
  void WantDetail();
  int BaseK() const;
  static DocRect Align(DocRect r, int k);
  void UpdateLevel(Level& l, const DocRect& damage, PcSnapshot* snap);
};
