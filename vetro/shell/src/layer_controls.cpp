#include "panels.h"
#include "icons_phosphor.h"
#include <cctype>

namespace {
ToolPanel* filterPanel = nullptr;
std::string query, effectQuery, kindFilter, blendFilter, labelFilter;
bool effectsOnly = false;
std::map<uint64_t, bool> expandedFx;
std::unique_ptr<Menu> menu;
std::map<uint64_t, Rect> rowRects;
uint64_t transferFrom = 0;
int transferIndex = -1;
bool transferSmart = false;
std::map<std::pair<uint64_t, int>, Rect> detailRects;
class FxBadge : public TextButton {
 public:
  uint64_t layer;
  float x0 = 0, y0 = 0;
  bool moving = false;
  explicit FxBadge(uint64_t id)
      : TextButton(L"fx", Quiet,
                   [id] {
                     expandedFx[id] = !expandedFx[id];
                     Exec("layer.setEffectsExpanded",
                          JsonObject().Num("layer", double(id)).Bool("expanded", expandedFx[id]).Text());
                   }),
        layer(id) {}
  void OnDown(const PointerEvent& e) override {
    TextButton::OnDown(e);
    x0 = e.x;
    y0 = e.y;
    moving = false;
  }
  void OnDrag(const PointerEvent& e) override { moving |= std::hypot(e.x - x0, e.y - y0) > 5; }
  void OnUp(const PointerEvent& e, bool inside) override {
    if (moving) {
      moving = false;
      for (auto& r : rowRects)
        if (r.first != layer && Inside(r.second, e.x, e.y)) {
          Exec("layer.layerStyle.transferEffects",
               JsonObject().Num("from", double(layer)).Num("to", double(r.first)).Bool("copy", e.mods.alt).Text());
          break;
        }
      return;
    }
    TextButton::OnUp(e, inside);
  }
};

void FilterRefresh() { RebuildLayerFilter(); }
class MaskThumb : public Widget {
 public:
  uint64_t layer;
  unsigned kind;
  bool on;
  Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
  MaskThumb(uint64_t id, unsigned k, bool enabled) : layer(id), kind(k), on(enabled) {
    Snapshot s(pc_snapshot(engine));
    std::vector<uint8_t> px(34 * 34 * 4);
    if (s && pc_mask_thumb(s.p, id, k, 34, px.data(), px.size(), 34 * 4))
      bitmap = MakeBitmap(34, 34, px.data(), 34 * 4);
    tooltip = k == 1 ? L"Vector mask" : k == 2 ? L"Smart Filter Mask" : L"Layer mask";
  }
  void Paint() override {
    if (bitmap) DrawBitmap(bitmap.Get(), rect, 1, nullptr, false);
    bool selected = uint64_t(docInfo["activeLayer"].Int()) == layer &&
                    docInfo["channels"]["target"]["kind"].Str() == (kind == 1 ? "vectorMask" : "layerMask");
    StrokeRound(rect, 3, selected ? P.accent : P.dim, selected ? 2.f : 1.f);
    if (!on) {
      Line(rect.left + 2, rect.top + 2, rect.right - 2, rect.bottom - 2, P.accent, 2);
      Line(rect.left + 2, rect.bottom - 2, rect.right - 2, rect.top + 2, P.accent, 2);
    }
  }
  void OnUp(const PointerEvent& e, bool inside) override {
    Widget::OnUp(e, inside);
    if (!inside) return;
    JsonObject p;
    p.Num("layer", double(layer));
    if (kind == 2) {
      if (e.mods.shift)
        Exec("layer.smartFilter.disableFilterMask", p.Text());
      else {
        std::vector<MenuItem> rows;
        rows.push_back(
            ActionItem(L"Enable / Disable Mask", [p] { Exec("layer.smartFilter.disableFilterMask", p.Text()); }));
        rows.push_back(
            ActionItem(L"Delete Filter Mask", [p] { Exec("layer.smartFilter.deleteFilterMask", p.Text()); }));
        menu = std::make_unique<Menu>(rows);
        menu->Open(rect, Side::Left);
      }
    } else if (e.mods.ctrl) {
      p.Str("channel", kind == 1 ? "vectorMask" : "mask")
          .Str("operation", e.mods.shift && e.mods.alt ? "intersect"
                            : e.mods.shift             ? "add"
                            : e.mods.alt               ? "subtract"
                                                       : "new");
      Exec("select.loadSelection", p.Text());
    } else if (e.mods.shift)
      Exec(kind == 1 ? "layer.vectorMask.enabled" : "layer.layerMask.enabled", p.Text());
    else if (e.mods.alt && !kind)
      Exec("view.layerMask", p.Str("mode", "toggleGray").Text(), [](bool, const std::string&) { canvas->Reload(); });
    else {
      const auto id = layer;
      const auto k = kind;
      Exec("layer.select", p.Str("mode", "replace").Text(), [id, k](bool ok, const std::string&) {
        if (ok) Exec("channel.target", JsonObject().Str("channel", k == 1 ? "vectorMask" : "mask").Text());
      });
    }
  }
};
class FilterMaskRow : public Widget {
 public:
  uint64_t layer;
  bool enabled;
  std::unique_ptr<MaskThumb> mask;
  explicit FilterMaskRow(const Json& l)
      : layer(uint64_t(l["id"].Int())),
        enabled(l["smartFiltersEnabled"].Truthy(true)),
        mask(std::make_unique<MaskThumb>(layer, 2, l["filterMaskEnabled"].Truthy(true))) {
    mask->parent = this;
  }
  void Place() { mask->rect = RWH(rect.left + 58, MidY(rect) - 17, 34, 34); }
  void Paint() override {
    Place();
    Text(enabled ? L"●" : L"○", R(rect.left + 38, rect.top, rect.left + 58, rect.bottom), F_Small, P.dim);
    mask->Paint();
    Text(L"Smart Filters", R(rect.left + 100, rect.top, rect.right - 6, rect.bottom), F_Small, P.text);
  }
  Widget* HitTest(float x, float y) override {
    Place();
    if (auto* hit = mask->HitTest(x, y)) return hit;
    return Widget::HitTest(x, y);
  }
  void OnUp(const PointerEvent& e, bool inside) override {
    Widget::OnUp(e, inside);
    if (inside && e.x < rect.left + 58)
      Exec("layer.smartFilter.disableSmartFilters", JsonObject().Num("layer", double(layer)).Text());
  }
};
std::string StyleId(std::string name) {
  static const std::map<std::string, std::string> ids{{"Drop Shadow", "dropShadow"},
                                                      {"Inner Shadow", "innerShadow"},
                                                      {"Outer Glow", "outerGlow"},
                                                      {"Inner Glow", "innerGlow"},
                                                      {"Stroke", "stroke"},
                                                      {"Color Overlay", "colorOverlay"},
                                                      {"Gradient Overlay", "gradientOverlay"},
                                                      {"Pattern Overlay", "patternOverlay"},
                                                      {"Satin", "satin"},
                                                      {"Bevel & Emboss", "bevelEmboss"}};
  auto i = ids.find(name);
  return "layer.layerStyle." + (i == ids.end() ? "blendingOptions" : i->second);
}
class DetailRow : public Widget {
 public:
  uint64_t layer;
  int index;
  bool smart, visibleItem;
  Json item;
  DetailRow(uint64_t id, int i, bool s, const Json& j)
      : layer(id), index(i), smart(s), visibleItem(j["visible"].Truthy(true)), item(j) {}
  void Paint() override {
    if (smart) detailRects[{layer, index}] = rect;
    Text(visibleItem ? L"●" : L"○", R(rect.left + 38, rect.top, rect.left + 60, rect.bottom), F_Small, P.dim);
    auto name = smart ? CommandLabel(item["command"].Str()) : W(item["name"].Str());
    Text(name, R(rect.left + 66, rect.top, rect.right - 34, rect.bottom), F_Small, P.text);
    if (smart) Text(L"◐", R(rect.right - 30, rect.top, rect.right - 4, rect.bottom), F_Small, P.dim);
  }
  void OnDown(const PointerEvent& e) override {
    if (e.button == 0 && e.x > rect.left + 62) {
      transferFrom = layer;
      transferIndex = index;
      transferSmart = smart;
    }
  }
  void OnUp(const PointerEvent& e, bool inside) override {
    Widget::OnUp(e, inside);
    if (transferFrom) {
      const uint64_t from = transferFrom;
      const int ix = transferIndex;
      const bool s = transferSmart;
      transferFrom = 0;
      if (!inside && s)
        for (auto& target : detailRects)
          if (target.first.first == from && target.first.second != ix && Inside(target.second, e.x, e.y)) {
            Exec("layer.smartFilter.move",
                 JsonObject().Num("layer", double(from)).Num("index", ix).Num("to", target.first.second).Text());
            return;
          }
      if (!inside)
        for (auto& target : rowRects)
          if (Inside(target.second, e.x, e.y) && target.first != from) {
            Exec(s ? "layer.smartFilter.transfer" : "layer.layerStyle.transferEffects",
                 JsonObject()
                     .Num("from", double(from))
                     .Num("to", double(target.first))
                     .Num(s ? "filter" : "effect", ix)
                     .Bool("copy", e.mods.alt)
                     .Text());
            return;
          }
    }
    if (!inside) return;
    JsonObject p;
    p.Num("layer", double(layer)).Num("index", index);
    if (e.x < rect.left + 62) {
      Exec(smart ? "layer.smartFilter.setVisible" : "layer.setEffectsVisible", p.Bool("visible", !visibleItem).Text());
      return;
    }
    if (smart && e.x > rect.right - 32) {
      static std::unique_ptr<ParamDialog> blending;
      std::vector<ParamField> fields;
      ParamField blend;
      blend.kind = ParamField::Choice;
      blend.key = "blend";
      blend.label = L"Blend";
      blend.choices = {"normal", "multiply", "screen",     "overlay", "softLight", "hardLight",
                       "darken", "lighten",  "difference", "color",   "luminosity"};
      auto current = item["blend"].Str();
      if (!current.empty()) current[0] = char(tolower(current[0]));
      for (size_t n = 0; n < blend.choices.size(); n++)
        if (blend.choices[n] == current) blend.choice = int(n);
      fields.push_back(blend);
      ParamField opacity;
      opacity.key = "opacity";
      opacity.label = L"Opacity";
      opacity.min = 0;
      opacity.max = 1;
      opacity.def = item["opacity"].Num(1);
      opacity.decimals = 2;
      fields.push_back(opacity);
      blending =
          std::make_unique<ParamDialog>(L"Smart Filter Blending Options", "layer.smartFilter.blendingOptions", fields);
      blending->finish = [id = layer, i = index](bool ok, const std::string& params) {
        if (!ok) return;
        auto j = Json::Parse(params);
        JsonObject p;
        p.Num("layer", double(id)).Num("index", i);
        for (auto& kv : j.o)
          if (kv.first != "index" && kv.first != "layer") p.Add(kv.first.c_str(), JsonText(kv.second));
        Exec("layer.smartFilter.blendingOptions", p.Text());
      };
      blending->Open(rect, Side::Left);
      return;
    }
    if (e.clicks == 2) {
      if (smart) {
        auto fields = ParseParams(paramsDoc[item["command"].Str()]);
        for (auto& f : fields) {
          const auto& v = item["params"][f.key.c_str()];
          if (v.kind == Json::Number) f.def = v.n;
          if (v.kind == Json::Bool) f.flag = v.b;
          if (v.kind == Json::String)
            for (size_t i = 0; i < f.choices.size(); i++)
              if (f.choices[i] == v.s) f.choice = int(i);
        }
        static std::unique_ptr<ParamDialog> edit;
        edit =
            std::make_unique<ParamDialog>(CommandLabel(item["command"].Str()), "layer.smartFilter.setParams", fields);
        edit->finish = [id = layer, i = index](bool ok, const std::string& params) {
          if (ok)
            Exec("layer.smartFilter.setParams",
                 JsonObject().Num("layer", double(id)).Num("index", i).Add("params", params).Text());
        };
        edit->Open(rect, Side::Left);
      } else
        Exec("layer.select", JsonObject().Num("layer", double(layer)).Text(),
             [id = StyleId(item["name"].Str())](bool ok, const std::string&) {
               if (ok) RunCommand(id, CommandLabel(id));
             });
    }
    if (e.button == 1 && smart) {
      std::vector<MenuItem> rows;
      rows.push_back(ActionItem(
          L"Move up", [p, i = index]() mutable { Exec("layer.smartFilter.move", p.Num("to", i + 1).Text()); }));
      rows.push_back(ActionItem(L"Move down", [p, i = index]() mutable {
        if (i > 0) Exec("layer.smartFilter.move", p.Num("to", i - 1).Text());
      }));
      rows.push_back(ActionItem(L"Delete", [p] { Exec("layer.smartFilter.delete", p.Text()); }));
      menu = std::make_unique<Menu>(rows);
      menu->Open(rect, Side::Left);
    }
  }
};
ScrollArea* adjustmentArea = nullptr;
uint64_t adjustmentLayer = 0;
std::string adjustmentKind, adjustmentParams;
std::string adjustmentSource;
std::shared_ptr<LivePreview> adjustmentPreview;
bool adjustmentPending = false;
ParamForm* adjustmentForm = nullptr;
std::vector<std::pair<Widget*, float>> adjustmentRows;
CurveEditor* curve = nullptr;
std::string toneChannel = "points";
Json adjustmentValues;
std::map<std::string, Json> adjustmentArrays;
void AdjustmentChange() {
  JsonObject p;
  p.Num("layer", double(adjustmentLayer));
  if (adjustmentForm) {
    JsonObject form;
    adjustmentForm->Write(form);
    if (adjustmentKind == "levels" && !toneChannel.empty())
      p.Add(toneChannel.c_str(), form.Text());
    else
      for (auto& kv : Json::Parse(form.Text()).o) p.Add(kv.first.c_str(), JsonText(kv.second));
  }
  if (curve) p.Add(toneChannel.c_str(), curve->PointsJson()).Bool("pencil", false);
  for (auto& kv : adjustmentArrays) p.Add(kv.first.c_str(), JsonText(kv.second));
  adjustmentParams = p.Text();
  adjustmentPending = true;
}
class AdjustmentPicker : public CanvasTool {
 public:
  std::string point;
  explicit AdjustmentPicker(std::string p) : point(std::move(p)) {}
  void Drag(double, double, const PointerEvent&) override {}
  void Up(double, double, const PointerEvent&) override {}
  void Down(double x, double y, const PointerEvent&) override {
    FinishAdjustmentPreview();
    Exec("layer.setAdjustment",
         JsonObject()
             .Num("layer", double(adjustmentLayer))
             .Add("eyedropper", JsonObject().Str("point", point).Add("at", Pt(x, y)).Text())
             .Text(),
         [this](bool, const std::string&) {
           LeaveMode(this);
           adjustmentLayer = 0;
           SyncAdjustmentProperties();
         });
  }
  bool Key(UINT vk, Mods) override {
    if (vk == VK_ESCAPE) {
      LeaveMode(this);
      return true;
    }
    return false;
  }
};
std::unique_ptr<AdjustmentPicker> picker;
std::string KindId(const std::string& label) {
  static const std::map<std::string, std::string> kinds{{"Brightness/Contrast", "brightnessContrast"},
                                                        {"Hue/Saturation", "hueSaturation"},
                                                        {"Color Balance", "colorBalance"},
                                                        {"Black & White", "blackWhite"},
                                                        {"Photo Filter", "photoFilter"},
                                                        {"Channel Mixer", "channelMixer"},
                                                        {"Color Lookup", "colorLookup"},
                                                        {"Gradient Map", "gradientMap"},
                                                        {"Selective Color", "selectiveColor"}};
  auto i = kinds.find(label);
  if (i != kinds.end()) return i->second;
  auto s = label;
  if (!s.empty()) s[0] = char(tolower(s[0]));
  return s;
}
}  // namespace
void InitializeLayerControls() {
  filterPanel = new ToolPanel(L"Layer Filters");
  ui::AddLayer(filterPanel);
  auto* name = filterPanel->Row<TextField>(30, L"Name", L"");
  name->onChange = [](const std::wstring& t) {
    int n = WideCharToMultiByte(CP_UTF8, 0, t.data(), int(t.size()), nullptr, 0, nullptr, nullptr);
    query.resize(size_t(n));
    if (n) WideCharToMultiByte(CP_UTF8, 0, t.data(), int(t.size()), query.data(), n, nullptr, nullptr);
    FilterRefresh();
  };
  std::vector<std::string> kinds{"", "Pixel", "Type", "Shape", "Adjustment", "Fill", "Smart Object", "Group"};
  std::vector<std::wstring> names{L"All types"};
  for (size_t i = 1; i < kinds.size(); i++) names.push_back(W(kinds[i]));
  auto* type = filterPanel->Row<SelectList>(30, names, 0);
  type->onChange = [kinds](int i) {
    kindFilter = kinds[size_t(i)];
    FilterRefresh();
  };
  auto blends = BlendNames;
  blends.insert(blends.begin(), L"All blend modes");
  auto* blend = filterPanel->Row<SelectList>(30, blends, 0);
  blend->label = L"Blend";
  blend->labelWidth = 70;
  blend->onChange = [](int i) {
    if (!i) {
      blendFilter.clear();
      FilterRefresh();
      return;
    }
    int n = WideCharToMultiByte(CP_UTF8, 0, BlendNames[size_t(i - 1)].c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), 0);
    WideCharToMultiByte(CP_UTF8, 0, BlendNames[size_t(i - 1)].c_str(), -1, s.data(), n, nullptr, nullptr);
    s.resize(size_t(n - 1));
    blendFilter = s;
    FilterRefresh();
  };
  auto* effects = filterPanel->Row<Toggle>(28, L"With effects", false);
  effects->onChange = [](bool on) {
    effectsOnly = on;
    FilterRefresh();
  };
  auto* effect = filterPanel->Row<TextField>(30, L"Effect", L"");
  effect->onChange = [](const std::wstring& t) {
    int n = WideCharToMultiByte(CP_UTF8, 0, t.data(), int(t.size()), nullptr, 0, nullptr, nullptr);
    effectQuery.resize(size_t(n));
    if (n) WideCharToMultiByte(CP_UTF8, 0, t.data(), int(t.size()), effectQuery.data(), n, nullptr, nullptr);
    FilterRefresh();
  };
  std::vector<std::string> labels{"",     "red",    "orange",  "yellow",  "green",  "seafoam",
                                  "blue", "indigo", "magenta", "fuchsia", "violet", "gray"};
  std::vector<std::wstring> colors{L"All colors"};
  for (size_t i = 1; i < labels.size(); i++) colors.push_back(HumanKey(labels[i]));
  auto* color = filterPanel->Row<SelectList>(30, colors, 0);
  color->onChange = [labels](int i) {
    labelFilter = labels[size_t(i)];
    FilterRefresh();
  };
  filterPanel->Row<TextButton>(30, L"Clear filters", TextButton::Secondary, [] {
    query.clear();
    effectQuery.clear();
    kindFilter.clear();
    blendFilter.clear();
    labelFilter.clear();
    effectsOnly = false;
    for (auto& w : filterPanel->body->children) {
      if (auto* f = dynamic_cast<TextField*>(w.get())) f->value.clear();
      if (auto* list = dynamic_cast<SelectList*>(w.get())) list->index = 0;
      if (auto* toggle = dynamic_cast<Toggle*>(w.get()); toggle && toggle->label == L"With effects") toggle->Set(false);
    }
    FilterRefresh();
  });
  filterPanel->Row<Label>(28, L"Locks", F_Section);
  for (auto key : {"transparency", "pixels", "position", "artboard", "all"}) {
    auto* t = filterPanel->Row<Toggle>(28, HumanKey(key), false);
    t->onChange = [key](bool on) { Exec("layer.lockLayers", JsonObject().Bool(key, on).Text()); };
  }
}
void LayoutLayerControls(bool snap) {
  if (filterPanel) filterPanel->Place(ui::Bounds(), snap);
}
bool LayerPanelCommand(const std::string& id) {
  if (id != "window.layerFilters") return false;
  filterPanel->visible = !filterPanel->visible;
  SyncLayerControls();
  return true;
}
void SyncLayerControls() {
  if (!filterPanel) return;
  auto* layer = ActiveLayer();
  for (auto key : {"transparency", "pixels", "position", "artboard", "all"})
    for (auto& c : filterPanel->body->children)
      if (auto* toggle = dynamic_cast<Toggle*>(c.get()); toggle && toggle->label == HumanKey(key)) {
        toggle->enabled = layer != nullptr;
        toggle->Set(layer && (*layer)["locks"][key].Truthy());
      }
}
bool LayerMatches(const Json& l) {
  auto fold = [](const std::wstring& value) {
    std::wstring out(value.size(), L' ');
    if (!value.empty())
      LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, value.data(), int(value.size()), out.data(),
                    int(out.size()), nullptr, nullptr, 0);
    return out;
  };
  auto name = fold(W(l["name"].Str())), needle = fold(W(query));
  bool effectMatch = effectQuery.empty();
  for (auto& fx : l["effects"].a) {
    auto name = fold(W(fx["name"].Str()) + L" " + i18n::Tr(W(fx["name"].Str()))), q = fold(W(effectQuery));
    effectMatch |= name.find(q) != std::string::npos;
  }
  return (needle.empty() || name.find(needle) != std::string::npos) &&
         (kindFilter.empty() || l["kind"].Str() == kindFilter) &&
         (blendFilter.empty() || l["blend"].Str() == blendFilter) &&
         (labelFilter.empty() || l["labelColor"].Str() == labelFilter) && (!effectsOnly || l["effects"].size() > 0) &&
         effectMatch;
}
void AddLayerAccessories(Container& row, const Json& l) {
  uint64_t id = uint64_t(l["id"].Int());
  if (l["hasMask"].Truthy()) row.Add<MaskThumb>(id, 0, l["mask"]["enabled"].Truthy(true));
  if (l["hasVectorMask"].Truthy()) row.Add<MaskThumb>(id, 1, l["vectorMask"]["enabled"].Truthy(true));
  if (l["effects"].size()) {
    auto* fx = row.Add<FxBadge>(id);
    fx->tooltip = L"Expand effects";
  }
}
float LayerAccessoryWidth(const Json& l) {
  return (l["hasMask"].Truthy() ? 40.f : 0) + (l["hasVectorMask"].Truthy() ? 40.f : 0);
}
void PlaceLayerAccessories(Container& row, const Rect& thumb) {
  float x = thumb.right + 5;
  for (auto& c : row.children) {
    if (dynamic_cast<MaskThumb*>(c.get())) {
      c->rect = RWH(x, thumb.top, 34, 34);
      x += 40;
    } else if (auto* fx = dynamic_cast<TextButton*>(c.get()))
      fx->rect = RWH(row.rect.right - 35, MidY(row.rect) - 16, 30, 32);
  }
}
void PaintLayerIndicators(const Json& l, const Rect& r) {
  static const std::map<std::string, D2D1_COLOR_F> colors{
      {"red", Rgba(.95f, .25f, .2f)},    {"orange", Rgba(1, .55f, .15f)},    {"yellow", Rgba(.95f, .85f, .2f)},
      {"green", Rgba(.35f, .75f, .3f)},  {"seafoam", Rgba(.3f, .85f, .7f)},  {"blue", Rgba(.3f, .5f, .95f)},
      {"indigo", Rgba(.45f, .4f, .8f)},  {"magenta", Rgba(.85f, .3f, .75f)}, {"fuchsia", Rgba(.95f, .25f, .6f)},
      {"violet", Rgba(.65f, .4f, .85f)}, {"gray", Rgba(.6f, .6f, .6f)}};
  auto color = colors.find(l["labelColor"].Str());
  if (color != colors.end()) FillRound(R(r.left + 3, r.top + 10, r.left + 6, r.bottom - 10), 1.5f, color->second);
  rowRects[uint64_t(l["id"].Int())] = r;
  bool lock = false;
  for (auto& k : l["locks"].o) lock |= k.second.Truthy();
  if (lock) IconAt(PhLockSimple, r.right - 43, r.top + 8, 12, P.dim);
  if (!l["linkGroup"].IsNull()) IconAt(PhLink, r.left + 42, r.top + 10, 12, P.dim);
  if (l["kind"].Str() == "Smart Object") Text(L"▣", RWH(r.right - 58, r.top, 16, 18), F_Small, P.dim);
}
void AddLayerDetails(Container& list, const Json& l) {
  uint64_t id = uint64_t(l["id"].Int());
  if (expandedFx[id])
    for (size_t i = 0; i < l["effects"].size(); i++) list.Add<DetailRow>(id, int(i), false, l["effects"][i]);
  for (size_t i = l["smartFilters"].size(); i > 0; i--)
    list.Add<DetailRow>(id, int(i - 1), true, l["smartFilters"][i - 1]);
  if (l["hasFilterMask"].Truthy()) list.Add<FilterMaskRow>(l);
}
void ClearLayerDetails() {
  rowRects.clear();
  detailRects.clear();
}
float LayerDetailHeight(Widget* w) {
  return dynamic_cast<DetailRow*>(w) ? 28.f : dynamic_cast<MaskThumb*>(w) ? 34.f : 46.f;
}
ScrollArea* BuildAdjustmentProperties(Container& dock) {
  adjustmentArea = dock.Add<ScrollArea>();
  adjustmentArea->visible = false;
  return adjustmentArea;
}
void FinishAdjustmentPreview() {
  if (adjustmentPending && !adjustmentPreview) adjustmentPreview = std::make_shared<LivePreview>("layer.setAdjustment");
  adjustmentPending = false;
  if (adjustmentPreview) {
    adjustmentPreview->Finish(true, adjustmentParams);
    adjustmentPreview.reset();
  }
}
void FlushAdjustmentPreview() {
  if (adjustmentPending && !jobs::Busy() && (!adjustmentPreview || !adjustmentPreview->Busy())) {
    adjustmentPending = false;
    if (!adjustmentPreview) adjustmentPreview = std::make_shared<LivePreview>("layer.setAdjustment");
    adjustmentPreview->Show(adjustmentParams);
  }
}
void SyncAdjustmentProperties() {
  if (!adjustmentArea) return;
  const Json* l = ActiveLayer();
  bool has = l && (*l)["kind"].Str() == "Adjustment";
  adjustmentArea->visible = has;
  uint64_t id = has ? uint64_t((*l)["id"].Int()) : 0;
  std::string source = has ? JsonText((*l)["adjustment"]) : "";
  if (id == adjustmentLayer && (source == adjustmentSource || adjustmentPreview || adjustmentPending)) return;
  FinishAdjustmentPreview();
  adjustmentLayer = id;
  adjustmentArea->Clear();
  adjustmentRows.clear();
  adjustmentArrays.clear();
  adjustmentForm = nullptr;
  curve = nullptr;
  if (!has) return;
  adjustmentKind = KindId((*l)["adjustmentKind"].Str());
  adjustmentValues = (*l)["adjustment"];
  adjustmentSource = source;
  auto fields = ParseParams(paramsDoc["layer.newAdjustmentLayer." + adjustmentKind]);
  if (adjustmentKind == "curves") fields.clear();
  for (auto& f : fields) {
    const auto& v = adjustmentValues[f.key.c_str()];
    if (v.kind == Json::Number) f.def = v.n;
    if (v.kind == Json::Bool) f.flag = v.b;
    if (v.kind == Json::String)
      for (size_t i = 0; i < f.choices.size(); i++)
        if (f.choices[i] == v.s) f.choice = int(i);
  }
  adjustmentForm = adjustmentArea->Add<ParamForm>(fields, true);
  adjustmentForm->touchedOnly = true;
  adjustmentForm->onChange = AdjustmentChange;
  adjustmentRows.push_back({adjustmentForm, adjustmentForm->Height()});
  for (auto& kv : adjustmentValues.o)
    if (kv.second.kind == Json::Array && kv.second.size() >= 3 && kv.second.size() <= 4 &&
        std::all_of(kv.second.a.begin(), kv.second.a.end(), [](const Json& v) { return v.kind == Json::Number; })) {
      adjustmentArrays[kv.first] = kv.second;
      auto* title = adjustmentArea->Add<Label>(HumanKey(kv.first), F_Section);
      adjustmentRows.push_back({title, 24.f});
      for (size_t i = 0; i < kv.second.size(); i++) {
        const wchar_t* labels[] = {L"Red / Cyan", L"Green / Magenta", L"Blue / Yellow", L"Constant"};
        double limit = adjustmentKind == "colorBalance" ? 100 : 200;
        auto* value = adjustmentArea->Add<NumberField>(labels[i], kv.second[i].Num(), -limit, limit, 1, L"%");
        auto key = kv.first;
        value->onChange = [key, i](double n) {
          adjustmentArrays[key].a[i] = Json::Parse(JsonNum(n));
          AdjustmentChange();
        };
        value->onCommit = [](double) { FinishAdjustmentPreview(); };
        adjustmentRows.push_back({value, 30.f});
      }
    }
  if (adjustmentKind == "curves" || adjustmentKind == "levels") {
    std::vector<std::string> ids{adjustmentKind == "curves" ? "points" : "", "red", "green", "blue"};
    std::vector<std::wstring> labels{L"Composite", L"Red", L"Green", L"Blue"};
    if (docInfo["mode"].Str() == "Cmyk") {
      ids = {adjustmentKind == "curves" ? "points" : "", "cyan", "magenta", "yellow", "black"};
      labels = {L"Composite", L"Cyan", L"Magenta", L"Yellow", L"Black"};
    }
    if (docInfo["mode"].Str() == "Lab") {
      ids = {adjustmentKind == "curves" ? "points" : "", "a", "b"};
      labels = {L"Lightness", L"a", L"b"};
    }
    if (docInfo["mode"].Str() == "Grayscale") {
      ids = {adjustmentKind == "curves" ? "points" : ""};
      labels = {L"Gray"};
    }
    auto* channels = adjustmentArea->Add<SelectList>(labels, 0);
    adjustmentRows.push_back({channels, 30.f});
    toneChannel = ids[0];
    if (adjustmentKind == "curves") {
      curve = adjustmentArea->Add<CurveEditor>();
      curve->onChange = AdjustmentChange;
      adjustmentRows.push_back({curve, 190.f});
      curve->points.clear();
      const auto& displayed = (*l)["curvePoints"].IsNull() ? adjustmentValues : (*l)["curvePoints"];
      for (auto& p : displayed["points"].a) curve->points.push_back({float(p[0].Num()), float(p[1].Num())});
      if (curve->points.size() < 2) curve->Reset();
      channels->onChange = [ids](int i) {
        FinishAdjustmentPreview();
        if (ActiveLayer()) adjustmentValues = (*ActiveLayer())["adjustment"];
        toneChannel = ids[size_t(i)];
        curve->points.clear();
        const auto* selected = ActiveLayer();
        const auto& displayed =
            selected && !(*selected)["curvePoints"].IsNull() ? (*selected)["curvePoints"] : adjustmentValues;
        for (auto& p : displayed[toneChannel.c_str()].a)
          curve->points.push_back({float(p[0].Num()), float(p[1].Num())});
        if (curve->points.size() < 2) curve->Reset();
      };
    } else {
      channels->onChange = [ids](int i) {
        FinishAdjustmentPreview();
        toneChannel = ids[size_t(i)];
        if (ActiveLayer()) adjustmentValues = (*ActiveLayer())["adjustment"];
        const auto& values = toneChannel.empty() ? adjustmentValues : adjustmentValues[toneChannel.c_str()];
        for (size_t n = 0; n < adjustmentForm->fields.size(); n++) {
          auto& f = adjustmentForm->fields[n];
          f.def = values[f.key.c_str()].Num(f.def);
          f.touched = false;
          if (n < adjustmentForm->children.size())
            if (auto* s = dynamic_cast<Slider*>(adjustmentForm->children[n].get())) s->Set(f.def);
        }
      };
    }
    for (auto name : {"black", "gray", "white"}) {
      auto* b = adjustmentArea->Add<TextButton>(HumanKey(name) + L" point", TextButton::Secondary, [name] {
        picker = std::make_unique<AdjustmentPicker>(name);
        EnterMode(picker.get());
      });
      adjustmentRows.push_back({b, 30.f});
    }
    auto* automatic = adjustmentArea->Add<TextButton>(L"Auto", TextButton::Secondary, [] {
      FinishAdjustmentPreview();
      auto params = std::make_shared<std::string>();
      auto error = std::make_shared<std::string>();
      auto id = adjustmentLayer;
      jobs::Run(
          [params, error, id] {
            *params = TakeString(pc_adjustment_auto(engine, id));
            if (params->empty()) *error = LastError();
          },
          [params, error] {
            if (!error->empty()) {
              ui::Toast(W(*error), true);
              return;
            }
            Exec("layer.setAdjustment", *params, [](bool, const std::string&) {
              adjustmentLayer = 0;
              SyncAdjustmentProperties();
            });
          });
    });
    adjustmentRows.push_back({automatic, 30.f});
    auto* presets = adjustmentArea->Add<SelectList>(
        std::vector<std::wstring>{L"Default", L"Medium Contrast", L"Strong Contrast"}, 0);
    presets->onChange = [](int i) {
      FinishAdjustmentPreview();
      JsonObject p;
      p.Num("layer", double(adjustmentLayer));
      if (adjustmentKind == "curves") {
        p.Add(toneChannel.c_str(), i == 0   ? "[[0,0],[255,255]]"
                                   : i == 1 ? "[[0,0],[64,48],[192,208],[255,255]]"
                                            : "[[0,0],[64,32],[192,224],[255,255]]")
            .Bool("pencil", false);
      } else {
        JsonObject values;
        values.Num("inBlack", i == 0   ? 0
                              : i == 1 ? 10
                                       : 25)
            .Num("inWhite", i == 0   ? 255
                            : i == 1 ? 245
                                     : 230)
            .Num("gamma", 1)
            .Num("outBlack", 0)
            .Num("outWhite", 255);
        if (toneChannel.empty()) {
          for (auto& kv : Json::Parse(values.Text()).o) p.Add(kv.first.c_str(), JsonText(kv.second));
        } else
          p.Add(toneChannel.c_str(), values.Text());
      }
      Exec("layer.setAdjustment", p.Text(), [](bool, const std::string&) {
        adjustmentLayer = 0;
        SyncAdjustmentProperties();
      });
    };
    adjustmentRows.push_back({presets, 30.f});
  }
  auto* apply = adjustmentArea->Add<TextButton>(L"Apply", TextButton::Secondary, FinishAdjustmentPreview);
  adjustmentRows.push_back({apply, 30.f});
}
void LayoutAdjustmentProperties(const Rect& r) {
  if (!adjustmentArea) return;
  adjustmentArea->rect = r;
  float y = r.top - adjustmentArea->Scroll();
  for (auto& p : adjustmentRows) {
    p.first->rect = R(r.left + 3, y, r.right - 8, y + p.second);
    if (auto* f = dynamic_cast<ParamForm*>(p.first)) f->Place(r.left + 3, y, r.right - 8);
    y += p.second + 6;
  }
  adjustmentArea->contentHeight = y + adjustmentArea->Scroll() - r.top;
  adjustmentArea->Clamp();
}
