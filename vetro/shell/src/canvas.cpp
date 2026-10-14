// PhotoCraft by Vetro -- the canvas (see canvas.h).
#include "canvas.h"
#include <cmath>

namespace {
constexpr UINT WM_RENDERED = WM_APP + 1;
constexpr int32_t BaseSide = 2600;          // the base level's longest side, at most
constexpr int64_t SyncPixels = 2'500'000;   // damage larger than this re-renders off the UI thread

DocRect Intersect(const DocRect& a, const DocRect& b) {
  DocRect r{std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1), std::min(a.y1, b.y1)};
  if (r.Empty()) return {};
  return r;
}
int32_t FloorTo(int32_t v, int k) { return v >= 0 ? v / k * k : -((-v + k - 1) / k * k); }
int32_t CeilTo(int32_t v, int k) { return v >= 0 ? (v + k - 1) / k * k : -((-v) / k * k); }
}  // namespace

Canvas::Canvas(PcBridge* e) : engine(e) {
  worker = std::thread([this] { Work(); });
}

Canvas::~Canvas() {
  {
    std::lock_guard<std::mutex> lock(mutex);
    quit = true;
  }
  cv.notify_all();
  if (worker.joinable()) worker.join();
}

// ---------------------------------------------------------------------------- render thread --
void Canvas::Work() {
  for (;;) {
    Job job;
    std::optional<GpuJob> gj;
    bool init = false;
    {
      std::unique_lock<std::mutex> lock(mutex);
      cv.wait(lock, [this] { return quit || gpuInit || gpuPending || pending[0] || pending[1]; });
      if (quit) break;
      if (gpuInit) {
        init = true;
        gpuInit = false;
      } else if (gpuPending) {
        gj = std::move(gpuPending);
        gpuPending.reset();
      } else {
        int which = pending[0] ? 0 : 1;  // the base first: it is what a fit view shows
        job = std::move(*pending[which]);
        pending[which].reset();
      }
      working++;
    }
    if (init || gj) {
      GpuResult r;
      if (init) {
        // Device and compositor pipelines: warm before the first document arrives.
        gpu = pc_gpu_new(luidLow, luidHigh);
        r.status = gpu ? 1 : 0;
        if (!gpu) r.why = LastError();
        r.seq = 0;
      } else {
        r.seq = gj->seq;
        r.w = gj->w;
        r.h = gj->h;
        r.snap = gj->snap;
        if (!gpu) {
          r.status = 0;
          r.why = "no GPU canvas";
        } else {
          r.status = 1;
          if (gj->attach) {
            void* h = nullptr;
            if (pc_gpu_target(gpu, gj->w, gj->h, &h) == 1) r.handle = h;
            else { r.status = 2; r.why = LastError(); }
          }
          if (r.status == 1 && gj->snap && *gj->snap) {
            r.status = pc_gpu_render(gpu, gj->snap->p, gj->region.x0, gj->region.y0, gj->region.x1, gj->region.y1);
            if (r.status != 1) r.why = LastError();
          }
        }
      }
      {
        std::lock_guard<std::mutex> lock(mutex);
        working--;
        gpuDone.push_back(std::move(r));
      }
      PostMessage(ui::Window(), WM_RENDERED, 0, 0);
      continue;
    }
    Result r;
    r.level = job.level;
    r.k = job.k;
    r.rect = job.rect;
    r.seq = job.seq;
    r.w = uint32_t(job.rect.W() / job.k);
    r.h = uint32_t(job.rect.H() / job.k);
    bool ok = false;
    if (r.w > 0 && r.h > 0 && job.snap && *job.snap) {
      r.px.resize(size_t(r.w) * r.h * 4);
      uint32_t ow = 0, oh = 0;
      ok = pc_render_view(job.snap->p, job.rect.x0, job.rect.y0, job.rect.x1, job.rect.y1, r.w, r.h, r.px.data(), r.px.size(), size_t(r.w) * 4, &ow, &oh) == 1;
      ok = ok && ow == r.w && oh == r.h;
    }
    {
      std::lock_guard<std::mutex> lock(mutex);
      working--;
      if (ok) done.push_back(std::move(r));
    }
    PostMessage(ui::Window(), WM_RENDERED, 0, 0);
  }
  if (gpu) pc_gpu_free(gpu);
  gpu = nullptr;
}

void Canvas::UseGpu(uint32_t low, int32_t high) {
  {
    std::lock_guard<std::mutex> lock(mutex);
    luidLow = low;
    luidHigh = high;
    gpuInit = true;
  }
  gpuWanted = true;
  cv.notify_one();
}

std::wstring Canvas::PathName() const {
  if (mode == Mode::Gpu) return L"GPU";
  return L"CPU";
}

bool Canvas::GpuFits() const {
  return docW > 0 && docH > 0 && docW <= 16384 && docH <= 16384 && uint64_t(docW) * docH <= 120'000'000ull;
}

void Canvas::RequestGpu(const DocRect& region, bool attach, std::shared_ptr<Snapshot> snap) {
  if (!snap) snap = std::make_shared<Snapshot>(pc_snapshot(engine));
  if (!*snap) return;
  {
    std::lock_guard<std::mutex> lock(mutex);
    GpuJob j;
    j.region = region;
    j.attach = attach;
    if (gpuPending) {
      // Not started yet: one job covering both.
      const DocRect& p = gpuPending->region;
      if (!p.Empty()) j.region = region.Empty() ? p : DocRect{std::min(p.x0, region.x0), std::min(p.y0, region.y0), std::max(p.x1, region.x1), std::max(p.y1, region.y1)};
      j.attach = j.attach || gpuPending->attach;
    }
    j.snap = std::move(snap);
    j.w = docW;
    j.h = docH;
    j.seq = ++gpuSeq;
    gpuPending = std::move(j);
  }
  cv.notify_one();
}

void Canvas::UseCpu(const std::string& why, std::shared_ptr<Snapshot> snap) {
  mode = Mode::Cpu;
  gpuShown = false;
  gpuBitmap.Reset();
  gpuW = gpuH = 0;
  gpuWhy = why;
  if (!docW) return;
  int k = BaseK();
  Request(0, k, Align({0, 0, int32_t(docW), int32_t(docH)}, k), std::move(snap));
  sceneDirty = true;
}

void Canvas::Request(int level, int k, DocRect rect, std::shared_ptr<Snapshot> snap) {
  if (!snap) snap = std::make_shared<Snapshot>(pc_snapshot(engine));
  if (!*snap) return;
  {
    std::lock_guard<std::mutex> lock(mutex);
    Job j;
    j.snap = std::move(snap);
    j.level = level;
    j.k = k;
    j.rect = rect;
    j.seq = ++seq;
    wanted[level] = j.seq;
    pending[level] = std::move(j);  // a newer request for the same level replaces the older
  }
  cv.notify_one();
}

bool Canvas::Busy() {
  std::lock_guard<std::mutex> lock(mutex);
  return working > 0 || pending[0] || pending[1] || gpuPending || gpuInit;
}

void Canvas::TakeResults() {
  std::vector<Result> results;
  std::vector<GpuResult> gpuResults;
  {
    std::lock_guard<std::mutex> lock(mutex);
    results.swap(done);
    gpuResults.swap(gpuDone);
  }
  for (auto& g : gpuResults) {
    if (g.seq == 0) {  // the device
      if (g.status != 1) {
        gpuBroken = true;
        gpuWhy = g.why;
        if (mode == Mode::Gpu) UseCpu(g.why, nullptr);
      }
      continue;
    }
    // Replies retain their original snapshot. An obsolete channel-view rejection must not
    // switch a newer document to CPU and render the old document into its canvas.
    if(g.seq<gpuShowFrom || (g.status!=1 && g.seq<gpuSeq)) {
      if(g.handle)CloseHandle(g.handle);
      continue;
    }
    if (g.handle) {
      gpuBitmap = GfxOpenShared(g.handle);
      gpuW = g.w;
      gpuH = g.h;
      if (!gpuBitmap) {
        gpuBroken = true;
        if (mode == Mode::Gpu) UseCpu("the shared canvas texture could not be opened", g.snap);
        continue;
      }
    }
    if (mode != Mode::Gpu) continue;
    if (g.status == 1) {
      if (g.seq >= gpuShowFrom && gpuBitmap && gpuW == docW && gpuH == docH) {
        gpuShown = true;
        appear.To(1);
      }
      sceneDirty = true;
    } else {
      // Not for this document (or the device failed): the CPU path takes over.
      if (g.status == 0) gpuBroken = true;
      UseCpu(g.why, g.snap);
    }
  }
  if (!gpuResults.empty()) ui::RequestFrame();
  for (auto& r : results) {
    if (r.seq != wanted[r.level]) continue;  // superseded while it rendered
    Level& l = levels[r.level];
    if (l.bitmap && l.w == r.w && l.h == r.h) {
      l.bitmap->CopyFromMemory(nullptr, r.px.data(), r.w * 4);
    } else {
      l.bitmap = MakeBitmap(r.w, r.h, r.px.data(), r.w * 4);
    }
    l.k = r.k;
    l.rect = r.rect;
    l.w = r.w;
    l.h = r.h;
    shown[r.level] = r.seq;
    sceneDirty = true;
    if (r.level == 0) appear.To(1);
  }
  if (!results.empty()) ui::RequestFrame();
}

// -------------------------------------------------------------------------------- document --
int Canvas::BaseK() const {
  int32_t side = int32_t(std::max(docW, docH));
  return std::max(1, (side + BaseSide - 1) / BaseSide);
}

DocRect Canvas::Align(DocRect r, int k) {
  return {FloorTo(r.x0, k), FloorTo(r.y0, k), CeilTo(r.x1, k), CeilTo(r.y1, k)};
}

void Canvas::Reload() {
  auto snap = std::make_shared<Snapshot>(pc_snapshot(engine));
  uint32_t w = 0, h = 0;
  if (*snap) pc_snapshot_size(snap->p, &w, &h);
  bool fresh = w != docW || h != docH;
  docW = w;
  docH = h;
  uint64_t rev = 0;
  int32_t dmg[4]{};
  pc_revision(engine, &rev, dmg);
  revision = rev;
  levels[1] = {};
  gpuShown = false;
  if (!docW) {
    levels[0] = {};
    sceneDirty = true;
    return;
  }
  if (fresh) {
    levels[0] = {};
    appear.Reset(0);
  }
  if (gpuWanted && !gpuBroken && GpuFits()) {
    mode = Mode::Gpu;
    levels[0] = {};
    RequestGpu({0, 0, int32_t(docW), int32_t(docH)}, !gpuBitmap || gpuW != docW || gpuH != docH, snap);
    gpuShowFrom = gpuSeq;
  } else {
    mode = Mode::Cpu;
    int k = BaseK();
    Request(0, k, Align({0, 0, int32_t(docW), int32_t(docH)}, k), snap);
  }
  if (fresh) Fit(false);
  settledAt = ui::NowMs();
  sceneDirty = true;
}

void Canvas::UpdateLevel(Level& l, const DocRect& damage, PcSnapshot* snap) {
  if (!l.bitmap) return;
  DocRect r = Intersect(Align(damage, l.k), l.rect);
  if (r.Empty()) return;
  uint32_t w = uint32_t(r.W() / l.k), h = uint32_t(r.H() / l.k);
  if (!w || !h) return;
  std::vector<uint8_t> px(size_t(w) * h * 4);
  uint32_t ow = 0, oh = 0;
  if (pc_render_view(snap, r.x0, r.y0, r.x1, r.y1, w, h, px.data(), px.size(), size_t(w) * 4, &ow, &oh) != 1 || ow != w || oh != h) return;
  D2D1_RECT_U dst{uint32_t((r.x0 - l.rect.x0) / l.k), uint32_t((r.y0 - l.rect.y0) / l.k), 0, 0};
  dst.right = dst.left + w;
  dst.bottom = dst.top + h;
  l.bitmap->CopyFromMemory(&dst, px.data(), w * 4);
}

void Canvas::Damage(const DocRect& r) {
  if (!docW || r.Empty()) return;
  if (mode == Mode::Gpu) {
    RequestGpu(r, false);
    sceneDirty = true;
    return;
  }
  Snapshot snap(pc_snapshot(engine));
  if (!snap) return;
  for (int i = 0; i < 2; i++) {
    Level& l = levels[i];
    // An older full render must not overwrite this edit when it eventually completes.
    // This also covers an edit before the very first bitmap arrived.
    if (shown[i] != wanted[i] || (i == 0 && !l.bitmap)) {
      int k = l.bitmap ? l.k : BaseK();
      Request(i, k, l.bitmap ? l.rect : Align({0, 0, int32_t(docW), int32_t(docH)}, k));
      continue;
    }
    if (!l.bitmap) continue;
    DocRect a = Intersect(Align(r, l.k), l.rect);
    int64_t px = int64_t(a.W() / l.k) * (a.H() / l.k);
    if (px > SyncPixels) Request(i, l.k, l.rect);  // too big to wait for: re-render off-thread
    else UpdateLevel(l, r, snap.p);
  }
  sceneDirty = true;
}

void Canvas::Sync() {
  uint64_t rev = 0;
  int32_t dmg[4]{};
  int status = pc_revision(engine, &rev, dmg);
  Snapshot snap(pc_snapshot(engine));
  uint32_t w = 0, h = 0;
  if (snap) pc_snapshot_size(snap.p, &w, &h);
  if (status == 0 || w != docW || h != docH) {
    Reload();
    return;
  }
  if (rev == revision) return;
  // Several steps since the last frame (a preview's undo, then its next apply): their damage
  // together, not just the latest's.
  if (rev != revision + 1) status = pc_damage_since(engine, revision, dmg);
  revision = rev;
  if (status == 2) {
    Damage({dmg[0], dmg[1], dmg[2], dmg[3]});
  } else if (mode == Mode::Gpu) {
    RequestGpu({0, 0, int32_t(docW), int32_t(docH)}, false);
  } else {
    // The whole document may have changed: keep showing the old pixels until the new arrive.
    int k = levels[0].bitmap ? levels[0].k : BaseK();
    Request(0, k, levels[0].bitmap ? levels[0].rect : Align({0, 0, int32_t(docW), int32_t(docH)}, k));
    if (levels[1].bitmap) Request(1, levels[1].k, levels[1].rect);
  }
  sceneDirty = true;
}

// ------------------------------------------------------------------------------------ view --
D2D_POINT_2F Canvas::ToDoc(float x, float y) const {
  float z = std::max(zoom.v, 1e-4f);
  double a = angle.v * 0.0174532925199433, c = std::cos(a), s = std::sin(a);
  double dx = x - cx.v, dy = y - cy.v;
  return {float((c * dx + s * dy) / z + docW / 2.0), float((-s * dx + c * dy) / z + docH / 2.0)};
}
D2D_POINT_2F Canvas::ToScreen(double u, double v) const {
  double a = angle.v * 0.0174532925199433, c = std::cos(a), s = std::sin(a);
  double dx = (u - docW / 2.0) * zoom.v, dy = (v - docH / 2.0) * zoom.v;
  return {float(cx.v + c * dx - s * dy), float(cy.v + s * dx + c * dy)};
}
Rect Canvas::DocOnScreen() const {
  Rect r = R(cx.v, cy.v, cx.v, cy.v);
  for (auto p : {ToScreen(0, 0), ToScreen(docW, 0), ToScreen(docW, docH), ToScreen(0, docH)}) {
    r.left = std::min(r.left, p.x); r.top = std::min(r.top, p.y);
    r.right = std::max(r.right, p.x); r.bottom = std::max(r.bottom, p.y);
  }
  return r;
}
void Canvas::Fit(bool animate) {
  if (!docW) return;
  Rect f = W(free) > 50 && H(free) > 50 ? free : ui::Bounds();
  float a = angle.target * .01745329252f, c = std::fabs(std::cos(a)), s = std::fabs(std::sin(a));
  float z = std::min((W(f) - 24) / (c * docW + s * docH), (H(f) - 24) / (s * docW + c * docH));
  z = std::clamp(z, 0.005f, 32.f);
  zoom.To(z);
  cx.To(MidX(f));
  cy.To(MidY(f));
  if (!animate) {
    zoom.Reset(z);
    cx.Reset(MidX(f));
    cy.Reset(MidY(f));
  }
  settledAt = ui::NowMs();
  sceneDirty = true;
}
void Canvas::ActualPixels(bool animate) { ZoomTo(1.f / GfxDpi(), animate); }
void Canvas::ZoomTo(float z, bool animate) {
  Rect f = W(free) > 50 ? free : ui::Bounds();
  float factor = z / std::max(zoom.target, 1e-4f);
  ZoomBy(factor, MidX(f), MidY(f));
  if (!animate) {
    zoom.Reset(zoom.target);
    cx.Reset(cx.target);
    cy.Reset(cy.target);
  }
}
void Canvas::ZoomBy(float factor, float x, float y) {
  if (!docW) return;
  float z0 = std::max(zoom.target, 1e-4f);
  float z1 = std::clamp(z0 * factor, 0.005f, 64.f);
  // The document point under the pointer stays under it.
  float u = (x - cx.target) / z0, v = (y - cy.target) / z0;
  cx.To(x - u * z1);
  cy.To(y - v * z1);
  zoom.To(z1);
  settledAt = ui::NowMs();
  sceneDirty = true;
}
void Canvas::PanBy(float dx, float dy) {
  // A dragged view follows the hand exactly; only zoom and fit glide.
  cx.Reset(cx.v + dx);
  cy.Reset(cy.v + dy);
  sceneDirty = true;
  settledAt = ui::NowMs();
}
void Canvas::RotateTo(float degrees, bool animate) {
  if (!std::isfinite(degrees)) return;
  angle.To(std::clamp(degrees, -3600.f, 3600.f));
  if (!animate) angle.Reset(angle.target);
  sceneDirty = true;
  settledAt = ui::NowMs();
}

void Canvas::WantDetail() {
  if (mode == Mode::Gpu || !docW || !levels[0].bitmap) return;
  float devicePerDoc = zoom.v * GfxDpi();
  int k = devicePerDoc >= 1 ? 1 : std::max(1, int(std::floor(1.f / devicePerDoc)));
  if (k >= levels[0].k) {
    if (levels[1].bitmap) { levels[1] = {}; sceneDirty = true; }
    return;
  }
  Rect win = ui::Bounds();
  auto a = ToDoc(win.left, win.top), b = ToDoc(win.right, win.bottom);
  for (auto p : {ToDoc(win.right, win.top), ToDoc(win.left, win.bottom)}) {
    float lx = std::min({a.x, b.x, p.x}), ly = std::min({a.y, b.y, p.y});
    float rx = std::max({a.x, b.x, p.x}), ry = std::max({a.y, b.y, p.y});
    a = {lx, ly}; b = {rx, ry};
  }
  DocRect canvas{0, 0, int32_t(docW), int32_t(docH)};
  DocRect visible = Intersect({int32_t(std::floor(a.x)), int32_t(std::floor(a.y)), int32_t(std::ceil(b.x)), int32_t(std::ceil(b.y))}, canvas);
  if (visible.Empty()) return;
  const Level& d = levels[1];
  bool covers = d.bitmap && d.k == k && visible.x0 >= d.rect.x0 && visible.y0 >= d.rect.y0 && visible.x1 <= d.rect.x1 && visible.y1 <= d.rect.y1;
  if (covers) return;
  float mx = (b.x - a.x) * .12f, my = (b.y - a.y) * .12f;
  DocRect want{int32_t(std::floor(a.x - mx)), int32_t(std::floor(a.y - my)), int32_t(std::ceil(b.x + mx)), int32_t(std::ceil(b.y + my))};
  want = Align(Intersect(want, canvas), k);
  if (want.Empty()) return;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (pending[1] && pending[1]->k == k && pending[1]->rect.x0 == want.x0 && pending[1]->rect.y0 == want.y0 && pending[1]->rect.x1 == want.x1 &&
        pending[1]->rect.y1 == want.y1)
      return;
    if (wanted[1] && working > 0) return;  // one detail render at a time
  }
  if (int64_t(want.W() / k) * (want.H() / k) > 40'000'000) return;
  Request(1, k, want);
}

bool Canvas::Animate(float dt) {
  bool moving = Widget::Animate(dt);
  bool view = zoom.Step(dt);
  view |= angle.Step(dt);
  view |= cx.Step(dt);
  view |= cy.Step(dt);
  bool a = appear.Step(dt);
  if (view || a) sceneDirty = true;
  moving |= view || a;
  if (view) settledAt = ui::NowMs();
  else if (docW && ui::NowMs() - settledAt > 90) WantDetail();
  if (Busy()) moving = true;
  return moving;
}

// ----------------------------------------------------------------------------------- paint --
void Canvas::PaintScene() {
  Rect win = ui::Bounds();
  // The backdrop: a calm graphite field with a soft pool of light where the document sits.
  auto* dc = Dc();
  {
    Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> stops;
    D2D1_GRADIENT_STOP gs[2] = {{0.f, P.canvasTop}, {1.f, P.canvasBottom}};
    dc->CreateGradientStopCollection(gs, 2, &stops);
    Microsoft::WRL::ComPtr<ID2D1RadialGradientBrush> pool;
    if (stops)
      dc->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(MidX(win), MidY(win) * .9f), D2D1::Point2F(0, 0), W(win) * .75f, H(win) * .9f),
                                    stops.Get(), &pool);
    if (pool) dc->FillRectangle(win, pool.Get());
    else FillRect(win, P.canvasBottom);
  }
  if (!docW) {
    sceneDirty = false;
    return;
  }
  PushTransform(D2D1::Matrix3x2F::Rotation(angle.v, {cx.v, cy.v}));
  Rect page = R(cx.v - docW * zoom.v / 2, cy.v - docH * zoom.v / 2,
                cx.v + docW * zoom.v / 2, cy.v + docH * zoom.v / 2);
  float a = std::clamp(appear.v, 0.f, 1.f);
  SoftShadow(RR(page, 2), 2.2f * (.4f + .6f * a), 3.f);
  Checkerboard(page, 8);
  PushClip(page);
  float device = zoom.v * GfxDpi();
  if (mode == Mode::Gpu) {
    // Full resolution: sharp pixels once one covers two device pixels, filtered below that.
    if (gpuShown && gpuBitmap) GfxDrawCanvas(gpuBitmap.Get(), page, R(0, 0, float(docW), float(docH)), a, device >= 2.f);
  } else {
    for (int i = 0; i < 2; i++) {
      const Level& l = levels[i];
      if (!l.bitmap) continue;
      D2D_POINT_2F p0{cx.v + (l.rect.x0 - docW / 2.f) * zoom.v, cy.v + (l.rect.y0 - docH / 2.f) * zoom.v};
      D2D_POINT_2F p1{cx.v + (l.rect.x1 - docW / 2.f) * zoom.v, cy.v + (l.rect.y1 - docH / 2.f) * zoom.v};
      bool nearest = i == 1 && device * float(l.k) >= 2.f;
      GfxDrawCanvas(l.bitmap.Get(), R(p0.x, p0.y, p1.x, p1.y), R(0, 0, float(l.w), float(l.h)), i == 0 ? a : 1.f, nearest);
    }
  }
  PopClip();
  if (a < .999f) FillRect(page, Rgba(.13f, .13f, .14f, 1.f - a));
  PopTransform();
  sceneDirty = false;
}

void Canvas::Paint() {
  if (docW && tool && (!spaceHand || (toolActive && tool->SpaceRepositions()))) tool->Overlay();
}

// ----------------------------------------------------------------------------------- input --
void Canvas::OnDown(const PointerEvent& e) {
  if (!docW) return;
  if (e.button == 1) {
    if (toolActive || panning) return;  // the other button is in the middle of something
    if (e.mods.alt && onBrushDrag && onBrushDrag(0, 0, 0)) {
      sizing = true;
      sizeX0 = e.x;
      sizeY0 = e.y;
      return;
    }
    auto d = ToDoc(e.x, e.y);
    if (onContext) onContext(d.x, d.y, e);
    return;
  }
  if (e.button == 0 && spaceZoom) {
    zooming = spaceZoom;
    zoomX = zoomLastX = e.x; zoomY = e.y; zoomDragged = false;
    return;
  }
  if (e.button == 2 || (spaceHand && !(toolActive && tool && tool->SpaceRepositions())) || !tool) {
    if (e.clicks == 2 && e.button == 0 && !tool) { Fit(true); return; }
    panning = true;
    panX = e.x;
    panY = e.y;
    return;
  }
  auto d = ToDoc(e.x, e.y);
  if (e.mods.alt && onSample && onSample(d.x, d.y, e)) { sampling = true; return; }
  if (onToolPoint) d = onToolPoint(d, e, 0);
  toolActive = true;
  tool->Down(d.x, d.y, e);
}
void Canvas::OnDrag(const PointerEvent& e) {
  if (sampling) return;
  if (zooming) {
    if (std::fabs(e.x - zoomX) > 3) zoomDragged = true;
    if (zoomDragged) ZoomBy(std::exp((e.x - zoomLastX) * .012f), zoomX, zoomY);
    zoomLastX = e.x;
    return;
  }
  if (sizing) {
    if (onBrushDrag) onBrushDrag(e.x - sizeX0, e.y - sizeY0, 1);
    return;
  }
  if (panning) {
    PanBy(e.x - panX, e.y - panY);
    panX = e.x;
    panY = e.y;
    return;
  }
  if (toolActive && tool) {
    auto d = ToDoc(e.x, e.y);
    if (onToolPoint) d = onToolPoint(d, e, 1);
    tool->Drag(d.x, d.y, e);
  }
}
void Canvas::OnUp(const PointerEvent& e, bool inside) {
  Widget::OnUp(e, inside);
  if (e.button == 1) {  // the context menu's press, or the end of a sizing drag
    if (sizing) {
      sizing = false;
      if (onBrushDrag) onBrushDrag(e.x - sizeX0, e.y - sizeY0, 2);
    }
    return;
  }
  if (sampling) { sampling = false; return; }
  if (zooming) {
    if (!zoomDragged) ZoomBy(zooming > 0 ? 1.6f : 1 / 1.6f, zoomX, zoomY);
    zooming = 0;
    return;
  }
  if (panning) {
    panning = false;
    return;
  }
  if (toolActive && tool) {
    toolActive = false;
    auto d = ToDoc(e.x, e.y);
    if (onToolPoint) d = onToolPoint(d, e, 2);
    tool->Up(d.x, d.y, e);
  }
}
bool Canvas::OnWheel(float delta, const PointerEvent& e) {
  if (!docW) return false;
  if (e.mods.alt) { ZoomBy(std::pow(1.0016f, delta), e.x, e.y); return true; }
  if (e.mods.shift) { PanBy(delta * .5f, 0); return true; }
  PanBy(0, delta * .5f);
  return true;
}
HCURSOR Canvas::Cursor() {
  if (spaceZoom || zooming) return LoadCursor(nullptr, IDC_CROSS);
  if (panning || spaceHand) return LoadCursor(nullptr, IDC_SIZEALL);
  if (!docW || !tool) return LoadCursor(nullptr, IDC_ARROW);
  return tool->Cursor();
}
void Canvas::CancelInput() {
  if (toolActive && tool) tool->Cancel();
  toolActive = panning = sizing = sampling = false;
  zooming = spaceZoom = 0;
  spaceHand = false;
}
