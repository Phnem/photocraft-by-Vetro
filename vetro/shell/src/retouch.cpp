#include "panels.h"
#include <array>

namespace {
ToolPanel* clonePanel = nullptr;
Json sources;
bool overlay = true, autoHide = true, invert = false, clipped = true;
double overlayOpacity = 50;
std::string overlayBlend = "normal";
int slot = 0;
std::vector<NumberField*> fields;
std::vector<Toggle*> flags;
Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
Rect previewRegion{};
std::string previewKey;
double previewAt = 0;
void SetSource(const std::string& p) {
  Exec("cloneSource.set", p, [](bool ok, const std::string&) {
    if (ok) RefreshCloneSource();
  });
}
class PatchTool : public CanvasTool {
 public:
  bool move, dragging = false, selecting = false, contentAware = true, extend = false, all = false;
  int structure = 4, color = 0;
  D2D_POINT_2F start{}, end{};
  std::vector<D2D_POINT_2F> path;
  explicit PatchTool(bool m) : move(m) {}
  void Down(double x, double y, const PointerEvent&) override {
    start = end = {float(x), float(y)};
    dragging = true;
    Snapshot snapshot(pc_snapshot(engine));
    selecting = !snapshot || !pc_selection_contains(snapshot.p, int32_t(std::floor(x)), int32_t(std::floor(y)));
    path = {{float(x), float(y)}};
  }
  void Drag(double x, double y, const PointerEvent&) override {
    if (!dragging) return;
    end = {float(x), float(y)};
    if (selecting) path.push_back(end);
  }
  void Up(double x, double y, const PointerEvent& e) override {
    if (!dragging) return;
    Drag(x, y, e);
    dragging = false;
    if (selecting) {
      if (path.size() > 2)
        Exec("select.lasso",
             JsonObject().Add("points", PointList(path, false)).Str("mode", "replace").Bool("antiAlias", true).Text());
      path.clear();
      return;
    }
    JsonObject p;
    p.Add("offset", Pt(end.x - start.x, end.y - start.y))
        .Bool("sampleAllLayers", all)
        .Num("structure", structure)
        .Num("color", color);
    if (move)
      p.Str("mode", extend ? "extend" : "move");
    else
      p.Str("mode", "source").Bool("contentAware", contentAware);
    Exec(move ? "paint.contentAwareMove" : "paint.patch", p.Text());
  }
  void Overlay() override {
    if (!dragging) return;
    if (selecting) {
      auto s = path;
      for (auto& p : s) p = canvas->ToScreen(p.x, p.y);
      if (auto g = Polyline(s, true)) Dc()->DrawGeometry(g.Get(), Ink(P.accent), 1.2f);
    } else {
      auto bounds = docInfo["selectionBounds"];
      if (bounds.size() == 4) {
        auto a = canvas->ToScreen(bounds[0].Num() + end.x - start.x, bounds[1].Num() + end.y - start.y);
        auto b = canvas->ToScreen(bounds[0].Num() + bounds[2].Num() + end.x - start.x,
                                  bounds[1].Num() + bounds[3].Num() + end.y - start.y);
        StrokeRound(R(a.x, a.y, b.x, b.y), 0, P.accent, 1.2f);
      }
    }
  }
  bool Options(Container& dock, std::vector<Widget*>& out) override {
    if (move) {
      auto* modes = dock.Add<SelectList>(std::vector<std::wstring>{L"Move", L"Extend"}, extend ? 1 : 0);
      modes->onChange = [this](int i) { extend = i == 1; };
      out.push_back(modes);
    } else {
      auto* aware = dock.Add<Toggle>(L"Content-Aware", contentAware);
      aware->onChange = [this](bool on) { contentAware = on; };
      out.push_back(aware);
    }
    auto* s = dock.Add<NumberField>(L"Structure", structure, 1, 7, 0);
    s->onChange = [this](double n) { structure = int(n); };
    out.push_back(s);
    auto* c = dock.Add<NumberField>(L"Color", color, 0, 10, 0);
    c->onChange = [this](double n) { color = int(n); };
    out.push_back(c);
    auto* a = dock.Add<Toggle>(L"Sample All Layers", all);
    a->onChange = [this](bool on) { all = on; };
    out.push_back(a);
    out.push_back(dock.Add<Label>(L"Select a region, then drag it", F_Label));
    return true;
  }
  bool Key(UINT vk, Mods) override {
    if (vk == VK_ESCAPE) {
      dragging = false;
      path.clear();
      return true;
    }
    return false;
  }
};
}  // namespace
void InitializeRetouchPanel() {
  clonePanel = new ToolPanel(L"Clone Source");
  ui::AddLayer(clonePanel);
  auto* slots = clonePanel->Row<Segmented>(32, std::vector<std::wstring>{L"1", L"2", L"3", L"4", L"5"}, 0);
  slots->onChange = [](int i) {
    slot = i;
    Exec("cloneSource.select", JsonObject().Num("index", i).Text(), [](bool ok, const std::string&) {
      if (ok) RefreshCloneSource();
    });
  };
  for (auto key : {"offsetX", "offsetY", "width", "height", "rotation"}) {
    auto* n = clonePanel->Row<NumberField>(
        30, HumanKey(key), std::string(key) == "width" || std::string(key) == "height" ? 100 : 0, -100000, 100000, 1);
    n->onChange = [key](double v) {
      JsonObject p;
      if (std::string(key) == "offsetX" || std::string(key) == "offsetY") {
        auto s = CloneSourceState();
        double x = s["offset"][0].Num(), y = s["offset"][1].Num();
        if (std::string(key) == "offsetX")
          x = v;
        else
          y = v;
        p.Add("offset", Pt(x, y));
      } else
        p.Num(key, v);
      SetSource(p.Text());
    };
    fields.push_back(n);
  }
  for (auto key : {"flipH", "flipV", "linked"}) {
    auto* t = clonePanel->Row<Toggle>(28, HumanKey(key), false);
    t->onChange = [key](bool on) { SetSource(JsonObject().Bool(key, on).Text()); };
    flags.push_back(t);
  }
  clonePanel->Row<TextButton>(30, L"Reset Transform", TextButton::Secondary, [] {
    Exec("cloneSource.resetTransform", "{}", [](bool ok, const std::string&) {
      if (ok) RefreshCloneSource();
    });
  });
  auto* show = clonePanel->Row<Toggle>(28, L"Show Overlay", true);
  show->onChange = [](bool on) {
    overlay = on;
    Exec("cloneSource.overlay", JsonObject().Bool("show", on).Text());
  };
  auto* alpha = clonePanel->Row<NumberField>(30, L"Overlay Opacity", 50, 0, 100, 0, L"%");
  alpha->onChange = [](double v) {
    overlayOpacity = v;
    Exec("cloneSource.overlay", JsonObject().Num("opacity", v).Text());
  };
  auto* hide = clonePanel->Row<Toggle>(28, L"Auto Hide", true);
  hide->onChange = [](bool on) {
    autoHide = on;
    Exec("cloneSource.overlay", JsonObject().Bool("autoHide", on).Text());
  };
  auto* inv = clonePanel->Row<Toggle>(28, L"Invert", false);
  inv->onChange = [](bool on) {
    invert = on;
    Exec("cloneSource.overlay", JsonObject().Bool("invert", on).Text());
  };
  auto* clip = clonePanel->Row<Toggle>(28, L"Clipped", true);
  clip->onChange = [](bool on) {
    clipped = on;
    Exec("cloneSource.overlay", JsonObject().Bool("clipped", on).Text());
  };
  auto* blend =
      clonePanel->Row<SelectList>(30, std::vector<std::wstring>{L"Normal", L"Darken", L"Lighten", L"Difference"}, 0);
  blend->onChange = [](int i) {
    const char* ids[] = {"normal", "darken", "lighten", "difference"};
    overlayBlend = ids[std::clamp(i, 0, 3)];
    Exec("cloneSource.overlay", JsonObject().Str("blend", overlayBlend).Text());
  };
}
void LayoutRetouchPanel(bool snap) {
  if (clonePanel) clonePanel->Place(ui::Bounds(), snap);
}
bool RetouchPanelCommand(const std::string& id) {
  if (id != "window.cloneSource" && id != "window.panel.cloneSource") return false;
  clonePanel->visible = !clonePanel->visible;
  if (clonePanel->visible) RefreshCloneSource();
  return true;
}
void RefreshCloneSource() {
  Exec("cloneSource.list", "{}", [](bool ok, const std::string& s) {
    if (!ok) return;
    sources = Json::Parse(s);
    slot = int(sources["active"].Int());
    auto source = CloneSourceState();
    if (fields.size() == 5) {
      fields[0]->Set(source["offset"][0].Num());
      fields[1]->Set(source["offset"][1].Num());
      fields[2]->Set(source["width"].Num(100));
      fields[3]->Set(source["height"].Num(100));
      fields[4]->Set(source["rotation"].Num());
      for (size_t i = 0; i < flags.size(); i++)
        flags[i]->Set(source[i == 0 ? "flipH" : i == 1 ? "flipV" : "linked"].Truthy());
    }
    previewKey.clear();
  });
}
Json CloneSourceState() { return sources["sources"][slot]; }
void DrawCloneOverlay(double x, double y, const std::string& sample, bool stroking) {
  if (!overlay || autoHide && stroking || CloneSourceState()["source"].size() != 2 || jobs::Busy()) return;
  // A bounded, cached hover patch. Live strokes themselves use the engine's stroke renderer.
  int side = std::clamp(int(std::ceil(brush.size)), 8, 128);
  int x0 = int(std::floor(x)) - side / 2, y0 = int(std::floor(y)) - side / 2;
  std::string key = std::to_string(x0) + ":" + std::to_string(y0) + ":" + std::to_string(side) + ":" +
                    std::to_string(docInfo["revision"].Int()) + sample + overlayBlend + (invert ? "i" : "") +
                    (clipped ? "c" : "");
  if (key != previewKey && ui::NowMs() - previewAt > 40) {
    previewAt = ui::NowMs();
    std::vector<uint8_t> px(size_t(side) * side * 4);
    std::string p = JsonObject().Add("at", Pt(x, y)).Str("sampleLayer", sample).Str("blend", overlayBlend).Text();
    if (pc_clone_preview(engine, p.c_str(), x0, y0, x0 + side, y0 + side, px.data(), px.size(), size_t(side) * 4)) {
      for (int py = 0; py < side; py++)
        for (int pxi = 0; pxi < side; pxi++) {
          size_t i = (size_t(py) * side + pxi) * 4;
          float dx = float(pxi) - float(side) / 2, dy = float(py) - float(side) / 2;
          if (clipped && dx * dx + dy * dy > float(side * side) / 4) {
            for (size_t c = 0; c < 4; c++) px[i + c] = 0;
          } else if (invert) {
            for (size_t c = 0; c < 3; c++) px[i + c] = px[i + 3] - px[i + c];
          }
        }
      bitmap = MakeBitmap(UINT(side), UINT(side), px.data(), UINT(side) * 4);
      previewRegion = R(float(x0), float(y0), float(x0 + side), float(y0 + side));
      previewKey = key;
    } else
      bitmap.Reset();
  }
  if (bitmap) {
    auto a = canvas->ToScreen(previewRegion.left, previewRegion.top),
         b = canvas->ToScreen(previewRegion.right, previewRegion.bottom);
    DrawBitmap(bitmap.Get(), R(a.x, a.y, b.x, b.y), float(overlayOpacity / 100), nullptr, false);
  }
}
CanvasTool* NewPatchTool(bool move) { return new PatchTool(move); }
