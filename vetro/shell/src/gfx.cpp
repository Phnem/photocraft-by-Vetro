// PhotoCraft by Vetro. The device chain follows VetroView's gfx.cpp: Direct3D 11 -> Direct2D 1.1 ->
// a composition swap chain on a DirectComposition visual, so every pixel carries its own alpha.
#include "gfx.h"
#include "i18n.h"
#include <d2d1_3.h>
#include <d2d1effects_2.h>
#include <d3d11_1.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <map>
#include <random>
#include <vector>

using Microsoft::WRL::ComPtr;

bool reducedMotion = false;
Palette P;

namespace {
ComPtr<ID3D11Device> d3d;
ComPtr<IDXGISwapChain1> swap;
ComPtr<ID2D1Factory1> factory;
ComPtr<ID2D1Device> device;
ComPtr<ID2D1DeviceContext> dc;
ComPtr<ID2D1Bitmap1> backBuffer, scene, frost, noise;
ComPtr<ID2D1Effect> blur, saturate, contrast;
ComPtr<ID2D1BitmapBrush1> frostBrush, noiseBrush;
ComPtr<ID2D1SolidColorBrush> ink;
ComPtr<IDCompositionDevice> compositor;
ComPtr<IDCompositionTarget> target;
ComPtr<IDCompositionVisual> visual;
ComPtr<IDWriteFactory> writer;
ComPtr<IDWriteTextFormat> faces[F_COUNT][3];
ComPtr<ID2D1StrokeStyle> roundStroke;
std::map<const wchar_t*, ComPtr<ID2D1PathGeometry>> icons;
UINT surfaceW = 0, surfaceH = 0;
float dpi = 1;
bool frostValid = false, drawing = false;
std::vector<D2D1_MATRIX_3X2_F> transforms;
std::vector<int> clipKinds;  // 0 axis-aligned clip, 1 layer

D2D1::Matrix3x2F AsMatrix(const D2D1_MATRIX_3X2_F& m) { return *D2D1::Matrix3x2F::ReinterpretBaseType(&m); }

void ReleaseTargets() {
  if (dc) dc->SetTarget(nullptr);
  frostBrush.Reset();
  backBuffer.Reset();
  scene.Reset();
  frost.Reset();
  frostValid = false;
}

bool MakeTargets(UINT w, UINT h, float scale) {
  ReleaseTargets();
  surfaceW = w = std::max(w, 8u);
  surfaceH = h = std::max(h, 8u);
  dpi = scale;
  dc->SetDpi(96 * scale, 96 * scale);
  ComPtr<IDXGISurface> surface;
  if (FAILED(swap->GetBuffer(0, IID_PPV_ARGS(&surface)))) return false;
  auto back = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96 * scale, 96 * scale);
  if (FAILED(dc->CreateBitmapFromDxgiSurface(surface.Get(), &back, &backBuffer))) return false;
  auto off = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
                                     D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96 * scale, 96 * scale);
  if (FAILED(dc->CreateBitmap(D2D1::SizeU(w, h), nullptr, 0, off, &scene))) return false;
  if (FAILED(dc->CreateBitmap(D2D1::SizeU(w, h), nullptr, 0, off, &frost))) return false;
  auto bp = D2D1::BitmapBrushProperties1(D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_INTERPOLATION_MODE_LINEAR);
  dc->CreateBitmapBrush(frost.Get(), bp, &frostBrush);
  // Blur first, then a restrained vibrancy and contrast lift; the tint comes after (§4).
  if (!blur) {
    dc->CreateEffect(CLSID_D2D1GaussianBlur, &blur);
    dc->CreateEffect(CLSID_D2D1Saturation, &saturate);
    dc->CreateEffect(CLSID_D2D1ColorMatrix, &contrast);
  }
  if (blur && saturate && contrast) {
    blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
    blur->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION, D2D1_GAUSSIANBLUR_OPTIMIZATION_SPEED);
    // Effect graphs work in input pixels: the sigma is scaled to the display.
    blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, tok::BlurSigma * scale);
    saturate->SetInputEffect(0, blur.Get());
    saturate->SetValue(D2D1_SATURATION_PROP_SATURATION, 1.16f);
    constexpr float k = 1.05f, m = (1.f - k) * .5f;
    D2D1_MATRIX_5X4_F matrix = {k, 0, 0, 0, 0, k, 0, 0, 0, 0, k, 0, 0, 0, 0, 1, m, m, m, 0};
    contrast->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, matrix);
    contrast->SetInputEffect(0, saturate.Get());
  }
  // A resized swap chain is handed to the compositor again, or the visual keeps presenting the
  // buffers it was bound to.
  if (visual) {
    visual->SetContent(swap.Get());
    compositor->Commit();
  }
  return true;
}

void MakeNoise() {
  // One cached 64x64 neutral grain tile, repeated inside each glass shape at 1-3 % (§12).
  constexpr UINT N = 64;
  std::vector<uint32_t> px(N * N);
  std::mt19937 rng(0x5eed);
  std::uniform_int_distribution<int> d(0, 255);
  for (auto& p : px) {
    uint32_t v = uint32_t(d(rng));
    p = (255u << 24) | (v << 16) | (v << 8) | v;
  }
  auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  if (FAILED(dc->CreateBitmap(D2D1::SizeU(N, N), px.data(), N * 4, props, &noise))) return;
  auto bp = D2D1::BitmapBrushProperties1(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
  dc->CreateBitmapBrush(noise.Get(), bp, &noiseBrush);
}

void MakeFonts() {
  if (writer) return;
  DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(writer.GetAddressOf()));
  if (!writer) return;
  struct Spec { Face face; float size; DWRITE_FONT_WEIGHT weight; const wchar_t* family; };
  const Spec specs[] = {
      {F_Brand, 15.f, DWRITE_FONT_WEIGHT_BOLD, L"Segoe UI Variable Display"},
      {F_Menu, 13.f, DWRITE_FONT_WEIGHT_MEDIUM, L"Segoe UI Variable Text"},
      {F_Row, 13.f, DWRITE_FONT_WEIGHT_MEDIUM, L"Segoe UI Variable Text"},
      {F_Label, 12.f, DWRITE_FONT_WEIGHT_NORMAL, L"Segoe UI Variable Text"},
      {F_Value, 12.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, L"Segoe UI Variable Text"},
      {F_Section, 10.5f, DWRITE_FONT_WEIGHT_BOLD, L"Segoe UI Variable Text"},
      {F_Button, 12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, L"Segoe UI Variable Text"},
      {F_Small, 11.f, DWRITE_FONT_WEIGHT_MEDIUM, L"Segoe UI Variable Text"},
      {F_Title, 15.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, L"Segoe UI Variable Display"},
      {F_Mono, 11.f, DWRITE_FONT_WEIGHT_MEDIUM, L"Consolas"},
      {F_Big, 26.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, L"Segoe UI Variable Display"},
  };
  const DWRITE_TEXT_ALIGNMENT aligns[3] = {DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_TEXT_ALIGNMENT_TRAILING};
  for (auto& s : specs) {
    for (int a = 0; a < 3; a++) {
      auto& f = faces[s.face][a];
      writer->CreateTextFormat(s.family, nullptr, s.weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, s.size, L"en-us", &f);
      if (!f) writer->CreateTextFormat(L"Segoe UI", nullptr, s.weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, s.size, L"en-us", &f);
      if (!f) continue;
      f->SetTextAlignment(aligns[a]);
      f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
      f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
      DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
      ComPtr<IDWriteInlineObject> ellipsis;
      writer->CreateEllipsisTrimmingSign(f.Get(), &ellipsis);
      f->SetTrimming(&trim, ellipsis.Get());
    }
  }
}

// Compact SVG path reader (after VetroView gfx.cpp): M L H V C S Q A Z, absolute and relative.
ComPtr<ID2D1PathGeometry> BuildPath(const wchar_t* d) {
  ComPtr<ID2D1PathGeometry> geometry;
  if (!factory || FAILED(factory->CreatePathGeometry(&geometry))) return geometry;
  ComPtr<ID2D1GeometrySink> sink;
  if (FAILED(geometry->Open(&sink))) { geometry.Reset(); return geometry; }
  sink->SetFillMode(D2D1_FILL_MODE_WINDING);
  const wchar_t* p = d;
  wchar_t command = 0;
  bool open = false;
  D2D1_POINT_2F at{}, start{}, lastControl{};
  bool lastWasCubic = false;
  auto number = [&](float& out) -> bool {
    while (*p == L' ' || *p == L',') p++;
    if (!*p || (!iswdigit(*p) && *p != L'-' && *p != L'.' && *p != L'+')) return false;
    wchar_t* end = nullptr;
    out = wcstof(p, &end);
    if (end == p) return false;
    p = end;
    return true;
  };
  while (*p) {
    while (*p == L' ' || *p == L',') p++;
    if (!*p) break;
    if (iswalpha(*p)) command = *p++;
    bool relative = iswlower(command) != 0;
    wchar_t op = wchar_t(towupper(command));
    if (op == L'Z') {
      if (open) { sink->EndFigure(D2D1_FIGURE_END_CLOSED); open = false; at = start; }
      lastWasCubic = false;
      continue;
    }
    float a = 0, b = 0, c = 0, e = 0, f = 0, g = 0;
    auto point = [&](float x, float y) { return D2D1::Point2F(relative ? at.x + x : x, relative ? at.y + y : y); };
    if (op == L'M') {
      if (!number(a) || !number(b)) break;
      if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
      at = point(a, b);
      start = at;
      sink->BeginFigure(at, D2D1_FIGURE_BEGIN_FILLED);
      open = true;
      command = relative ? L'l' : L'L';
      lastWasCubic = false;
      continue;
    }
    if (!open) { sink->BeginFigure(at, D2D1_FIGURE_BEGIN_FILLED); open = true; }
    bool cubic = false;
    if (op == L'L') { if (!number(a) || !number(b)) break; at = point(a, b); sink->AddLine(at); }
    else if (op == L'H') { if (!number(a)) break; at.x = relative ? at.x + a : a; sink->AddLine(at); }
    else if (op == L'V') { if (!number(a)) break; at.y = relative ? at.y + a : a; sink->AddLine(at); }
    else if (op == L'C') {
      if (!number(a) || !number(b) || !number(c) || !number(e) || !number(f) || !number(g)) break;
      auto p1 = point(a, b), p2 = point(c, e), p3 = point(f, g);
      sink->AddBezier(D2D1::BezierSegment(p1, p2, p3));
      lastControl = p2; at = p3; cubic = true;
    } else if (op == L'S') {
      if (!number(c) || !number(e) || !number(f) || !number(g)) break;
      D2D1_POINT_2F p1 = lastWasCubic ? D2D1::Point2F(2 * at.x - lastControl.x, 2 * at.y - lastControl.y) : at;
      auto p2 = point(c, e), p3 = point(f, g);
      sink->AddBezier(D2D1::BezierSegment(p1, p2, p3));
      lastControl = p2; at = p3; cubic = true;
    } else if (op == L'Q') {
      if (!number(a) || !number(b) || !number(c) || !number(e)) break;
      auto p1 = point(a, b), p2 = point(c, e);
      sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(p1, p2));
      at = p2;
    } else if (op == L'A') {
      float rot = 0, large = 0, sweep = 0;
      if (!number(a) || !number(b) || !number(rot) || !number(large) || !number(sweep) || !number(c) || !number(e)) break;
      auto end = point(c, e);
      D2D1_ARC_SEGMENT arc = {end, D2D1::SizeF(std::fabs(a), std::fabs(b)), rot,
                              sweep > .5f ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                              large > .5f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL};
      sink->AddArc(arc);
      at = end;
    } else break;
    lastWasCubic = cubic;
  }
  if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
  if (FAILED(sink->Close())) geometry.Reset();
  return geometry;
}
}  // namespace

ID2D1DeviceContext* Dc() { return dc.Get(); }
ID2D1Factory1* GfxFactory() { return factory.Get(); }
float GfxDpi() { return dpi; }
bool GfxReady() { return dc && backBuffer; }

bool GfxCreate(HWND window, float scale) {
  MakeFonts();
  UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &d3d, nullptr, nullptr);
  if (FAILED(hr)) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &d3d, nullptr, nullptr);
  if (FAILED(hr)) return false;
  ComPtr<IDXGIDevice1> dxgi;
  if (FAILED(d3d.As(&dxgi))) return false;
  dxgi->SetMaximumFrameLatency(1);
  D2D1_FACTORY_OPTIONS options{};
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options, reinterpret_cast<void**>(factory.GetAddressOf())))) return false;
  if (FAILED(factory->CreateDevice(dxgi.Get(), &device))) return false;
  if (FAILED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc))) return false;
  dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
  dc->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &ink);
  auto sp = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
  factory->CreateStrokeStyle(sp, nullptr, 0, &roundStroke);
  MakeNoise();
  ComPtr<IDXGIAdapter> adapter;
  if (FAILED(dxgi->GetAdapter(&adapter))) return false;
  ComPtr<IDXGIFactory2> dxgiFactory;
  if (FAILED(adapter->GetParent(IID_PPV_ARGS(&dxgiFactory)))) return false;
  RECT r{};
  GetClientRect(window, &r);
  DXGI_SWAP_CHAIN_DESC1 desc{};
  desc.Width = std::max(UINT(r.right), 8u);
  desc.Height = std::max(UINT(r.bottom), 8u);
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
  desc.Scaling = DXGI_SCALING_STRETCH;
  if (FAILED(dxgiFactory->CreateSwapChainForComposition(d3d.Get(), &desc, nullptr, &swap))) return false;
  if (FAILED(DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(&compositor)))) return false;
  if (FAILED(compositor->CreateTargetForHwnd(window, TRUE, &target))) return false;
  if (FAILED(compositor->CreateVisual(&visual))) return false;
  visual->SetContent(swap.Get());
  target->SetRoot(visual.Get());
  compositor->Commit();
  return MakeTargets(desc.Width, desc.Height, scale);
}

void GfxResize(UINT w, UINT h, float scale) {
  if (!swap || !dc) return;
  w = std::max(w, 8u);
  h = std::max(h, 8u);
  if (w == surfaceW && h == surfaceH && scale == dpi) return;
  ReleaseTargets();
  if (FAILED(swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0))) return;
  MakeTargets(w, h, scale);
}

void GfxDestroy() {
  ReleaseTargets();
  icons.clear();
  noiseBrush.Reset(); noise.Reset(); blur.Reset(); saturate.Reset(); contrast.Reset();
  ink.Reset(); roundStroke.Reset();
  visual.Reset(); target.Reset(); compositor.Reset(); swap.Reset(); dc.Reset(); device.Reset(); factory.Reset(); d3d.Reset();
  for (auto& f : faces) for (auto& a : f) a.Reset();
  writer.Reset();
}

void GfxBeginScene() {
  if (!GfxReady()) return;
  dc->SetTarget(scene.Get());
  dc->BeginDraw();
  dc->SetTransform(D2D1::Matrix3x2F::Identity());
  dc->Clear(D2D1::ColorF(0, 0, 0, 0));
}

void GfxEndScene() {
  if (!GfxReady()) return;
  dc->EndDraw();
  frostValid = false;
  if (blur && contrast) {
    // The scene is a render target: Direct2D cannot see that it changed and would keep the blur
    // it cached. Reconnecting the input marks the graph dirty (VetroView gfx.cpp).
    blur->SetInput(0, nullptr);
    blur->SetInput(0, scene.Get());
    dc->SetTarget(frost.Get());
    dc->BeginDraw();
    dc->SetTransform(D2D1::Matrix3x2F::Identity());
    dc->Clear(D2D1::ColorF(0, 0, 0, 0));
    dc->DrawImage(contrast.Get());
    frostValid = SUCCEEDED(dc->EndDraw());
  }
}

void GfxBeginInterface() {
  if (!GfxReady()) return;
  dc->SetTarget(backBuffer.Get());
  dc->BeginDraw();
  dc->SetTransform(D2D1::Matrix3x2F::Identity());
  dc->Clear(D2D1::ColorF(0, 0, 0, 0));
  dc->DrawBitmap(scene.Get(), nullptr, 1.f, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
  transforms.clear();
  drawing = true;
}

void GfxPresent() {
  if (!GfxReady()) return;
  if (drawing) {
    dc->EndDraw();
    drawing = false;
  }
  swap->Present(1, 0);
  compositor->Commit();
}

// -------------------------------------------------------------------------- primitives --
ID2D1SolidColorBrush* Ink(const D2D1_COLOR_F& c) {
  ink->SetColor(c);
  return ink.Get();
}
void FillRect(const Rect& r, const D2D1_COLOR_F& c) {
  if (c.a > .002f) dc->FillRectangle(r, Ink(c));
}
void FillRound(const Rect& r, float radius, const D2D1_COLOR_F& c) {
  if (c.a > .002f) dc->FillRoundedRectangle(RR(r, radius), Ink(c));
}
void StrokeRound(const Rect& r, float radius, const D2D1_COLOR_F& c, float width) {
  if (c.a > .002f) dc->DrawRoundedRectangle(RR(r, radius), Ink(c), width);
}
void Line(float x0, float y0, float x1, float y1, const D2D1_COLOR_F& c, float width) {
  if (c.a > .002f) dc->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1), Ink(c), width, roundStroke.Get());
}
void FillEllipse(float cx, float cy, float rx, float ry, const D2D1_COLOR_F& c) {
  if (c.a > .002f) dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry), Ink(c));
}
void StrokeEllipse(float cx, float cy, float rx, float ry, const D2D1_COLOR_F& c, float width) {
  if (c.a > .002f) dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry), Ink(c), width);
}

void SoftShadow(const D2D1_ROUNDED_RECT& rr, float opacity, float spread) {
  if (opacity <= .01f) return;
  // Several widening layers of low alpha: wide and soft, separation rather than a floating card.
  const float grow[] = {1.f, 2.5f, 4.5f, 7.f, 10.f, 14.f};
  const float alpha[] = {.030f, .026f, .022f, .018f, .013f, .009f};
  for (int i = 0; i < 6; i++) {
    float g = grow[i] * spread;
    auto s = D2D1::RoundedRect(R(rr.rect.left - g, rr.rect.top - g * .55f, rr.rect.right + g, rr.rect.bottom + g * 1.45f), rr.radiusX + g, rr.radiusY + g);
    dc->FillRoundedRectangle(s, Ink(Rgba(0, 0, 0, alpha[i] * opacity)));
  }
}

void Checkerboard(const Rect& r, float cell) {
  // One cached 2x2-cell tile, wrapped: a full-window board would otherwise be thousands of rects.
  static ComPtr<ID2D1BitmapBrush1> board;
  if (!board) {
    uint32_t px[4] = {0xFFDCDCDE, 0xFFBDBDC2, 0xFFBDBDC2, 0xFFDCDCDE};
    auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    ComPtr<ID2D1Bitmap1> tile;
    if (FAILED(dc->CreateBitmap(D2D1::SizeU(2, 2), px, 8, props, &tile))) return;
    auto bp = D2D1::BitmapBrushProperties1(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    dc->CreateBitmapBrush(tile.Get(), bp, &board);
    if (!board) return;
  }
  board->SetTransform(D2D1::Matrix3x2F::Scale(cell, cell) * D2D1::Matrix3x2F::Translation(r.left, r.top));
  dc->FillRectangle(r, board.Get());
}

void Glass(const Rect& r, float radius, float opacity, GlassKind kind) {
  if (!GfxReady() || opacity <= .004f || W(r) <= 0 || H(r) <= 0) return;
  auto rr = RR(r, radius);
  // 7. The soft shadow sits outside the shape.
  SoftShadow(rr, (kind == GlassKind::Popover ? 1.35f : kind == GlassKind::Control ? .45f : 1.f) * opacity,
             kind == GlassKind::Popover ? 1.5f : 1.f);
  // 1-3. The live local backdrop, blurred, with its vibrancy and contrast lift.
  if (frostValid && frostBrush && kind != GlassKind::Control) {
    D2D1_MATRIX_3X2_F world;
    dc->GetTransform(&world);
    D2D1::Matrix3x2F inverse = AsMatrix(world);
    if (!inverse.Invert()) inverse = D2D1::Matrix3x2F::Identity();
    frostBrush->SetTransform(inverse);
    frostBrush->SetOpacity(opacity);
    dc->FillRoundedRectangle(rr, frostBrush.Get());
  }
  // 4. The neutral graphite tint. Without a live blur the material honestly turns denser (§4).
  D2D1_COLOR_F tint = kind == GlassKind::Popover ? P.tintDense : kind == GlassKind::Control ? Rgba(1, 1, 1, .06f) : P.tint;
  if (!frostValid && kind != GlassKind::Control) tint = P.tintFallback;
  dc->FillRoundedRectangle(rr, Ink(Fade(tint, opacity)));
  if (kind != GlassKind::Control) {
    // A light vertical variation of density, not a visible gradient.
    ComPtr<ID2D1GradientStopCollection> stops;
    D2D1_GRADIENT_STOP gs[2] = {{0.f, Rgba(1, 1, 1, .035f * opacity)}, {1.f, Rgba(1, 1, 1, 0)}};
    if (SUCCEEDED(dc->CreateGradientStopCollection(gs, 2, &stops))) {
      ComPtr<ID2D1LinearGradientBrush> sheen;
      dc->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, r.top), D2D1::Point2F(0, r.top + std::min(H(r), 90.f))),
                                    stops.Get(), &sheen);
      if (sheen) dc->FillRoundedRectangle(rr, sheen.Get());
    }
    // 5. Micro grain, the same tile everywhere.
    if (noiseBrush) {
      noiseBrush->SetOpacity(tok::NoiseAlpha * opacity);
      dc->FillRoundedRectangle(rr, noiseBrush.Get());
    }
  }
  // 6. The outer light edge and the softer inner highlight.
  dc->DrawRoundedRectangle(rr, Ink(Fade(P.edge, opacity)), 1.f);
  auto inner = rr;
  inner.rect = Inset(rr.rect, 1.5f);
  inner.radiusX = std::max(0.f, rr.radiusX - 1.5f);
  inner.radiusY = std::max(0.f, rr.radiusY - 1.5f);
  dc->DrawRoundedRectangle(inner, Ink(Fade(P.lift, opacity)), .8f);
}

// ----------------------------------------------------------------- clip, transform, opacity --
void PushClip(const Rect& r, float radius) {
  if (radius <= .5f) {
    dc->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    clipKinds.push_back(0);
    return;
  }
  ComPtr<ID2D1RoundedRectangleGeometry> g;
  factory->CreateRoundedRectangleGeometry(RR(r, radius), &g);
  dc->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), g.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE), nullptr);
  clipKinds.push_back(1);
}
void PopClip() {
  if (clipKinds.empty()) return;
  if (clipKinds.back() == 0) dc->PopAxisAlignedClip();
  else dc->PopLayer();
  clipKinds.pop_back();
}
void PushTransform(const D2D1_MATRIX_3X2_F& m) {
  D2D1_MATRIX_3X2_F current;
  dc->GetTransform(&current);
  transforms.push_back(current);
  dc->SetTransform(AsMatrix(m) * AsMatrix(current));
}
void PopTransform() {
  if (transforms.empty()) return;
  dc->SetTransform(transforms.back());
  transforms.pop_back();
}
void PushOpacity(float opacity) {
  D2D1_LAYER_PARAMETERS1 p = D2D1::LayerParameters1();
  p.opacity = std::clamp(opacity, 0.f, 1.f);
  dc->PushLayer(p, nullptr);
  clipKinds.push_back(1);
}
void PopOpacity() { PopClip(); }

// -------------------------------------------------------------------------------- text --
void Text(const std::wstring& english, const Rect& r, Face f, const D2D1_COLOR_F& c, Align a) {
  const std::wstring& s = i18n::Tr(english);  // the interface language (i18n.h)
  auto& fmt = faces[f][int(a)];
  if (s.empty() || !fmt || c.a <= .004f) return;
  dc->DrawTextW(s.c_str(), UINT32(s.size()), fmt.Get(), r, Ink(c), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
float Measure(const std::wstring& english, Face f) {
  const std::wstring& s = i18n::Tr(english);
  auto& fmt = faces[f][0];
  if (s.empty() || !fmt || !writer) return 0;
  ComPtr<IDWriteTextLayout> layout;
  if (FAILED(writer->CreateTextLayout(s.c_str(), UINT32(s.size()), fmt.Get(), 4096, 64, &layout))) return 0;
  DWRITE_TEXT_METRICS m{};
  layout->GetMetrics(&m);
  return m.widthIncludingTrailingWhitespace;
}

// ------------------------------------------------------------------------------- icons --
void Icon(const wchar_t* path, const Rect& box, const D2D1_COLOR_F& c, bool stroked, float rotation) {
  if (!path || !GfxReady() || c.a <= .004f) return;
  auto found = icons.find(path);
  if (found == icons.end()) found = icons.emplace(path, BuildPath(path)).first;
  if (!found->second) return;
  constexpr float viewBox = 256.f;
  float side = std::min(W(box), H(box)), scale = side / viewBox;
  if (scale <= 0) return;
  D2D1_MATRIX_3X2_F previous;
  dc->GetTransform(&previous);
  auto local = D2D1::Matrix3x2F::Translation(-viewBox / 2, -viewBox / 2) * D2D1::Matrix3x2F::Rotation(rotation) *
               D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(MidX(box), MidY(box));
  dc->SetTransform(local * AsMatrix(previous));
  if (stroked) dc->DrawGeometry(found->second.Get(), Ink(c), 16.f, roundStroke.Get());
  else dc->FillGeometry(found->second.Get(), Ink(c));
  dc->SetTransform(previous);
}
HCURSOR IconCursor(const wchar_t* path, int side, float hotX, float hotY) {
  if (!factory || side <= 0) return nullptr;
  auto geometry = BuildPath(path);
  if (!geometry) return nullptr;
  BITMAPINFO bi{};
  bi.bmiHeader = {sizeof(BITMAPINFOHEADER), side, -side, 1, 32, BI_RGB};
  void* bits = nullptr;
  HDC screen = GetDC(nullptr);
  HBITMAP color = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  HDC mem = CreateCompatibleDC(screen);
  ReleaseDC(nullptr, screen);
  HCURSOR out = nullptr;
  if (color && mem) {
    HGDIOBJ old = SelectObject(mem, color);
    ComPtr<ID2D1DCRenderTarget> rt;
    auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    RECT rc{0, 0, side, side};
    if (SUCCEEDED(factory->CreateDCRenderTarget(&props, &rt)) && SUCCEEDED(rt->BindDC(mem, &rc))) {
      ComPtr<ID2D1SolidColorBrush> halo, body;
      rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &halo);
      rt->CreateSolidColorBrush(D2D1::ColorF(.08f, .08f, .09f, 1), &body);
      ComPtr<ID2D1StrokeStyle> round;
      auto sp = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
      factory->CreateStrokeStyle(sp, nullptr, 0, &round);
      float s = float(side) / 256.f * .92f, pad = float(side) * .04f;
      rt->BeginDraw();
      rt->Clear(D2D1::ColorF(0, 0, 0, 0));
      rt->SetTransform(D2D1::Matrix3x2F::Scale(s, s) * D2D1::Matrix3x2F::Translation(pad, pad));
      rt->DrawGeometry(geometry.Get(), halo.Get(), 26.f, round.Get());
      rt->FillGeometry(geometry.Get(), body.Get());
      rt->EndDraw();
      GdiFlush();
      ICONINFO ii{};
      ii.fIcon = FALSE;
      ii.xHotspot = DWORD(std::clamp(int(hotX * s + pad), 0, side - 1));
      ii.yHotspot = DWORD(std::clamp(int(hotY * s + pad), 0, side - 1));
      ii.hbmColor = color;
      ii.hbmMask = CreateBitmap(side, side, 1, 1, nullptr);
      out = CreateIconIndirect(&ii);
      DeleteObject(ii.hbmMask);
    }
    SelectObject(mem, old);
  }
  if (mem) DeleteDC(mem);
  if (color) DeleteObject(color);
  return reinterpret_cast<HCURSOR>(out);
}
void IconAt(const wchar_t* path, float cx, float cy, float size, const D2D1_COLOR_F& c, bool stroked) {
  Icon(path, R(cx - size / 2, cy - size / 2, cx + size / 2, cy + size / 2), c, stroked);
}

// ----------------------------------------------------------------------------- bitmaps --
ComPtr<ID2D1Bitmap1> MakeBitmap(UINT w, UINT h, const void* bgra, UINT stride) {
  ComPtr<ID2D1Bitmap1> b;
  if (!dc || !w || !h) return b;
  auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  dc->CreateBitmap(D2D1::SizeU(w, h), bgra, stride, props, &b);
  return b;
}
void DrawBitmap(ID2D1Bitmap* b, const Rect& dest, float opacity, const Rect* source, bool nearest) {
  if (!b) return;
  dc->DrawBitmap(b, dest, opacity, nearest ? D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR : D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, source);
}

// ------------------------------------------------------------------------------ canvas --
namespace {
ComPtr<ID2D1Effect> lutEffect;
ComPtr<ID2D1LookupTable3D> lutTable;
}  // namespace

bool GfxAdapterLuid(uint32_t* low, int32_t* high) {
  ComPtr<IDXGIDevice> dxgi;
  ComPtr<IDXGIAdapter> adapter;
  DXGI_ADAPTER_DESC desc{};
  if (!d3d || FAILED(d3d.As(&dxgi)) || FAILED(dxgi->GetAdapter(&adapter)) || FAILED(adapter->GetDesc(&desc))) return false;
  *low = desc.AdapterLuid.LowPart;
  *high = desc.AdapterLuid.HighPart;
  return true;
}

ComPtr<ID2D1Bitmap1> GfxOpenShared(void* ntHandle) {
  ComPtr<ID2D1Bitmap1> bitmap;
  ComPtr<ID3D11Device1> d1;
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<IDXGISurface> surface;
  if (d3d && SUCCEEDED(d3d.As(&d1)) && SUCCEEDED(d1->OpenSharedResource1(HANDLE(ntHandle), IID_PPV_ARGS(&texture))) && SUCCEEDED(texture.As(&surface))) {
    // 96 DPI: one bitmap pixel is one DIP, as for MakeBitmap.
    auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    dc->CreateBitmapFromDxgiSurface(surface.Get(), &props, &bitmap);
  }
  CloseHandle(HANDLE(ntHandle));
  return bitmap;
}

void GfxSetCanvasLut(const uint8_t* rgba, UINT size) {
  lutTable.Reset();
  if (!rgba || size < 2 || !dc) return;
  ComPtr<ID2D1DeviceContext2> dc2;
  if (FAILED(dc.As(&dc2))) return;
  if (!lutEffect && FAILED(dc->CreateEffect(CLSID_D2D1LookupTable3D, &lutEffect))) return;
  // Direct2D's 8-bit entries are RGBA like the engine's (checked: swapping them turns red blue),
  // but its blue index runs fastest where the engine's red does (checked: an engine table that
  // passes red through showed the blue input). So the table is transposed on the way in; alpha
  // is not a colour and stays opaque.
  size_t n = size;
  std::vector<uint8_t> bgra(n * n * n * 4);
  for (size_t b = 0; b < n; b++)
    for (size_t g = 0; g < n; g++)
      for (size_t r = 0; r < n; r++) {
        const uint8_t* src = rgba + ((b * n + g) * n + r) * 4;
        uint8_t* dst = bgra.data() + ((r * n + g) * n + b) * 4;
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = 255;
      }
  UINT32 extents[3] = {size, size, size};
  UINT32 strides[2] = {size * 4, size * size * 4};
  if (FAILED(dc2->CreateLookupTable3D(D2D1_BUFFER_PRECISION_8BPC_UNORM, extents, bgra.data(), UINT32(bgra.size()), strides, &lutTable))) return;
  lutEffect->SetValue(D2D1_LOOKUPTABLE3D_PROP_LUT, lutTable.Get());
  lutEffect->SetValue(D2D1_LOOKUPTABLE3D_PROP_ALPHA_MODE, D2D1_ALPHA_MODE_PREMULTIPLIED);
}

bool GfxHasCanvasLut() { return lutTable != nullptr; }

void GfxDrawCanvas(ID2D1Bitmap* b, const Rect& dest, const Rect& source, float opacity, bool nearest) {
  if (!b || W(source) <= 0 || H(source) <= 0) return;
  if (!lutTable || !lutEffect) {
    DrawBitmap(b, dest, opacity, &source, nearest);
    return;
  }
  // Through the table: the bitmap is mapped onto `dest` by the world transform, so the effect runs
  // at the resolution it is shown at.
  D2D1_MATRIX_3X2_F previous;
  dc->GetTransform(&previous);
  float sx = W(dest) / W(source), sy = H(dest) / H(source);
  auto m = D2D1::Matrix3x2F::Scale(sx, sy) * D2D1::Matrix3x2F::Translation(dest.left - source.left * sx, dest.top - source.top * sy);
  dc->SetTransform(m * AsMatrix(previous));
  lutEffect->SetInput(0, b);
  if (opacity < .999f) PushOpacity(opacity);
  D2D1_RECT_F src = source;
  dc->DrawImage(lutEffect.Get(), nullptr, &src, nearest ? D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR : D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
  if (opacity < .999f) PopOpacity();
  dc->SetTransform(previous);
  lutEffect->SetInput(0, nullptr);
}
ID2D1StrokeStyle* RoundStyle() { return roundStroke.Get(); }
ID2D1StrokeStyle* DashStyle(float offset) {
  static ComPtr<ID2D1StrokeStyle> dashed;
  static float lastOffset = -1;
  if (!factory) return nullptr;
  if (!dashed || offset != lastOffset) {
    dashed.Reset();
    auto props = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_MITER, 10.f,
                                             D2D1_DASH_STYLE_CUSTOM, offset);
    const float pattern[] = {4.f, 4.f};
    factory->CreateStrokeStyle(props, pattern, 2, &dashed);
    lastOffset = offset;
  }
  return dashed.Get();
}

// ------------------------------------------------------------------------------- text --
std::wstring Widen(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
  std::wstring out(size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n);
  return out;
}
std::string Narrow(const std::wstring& s) {
  if (s.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
  std::string out(size_t(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n, nullptr, nullptr);
  return out;
}
