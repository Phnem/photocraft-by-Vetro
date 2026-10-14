// Packed DIB layout and ownership: Microsoft Win32 Clipboard Formats / BITMAPV5HEADER.
#include "clipboard.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <limits>

namespace clipboard {
namespace {
using Microsoft::WRL::ComPtr;
struct Apartment {
  HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};
bool Size(uint32_t w, uint32_t h, size_t& bytes) {
  if (!w || !h || w > 32768 || h > 32768) return false;
  uint64_t n = uint64_t(w) * h * 4;
  if (n > MaxBytes) return false;
  bytes = size_t(n); return true;
}
template<class T> bool Field(const std::vector<uint8_t>& b, size_t pos, T& value) {
  if (pos > b.size() || sizeof(T) > b.size() - pos) return false;
  memcpy(&value, b.data() + pos, sizeof(T)); return true;
}
uint8_t Component(uint32_t px, uint32_t mask) {
  if (!mask) return 0;
  unsigned shift = 0; while (!(mask & 1)) { mask >>= 1; shift++; }
  return uint8_t((uint64_t((px >> shift) & mask) * 255 + mask / 2) / mask);
}
bool Mask(uint32_t m) {
  if (!m) return false;
  while (!(m & 1)) m >>= 1;
  return (m & (m + 1)) == 0;
}
UINT PngFormat() { static UINT f = RegisterClipboardFormatW(L"PNG"); return f; }
struct Open {
  bool ok;
  explicit Open(HWND owner) : ok(OpenClipboard(owner) != FALSE) {}
  ~Open() { if (ok) CloseClipboard(); }
};
std::vector<uint8_t> Bytes(UINT format) {
  HANDLE h = GetClipboardData(format); if (!h) return {};
  size_t size = GlobalSize(h); if (!size || size > MaxBytes + 4096) return {};
  const auto* p = static_cast<const uint8_t*>(GlobalLock(h)); if (!p) return {};
  std::vector<uint8_t> bytes;
  try { bytes.assign(p, p + size); } catch (...) { GlobalUnlock(h); throw; }
  GlobalUnlock(h); return bytes;
}
HGLOBAL Memory(const std::vector<uint8_t>& bytes) {
  if (bytes.empty()) return nullptr;
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes.size()); if (!h) return nullptr;
  void* p = GlobalLock(h); if (!p) { GlobalFree(h); return nullptr; }
  memcpy(p, bytes.data(), bytes.size()); GlobalUnlock(h); return h;
}
}  // namespace

bool Valid(const Image& im) { size_t n = 0; return Size(im.width, im.height, n) && im.rgba.size() == n; }

bool DecodeDib(const std::vector<uint8_t>& b, Image& out) {
  BITMAPINFOHEADER h{};
  if (!Field(b, 0, h) || h.biSize < 40 || h.biSize > b.size() || h.biWidth <= 0 || !h.biHeight || h.biHeight == LONG_MIN || h.biPlanes != 1) return false;
  if (h.biSize != 40 && h.biSize != 52 && h.biSize != 56 && h.biSize != 108 && h.biSize != 124) return false;
  if (h.biCompression != BI_RGB && h.biCompression != BI_BITFIELDS && h.biCompression != 6 /* BI_ALPHABITFIELDS */) return false;
  uint32_t w = uint32_t(h.biWidth), height = uint32_t(h.biHeight < 0 ? -h.biHeight : h.biHeight);
  size_t length = 0; if (!Size(w, height, length)) return false;
  unsigned bits = h.biBitCount;
  if (bits != 1 && bits != 4 && bits != 8 && bits != 16 && bits != 24 && bits != 32) return false;
  if (h.biCompression != BI_RGB && bits != 16 && bits != 32) return false;
  uint32_t masks[4] = {bits == 16 ? 0x7c00u : 0xff0000u, bits == 16 ? 0x3e0u : 0xff00u, bits == 16 ? 0x1fu : 0xffu, 0};
  size_t offset = h.biSize;
  if (h.biSize >= 56) {
    for (int i = 0; i < 4; i++) if (!Field(b, 40 + i * 4, masks[i])) return false;
    if (h.biCompression == BI_RGB && !(masks[0] | masks[1] | masks[2])) {
      masks[0] = bits == 16 ? 0x7c00 : 0xff0000; masks[1] = bits == 16 ? 0x3e0 : 0xff00; masks[2] = bits == 16 ? 0x1f : 0xff;
    }
  } else if (h.biCompression != BI_RGB) {
    int count = h.biCompression == 6 ? 4 : 3;
    size_t start = h.biSize >= 52 ? 40 : h.biSize;
    for (int i = 0; i < count; i++) if (!Field(b, start + i * 4, masks[i])) return false;
    if (h.biSize < 52) offset += count * 4;
  }
  if (bits >= 16) {
    for (int i = 0; i < 3; i++) if (!Mask(masks[i]) || (bits == 16 && masks[i] > 0xffff)) return false;
    if ((masks[0] & masks[1]) || (masks[0] & masks[2]) || (masks[1] & masks[2]) ||
        (masks[3] && (!Mask(masks[3]) || (masks[3] & (masks[0] | masks[1] | masks[2]))))) return false;
  }
  size_t colors = bits <= 8 ? (h.biClrUsed ? h.biClrUsed : size_t(1) << bits) : h.biClrUsed;
  if (colors > 256 || colors * 4 > b.size() - offset) return false;
  size_t palette = offset; offset += colors * 4;
  if (h.biSize >= sizeof(BITMAPV5HEADER)) {
    BITMAPV5HEADER v{}; if (!Field(b, 0, v)) return false;
    // In a packed memory DIB, an embedded profile precedes the pixel array.
    if (v.bV5ProfileSize) {
      uint64_t end = uint64_t(v.bV5ProfileData) + v.bV5ProfileSize;
      if (v.bV5ProfileData < offset || end > b.size()) return false;
      if (v.bV5ProfileData == offset) offset = size_t(end);
    }
  }
  size_t stride = size_t((uint64_t(w) * bits + 31) / 32 * 4);
  if (offset > b.size() || uint64_t(stride) * height > b.size() - offset) return false;
  Image im{w, height, std::vector<uint8_t>(length)};
  for (uint32_t y = 0; y < height; y++) {
    const uint8_t* row = b.data() + offset + (h.biHeight < 0 ? y : height - 1 - y) * stride;
    for (uint32_t x = 0; x < w; x++) {
      uint8_t* px = im.rgba.data() + (size_t(y) * w + x) * 4;
      if (bits <= 8) {
        unsigned index = bits == 8 ? row[x] : bits == 4 ? (row[x / 2] >> (x % 2 ? 0 : 4)) & 15 : (row[x / 8] >> (7 - x % 8)) & 1;
        if (index >= colors) return false;
        const uint8_t* c = b.data() + palette + index * 4;
        px[0] = c[2]; px[1] = c[1]; px[2] = c[0]; px[3] = 255;
      } else if (bits == 24) {
        px[0] = row[x * 3 + 2]; px[1] = row[x * 3 + 1]; px[2] = row[x * 3]; px[3] = 255;
      } else {
        uint32_t v = 0; memcpy(&v, row + size_t(x) * (bits / 8), bits / 8);
        for (int i = 0; i < 3; i++) px[i] = Component(v, masks[i]);
        px[3] = masks[3] ? Component(v, masks[3]) : 255; // legacy BI_RGB's high byte is reserved
      }
    }
  }
  out = std::move(im); return true;
}

std::vector<uint8_t> EncodeDib(const Image& im) {
  if (!Valid(im)) return {};
  BITMAPV5HEADER h{}; h.bV5Size = sizeof h; h.bV5Width = LONG(im.width); h.bV5Height = -LONG(im.height);
  h.bV5Planes = 1; h.bV5BitCount = 32; h.bV5Compression = BI_BITFIELDS; h.bV5SizeImage = DWORD(im.rgba.size());
  h.bV5RedMask = 0xff0000; h.bV5GreenMask = 0xff00; h.bV5BlueMask = 0xff; h.bV5AlphaMask = 0xff000000;
  h.bV5CSType = LCS_sRGB; h.bV5Intent = LCS_GM_IMAGES;
  std::vector<uint8_t> b(sizeof h + im.rgba.size()); memcpy(b.data(), &h, sizeof h);
  for (size_t i = 0; i < im.rgba.size(); i += 4) {
    auto* p = b.data() + sizeof h + i; p[0] = im.rgba[i + 2]; p[1] = im.rgba[i + 1]; p[2] = im.rgba[i]; p[3] = im.rgba[i + 3];
  }
  return b;
}

bool DecodePng(const std::vector<uint8_t>& bytes, Image& out) {
  Apartment apartment;
  if (bytes.empty() || bytes.size() > MaxBytes) return false;
  ComPtr<IWICImagingFactory> factory; ComPtr<IWICStream> stream; ComPtr<IWICBitmapDecoder> decoder;
  ComPtr<IWICBitmapFrameDecode> frame; ComPtr<IWICFormatConverter> converter;
  HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
  if (SUCCEEDED(hr)) hr = factory->CreateStream(&stream);
  if (SUCCEEDED(hr)) hr = stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), DWORD(bytes.size()));
  if (SUCCEEDED(hr)) hr = factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
  if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
  Image im;
  if (SUCCEEDED(hr)) hr = frame->GetSize(&im.width, &im.height);
  size_t len = 0; if (FAILED(hr) || !Size(im.width, im.height, len)) return false;
  hr = factory->CreateFormatConverter(&converter);
  if (SUCCEEDED(hr)) hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
  if (FAILED(hr)) return false;
  im.rgba.resize(len); hr = converter->CopyPixels(nullptr, im.width * 4, UINT(len), im.rgba.data());
  if (FAILED(hr)) return false;
  out = std::move(im); return true;
}

std::vector<uint8_t> EncodePng(const Image& im) {
  Apartment apartment;
  if (!Valid(im)) return {};
  ComPtr<IWICImagingFactory> factory; ComPtr<IStream> stream; ComPtr<IWICBitmapEncoder> encoder; ComPtr<IWICBitmapFrameEncode> frame;
  HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
  if (SUCCEEDED(hr)) hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
  if (SUCCEEDED(hr)) hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
  if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
  if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, nullptr);
  if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
  if (SUCCEEDED(hr)) hr = frame->SetSize(im.width, im.height);
  WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
  if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
  if (FAILED(hr) || format != GUID_WICPixelFormat32bppBGRA) return {};
  auto bgra = im.rgba;
  for (size_t n = 0; n < bgra.size(); n += 4) std::swap(bgra[n], bgra[n + 2]);
  hr = frame->WritePixels(im.height, im.width * 4, UINT(bgra.size()), bgra.data());
  if (SUCCEEDED(hr)) hr = frame->Commit();
  if (SUCCEEDED(hr)) hr = encoder->Commit();
  STATSTG stat{}; if (SUCCEEDED(hr)) hr = stream->Stat(&stat, STATFLAG_NONAME);
  if (FAILED(hr) || stat.cbSize.QuadPart > MaxBytes) return {};
  LARGE_INTEGER start{}; if (FAILED(stream->Seek(start, STREAM_SEEK_SET, nullptr))) return {};
  std::vector<uint8_t> bytes(size_t(stat.cbSize.QuadPart)); ULONG read = 0;
  if (FAILED(stream->Read(bytes.data(), ULONG(bytes.size()), &read)) || read != bytes.size()) return {};
  return bytes;
}

bool Available() { return IsClipboardFormatAvailable(PngFormat()) || IsClipboardFormatAvailable(CF_DIBV5) || IsClipboardFormatAvailable(CF_DIB); }
Result Read(HWND owner, Image& image, std::wstring& error) {
  try {
    Open clip(owner); if (!clip.ok) { error = L"The clipboard is busy. Try again."; return Result::Error; }
    bool any = false;
    for (UINT f : {PngFormat(), UINT(CF_DIBV5), UINT(CF_DIB)}) {
      if (!IsClipboardFormatAvailable(f)) continue;
      any = true; auto b = Bytes(f);
      if (f == PngFormat() ? DecodePng(b, image) : DecodeDib(b, image)) return Result::Image;
    }
    if (!any) return Result::Empty;
    error = L"The clipboard image could not be read.";
  } catch (...) { error = L"The clipboard image is too large."; }
  return Result::Error;
}
PreparedImage::~PreparedImage() { if (dib) GlobalFree(dib); if (png) GlobalFree(png); }
std::shared_ptr<PreparedImage> Prepare(const Image& image) {
  auto data = std::make_shared<PreparedImage>();
  data->dib = Memory(EncodeDib(image)); data->png = Memory(EncodePng(image));
  return data->dib && data->png ? data : nullptr;
}
bool Publish(HWND owner, const std::shared_ptr<PreparedImage>& data, std::wstring& error) {
  if (!data || !data->dib || !data->png) { error = L"The clipboard image could not be encoded."; return false; }
  Open clip(owner);
  if (!clip.ok) error = L"The clipboard is busy. Try again.";
  else if (!EmptyClipboard()) error = L"The clipboard could not be updated.";
  else {
    bool done = false;
    if (SetClipboardData(CF_DIBV5, data->dib)) { data->dib = nullptr; done = true; }
    if (SetClipboardData(PngFormat(), data->png)) { data->png = nullptr; done = true; }
    if (done) return true;
    error = L"The clipboard could not be updated.";
  }
  return false;
}
bool Write(HWND owner, const Image& image, std::wstring& error) {
  try {
    return Publish(owner, Prepare(image), error);
  } catch (...) { error = L"The clipboard image is too large."; }
  return false;
}
}  // namespace clipboard
