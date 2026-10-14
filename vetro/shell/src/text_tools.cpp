#include "panels.h"
#include <array>

namespace {
struct Editor {
  uint64_t layer = 0, document = 0, revision = ~0ull;
  bool active = false, pending = false;
  std::wstring text, base;
  size_t caret = 0, anchor = 0;
  Json info, geometry;
  std::shared_ptr<LivePreview> preview;
  std::string params;
  std::vector<std::wstring> undo, redo;
} edit;
ToolPanel *character = nullptr, *paragraph = nullptr;
std::string font = "Inter", fontStyle = "Regular", align = "left", antialias = "smooth";
double fontSize = 24;
D2D1_COLOR_F textColor{0, 0, 0, 1};
bool textColorPicked = false;
std::unique_ptr<Popover> fontPicker;
Json families;
wchar_t pendingSurrogate = 0;
std::vector<std::string> styles{"Regular"};
size_t Scalar(const std::wstring&, size_t);
void SyncControls(const Json& info) {
  size_t position = edit.active ? Scalar(edit.text, std::min(edit.caret, edit.anchor)) : 0;
  Json st = info["runs"][0]["style"], para = info["paragraphs"][0]["style"];
  for (auto& run : info["runs"].a)
    if (size_t(run["start"].Int()) <= position && position <= size_t(run["end"].Int())) {
      st = run["style"];
      break;
    }
  font = st["font_family"].Str();
  fontStyle = st["font_style"].Str();
  fontSize = st["size_pt"].Num(24);
  antialias = info["antialias"].Str();
  if (!antialias.empty()) antialias[0] = char(tolower(antialias[0]));
  align = para["align"].Str();
  if (!align.empty()) align[0] = char(tolower(align[0]));
  for (auto& c : edit.geometry["colors"].a)
    if (size_t(c["start"].Int()) <= position && position <= size_t(c["end"].Int())) {
      auto color = c["color"];
      textColor = {float(color[0].Num()), float(color[1].Num()), float(color[2].Num()), float(color[3].Num(1))};
      textColorPicked = true;
      break;
    }
  if (character) {
    const std::map<std::string, std::string> keys{{"size", "size_pt"},
                                                  {"leading", "leading_pt"},
                                                  {"tracking", "tracking"},
                                                  {"kern", "kern"},
                                                  {"horizontalScale", "horizontal_scale"},
                                                  {"verticalScale", "vertical_scale"},
                                                  {"baselineShift", "baseline_shift_pt"},
                                                  {"fauxBold", "faux_bold"},
                                                  {"fauxItalic", "faux_italic"},
                                                  {"underline", "underline"},
                                                  {"strikethrough", "strikethrough"},
                                                  {"ligatures", "ligatures"},
                                                  {"discretionaryLigatures", "discretionary_ligatures"}};
    for (auto& pair : keys)
      for (auto& w : character->body->children) {
        auto key = HumanKey(pair.first);
        if (auto* n = dynamic_cast<NumberField*>(w.get()); n && n->label == key && !ui::IsCaptured(n)) {
          double v = st[pair.second.c_str()].Num();
          if (pair.first == "leading" && st[pair.second.c_str()].IsNull()) v = fontSize * para["auto_leading"].Num(1.2);
          if (pair.first == "horizontalScale" || pair.first == "verticalScale") v *= 100;
          n->Set(v);
        }
        if (auto* t = dynamic_cast<Toggle*>(w.get()); t && t->label == key) t->Set(st[pair.second.c_str()].Truthy());
      }
  }
  if (paragraph) {
    const std::map<std::string, std::string> keys{{"firstLineIndent", "first_line_indent_pt"},
                                                  {"startIndent", "start_indent_pt"},
                                                  {"endIndent", "end_indent_pt"},
                                                  {"spaceBefore", "space_before_pt"},
                                                  {"spaceAfter", "space_after_pt"},
                                                  {"autoLeading", "auto_leading"}};
    for (auto& pair : keys)
      for (auto& w : paragraph->body->children)
        if (auto* n = dynamic_cast<NumberField*>(w.get()); n && n->label == HumanKey(pair.first) && !ui::IsCaptured(n))
          n->Set(para[pair.second.c_str()].Num() * (pair.first == "autoLeading" ? 100 : 1));
  }
}
std::string Utf8(const std::wstring& t) {
  int n = WideCharToMultiByte(CP_UTF8, 0, t.data(), int(t.size()), nullptr, 0, nullptr, nullptr);
  std::string s(size_t(n), ' ');
  if (n) WideCharToMultiByte(CP_UTF8, 0, t.data(), int(t.size()), s.data(), n, nullptr, nullptr);
  return s;
}
bool High(wchar_t c) { return c >= 0xD800 && c <= 0xDBFF; }
bool Low(wchar_t c) { return c >= 0xDC00 && c <= 0xDFFF; }
size_t Next(const std::wstring& t, size_t i) {
  return std::min(t.size(), i + (i < t.size() && High(t[i]) && i + 1 < t.size() && Low(t[i + 1]) ? 2 : 1));
}
size_t Prev(const std::wstring& t, size_t i) {
  if (!i) return 0;
  return i - ((i >= 2 && Low(t[i - 1]) && High(t[i - 2])) ? 2 : 1);
}
size_t Scalar(const std::wstring& t, size_t i) {
  size_t n = 0;
  for (size_t at = 0; at < std::min(i, t.size()); at = Next(t, at)) n++;
  return n;
}
size_t Unit(const std::wstring& t, size_t i) {
  size_t at = 0;
  while (i-- && at < t.size()) at = Next(t, at);
  return at;
}
void Geometry() {
  if (edit.active && !jobs::Busy()) edit.geometry = Json::Parse(TakeString(pc_text_geometry(engine, edit.layer)));
}
void SelectionFill(const std::vector<D2D_POINT_2F>& points) {
  if (points.size() < 3) return;
  Microsoft::WRL::ComPtr<ID2D1PathGeometry> g;
  Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
  if (FAILED(GfxFactory()->CreatePathGeometry(&g)) || FAILED(g->Open(&sink))) return;
  sink->BeginFigure(points[0], D2D1_FIGURE_BEGIN_FILLED);
  for (size_t i = 1; i < points.size(); i++) sink->AddLine(points[i]);
  sink->EndFigure(D2D1_FIGURE_END_CLOSED);
  sink->Close();
  Dc()->FillGeometry(g.Get(), Ink(Fade(P.accent, .3f)));
}
std::string Params() {
  size_t prefix = 0;
  while (prefix < edit.base.size() && prefix < edit.text.size() && edit.base[prefix] == edit.text[prefix]) prefix++;
  if (prefix && prefix < edit.base.size() && Low(edit.base[prefix])) prefix--;
  size_t oldEnd = edit.base.size(), newEnd = edit.text.size();
  while (oldEnd > prefix && newEnd > prefix && edit.base[oldEnd - 1] == edit.text[newEnd - 1]) {
    oldEnd--;
    newEnd--;
  }
  if (oldEnd < edit.base.size() && Low(edit.base[oldEnd])) {
    oldEnd++;
    newEnd++;
  }
  JsonObject p;
  p.Num("layer", double(edit.layer))
      .Add("replace", JsonObject()
                          .Num("start", double(Scalar(edit.base, prefix)))
                          .Num("end", double(Scalar(edit.base, oldEnd)))
                          .Str("text", Utf8(edit.text.substr(prefix, newEnd - prefix)))
                          .Text());
  return p.Text();
}
void Flush() {
  if (!edit.active || !edit.pending || jobs::Busy() || edit.preview && edit.preview->Busy()) return;
  edit.pending = false;
  if (!edit.preview) edit.preview = std::make_shared<LivePreview>("type.edit");
  edit.params = Params();
  edit.preview->Show(edit.params);
}
void Insert(const std::wstring& text) {
  size_t selected =
      Scalar(edit.text, std::max(edit.caret, edit.anchor)) - Scalar(edit.text, std::min(edit.caret, edit.anchor));
  if (Scalar(edit.text, edit.text.size()) - selected + Scalar(text, text.size()) > 32768) {
    ui::Toast(L"Text is too long", true);
    return;
  }
  edit.undo.push_back(edit.text);
  if (edit.undo.size() > 100) edit.undo.erase(edit.undo.begin());
  edit.redo.clear();
  size_t a = std::min(edit.caret, edit.anchor), b = std::max(edit.caret, edit.anchor);
  edit.text.replace(a, b - a, text);
  edit.caret = edit.anchor = a + text.size();
  edit.pending = true;
  ui::RequestFrame();
}
void ReadInfo(uint64_t id, size_t caret, size_t anchor, bool selectAll = false) {
  Exec("type.info", JsonObject().Num("layer", double(id)).Text(),
       [id, caret, anchor, selectAll](bool ok, const std::string& s) {
         if (!ok) return;
         edit.info = Json::Parse(s);
         auto buffer = W(edit.info["text"].Str());
         if (Scalar(buffer, buffer.size()) > 32768) {
           ui::Toast(L"Text is too long", true);
           return;
         }
         edit.layer = id;
         edit.document = uint64_t(docInfo["id"].Int());
         edit.active = true;
         edit.pending = false;
         edit.preview.reset();
         edit.text = std::move(buffer);
         edit.base = edit.text;
         edit.caret = selectAll ? edit.text.size() : std::min(caret, edit.text.size());
         edit.anchor = selectAll ? 0 : std::min(anchor, edit.text.size());
         edit.undo.clear();
         edit.redo.clear();
         edit.revision = ~0ull;
         Geometry();
         SyncControls(edit.info);
         RebuildOptions();
         Exec("type.fonts", JsonObject().Str("family", font).Text(), [](bool good, const std::string& value) {
           if (!good) return;
           styles.clear();
           auto faces = Json::Parse(value);
           for (auto& f : faces["faces"].a) styles.push_back(f["style"].Str());
           if (styles.empty()) styles.push_back("Regular");
           RebuildOptions();
         });
       });
}
void Style(const std::string& params) {
  auto* layer = ActiveLayer();
  if (!layer || (*layer)["kind"].Str() != "Type") return;
  uint64_t id = uint64_t((*layer)["id"].Int());
  bool resume = edit.active;
  size_t caret = edit.caret, anchor = edit.anchor;
  size_t first = Scalar(edit.text, std::min(caret, anchor)), last = Scalar(edit.text, std::max(caret, anchor));
  FinishTextEditing();
  JsonObject p;
  p.Num("layer", double(id));
  if (resume && first != last) p.Add("range", "[" + std::to_string(first) + "," + std::to_string(last) + "]");
  auto patch = Json::Parse(params);
  for (auto& kv : patch.o) p.Add(kv.first.c_str(), JsonText(kv.second));
  Exec("type.setStyle", p.Text(), [id, resume, caret, anchor](bool ok, const std::string&) {
    if (!ok) return;
    if (resume)
      ReadInfo(id, caret, anchor);
    else
      Exec("type.info", JsonObject().Num("layer", double(id)).Text(), [](bool good, const std::string& s) {
        if (good) {
          SyncControls(Json::Parse(s));
          RebuildOptions();
        }
      });
  });
}
bool WriteClipboard() {
  auto a = std::min(edit.caret, edit.anchor), b = std::max(edit.caret, edit.anchor);
  if (a == b) return true;
  auto text = edit.text.substr(a, b - a);
  HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
  if (!block) return false;
  auto* dst = static_cast<wchar_t*>(GlobalLock(block));
  if (!dst) {
    GlobalFree(block);
    return false;
  }
  memcpy(dst, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
  GlobalUnlock(block);
  if (!OpenClipboard(hwnd)) {
    GlobalFree(block);
    return false;
  }
  EmptyClipboard();
  bool ok = SetClipboardData(CF_UNICODETEXT, block) != nullptr;
  CloseClipboard();
  if (!ok) GlobalFree(block);
  return ok;
}
std::wstring ReadClipboard() {
  std::wstring out;
  if (!OpenClipboard(hwnd)) return out;
  auto block = GetClipboardData(CF_UNICODETEXT);
  size_t size = block ? GlobalSize(block) : 0;
  if (size && size <= 256 * 1024) {
    auto* p = static_cast<const wchar_t*>(GlobalLock(block));
    if (p) {
      size_t n = 0, cap = size / sizeof(wchar_t);
      while (n < cap && p[n]) n++;
      if (n < cap) out.assign(p, n);
      GlobalUnlock(block);
    }
  }
  CloseClipboard();
  std::wstring text;
  for (size_t i = 0; i < out.size(); i++) {
    if (out[i] == L'\r') {
      text += L'\n';
      if (i + 1 < out.size() && out[i + 1] == L'\n') i++;
    } else
      text += out[i];
  }
  return text;
}
size_t Nearest(double x, double y) {
  double best = 1e100;
  size_t index = 0;
  const auto& carets = edit.geometry["carets"];
  for (size_t i = 0; i < carets.size(); i++) {
    auto a = carets[i][0], b = carets[i][1];
    double ax = a[0].Num(), ay = a[1].Num(), bx = b[0].Num(), by = b[1].Num();
    double dx = bx - ax, dy = by - ay, den = dx * dx + dy * dy;
    double t = den ? std::clamp(((x - ax) * dx + (y - ay) * dy) / den, 0.0, 1.0) : 0;
    double d = std::hypot(x - ax - t * dx, y - ay - t * dy);
    if (d < best) {
      best = d;
      index = i;
    }
  }
  return Unit(edit.text, index);
}
class FontRow : public Widget {
 public:
  std::wstring family;
  Microsoft::WRL::ComPtr<ID2D1Bitmap1> preview;
  bool attempted = false;
  explicit FontRow(std::wstring f) : family(std::move(f)) {}
  void Paint() override {
    if (!attempted) {
      attempted = true;
      std::vector<uint8_t> pixels(192 * 32 * 4);
      if (pc_font_preview(Utf8(family).c_str(), 192, 32, pixels.data(), pixels.size(), 192 * 4))
        preview = MakeBitmap(192, 32, pixels.data(), 192 * 4);
    }
    FillRound(rect, 8, Fade(P.hover, hover.v));
    Text(family, R(rect.left + 8, rect.top, rect.right - 8, rect.top + 20), F_Label, P.dim);
    if (preview) DrawBitmap(preview.Get(), RWH(rect.left + 8, rect.top + 21, 192, 32), .9f, nullptr, false);
  }
  void OnUp(const PointerEvent& e, bool inside) override {
    Widget::OnUp(e, inside);
    if (!inside) return;
    font = Utf8(family);
    fontStyle = "Regular";
    ui::ClosePopovers();
    Style(JsonObject().Str("font", font).Str("fontStyle", fontStyle).Text());
  }
};
class FontPicker : public Popover {
 public:
  TextField* query;
  ScrollArea* list;
  FontPicker() {
    query = Add<TextField>(L"Search fonts");
    list = Add<ScrollArea>();
    query->onChange = [this](const std::wstring&) { Build(); };
    Build();
  }
  void Build() {
    list->Clear();
    auto q = query->value;
    std::transform(q.begin(), q.end(), q.begin(), towlower);
    for (auto& f : families["families"].a) {
      auto name = W(f.Str()), low = name;
      std::transform(low.begin(), low.end(), low.begin(), towlower);
      if (q.empty() || low.find(q) != std::wstring::npos) list->Add<FontRow>(name);
    }
    list->offset.Reset(0);
    Arrange();
  }
  D2D1_SIZE_F Natural() override { return {320, std::min(500.f, H(ui::Bounds()) - 80)}; }
  void Arrange() override {
    query->rect = R(target.left + 12, target.top + 12, target.right - 12, target.top + 44);
    list->rect = R(target.left + 12, target.top + 54, target.right - 12, target.bottom - 12);
    float y = list->rect.top - list->Scroll();
    for (auto& c : list->children) {
      c->rect = R(list->rect.left, y, list->rect.right - 6, y + 54);
      y += 60;
    }
    list->contentHeight = float(list->children.size()) * 60;
  }
  void PaintContent() override { Container::Paint(); }
};
void OpenFonts(const Rect& anchor) {
  Exec("type.fonts", "{}", [anchor](bool ok, const std::string& s) {
    if (!ok) return;
    families = Json::Parse(s);
    fontPicker = std::make_unique<FontPicker>();
    fontPicker->Open(anchor, Side::Below);
  });
}
class TypeTool : public CanvasTool {
 public:
  bool vertical, dragging = false, selecting = false;
  D2D_POINT_2F start{}, end{};
  explicit TypeTool(bool v) : vertical(v) {}
  void Down(double x, double y, const PointerEvent& e) override {
    dragging = true;
    start = end = {float(x), float(y)};
    selecting = edit.active;
    if (selecting) {
      auto bounds = edit.info["bounds"];
      double slop = 6 / std::max(.01, double(canvas->Zoom()));
      if (bounds.size() == 4 && (x < bounds[0].Num() - slop || x > bounds[2].Num() + slop ||
                                 y < bounds[1].Num() - slop || y > bounds[3].Num() + slop)) {
        FinishTextEditing();
        selecting = false;
      }
    }
    if (selecting) {
      edit.caret = Nearest(x, y);
      if (!e.mods.shift) edit.anchor = edit.caret;
      if (e.clicks == 2) {
        auto a = edit.caret, b = a;
        while (a && !iswspace(edit.text[Prev(edit.text, a)])) a = Prev(edit.text, a);
        while (b < edit.text.size() && !iswspace(edit.text[b])) b = Next(edit.text, b);
        edit.anchor = a;
        edit.caret = b;
      }
    }
  }
  void Drag(double x, double y, const PointerEvent&) override {
    if (!dragging) return;
    end = {float(x), float(y)};
    if (selecting) edit.caret = Nearest(x, y);
    ui::RequestFrame();
  }
  void Up(double x, double y, const PointerEvent& e) override {
    if (!dragging) return;
    Drag(x, y, e);
    dragging = false;
    if (selecting) return;
    auto create = [this] {
      JsonObject p;
      p.Num("x", start.x)
          .Num("y", start.y)
          .Str("text", "")
          .Str("font", font)
          .Str("fontStyle", fontStyle)
          .Num("size", fontSize)
          .Str("align", align)
          .Str("orientation", vertical ? "vertical" : "horizontal");
      if (textColorPicked)
        p.Add("color", "[" + JsonNum(textColor.r) + "," + JsonNum(textColor.g) + "," + JsonNum(textColor.b) + "," +
                           JsonNum(textColor.a) + "]");
      if (std::hypot(end.x - start.x, end.y - start.y) > 4)
        p.Add("box", "[" + JsonNum(std::min(start.x, end.x)) + "," + JsonNum(std::min(start.y, end.y)) + "," +
                         JsonNum(std::max(1.f, std::abs(end.x - start.x))) + "," +
                         JsonNum(std::max(1.f, std::abs(end.y - start.y))) + "]");
      Exec("type.create", p.Text(), [aa = antialias](bool ok, const std::string& s) {
        if (!ok) return;
        auto id = uint64_t(Json::Parse(s)["layer"].Int());
        if (aa == "smooth")
          ReadInfo(id, 0, 0);
        else
          Exec("type.edit", JsonObject().Num("layer", double(id)).Str("antialias", aa).Text(),
               [id](bool good, const std::string&) {
                 if (good) ReadInfo(id, 0, 0);
               });
      });
    };
    if (std::hypot(end.x - start.x, end.y - start.y) > 4) {
      create();
      return;
    }
    Exec(
        "type.hitTest", JsonObject().Num("x", x).Num("y", y).Text(),
        [create](bool ok, const std::string& s) {
          if (!ok) {
            create();
            return;
          }
          auto j = Json::Parse(s);
          uint64_t id = uint64_t(j["layer"].Int());
          size_t scalar = size_t(j["index"].Int());
          Exec("layer.select", JsonObject().Num("layer", double(id)).Str("mode", "replace").Text(),
               [id, scalar](bool good, const std::string&) {
                 if (good) {
                   ReadInfo(id, 0, 0);
                   Exec("type.info", JsonObject().Num("layer", double(id)).Text(),
                        [scalar](bool valid, const std::string& s) {
                          if (valid) {
                            auto t = W(Json::Parse(s)["text"].Str());
                            edit.caret = edit.anchor = Unit(t, scalar);
                          }
                        });
                 }
               });
        },
        false);
  }
  void Tick() override {
    if (!edit.active) return;
    if (edit.document != uint64_t(docInfo["id"].Int()) || edit.layer != uint64_t(docInfo["activeLayer"].Int())) {
      FinishTextEditing();
      return;
    }
    Flush();
    if (!jobs::Busy() && !edit.pending && edit.revision != uint64_t(docInfo["revision"].Int())) {
      edit.revision = uint64_t(docInfo["revision"].Int());
      Geometry();
      auto id = edit.layer;
      Exec("type.info", JsonObject().Num("layer", double(id)).Text(), [id](bool ok, const std::string& s) {
        if (ok && edit.active && edit.layer == id) edit.info = Json::Parse(s);
      });
    }
    ui::RequestFrame();
  }
  void Overlay() override {
    if (dragging && !selecting) {
      auto a = canvas->ToScreen(start.x, start.y), b = canvas->ToScreen(end.x, end.y);
      StrokeRound(R(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)), 0, P.accent, 1.2f);
    }
    if (!edit.active) return;
    size_t a = Scalar(edit.text, std::min(edit.caret, edit.anchor)),
           b = Scalar(edit.text, std::max(edit.caret, edit.anchor));
    if (a != b) {
      for (auto& c : edit.geometry["clusters"].a)
        if (size_t(c["start"].Int()) < b && size_t(c["end"].Int()) > a) {
          std::vector<D2D_POINT_2F> points;
          for (auto& p : c["poly"].a) points.push_back(canvas->ToScreen(p[0].Num(), p[1].Num()));
          SelectionFill(points);
        }
    } else if (fmod(ui::NowMs(), 1000) < 600) {
      auto line = edit.geometry["carets"][a];
      if (line.size() == 2) {
        auto p = canvas->ToScreen(line[0][0].Num(), line[0][1].Num()),
             q = canvas->ToScreen(line[1][0].Num(), line[1][1].Num());
        Line(p.x, p.y, q.x, q.y, P.text, 1.6f);
      }
    }
    const auto& shape = edit.info["shape"]["Box"];
    if (!shape.IsNull()) {
      const auto& m = edit.info["transform"];
      auto p = canvas->ToScreen(m[4].Num(), m[5].Num()),
           q = canvas->ToScreen(m[4].Num() + shape["width"].Num(), m[5].Num() + shape["height"].Num());
      StrokeRound(R(p.x, p.y, q.x, q.y), 0, P.dim, 1);
    }
  }
  bool Options(Container& dock, std::vector<Widget*>& out) override {
    auto* f = dock.Add<TextButton>(W(font), TextButton::Secondary, nullptr);
    f->onClick = [f] { OpenFonts(f->rect); };
    out.push_back(f);
    std::vector<std::wstring> faces;
    for (auto& s : styles) faces.push_back(W(s));
    int style = 0;
    for (size_t i = 0; i < styles.size(); i++)
      if (styles[i] == fontStyle) style = int(i);
    auto* face = dock.Add<SelectList>(faces, style);
    face->onChange = [](int i) {
      fontStyle = styles[size_t(i)];
      Style(JsonObject().Str("fontStyle", fontStyle).Text());
    };
    out.push_back(face);
    auto* size = dock.Add<NumberField>(L"Size", fontSize, .1, 1296, 1, L" pt");
    size->onChange = [](double v) { fontSize = v; };
    size->onCommit = [](double v) { Style(JsonObject().Num("size", v).Text()); };
    out.push_back(size);
    auto* aa = dock.Add<SelectList>(std::vector<std::wstring>{L"None", L"Sharp", L"Crisp", L"Strong", L"Smooth"}, 4);
    const char* aaIds[] = {"none", "sharp", "crisp", "strong", "smooth"};
    for (int i = 0; i < 5; i++)
      if (antialias == aaIds[i]) aa->index = i;
    aa->onChange = [](int i) {
      const char* ids[] = {"none", "sharp", "crisp", "strong", "smooth"};
      antialias = ids[std::clamp(i, 0, 4)];
      if (!ActiveLayer() || (*ActiveLayer())["kind"].Str() != "Type") return;
      bool active = edit.active;
      auto id = edit.layer;
      auto a = edit.anchor, c = edit.caret;
      FinishTextEditing();
      Exec("type.edit", JsonObject().Str("antialias", antialias).Text(),
           [active, id, a, c](bool ok, const std::string&) {
             if (ok && active) ReadInfo(id, c, a);
           });
    };
    out.push_back(aa);
    auto* alignment = dock.Add<SelectList>(std::vector<std::wstring>{L"Left", L"Center", L"Right", L"Justify"}, 0);
    const char* alignIds[] = {"left", "center", "right", "justify"};
    for (int i = 0; i < 4; i++)
      if (align == alignIds[i]) alignment->index = i;
    alignment->onChange = [](int i) {
      const char* ids[] = {"left", "center", "right", "justify"};
      align = ids[std::clamp(i, 0, 3)];
      Style(JsonObject().Str("align", align).Text());
    };
    out.push_back(alignment);
    auto* color = dock.Add<ColorField>(L"", textColor, true);
    color->labelWidth = 0;
    color->onChange = [](D2D1_COLOR_F c) {
      textColor = c;
      textColorPicked = true;
      Style(JsonObject()
                .Add("color", "[" + JsonNum(c.r) + "," + JsonNum(c.g) + "," + JsonNum(c.b) + "," + JsonNum(c.a) + "]")
                .Text());
    };
    out.push_back(color);
    out.push_back(dock.Add<TextButton>(L"Warp Text…", TextButton::Secondary, [] {
      FinishTextEditing();
      RunCommand("type.warpText", L"Warp Text…");
    }));
    out.push_back(
        dock.Add<TextButton>(L"Character", TextButton::Secondary, [] { TextPanelCommand("window.character"); }));
    out.push_back(
        dock.Add<TextButton>(L"Paragraph", TextButton::Secondary, [] { TextPanelCommand("window.paragraph"); }));
    return true;
  }
  HCURSOR Cursor() override { return LoadCursor(nullptr, IDC_IBEAM); }
  void Deactivate() override {
    FinishTextEditing();
    dragging = false;
  }
  bool Key(UINT vk, Mods m) override { return TextEditorKey(vk, m); }
};
}  // namespace
CanvasTool* NewTypeTool(bool vertical) { return new TypeTool(vertical); }
bool TextEditing() { return edit.active; }
void EditTypeLayer(uint64_t layer, bool selectAll) { ReadInfo(layer, 0, 0, selectAll); }
void FinishTextEditing() {
  if (!edit.active) return;
  edit.active = false;
  pendingSurrogate = 0;
  if (edit.preview || edit.pending) {
    if (!edit.preview) edit.preview = std::make_shared<LivePreview>("type.edit");
    edit.preview->Finish(true, Params());
    edit.preview.reset();
  }
  edit.pending = false;
  ui::RequestFrame();
}
bool TextEditorChar(wchar_t c) {
  if (!edit.active) return false;
  if (High(c)) {
    pendingSurrogate = c;
    return true;
  }
  if (Low(c)) {
    if (pendingSurrogate) {
      Insert(std::wstring{pendingSurrogate, c});
      pendingSurrogate = 0;
    }
    return true;
  }
  pendingSurrogate = 0;
  if (c >= 32) Insert(std::wstring(1, c));
  return true;
}
bool TextEditorKey(UINT vk, Mods m) {
  if (!edit.active) return false;
  if (vk == VK_ESCAPE || vk == VK_RETURN && m.ctrl) {
    FinishTextEditing();
    return true;
  }
  if (m.ctrl && vk == 'A') {
    edit.anchor = 0;
    edit.caret = edit.text.size();
    return true;
  }
  if (m.ctrl && (vk == 'C' || vk == 'X')) {
    if (!WriteClipboard()) ui::Toast(L"Clipboard is unavailable", true);
    if (vk == 'X' && edit.anchor != edit.caret) Insert(L"");
    return true;
  }
  if (m.ctrl && vk == 'V') {
    auto text = ReadClipboard();
    if (!text.empty()) Insert(text);
    return true;
  }
  if (m.ctrl && (vk == 'Z' || vk == 'Y')) {
    auto& from = (vk == 'Y' || m.shift) ? edit.redo : edit.undo;
    auto& to = (vk == 'Y' || m.shift) ? edit.undo : edit.redo;
    if (!from.empty()) {
      to.push_back(edit.text);
      edit.text = from.back();
      from.pop_back();
      edit.caret = edit.anchor = edit.text.size();
      edit.pending = true;
    }
    return true;
  }
  if (vk == VK_RETURN) {
    Insert(L"\n");
    return true;
  }
  if (vk == VK_BACK || vk == VK_DELETE) {
    if (edit.anchor == edit.caret) {
      if (vk == VK_BACK)
        edit.anchor = Prev(edit.text, edit.caret);
      else
        edit.anchor = Next(edit.text, edit.caret);
    }
    if (edit.anchor != edit.caret) Insert(L"");
    return true;
  }
  if (vk == VK_LEFT || vk == VK_RIGHT) {
    if (!m.shift && edit.caret != edit.anchor)
      edit.caret = vk == VK_LEFT ? std::min(edit.caret, edit.anchor) : std::max(edit.caret, edit.anchor);
    else {
      edit.caret = vk == VK_LEFT ? Prev(edit.text, edit.caret) : Next(edit.text, edit.caret);
      if (m.ctrl) {
        while (edit.caret > 0 && edit.caret < edit.text.size() && !iswspace(edit.text[edit.caret]))
          edit.caret = vk == VK_LEFT ? Prev(edit.text, edit.caret) : Next(edit.text, edit.caret);
      }
    }
    if (!m.shift) edit.anchor = edit.caret;
    return true;
  }
  if (vk == VK_HOME || vk == VK_END) {
    if (m.ctrl)
      edit.caret = vk == VK_HOME ? 0 : edit.text.size();
    else if (vk == VK_HOME) {
      while (edit.caret > 0 && edit.text[Prev(edit.text, edit.caret)] != L'\n')
        edit.caret = Prev(edit.text, edit.caret);
    } else {
      while (edit.caret < edit.text.size() && edit.text[edit.caret] != L'\n') edit.caret = Next(edit.text, edit.caret);
    }
    if (!m.shift) edit.anchor = edit.caret;
    return true;
  }
  if (vk == VK_UP || vk == VK_DOWN) {
    size_t scalar = Scalar(edit.text, edit.caret);
    auto stop = edit.geometry["carets"][scalar];
    if (stop.size() == 2) {
      double x = (stop[0][0].Num() + stop[1][0].Num()) / 2, y = (stop[0][1].Num() + stop[1][1].Num()) / 2;
      double lineHeight = std::hypot(stop[0][0].Num() - stop[1][0].Num(), stop[0][1].Num() - stop[1][1].Num()) * 1.2;
      edit.caret = Nearest(x, y + (vk == VK_UP ? -lineHeight : lineHeight));
      if (!m.shift) edit.anchor = edit.caret;
    }
    return true;
  }
  if (vk == VK_TAB) {
    Insert(L"\t");
    return true;
  }
  if (m.ctrl || m.alt || (vk >= VK_F1 && vk <= VK_F24)) return false;
  return true;
}
void InitializeTextPanels() {
  character = new ToolPanel(L"Character");
  paragraph = new ToolPanel(L"Paragraph");
  ui::AddLayer(character);
  ui::AddLayer(paragraph);
  character->Row<TextButton>(30, L"Choose font…", TextButton::Secondary, [] { OpenFonts(character->rect); });
  for (auto key : {"size", "leading", "tracking", "kern", "horizontalScale", "verticalScale", "baselineShift"}) {
    double value = std::string(key) == "size"                                                     ? fontSize
                   : std::string(key) == "horizontalScale" || std::string(key) == "verticalScale" ? 100
                                                                                                  : 0;
    double lo = -5000, hi = 5000;
    if (std::string(key) == "size") {
      lo = .1;
      hi = 1296;
    }
    if (std::string(key) == "leading") {
      lo = .01;
      hi = 5000;
      value = fontSize * 1.2;
    }
    if (std::string(key) == "tracking") {
      lo = -1000;
      hi = 10000;
    }
    if (std::string(key) == "kern") {
      lo = -1000;
      hi = 1000;
    }
    if (std::string(key) == "horizontalScale" || std::string(key) == "verticalScale") {
      lo = .01;
      hi = 10000;
    }
    auto* n = character->Row<NumberField>(30, HumanKey(key), value, lo, hi, 1);
    n->onCommit = [key](double v) { Style(JsonObject().Num(key, v).Text()); };
  }
  auto* kern = character->Row<SelectList>(30, std::vector<std::wstring>{L"Metrics", L"Optical", L"Off"}, 0);
  kern->label = L"Kerning";
  kern->labelWidth = 100;
  kern->onChange = [](int i) {
    const char* ids[] = {"metrics", "optical", "off"};
    Style(JsonObject().Str("kerning", ids[std::clamp(i, 0, 2)]).Text());
  };
  character->Row<TextButton>(30, L"Auto Leading", TextButton::Secondary, [] { Style("{\"leading\":\"auto\"}"); });
  for (auto key : {"fauxBold", "fauxItalic", "underline", "strikethrough", "ligatures", "discretionaryLigatures"}) {
    auto* t = character->Row<Toggle>(28, HumanKey(key), std::string(key) == "ligatures");
    t->onChange = [key](bool on) { Style(JsonObject().Bool(key, on).Text()); };
  }
  auto* caps = character->Row<SelectList>(30, std::vector<std::wstring>{L"Normal", L"Small Caps", L"All Caps"}, 0);
  caps->onChange = [](int i) {
    const char* ids[] = {"normal", "small", "all"};
    Style(JsonObject().Str("caps", ids[std::clamp(i, 0, 2)]).Text());
  };
  auto* alignment =
      paragraph->Row<SelectList>(30,
                                 std::vector<std::wstring>{L"Left", L"Center", L"Right", L"Justify", L"Justify Center",
                                                           L"Justify Right", L"Justify All"},
                                 0);
  alignment->onChange = [](int i) {
    const char* ids[] = {"left", "center", "right", "justify", "justifyCenter", "justifyRight", "justifyAll"};
    Style(JsonObject().Str("align", ids[std::clamp(i, 0, 6)]).Text());
  };
  for (auto key : {"firstLineIndent", "startIndent", "endIndent", "spaceBefore", "spaceAfter", "autoLeading"}) {
    auto* n = paragraph->Row<NumberField>(30, HumanKey(key), std::string(key) == "autoLeading" ? 120 : 0, -5000, 5000,
                                          1, L" pt");
    n->onCommit = [key](double v) { Style(JsonObject().Num(key, v).Text()); };
  }
  auto* direction =
      paragraph->Row<SelectList>(30, std::vector<std::wstring>{L"Auto", L"Left to Right", L"Right to Left"}, 0);
  direction->onChange = [](int i) {
    const char* ids[] = {"auto", "ltr", "rtl"};
    Style(JsonObject().Str("direction", ids[std::clamp(i, 0, 2)]).Text());
  };
  auto* hyphen = paragraph->Row<Toggle>(28, L"Hyphenate", false);
  hyphen->onChange = [](bool on) { Style(JsonObject().Bool("hyphenate", on).Text()); };
}
void LayoutTextPanels(bool snap) {
  for (auto* p : {character, paragraph})
    if (p) p->Place(ui::Bounds(), snap);
}
bool TextPanelCommand(const std::string& id) {
  auto* p = (id == "window.character" || id == "window.panel.character")   ? character
            : (id == "window.paragraph" || id == "window.panel.paragraph") ? paragraph
                                                                           : nullptr;
  if (!p) return false;
  p->visible = !p->visible;
  if (p->visible) {
    if (edit.active)
      SyncControls(edit.info);
    else if (ActiveLayer() && (*ActiveLayer())["kind"].Str() == "Type")
      Exec("type.info", "{}", [](bool ok, const std::string& s) {
        if (ok) SyncControls(Json::Parse(s));
      });
  }
  return true;
}
Json TextEditorState() {
  return Json::Parse(JsonObject()
                         .Bool("active", edit.active)
                         .Num("layer", double(edit.layer))
                         .Str("text", Utf8(edit.text))
                         .Num("caret", double(Scalar(edit.text, edit.caret)))
                         .Num("anchor", double(Scalar(edit.text, edit.anchor)))
                         .Add("geometry", JsonText(edit.geometry))
                         .Text());
}
