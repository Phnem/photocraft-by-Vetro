#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace clipboard {
constexpr size_t MaxBytes = 256ull * 1024 * 1024;
struct Image { uint32_t width = 0, height = 0; std::vector<uint8_t> rgba; };
enum class Result { Empty, Image, Error };
bool Valid(const Image& image);
bool DecodeDib(const std::vector<uint8_t>& dib, Image& image);
std::vector<uint8_t> EncodeDib(const Image& image);
bool DecodePng(const std::vector<uint8_t>& png, Image& image);
std::vector<uint8_t> EncodePng(const Image& image);
bool Available();  // format metadata only; pixels are read only on an explicit Paste
Result Read(HWND owner, Image& image, std::wstring& error);
// Encoding and allocation can run on a worker; publishing only transfers prepared handles.
struct PreparedImage {
  HGLOBAL dib = nullptr, png = nullptr;
  ~PreparedImage();
  PreparedImage() = default;
  PreparedImage(const PreparedImage&) = delete;
  PreparedImage& operator=(const PreparedImage&) = delete;
};
std::shared_ptr<PreparedImage> Prepare(const Image& image);
bool Publish(HWND owner, const std::shared_ptr<PreparedImage>& data, std::wstring& error);
bool Write(HWND owner, const Image& image, std::wstring& error);
}  // namespace clipboard
