#pragma once
// The C ABI of crates/bridge (photocraft_bridge.dll), and a thin owner for it.
// Conventions are documented in crates/bridge/src/ffi.rs: 1 = success, 0 = failure with the reason
// in pc_last_error; strings out are freed with pc_string_free; pixels are premultiplied BGRA8.
#include <cstdint>
#include <cstddef>
#include <string>
#include <utility>

extern "C" {
struct PcBridge;
struct PcSnapshot;

PcBridge* pc_new();
void pc_free(PcBridge*);
void pc_string_free(char*);
char* pc_last_error();

int pc_execute(PcBridge*, const char* id, const char* params, char** result);
int pc_enabled(PcBridge*, const char* id);
char* pc_enabled_many(PcBridge*, const char* ids);
char* pc_disabled_reason(PcBridge*, const char* id);
char* pc_catalog();
char* pc_commands();
char* pc_session_info(PcBridge*);
char* pc_document_info(PcBridge*);
char* pc_marks(PcBridge*);
char* pc_ui_strings(const char* code);  // the interface translation catalog (TSV), null for English  // Count markers, notes, the Ruler line (JSON)
int pc_revision(PcBridge*, uint64_t* revision, int32_t* damage);
// Every step after `since`, together (a live preview's undo + apply): 2 rect, 1 whole, 0 none.
int pc_damage_since(PcBridge*, uint64_t since, int32_t* damage);
int pc_undo(PcBridge*);
int pc_redo(PcBridge*);
int pc_set_active(PcBridge*, size_t index);
int pc_select_layer(PcBridge*, uint64_t layer);

int pc_open(PcBridge*, const char* path, char** result);
int pc_save(PcBridge*, const char* path, int markSaved, char** result);
int pc_close_active(PcBridge*);
// Clipboard pixels are straight RGBA8. Query size, then provide exactly width*height*4 bytes.
int pc_clipboard_size(PcBridge*, uint32_t* width, uint32_t* height);
int pc_clipboard_read(PcBridge*, uint8_t*, size_t len);
int pc_clipboard_set(PcBridge*, uint32_t width, uint32_t height, const uint8_t*, size_t len);
void pc_clipboard_clear(PcBridge*);

int pc_stroke_begin(PcBridge*, const char* cmd, const char* params, int32_t* damage);
int pc_stroke_push(PcBridge*, const double* points, size_t count, int32_t* damage);
int pc_stroke_end(PcBridge*, int commit, char** result);

PcSnapshot* pc_snapshot(PcBridge*);
void pc_snapshot_free(PcSnapshot*);
int pc_snapshot_size(const PcSnapshot*, uint32_t* width, uint32_t* height);
int pc_render_view(const PcSnapshot*, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t width, uint32_t height,
                   uint8_t* out, size_t len, size_t stride, uint32_t* outW, uint32_t* outH);
int pc_render_damage(const PcSnapshot*, uint32_t width, uint32_t height, const int32_t* damage, uint8_t* out, size_t len,
                     size_t stride, int32_t* outRect);
int pc_layer_thumb(const PcSnapshot*, uint64_t layer, uint32_t side, uint8_t* out, size_t len, size_t stride);
int pc_brush_tip(const char* settings, uint32_t side, uint8_t* out, size_t len, size_t stride);
int pc_mask_thumb(const PcSnapshot*, uint64_t layer, uint32_t kind, uint32_t side, uint8_t* out, size_t len, size_t stride);
char* pc_text_geometry(PcBridge*,uint64_t layer);
int pc_font_preview(const char* family,uint32_t width,uint32_t height,uint8_t* out,size_t len,size_t stride);
char* pc_adjustment_auto(PcBridge*,uint64_t layer);
int pc_clone_preview(PcBridge*,const char* params,int32_t x0,int32_t y0,int32_t x1,int32_t y1,uint8_t* out,size_t len,size_t stride);
int pc_sample(const PcSnapshot*, int32_t x, int32_t y, float* rgba);
int pc_selection_contains(const PcSnapshot*, int32_t x, int32_t y);
char* pc_snap_targets(PcBridge*, const char* params);

// The display: the window's monitor profile, and the canvas -> monitor table (2 written, 1 none).
int pc_set_display(PcBridge*, uint32_t id, const char* name, const uint8_t* icc, size_t len, double x, double y, double w, double h);
uint64_t pc_display_key(PcBridge*);
uint64_t pc_history_len(PcBridge*);
int pc_canvas_lut(PcBridge*, uint32_t size, uint8_t* out, size_t len);

// The GPU canvas: wgpu on Direct3D 12 drawing into a texture shared with this Direct3D 11 device.
// pc_gpu_render: 1 done, 2 not for this document (use the CPU canvas), 0 error. One thread only.
struct PcGpu;
PcGpu* pc_gpu_new(uint32_t luidLow, int32_t luidHigh);
void pc_gpu_free(PcGpu*);
int pc_gpu_target(PcGpu*, uint32_t width, uint32_t height, void** ntHandle);
int pc_gpu_render(PcGpu*, const PcSnapshot*, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
}

// A string the bridge allocated, taken over and freed once.
inline std::string TakeString(char* s) {
  if (!s) return {};
  std::string out(s);
  pc_string_free(s);
  return out;
}
inline std::string LastError() { return TakeString(pc_last_error()); }

// Owned snapshot: the canvas hands one to its render thread while the UI thread may already have
// taken the next.
struct Snapshot {
  PcSnapshot* p = nullptr;
  Snapshot() = default;
  explicit Snapshot(PcSnapshot* s) : p(s) {}
  Snapshot(const Snapshot&) = delete;
  Snapshot& operator=(const Snapshot&) = delete;
  Snapshot(Snapshot&& o) noexcept : p(std::exchange(o.p, nullptr)) {}
  Snapshot& operator=(Snapshot&& o) noexcept {
    if (this != &o) {
      if (p) pc_snapshot_free(p);
      p = std::exchange(o.p, nullptr);
    }
    return *this;
  }
  ~Snapshot() {
    if (p) pc_snapshot_free(p);
  }
  explicit operator bool() const { return p != nullptr; }
};
