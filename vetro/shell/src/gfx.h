#pragma once
// Direct3D 11 -> Direct2D 1.1 -> DirectComposition, the frosted material, text and icons.
//
// A frame has two passes. The scene (backdrop and document) is drawn into an offscreen bitmap and,
// when it changed, blurred once into the frost bitmap. The interface pass then puts the scene on the
// back buffer and paints every glass surface from the frost under it -- the local, live blur the
// Vetro material is defined by (design.md §4) -- with tint, grain, edge, inner light and shadow on
// top. The scene is not re-blurred while only the interface animates.
#include "core.h"
#include <d2d1_1helper.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <cstdint>

bool GfxCreate(HWND window, float dpiScale);
void GfxResize(UINT pixelsW, UINT pixelsH, float dpiScale);
void GfxDestroy();
bool GfxReady();
ID2D1DeviceContext* Dc();
ID2D1Factory1* GfxFactory();
float GfxDpi();  // DIP -> pixel scale

// Frame. BeginScene/EndScene are skipped while the scene is unchanged.
void GfxBeginScene();
void GfxEndScene();
void GfxBeginInterface();
void GfxPresent();

// Primitives (DIPs).
ID2D1SolidColorBrush* Ink(const D2D1_COLOR_F& c);
void FillRect(const Rect& r, const D2D1_COLOR_F& c);
void FillRound(const Rect& r, float radius, const D2D1_COLOR_F& c);
void StrokeRound(const Rect& r, float radius, const D2D1_COLOR_F& c, float width = 1);
void Line(float x0, float y0, float x1, float y1, const D2D1_COLOR_F& c, float width = 1);
void FillEllipse(float cx, float cy, float rx, float ry, const D2D1_COLOR_F& c);
void StrokeEllipse(float cx, float cy, float rx, float ry, const D2D1_COLOR_F& c, float width = 1);
void SoftShadow(const D2D1_ROUNDED_RECT& rr, float opacity, float spread = 1.f);
void Checkerboard(const Rect& r, float cell);

// The one glass. Popover is the same material one step denser with a deeper shadow (§7).
enum class GlassKind { Dock, Popover, Control };
void Glass(const Rect& r, float radius, float opacity = 1.f, GlassKind kind = GlassKind::Dock);

// Clipping, transforms, group opacity.
void PushClip(const Rect& r, float radius = 0);
void PopClip();
void PushTransform(const D2D1_MATRIX_3X2_F& m);
void PopTransform();
void PushOpacity(float opacity);
void PopOpacity();

// Text.
enum Face { F_Brand, F_Menu, F_Row, F_Label, F_Value, F_Section, F_Button, F_Small, F_Title, F_Mono, F_Big, F_COUNT };
enum class Align { Leading, Center, Trailing };
void Text(const std::wstring& s, const Rect& r, Face f, const D2D1_COLOR_F& c, Align a = Align::Leading);
float Measure(const std::wstring& s, Face f);

// Icons. Phosphor paths are filled in their 256 grid; custom ones are stroked on the same grid
// with Phosphor's Regular stroke (16 units, round caps and joins).
void Icon(const wchar_t* path, const Rect& box, const D2D1_COLOR_F& c, bool stroked = false, float rotation = 0);
void IconAt(const wchar_t* path, float cx, float cy, float size, const D2D1_COLOR_F& c, bool stroked = false);
// A mouse cursor drawn from an icon path (dark, with a light halo), `side` physical pixels square,
// its hot spot at (hotX, hotY) in the path's 256 grid. Null if it could not be made.
HCURSOR IconCursor(const wchar_t* path, int side, float hotX, float hotY);

// The canvas. The GPU canvas is a texture the engine's Direct3D 12 device draws into: it has to be
// on this device's adapter, and is opened here from its NT handle (which this closes).
bool GfxAdapterLuid(uint32_t* low, int32_t* high);
Microsoft::WRL::ComPtr<ID2D1Bitmap1> GfxOpenShared(void* ntHandle);
// Colour management: a size^3 RGBA8 table (red fastest) mapping canvas values to the monitor;
// null to show them unchanged. The document is drawn through it, nothing else is.
void GfxSetCanvasLut(const uint8_t* rgba, UINT size);
bool GfxHasCanvasLut();
// A document bitmap (`source` in its pixels) into `dest` (DIPs), through the colour table.
void GfxDrawCanvas(ID2D1Bitmap* b, const Rect& dest, const Rect& source, float opacity, bool nearest);

// Bitmaps.
Microsoft::WRL::ComPtr<ID2D1Bitmap1> MakeBitmap(UINT w, UINT h, const void* bgra = nullptr, UINT stride = 0);
void DrawBitmap(ID2D1Bitmap* b, const Rect& dest, float opacity, const Rect* source, bool nearest);
ID2D1StrokeStyle* DashStyle(float offset);
// Round caps and joins: wide strokes along a hand-drawn path, with no miter spikes at its turns.
ID2D1StrokeStyle* RoundStyle();
