#pragma once
// Icons Phosphor does not have, drawn by hand on Phosphor's 256-unit grid and meant to be stroked
// with its Regular stroke (16 units, round caps and joins): Icon(path, box, colour, /*stroked*/true).
// Geometry follows Phosphor's conventions: a 32-unit safe margin, optical centring on (128, 128).

// Rounded Rectangle: the Rectangle with generous corners.
inline const wchar_t* CuRoundRect = L"M76 56 H180 A44 44 0 0 1 224 100 V156 A44 44 0 0 1 180 200 H76 A44 44 0 0 1 32 156 V100 A44 44 0 0 1 76 56 Z";

// Triangle: the shape tool's, point up, filling its frame.
inline const wchar_t* CuTriangle = L"M128 40 L224 208 H32 Z";

// Artboard: a frame whose edges run past its corners.
inline const wchar_t* CuArtboard = L"M80 32 V224 M176 32 V224 M32 80 H224 M32 176 H224";

// Elliptical marquee: the selection ellipse as eight marching dashes (r = 88).
inline const wchar_t* CuEllipseMarquee =
    L"M213.6 107.5 A88 88 0 0 1 213.6 148.5 M203 174 A88 88 0 0 1 174 203 M148.5 213.6 A88 88 0 0 1 107.5 213.6 "
    L"M82 203 A88 88 0 0 1 53 174 M42.4 148.5 A88 88 0 0 1 42.4 107.5 M53 82 A88 88 0 0 1 82 53 "
    L"M107.5 42.4 A88 88 0 0 1 148.5 42.4 M174 53 A88 88 0 0 1 203 82";

// Layer styles: an italic f and a small x.
inline const wchar_t* CuFx = L"M156 52 C132 40 112 54 106 80 L82 208 M72 112 H136 M150 136 L206 204 M206 136 L150 204";

// Default colours: black over white, the two swatches in miniature.
inline const wchar_t* CuDefaultColors = L"M48 48 H136 V136 H48 Z M120 120 H208 V208 H120 Z";

// Quick Mask: a frame with a round window.
inline const wchar_t* CuQuickMask = L"M40 56 H216 V200 H40 Z M168 128 A40 40 0 1 1 88 128 A40 40 0 1 1 168 128";

// Dodge, burn and smudge are not in Phosphor's tool vocabulary either.
inline const wchar_t* CuDodge = L"M128 160 A48 48 0 1 1 128 64 A48 48 0 1 1 128 160 M128 160 V232";
inline const wchar_t* CuBurn = L"M72 208 C72 160 96 136 128 136 C160 136 184 160 184 208 M100 136 C92 96 112 64 128 40 C144 64 164 96 156 136";
inline const wchar_t* CuSmudge = L"M88 216 V112 A20 20 0 0 1 128 112 V88 A20 20 0 0 1 168 88 V176 C168 200 152 224 120 224 H112 C96 224 88 216 88 216 M48 80 L72 56 M32 128 H64";

// Single-row and single-column marquees.
inline const wchar_t* CuRowMarquee = L"M32 128 H56 M88 128 H112 M144 128 H168 M200 128 H224";
inline const wchar_t* CuColumnMarquee = L"M128 32 V56 M128 88 V112 M128 144 V168 M128 200 V224";

// The Vetro brand mark: a pane of glass catching light.
inline const wchar_t* CuBrand = L"M64 40 H192 A24 24 0 0 1 216 64 V192 A24 24 0 0 1 192 216 H64 A24 24 0 0 1 40 192 V64 A24 24 0 0 1 64 40 Z M88 168 L168 88";
