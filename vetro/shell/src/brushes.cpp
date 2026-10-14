#include "panels.h"
#include <commdlg.h>
#include <set>

namespace {
ToolPanel *brushesPanel = nullptr, *settingsPanel = nullptr;
Json brushSettings, presets;
std::wstring search;
std::string group, section = "tip";
std::unique_ptr<Popover> palette;
void BuildSettings();

std::string Utf8(const std::wstring& t) {
  int n = WideCharToMultiByte(CP_UTF8, 0, t.data(), int(t.size()), nullptr, 0, nullptr, nullptr);
  std::string s(size_t(n), ' ');
  if (n) WideCharToMultiByte(CP_UTF8, 0, t.data(), int(t.size()), s.data(), n, nullptr, nullptr);
  return s;
}
void Change(const std::vector<std::string>& path, const Json& value) {
  std::string patch = JsonText(value);
  for (auto i = path.rbegin(); i != path.rend(); ++i) patch = "{" + JsonQuote(*i) + ":" + patch + "}";
  Exec("tools.setBrush", patch, [](bool ok, const std::string& s) {
    if (ok) {
      ReadBrushState(Json::Parse(s));
      BuildSettings();
      RebuildOptions();
    }
  });
}
void BuildPresets();
void BuildSettings();
void Reload() {
  Exec("brush.get", "{}", [](bool ok, const std::string& s) {
    if (ok) {
      ReadBrushState(Json::Parse(s));
      BuildSettings();
    }
  });
  Exec("brush.presets.list", "{\"full\":true}", [](bool ok, const std::string& s) {
    if (ok) {
      presets = Json::Parse(s)["presets"];
      BuildPresets();
    }
  });
}
void ImportAbr() {
  wchar_t path[32768] = {};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof ofn;
  ofn.hwndOwner = hwnd;
  ofn.lpstrFile = path;
  ofn.nMaxFile = 32768;
  ofn.lpstrFilter = L"Adobe brushes (*.abr)\0*.abr\0\0";
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;
  if (GetOpenFileNameW(&ofn))
    Exec("brush.presets.importAbr", JsonObject().Str("path", Utf8(path)).Bool("select", false).Text(),
         [](bool ok, const std::string&) {
           if (ok) Reload();
         });
}
class PresetRow : public Widget {
 public:
  Json preset;
  Microsoft::WRL::ComPtr<ID2D1Bitmap1> tip;
  bool attempted = false;
  explicit PresetRow(const Json& p) : preset(p) {}
  void Paint() override {
    if (!attempted) {
      attempted = true;
      std::vector<uint8_t> px(48 * 48 * 4);
      if (pc_brush_tip(JsonText(preset["brush"]).c_str(), 48, px.data(), px.size(), 48 * 4))
        tip = MakeBitmap(48, 48, px.data(), 48 * 4);
    }
    FillRound(rect, 8, Fade(P.accent, .06f + hover.v * .12f));
    Rect t = RWH(rect.left + 4, rect.top + 2, 40, 40);
    if (tip) DrawBitmap(tip.Get(), t, 1, nullptr, false);
    Text(W(preset["name"].Str()), R(rect.left + 52, rect.top, rect.right - 4, rect.bottom), F_Row, P.text);
  }
  void OnUp(const PointerEvent& e, bool inside) override {
    Widget::OnUp(e, inside);
    if (!inside) return;
    Exec("brush.presets.setCurrent", JsonObject().Str("name", preset["name"].Str()).Text(),
         [](bool ok, const std::string&) {
           if (ok)
             Exec("brush.get", "{}", [](bool good, const std::string& s) {
               if (good) {
                 ReadBrushState(Json::Parse(s));
                 BuildSettings();
                 RebuildOptions();
               }
             });
         });
  }
};
void PresetList(Container& list, const std::function<void(float)>& height) {
  std::wstring currentGroup = L"\u0001";
  for (const Json& p : presets.a) {
    if (!group.empty() && p["group"].Str() != group) continue;
    auto name = W(p["name"].Str());
    auto needle = search;
    std::transform(name.begin(), name.end(), name.begin(), towlower);
    std::transform(needle.begin(), needle.end(), needle.begin(), towlower);
    if (!needle.empty() && name.find(needle) == std::wstring::npos) continue;
    std::wstring folder = W(p["group"].Str());
    for (auto& f : p["folder"].a) folder += L" / " + W(f.Str());
    if (folder != currentGroup) {
      list.Add<Label>(folder.empty() ? L"General" : folder, F_Section);
      height(24);
      currentGroup = folder;
    }
    list.Add<PresetRow>(p);
    height(44);
  }
}
void BuildPresets() {
  if (!brushesPanel) return;
  brushesPanel->Reset();
  auto* query = brushesPanel->Row<TextField>(30, L"Search", search);
  query->onChange = [](const std::wstring& t) {
    search = t;
    BuildPresets();
  };
  std::set<std::string> groups;
  for (auto& p : presets.a) if (!p["group"].Str().empty()) groups.insert(p["group"].Str());
  if (!group.empty() && !groups.count(group)) group.clear();
  std::vector<std::wstring> labels{L"All groups"};
  std::vector<std::string> ids{""};
  for (auto& g : groups) {
    labels.push_back(W(g));
    ids.push_back(g);
  }
  auto* folders = brushesPanel->Row<SelectList>(30, labels, 0);
  for (size_t i = 0; i < ids.size(); i++)
    if (ids[i] == group) folders->index = int(i);
  folders->onChange = [ids](int i) {
    group = ids[size_t(i)];
    BuildPresets();
  };
  brushesPanel->Row<TextButton>(30, L"Import ABR…", TextButton::Secondary, ImportAbr);
  PresetList(*brushesPanel->body, [](float h) { brushesPanel->heights.push_back(h); });
  brushesPanel->Arrange();
}
std::vector<std::string> Choices(const std::string& key, const std::string& current) {
  if (key == "control")
    return {"off", "fade", "penPressure", "penTilt", "stylusWheel", "rotation", "initialDirection", "direction"};
  if (key == "mode")
    return {"normal",     "multiply",  "screen",      "overlay",    "darken",    "lighten",
            "colorDodge", "colorBurn", "linearDodge", "linearBurn", "difference"};
  if (key == "style") return {"paper", "canvas", "noise", "burlap"};
  if (key == "tip") return {"round", "square"};
  return {current};
}
void Fields(const Json& value, std::vector<std::string> path, int depth = 0) {
  if (depth > 5 || value.kind != Json::Object) return;
  for (auto& kv : value.o) {
    if (kv.first == "seed" || kv.first == "locks" || kv.first == "color" || kv.first == "background" ||
        kv.first == "data" || kv.first == "preview")
      continue;
    auto p = path;
    p.push_back(kv.first);
    const auto& v = kv.second;
    auto label = HumanKey(kv.first);
    if (v.kind == Json::Object) {
      settingsPanel->Row<Label>(24, label, F_Section);
      Fields(v, p, depth + 1);
    } else if (v.kind == Json::Bool) {
      auto* t = settingsPanel->Row<Toggle>(28, label, v.b);
      t->onChange = [p](bool on) { Change(p, Json::Parse(on ? "true" : "false")); };
    } else if (v.kind == Json::Number) {
      double lo = 0, hi = 1;
      const auto& k = kv.first;
      if (k == "size") {
        lo = .1;
        hi = 5000;
      } else if (k == "angle" || k == "rotation") {
        lo = -180;
        hi = 180;
      } else if (k == "count") {
        lo = 1;
        hi = 16;
      } else if (k == "fadeSteps") {
        lo = 1;
        hi = 10000;
      } else if (k == "scale") {
        lo = .01;
        hi = 10;
      } else if (k == "brightness" || k == "contrast" || k == "purity" || k == "tiltX" || k == "tiltY") {
        lo = -1;
        hi = 1;
      } else if (k == "spacing" || k == "tiltScale" || k == "buildUpRate")
        hi = 10;
      if (v.n > hi) hi = v.n * 2;
      auto* f = settingsPanel->Row<NumberField>(30, label, v.n, lo, hi, 2, L"");
      if (k == "minimum" || k == "min")
        f->enabled = value["control"].IsNull() || value["control"].Str() != "off";
      if (k == "fadeSteps") f->enabled = value["control"].Str() == "fade";
      f->onChange = [p](double n) { Change(p, Json::Parse(JsonNum(n))); };
    } else if (v.kind == Json::String && v.s.size() < 100) {
      auto choices = Choices(kv.first, v.s);
      if (std::find(choices.begin(), choices.end(), v.s) == choices.end()) choices.push_back(v.s);
      std::vector<std::wstring> labels;
      for (auto& s : choices) labels.push_back(HumanKey(s));
      auto* f = settingsPanel->Row<SelectList>(30, labels,
                                               int(std::find(choices.begin(), choices.end(), v.s) - choices.begin()));
      f->label = label;
      f->labelWidth = 110;
      f->onChange = [p, choices](int i) { Change(p, Json::Parse(JsonQuote(choices[size_t(i)]))); };
    }
  }
}
void BuildSettings() {
  if (!settingsPanel) return;
  settingsPanel->Reset();
  std::vector<std::string> ids{"tip",      "shapeDynamics", "scattering", "texture",  "dualBrush", "colorDynamics",
                               "transfer", "pose",          "noise",      "wetEdges", "buildUp",   "smoothing"};
  std::vector<std::wstring> names{L"Brush Tip Shape", L"Shape Dynamics", L"Scattering", L"Texture",
                                  L"Dual Brush",      L"Color Dynamics", L"Transfer",   L"Brush Pose",
                                  L"Noise",           L"Wet Edges",      L"Airbrush",   L"Smoothing"};
  auto* choose =
      settingsPanel->Row<SelectList>(32, names, int(std::find(ids.begin(), ids.end(), section) - ids.begin()));
  choose->onChange = [ids](int i) {
    section = ids[size_t(i)];
    BuildSettings();
  };
  Json fields = brushSettings;
  if (section == "tip") {
    fields.o.clear();
    for (auto& kv : brushSettings.o)
      if (kv.first == "size" || kv.first == "hardness" || kv.first == "roundness" || kv.first == "angle" ||
          kv.first == "spacing" || kv.first == "spacingEnabled" || kv.first == "flipX" || kv.first == "flipY" ||
          kv.first == "pressureSize" || kv.first == "pressureOpacity" || kv.first == "opacity" || kv.first == "flow")
        fields.o.push_back(kv);
    Fields(fields, {});
  } else if (brushSettings[section.c_str()].kind == Json::Object)
    Fields(brushSettings[section.c_str()], {section});
  else {
    fields.o.clear();
    for (auto& kv : brushSettings.o)
      if (kv.first == section || (section == "buildUp" && kv.first == "buildUpRate")) fields.o.push_back(kv);
    Fields(fields, {});
  }
  settingsPanel->Arrange();
}
class BrushPalette : public Popover {
 public:
  ScrollArea* list;
  std::vector<float> heights;
  BrushPalette() {
    auto* size = Add<NumberField>(L"Size", brush.size, .1, 5000, 1, L" px");
    size->onChange = [](double n) {
      brush.size = n;
      Change({"size"}, Json::Parse(JsonNum(n)));
    };
    auto* hard = Add<NumberField>(L"Hardness", brush.hardness, 0, 100, 0, L"%");
    hard->onChange = [](double n) {
      brush.hardness = n;
      Change({"hardness"}, Json::Parse(JsonNum(n / 100)));
    };
    list = Add<ScrollArea>();
    PresetList(*list, [this](float h) { heights.push_back(h); });
  }
  D2D1_SIZE_F Natural() override { return {310, std::min(440.f, H(ui::Bounds()) - 60)}; }
  void Arrange() override {
    children[0]->rect = R(target.left + 14, target.top + 12, target.right - 14, target.top + 42);
    children[1]->rect = R(target.left + 14, target.top + 48, target.right - 14, target.top + 78);
    list->rect = R(target.left + 10, target.top + 88, target.right - 10, target.bottom - 12);
    float y = list->rect.top - list->Scroll();
    for (size_t i = 0; i < list->children.size(); i++) {
      list->children[i]->rect = R(list->rect.left, y, list->rect.right - 6, y + heights[i]);
      y += heights[i] + 4;
    }
    list->contentHeight = y + list->Scroll() - list->rect.top;
  }
  void PaintContent() override { Container::Paint(); }
};
}  // namespace
void ReadBrushState(const Json& result) {
  brushSettings = result["brush"].IsNull() ? result : result["brush"];
  brush.size = brushSettings["size"].Num(brush.size);
  brush.hardness = brushSettings["hardness"].Num(brush.hardness / 100) * 100;
  brush.opacity = brushSettings["opacity"].Num(brush.opacity / 100) * 100;
  brush.flow = brushSettings["flow"].Num(brush.flow / 100) * 100;
  brush.smoothing = brushSettings["smoothing"]["amount"].Num(brush.smoothing / 100) * 100;
}
ToolPanel* BrushPanel(bool settings) { return settings ? settingsPanel : brushesPanel; }
void InitializeToolPanels() {
  brushesPanel = new ToolPanel(L"Brushes");
  settingsPanel = new ToolPanel(L"Brush Settings");
  ui::AddLayer(brushesPanel);
  ui::AddLayer(settingsPanel);
}
void LayoutToolPanels(bool snap) {
  for (auto* p : {brushesPanel, settingsPanel})
    if (p) p->Place(ui::Bounds(), snap);
}
bool ToolPanelCommand(const std::string& id) {
  auto* panel = (id == "window.brushes" || id == "window.panel.brushes")               ? brushesPanel
                : (id == "window.brushSettings" || id == "window.panel.brushSettings") ? settingsPanel
                                                                                       : nullptr;
  if (!panel) return false;
  panel->visible = !panel->visible;
  if (panel->visible) Reload();
  return true;
}
void OpenBrushPalette(const PointerEvent& e) {
  float x = e.x, y = e.y;
  Exec("brush.presets.list", "{\"full\":true}", [x, y](bool ok, const std::string& s) {
    if (!ok) return;
    presets = Json::Parse(s)["presets"];
    palette = std::make_unique<BrushPalette>();
    palette->Open(RWH(x, y, 1, 1), Side::Below);
  });
}
