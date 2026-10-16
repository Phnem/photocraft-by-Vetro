#define VETRO_MILESTONE2
#include "milestone1.cpp"
#include <fstream>

namespace {
void Preview() { if (Mode()) Mode()->Tick(); else if (canvas->tool) canvas->tool->Tick(); FlushParameterPreviews(); Drain(); }
NumberField* Number(const std::wstring& label) {
  for (auto* w : optionWidgets) if (auto* n = dynamic_cast<NumberField*>(w); n && n->label == label) return n;
  throw std::runtime_error("numeric field missing: " + Narrow(label));
}
void Value(const std::wstring& label, double v) { auto* n = Number(label); n->Set(v); n->onChange(v); Preview(); }
void TypeValue(const std::wstring& label, const std::wstring& text) {
  Layout(true); auto* n = Number(label); PointerEvent e; e.x = MidX(n->rect); e.y = MidY(n->rect);
  n->OnDown(e); n->OnUp(e,true);
  for (wchar_t c : text) Check(ui::Char(c), "numeric input must accept characters");
  Key(VK_RETURN); Preview();
}
void Action(const std::wstring& label) {
  Check(Mode() != nullptr,"transform mode missing"); std::vector<MenuItem> rows; Mode()->Context(50,50,{},rows);
  for (auto& r : rows) if (r.label == label) { Check(r.enabled && bool(r.action),"transform action is disabled"); r.action(); Preview(); return; }
  throw std::runtime_error("transform action missing: " + Narrow(label));
}
Json Quad() { return TransformState()["quad"]; }
Json Bounds() { return ActiveLayer() ? (*ActiveLayer())["bounds"] : Json{}; }
void Patch() { Command("shape.create","{\"kind\":\"rect\",\"rect\":[20,30,60,40],\"fill\":\"#ff0000\"}"); }
SelectList* Choice(const std::wstring& first) { for (auto* w : optionWidgets) if (auto* s = dynamic_cast<SelectList*>(w); s && !s->options.empty() && s->options[0] == first) return s; throw std::runtime_error("choice missing: " + Narrow(first)); }
void Choose(const std::wstring& first,int i) { auto* s = Choice(first); s->index = i; auto f = s->onChange; f(i); Preview(); }
void ToggleOption(const std::wstring& label,bool on) { for (auto* w : optionWidgets) if (auto* t = dynamic_cast<Toggle*>(w); t && t->label == label) { auto f = t->onChange; t->Set(on); f(on); Preview(); return; } throw std::runtime_error("toggle missing: " + Narrow(label)); }
void Button(const std::wstring& label) { for (auto* w : optionWidgets) if (auto* b = dynamic_cast<TextButton*>(w); b && b->label == label) { auto f = b->onClick; f(); Preview(); return; } throw std::runtime_error("button missing: " + Narrow(label)); }
void Icon(const std::wstring& label) { for (auto* w : optionWidgets) if (auto* b = dynamic_cast<IconButton*>(w); b && b->tooltip == label) { auto f = b->onClick; f(); Preview(); return; } throw std::runtime_error("icon missing: " + Narrow(label)); }
Json ShapeInfo() { return Command("shape.info"); }
void ShapesAndAlignment() {
  for (int depth : {8,16,32}) {
    NewDocument(depth); Tool("roundedRectangle"); Value(L"W",0); Value(L"H",0); Choose(L"New Layer",0);
    Stroke(20,30,80,70); Preview(); auto info = ShapeInfo();
    Check(info["live"]["rect"][2].Int() == 60 && info["live"]["rect"][3].Int() == 40,"shape toolbar must create live dimensions");
    auto history = docInfo["history"].size(); Value(L"W",70); Value(L"W",80); Value(L"H",50); FinishParameterPreviews(); Drain();
    info = ShapeInfo(); Check(info["live"]["rect"][2].Int() == 80 && info["live"]["rect"][3].Int() == 50 && docInfo["history"].size() == history+1,"shape dimensions must preview in one undo step");
    Undo(); Drain(); Preview(); Check(ShapeInfo()["live"]["rect"][2].Int() == 60,"shape dimensions Undo must restore live geometry");
    ToggleOption(L"With Stroke",true); Value(L"Stroke Width",4); FinishParameterPreviews(); Drain(); Choose(L"Solid",1); Choose(L"Stroke Center",1);
    info = ShapeInfo(); Check(info["stroke"]["width"].Int() == 4 && info["stroke"]["dashes"].size() == 2 && info["stroke"]["align"].Str() == "inside","shape stroke must support width, dashes and alignment");
    Value(L"Radius",5); FinishParameterPreviews(); Drain(); Preview();
    Stroke(25,35,32,42,{false,false,true}); info = ShapeInfo();
    Check(info["live"]["radii"][0].Int() == 12 && info["live"]["radii"][1].Int() == 5,"Alt corner handle must change just that live radius");
    history = docInfo["history"].size(); Choose(L"New Layer",2); Stroke(110,50,140,80); Preview();
    Check(ShapeInfo()["path"]["subpaths"].size() == 2 && docInfo["history"].size() == history+1,"path operations must add a subpath to the active shape");
    NewDocument(depth); Patch(); Tool("move"); Choose(L"Auto",2); Icon(L"Align Horizontal Centers"); Icon(L"Align Vertical Centers");
    Check(Bounds()[0].Int() == 70 && Bounds()[1].Int() == 60,"Move alignment buttons must align to the canvas");
    Command("select.rect","{\"x\":10,\"y\":10,\"width\":50,\"height\":50}"); Choose(L"Auto",1); Icon(L"Align Left Edges"); Check(Bounds()[0].Int() == 10,"Move alignment must support the selection target");
    Command("select.deselect"); auto a = Rectangle("Left"); auto b = Rectangle("Middle"); Command("layer.translate",R"({"dx":20,"dy":0})"); auto c = Rectangle("Right"); Command("layer.translate",R"({"dx":120,"dy":0})");
    Command("layer.select",JsonObject().Num("layer",double(a)).Str("mode","add").Text()); Command("layer.select",JsonObject().Num("layer",double(b)).Str("mode","add").Text());
    Icon(L"Distribute Horizontally");
    double left = -1,middle = -1,right = -1; for (size_t i = 0; i < docInfo["layers"].size(); i++) { const auto& l = docInfo["layers"][i]; if (l["id"].Int() == a) left = l["bounds"][0].Num(); if (l["id"].Int() == b) middle = l["bounds"][0].Num(); if (l["id"].Int() == c) right = l["bounds"][0].Num(); }
    Check(std::abs((middle-left)-(right-middle)) < .1 && right > left,"distribution buttons must space three selected layers evenly");
  }
}
void SelectionMenus() {
  for (int depth : {8,16,32}) {
    NewDocument(depth); Patch(); Command("layer.rasterize.layer"); Tool("magicWand"); Choose(L"Point Sample",1); ToggleOption(L"Anti-alias",false); ToggleOption(L"Sample All Layers",true); Stroke(40,40,40,40);
    Check(docInfo["hasSelection"].Truthy() && sel.sampleSize == 3,"Wand must use averaged samples and selection options");
    auto original = docInfo["selectionBounds"];
    RunCommand("select.saveSelection",L""); Check(tabMenu && tabMenu->open && !tabMenu->items.empty(),"Save Selection must open a channel menu");
    tabMenu->items[0].action(); Check(typePopover && typePopover->open,"new selection channel must ask for a name"); typePopover->text = L"Saved test"; Key(VK_RETURN);
    Check(docInfo["channels"]["alpha"].size() == 1,"Save Selection must create a named alpha channel");
    Command("select.deselect"); RunCommand("select.loadSelection",L"");
    Check(tabMenu->items[0].label == L"Saved test","Load Selection must list saved channels"); auto load = tabMenu->items[0].submenu[0].action; tabMenu->Close(); load(); Drain();
    Check(JsonText(docInfo["selectionBounds"]) == JsonText(original),"saved channel must restore the original selection");
    std::vector<MenuItem> rows; canvas->tool->Context(40,40,Pointer(40,40),rows); // selection tools use Canvas context below
    CanvasContext(40,40,Pointer(40,40));
    for (auto label : {L"Save Selection…",L"Make Work Path…",L"Transform Selection",L"Content-Aware Fill…",L"Last Filter",L"Fade…"}) {
      bool found = false; for (auto& row : contextMenu->items) found |= row.label == label; Check(found,"selection context menu must expose milestone actions");
    }
    for (auto& row : contextMenu->items) if (row.label == L"Transform Selection") {
      auto act = row.action; contextMenu->Close(); act(); Preview();
      Check(Mode() && TransformState()["selection"].Truthy(),"the context menu's Transform Selection must start the outline frame"); Key(VK_ESCAPE); break;
    }
    if (contextMenu->open) contextMenu->Close();
    Tool("move"); Tool("magicWand"); Button(L"Select Subject"); Button(L"Select and Mask…"); if (ui::ActivePopover()) ui::ClosePopovers(); if (Mode()) Mode()->Cancel(); Drain();
    sel.sampleAllLayers = false; sel.sampleSize = 1; sel.antiAlias = true;
  }
}
void SamplingAndGradients() {
  for (int depth : {8,16,32}) {
    NewDocument(depth); Patch(); Tool("eyedropper"); Choose(L"Point Sample",0); Choose(L"Current Layer",0); Stroke(40,40,40,40);
    Check(sessionInfo["foreground"][0].Num() > .99 && sessionInfo["foreground"][1].Num() < .01,"eyedropper must sample the current layer");
    Stroke(40,40,40,40,{false,false,true}); Check(sessionInfo["background"][0].Num() > .99,"Alt eyedropper must set background");
    NewDocument(depth); Command("tools.setColors","{\"foreground\":\"#ff0000\",\"background\":\"#0000ff\"}"); Tool("gradient"); ToggleOption(L"Classic Gradient",false); Choose(L"Linear",0); ToggleOption(L"Reverse",false);
    Stroke(20,40,140,40); Preview(); auto g = Command("gradient.fill.get");
    Check(g["from"][0].Int() == 20 && g["to"][0].Int() == 140,"Gradient must create a fill with editable endpoints");
    auto history = docInfo["history"].size(); Stroke(140,40,120,80); Preview(); g = Command("gradient.fill.get");
    Check(g["to"][0].Int() == 120 && g["to"][1].Int() == 80 && docInfo["history"].size() == history+1,"gradient endpoint drag must make one undo step");
    Undo(); Drain(); Preview(); ToggleOption(L"Reverse",true); ToggleOption(L"Dither",false); Choose(L"Linear",1); Value(L"Opacity",50); FinishGradientPreview(); Drain(); g = Command("gradient.fill.get");
    Check(g["reverse"].Truthy() && !g["dither"].Truthy(true) && g["style"].Str() == "radial" && std::abs((*ActiveLayer())["opacity"].Num()-.5)<.001,"gradient options must apply reverse, dither, style and opacity");
    if (std::abs(g["to"][0].Num()-140) > .01 || std::abs(g["to"][1].Num()-40) > .01) throw std::runtime_error("Undo gradient endpoints: " + JsonText(g) + "; UI: " + JsonText(GradientState()));    history = docInfo["history"].size(); Button(L"Edit Gradient…"); auto* editor = ui::ActivePopover(); Check(editor && editor->modal,"Gradient Editor must be a modal with stops");
    auto* ramp = editor->children[0].get(); auto r = ramp->rect; PointerEvent e; e.x = MidX(r); e.y = r.bottom-4; ramp->OnDown(e); ramp->OnUp(e,true); Preview();
    Check(GradientState()["stops"].size() == 3,"clicking the gradient ramp must add a color stop");
    auto* color = dynamic_cast<ColorField*>(editor->children[3].get()); color->onChange(Rgba(0,1,0,1)); Preview();
    e.y = r.top+4; ramp->OnDown(e); ramp->OnUp(e,true); Preview(); auto* alpha = dynamic_cast<NumberField*>(editor->children[2].get()); alpha->onChange(25); Preview();
    Check(GradientState()["transparency"].size() == 3,"editor must separately add and edit opacity stops");
    Key(VK_DELETE); Preview(); Check(GradientState()["transparency"].size() == 2,"Delete must remove the selected opacity stop");
    Key(VK_ESCAPE); Check(Command("gradient.fill.get")["stops"].size() == 2 && docInfo["history"].size() == history,"Cancel must restore ramp and undo history");
    Button(L"Edit Gradient…"); editor = ui::ActivePopover(); ramp = editor->children[0].get(); r = ramp->rect; e.x = MidX(r); e.y = r.bottom-4; ramp->OnDown(e); ramp->OnUp(e,true); Preview(); Key(VK_RETURN);
    Check(Command("gradient.fill.get")["stops"].size() == 3 && docInfo["history"].size() == history+1,"Apply must keep all stop edits in one history step");
    // #5520: smoothness and the interpolation method belong to the gradient, next to its few stops.
    history = docInfo["history"].size(); Button(L"Edit Gradient…"); editor = ui::ActivePopover(); Check(editor && editor->children.size() > 8,"Gradient Editor must have Smoothness and Method");
    auto* smooth = dynamic_cast<NumberField*>(editor->children[7].get()); auto* method = dynamic_cast<SelectList*>(editor->children[8].get()); Check(smooth && method,"Gradient Editor: a Smoothness field and a Method list");
    smooth->onChange(100); method->onChange(1); Preview(); Key(VK_RETURN); Drain(); g = Command("gradient.fill.get");
    Check(std::abs(g["smoothness"].Num()-100) < .01 && g["method"].Str() == "perceptual" && g["stops"].size() == 3 && docInfo["history"].size() == history+1,"smoothness and method must be one history step and keep the stops");
    Button(L"Edit Gradient…"); editor = ui::ActivePopover(); smooth = dynamic_cast<NumberField*>(editor->children[7].get()); Check(smooth && std::abs(smooth->value-100) < .01,"the editor must show the gradient's smoothness"); Key(VK_ESCAPE);
    Check(std::abs(Command("gradient.fill.get")["smoothness"].Num()-100) < .01 && docInfo["history"].size() == history+1,"Cancel must keep the applied smoothness and add no step");
    Button(L"Presets…"); auto* menu = dynamic_cast<Menu*>(ui::ActivePopover()); Check(menu && !menu->items.empty() && !menu->items[0].submenu.empty(),"gradient presets must be grouped in a usable menu"); auto preset = menu->items[0].submenu[0].action; menu->Close(); preset(); Preview(); Check(!Command("gradient.fill.get")["stops"].IsNull(),"choosing a preset must update the active gradient");
    Button(L"Edit Gradient…"); PrepareDocumentChange(); Check(!ui::ActivePopover() || !ui::ActivePopover()->modal,"document change must close the gradient editor"); Drain();
    NewDocument(depth); Tool("gradient"); ToggleOption(L"Classic Gradient",true); Choose(L"Linear",0); Value(L"Opacity",100); ToggleOption(L"Reverse",false); history = docInfo["history"].size(); Stroke(20,40,140,40);
    Check((*ActiveLayer())["kind"].Str() == "Pixel" && Sample(60,40)[3] > .9 && docInfo["history"].size() == history+1,"Classic Gradient must paint pixels in one step"); ToggleOption(L"Classic Gradient",false);
  }
}
void CroppingAndPaths() {
  for (int depth : {8,16,32}) {
    NewDocument(depth); Patch(); Tool("crop"); Choose(L"Unconstrained",0); Value(L"Output W",0); Value(L"Output H",0); Stroke(20,20,120,100);
    Check(CropState()["width"].Int() == 100 && CropState()["height"].Int() == 80,"crop drag must draw a frame");
    for (int i = 0; i < 6; i++) { Key('O'); Check(CropState()["overlay"].Int() == (i+1)%6,"O must cycle all crop overlays"); }
    Stroke(150,60,140,100,{false,true}); Check(std::abs(std::remainder(CropState()["angle"].Num(),15)) < .01,"outside crop rotation must snap to 15 degrees");
    Button(L"Straighten"); Stroke(30,30,90,45); Check(std::abs(CropState()["angle"].Num() - std::atan2(15.,60.)*180/3.141592653589793) < .01,"straighten must align to the drawn line");
    Key(VK_ESCAPE); Check(docInfo["width"].Int() == 200,"cancel crop must preserve dimensions"); Tool("move"); Tool("crop"); Choose(L"Unconstrained",2); Stroke(20,20,120,100); Check(CropState()["width"].Int() == CropState()["height"].Int(),"ratio preset must constrain new crop");
    Value(L"Angle",0); Value(L"Output W",50); Value(L"Output H",50); Value(L"Resolution",144); auto history = docInfo["history"].size(); Key(VK_RETURN);
    Check(docInfo["width"].Int() == 50 && docInfo["height"].Int() == 50 && docInfo["resolution"].Int() == 144 && docInfo["history"].size() == history+1,"crop output dimensions and resolution must commit one step"); Undo(); Drain();
    NewDocument(depth); Tool("pen"); Stroke(20,30,20,30); Stroke(80,30,80,30,{false,true}); Stroke(80,90,80,90); Key(VK_RETURN);
    auto path = Command("path.info","{\"name\":\"work\"}")["path"]; Check(path["subpaths"][0]["knots"].size() == 3,"Pen must commit an open work path");
    Stroke(50,30,50,30); path = Command("path.info","{\"name\":\"work\"}")["path"]; Check(path["subpaths"][0]["knots"].size() == 4,"Pen hover on segment must insert an anchor");
    Stroke(50,30,50,30); Check(Command("path.info","{\"name\":\"work\"}")["path"]["subpaths"][0]["knots"].size() == 3,"Pen hover on anchor must remove it");
    Stroke(80,30,80,30,{false,false,true}); Check(!Command("path.info","{\"name\":\"work\"}")["path"]["subpaths"][0]["knots"][1]["smooth"].Truthy(),"Alt Pen must convert a node to a corner");
    Stroke(80,30,85,35,{true,true}); path = Command("path.info","{\"name\":\"work\"}")["path"]; Check(path["subpaths"][0]["knots"][1]["anchor"][0].Int() == 85,"Ctrl Pen must temporarily drag with Direct Selection");
    Tool("directSelection"); Stroke(85,35,85,35); Key(VK_RIGHT); Key(VK_DOWN,{false,true}); path = Command("path.info","{\"name\":\"work\"}")["path"]; Check(path["subpaths"][0]["knots"][1]["anchor"][0].Int() == 86 && path["subpaths"][0]["knots"][1]["anchor"][1].Int() == 45,"arrow keys must move selected anchors by one / ten pixels");
    history = docInfo["history"].size(); Stroke(86,45,96,55,{false,false,true}); path = Command("path.info","{\"name\":\"work\"}")["path"]; Check(path["subpaths"][0]["knots"][1]["smooth"].Truthy() && docInfo["history"].size() == history+1,"Alt Direct Selection drag must create symmetric handles in one step");
    auto handle = path["subpaths"][0]["knots"][1]["out"]; auto in = path["subpaths"][0]["knots"][1]["in"]; Stroke(handle[0].Num(),handle[1].Num(),handle[0].Num()+4,handle[1].Num(),{false,false,true}); path = Command("path.info","{\"name\":\"work\"}")["path"]; Check(JsonText(path["subpaths"][0]["knots"][1]["in"]) == JsonText(in),"Alt handle drag must leave the opposite handle independent");
    const auto& knots = path["subpaths"][0]["knots"]; double sx = (knots[0]["anchor"][0].Num() + 3*knots[0]["out"][0].Num() + 3*knots[1]["in"][0].Num() + knots[1]["anchor"][0].Num())/8,sy = (knots[0]["anchor"][1].Num() + 3*knots[0]["out"][1].Num() + 3*knots[1]["in"][1].Num() + knots[1]["anchor"][1].Num())/8;
    history = docInfo["history"].size(); Stroke(sx,sy,sx,sy+6); auto bent = Command("path.info",R"({"name":"work"})")["path"]; Check(JsonText(bent) != JsonText(path) && docInfo["history"].size() == history+1,"dragging a segment must bend its curve in one step"); Undo(); Drain(); Preview(); Check(JsonText(Command("path.info",R"({"name":"work"})")["path"]) == JsonText(path),"Undo must restore a bent segment and tool cache");
  }
}
void Transformations() {
  for (int depth : {8,16,32}) {
    NewDocument(depth); Patch(); size_t history = docInfo["history"].size(); StartTransform();
    Check(Mode() && TransformState()["active"].Truthy(),"Free Transform must start");
    TypeValue(L"W",L"200"); Check(std::abs(Number(L"H")->value - 200) < .01,"linked dimensions must scale together");
    Value(L"X",70); Value(L"Y",60); Check(Quad()[0][0].Int() == 10 && Quad()[0][1].Int() == 20,"numeric coordinates must use the reference point");
    Value(L"Angle",90); Check(std::abs(Number(L"Angle")->value - 90) < .01,"rotation must update its field");
    Key(VK_RETURN); Check(!Mode(),"Enter must leave Free Transform");
    Check(docInfo["history"].size() == history + 1,"numeric transformation must commit one history step");
    auto b = Bounds(); Check(std::abs(b[2].Num() - 80) <= 1 && std::abs(b[3].Num() - 120) <= 1,"numeric transform must affect layer geometry");
    Undo(); Drain(); Check(Bounds()[2].Int() == 60 && Bounds()[3].Int() == 40,"Undo must restore original dimensions");
    StartTransform(); Key(VK_RIGHT); Key(VK_DOWN,{false,true}); Preview();
    Check(Quad()[0][0].Int() == 21 && Quad()[0][1].Int() == 40,"arrows must move transform frame by one / ten pixels");
    Key(VK_ESCAPE); Check(Bounds()[0].Int() == 20 && docInfo["history"].size() == history,"Esc must restore pixels and history");
    StartTransform(); Action(L"Flip Horizontal"); Check(Number(L"W")->value < 0,"flip must show signed width");
    Value(L"W",-100); Check(Number(L"W")->value == -100,"re-entering signed width must not flip it back");
    Action(L"Flip Horizontal"); Key(VK_RETURN); Check(docInfo["history"].size() == history,"identity transformation must not add history");
    StartTransform(); ToggleOption(L"Link proportions",false); Value(L"W",200); Check(Number(L"H")->value == 100,"unlinking dimensions must preserve the other scale");
    auto pivotDown = Pointer(100,100,{false,false,true}); canvas->OnDown(pivotDown); canvas->OnUp(pivotDown,true); Preview();
    Check(TransformState()["pivot"][0].Int() == 100 && TransformState()["pivot"][1].Int() == 100,"Alt click must relocate the reference point"); Key(VK_ESCAPE);
    StartTransform(); auto down = Pointer(20,30,{true,true,true}); canvas->OnDown(down);
    auto up = Pointer(30,40,{true,true,true}); canvas->OnDrag(up); canvas->OnUp(up,true); Preview();
    auto q = Quad(); if (!(q[0][0].Int() == 30 && q[1][0].Int() == 70 && q[0][1].Int() == 40 && q[1][1].Int() == 40)) throw std::runtime_error("Ctrl+Alt+Shift corner must make symmetric perspective: " + JsonText(q));
    Key(VK_ESCAPE);
    StartTransform(); down = Pointer(50,30,{true}); canvas->OnDown(down); up = Pointer(60,30,{true}); canvas->OnDrag(up); canvas->OnUp(up,true); Preview();
    q = Quad(); Check(q[0][0].Int() == 30 && q[1][0].Int() == 90 && q[3][0].Int() == 20,"Ctrl edge must skew only that edge"); Key(VK_ESCAPE);
    Command("select.rect","{\"x\":40,\"y\":40,\"width\":20,\"height\":10}"); history = docInfo["history"].size(); auto original = Sample(45,45);
    RunCommand("select.transformSelection",L""); Preview(); Check(TransformState()["selection"].Truthy(),"Transform Selection must start outline mode");
    Value(L"W",200); Key(VK_RIGHT,{false,true}); Preview(); Key(VK_RETURN);
    auto selectionBounds = docInfo["selectionBounds"];
    if (selectionBounds[2].Int() < 40 || selectionBounds[2].Int() > 42 || selectionBounds[3].Int() < 20 || selectionBounds[3].Int() > 22)
      throw std::runtime_error("wrong transformed selection bounds: " + JsonText(selectionBounds));    Check(Sample(45,45) == original && docInfo["history"].size() == history + 1,"selection transform must leave pixels unchanged in one step"); Undo(); Drain();
    Command("select.deselect"); StartTransform(); Key(VK_RIGHT,{false,true}); Preview(); Key(VK_RETURN);
    auto moved = Bounds()[0].Int(); Key('T',{true,true}); Check(Bounds()[0].Int() == moved + 10,"Ctrl+Shift+T must repeat the last transformation");
    auto count = docInfo["layers"].size(); Key('T',{true,true,true}); Check(docInfo["layers"].size() == count + 1,"Ctrl+Alt+Shift+T must repeat onto a copy");
    Key('T',{true,false,true}); Check(Mode() && docInfo["layers"].size() == count + 2,"Ctrl+Alt+T must start transformation on a duplicate"); Key(VK_ESCAPE);
  }
}
void Warps() {
  for (int depth : {8,16,32}) {
    NewDocument(depth); Patch(); auto history = docInfo["history"].size(); RunCommand("edit.transform.warp",L""); Preview();
    Check(Mode() && TransformState()["warp"].Truthy(),"Warp menu must start interactive warp");
    SelectList* grid = nullptr; SelectList* style = nullptr;
    for (auto* w : optionWidgets) if (auto* s = dynamic_cast<SelectList*>(w)) { if (s->options.size() == 4) grid = s; if (s->options.size() == 16) style = s; }
    Check(grid && style,"Warp must expose grid sizes and styles");
    style->onChange(1); Drain(); Preview(); Check(TransformState()["params"]["mesh"]["points"].size() == 16,"Arc preset must provide an editable mesh");
    grid->onChange(3); Drain(); Preview(); auto mesh = TransformState()["params"]["mesh"];
    Check(mesh["us"].size() == 6 && mesh["points"].size() == 256,"5x5 Warp must have a real 16x16 control mesh");
    for (auto* w : optionWidgets) if (auto* b = dynamic_cast<TextButton*>(w); b && b->label == L"Split horizontally") b->onClick();
    Drain(); Preview(); mesh = TransformState()["params"]["mesh"];
    if (mesh["vs"].size() != 7) throw std::runtime_error("horizontal split knots: " + JsonText(mesh["vs"]));    for (auto* w : optionWidgets) if (auto* b = dynamic_cast<TextButton*>(w); b && b->label == L"Split vertically") b->onClick();
    Drain(); Preview(); mesh = TransformState()["params"]["mesh"]; Check(mesh["us"].size() == 7,"vertical split must add a column");
    Key('Z',{true}); Preview(); Check(TransformState()["params"]["mesh"]["us"].size() == 6,"local Warp undo must restore the previous grid");
    Key(VK_ESCAPE); Check(Bounds()[0].Int() == 20 && docInfo["history"].size() == history,"cancelled warp model editing must leave no history");
    RunCommand("edit.transform.warp",L""); Preview(); mesh = TransformState()["params"]["mesh"];
    auto p = mesh["points"][5]; auto down = Pointer(p[0].Num(),p[1].Num()); canvas->OnDown(down); auto up = Pointer(p[0].Num() + 8,p[1].Num() + 5); canvas->OnDrag(up); canvas->OnUp(up,true); Preview();
    Key(VK_RETURN); Check(docInfo["history"].size() == history + 1,"editing a Warp control point must be one undo step"); Undo(); Drain();
  }
}
double Skewed() { return Number(L"Skew")->value; }
void DistributeAll() {
  for (int depth : {8,16,32}) {
    NewDocument(depth); auto a = Rectangle("A"); auto b = Rectangle("B"); Command("layer.translate",R"({"dx":20,"dy":40})"); auto c = Rectangle("C"); Command("layer.translate",R"({"dx":120,"dy":70})");
    for (auto id : {a,b}) Command("layer.select",JsonObject().Num("layer",double(id)).Str("mode","add").Text());
    Tool("move");
    struct Kind { const wchar_t* label; int coordinate; };  // 0 top, 1 vertical centre, 2 bottom, 3 left, 4 horizontal centre, 5 right
    for (auto kind : {Kind{L"Distribute Top Edges",0},Kind{L"Distribute Vertical Centers",1},Kind{L"Distribute Bottom Edges",2},Kind{L"Distribute Left Edges",3},Kind{L"Distribute Horizontal Centers",4},Kind{L"Distribute Right Edges",5}}) {
      Icon(kind.label);
      std::vector<double> v;
      for (size_t i = 0; i < docInfo["layers"].size(); i++) {
        const auto& l = docInfo["layers"][i]; int64_t id = l["id"].Int(); if (id != a && id != b && id != c) continue;
        const auto& r = l["bounds"]; double x = r[0].Num(),y = r[1].Num(),w = r[2].Num(),h = r[3].Num();
        v.push_back(kind.coordinate == 0 ? y : kind.coordinate == 1 ? y + h / 2 : kind.coordinate == 2 ? y + h : kind.coordinate == 3 ? x : kind.coordinate == 4 ? x + w / 2 : x + w);
      }
      std::sort(v.begin(),v.end()); Check(v.size() == 3 && std::abs((v[1] - v[0]) - (v[2] - v[1])) < 1.5,"every distribute button must space three layers evenly");
      Undo(); Drain();
    }
  }
}
void ReviewFixes() {
  for (int depth : {8,16,32}) {
    // Free Transform's menu modes each take over the drag, and perspective works along either axis.
    NewDocument(depth); Patch(); StartTransform();
    for (auto label : {L"Scale",L"Rotate",L"Skew",L"Distort",L"Perspective",L"Free Transform"}) {
      Action(label); std::vector<MenuItem> rows; Mode()->Context(50,50,{},rows); bool on = false; for (auto& r : rows) if (r.label == label) on = r.checked;
      Check(on,"the chosen transform mode must be checked in the menu");
    }
    Action(L"Rotate"); Stroke(80,50,50,80); Preview(); Check(std::abs(Number(L"Angle")->value - 90) < 1,"Rotate mode must rotate by dragging anywhere"); Key(VK_ESCAPE);
    StartTransform(); Action(L"Skew"); Stroke(80,50,80,60); Preview(); auto q = Quad();
    Check(std::abs(q[1][1].Num() - 40) < .01 && std::abs(q[2][1].Num() - 80) < .01 && std::abs(q[0][1].Num() - 30) < .01,"Skew mode must slide an edge along itself"); Key(VK_ESCAPE);
    StartTransform(); Action(L"Distort"); Stroke(80,70,90,80); Preview(); q = Quad();
    Check(std::abs(q[2][0].Num() - 90) < .01 && std::abs(q[2][1].Num() - 80) < .01 && std::abs(q[0][0].Num() - 20) < .01,"Distort mode must move one corner"); Key(VK_ESCAPE);
    StartTransform(); Action(L"Perspective"); Stroke(20,30,30,30); Preview(); q = Quad();
    Check(std::abs(q[0][0].Num() - 30) < .01 && std::abs(q[1][0].Num() - 70) < .01,"Perspective mode must mirror the neighbour along the top edge"); Key(VK_ESCAPE);
    StartTransform(); Action(L"Perspective"); Stroke(20,30,25,40); Preview(); q = Quad();
    Check(std::abs(q[0][0].Num() - 25) < .01 && std::abs(q[0][1].Num() - 40) < .01 && std::abs(q[3][0].Num() - 25) < .01 && std::abs(q[3][1].Num() - 60) < .01 && std::abs(q[1][1].Num() - 30) < .01,"a vertical perspective drag must mirror the neighbour along the left edge"); Key(VK_ESCAPE);
    StartTransform(); Action(L"Rotate 180°"); Check(std::abs(std::abs(Number(L"Angle")->value) - 180) < .01,"Rotate 180° must turn the frame"); Key(VK_ESCAPE);
    // The Skew and Height fields read the frame itself, so they agree with a skew dragged by the edge.
    StartTransform(); auto down = Pointer(50,30,{true}); canvas->OnDown(down); auto up = Pointer(60,30,{true}); canvas->OnDrag(up); canvas->OnUp(up,true); Preview();
    Check(std::abs(Skewed() + 14.04) < .1 && std::abs(Number(L"H")->value - 100) < .01,"the Skew field must show a skew dragged by the edge without inflating the height");
    Value(L"Skew",0); q = Quad(); Check(std::abs(q[0][0].Num() - q[3][0].Num()) < .01,"setting the skew to 0 must make the sides upright again"); Key(VK_ESCAPE);
    // A click that changes nothing is not an undo step: one Ctrl+Z takes back the one real change.
    StartTransform(); double x0 = Quad()[0][0].Num(); Value(L"X",60); Check(std::abs(Quad()[0][0].Num() - x0 - 10) < .01,"X must move the frame");
    down = Pointer(60,50); canvas->OnDown(down); canvas->OnUp(down,true); Preview();
    Key('Z',{true}); Preview(); Check(std::abs(Quad()[0][0].Num() - x0) < .01,"an empty click must not add an undo step"); Key(VK_ESCAPE);

    // Warp: all sixteen styles produce a mesh of their own; 3x3 and 4x4 grids; the cross split.
    RunCommand("edit.transform.warp",L""); Preview();
    SelectList* grid = nullptr; SelectList* style = nullptr;
    for (auto* w : optionWidgets) if (auto* s = dynamic_cast<SelectList*>(w)) { if (s->options.size() == 4) grid = s; if (s->options.size() == 16) style = s; }
    Check(grid && style,"Warp options are missing");
    std::string identity = JsonText(TransformState()["params"]["mesh"]);
    for (int i = 1; i < 16; i++) {
      style->onChange(i); Drain(); Preview(); auto mesh = TransformState()["params"]["mesh"];
      Check(mesh["points"].size() == 16 && JsonText(mesh) != identity,"every Warp style must reshape the mesh");
    }
    grid->onChange(1); Drain(); Preview(); Check(TransformState()["params"]["mesh"]["us"].size() == 4 && TransformState()["params"]["mesh"]["points"].size() == 100,"3x3 Warp must have a 10x10 control mesh");
    grid->onChange(2); Drain(); Preview(); Check(TransformState()["params"]["mesh"]["us"].size() == 5 && TransformState()["params"]["mesh"]["points"].size() == 169,"4x4 Warp must have a 13x13 control mesh");
    Key(VK_ESCAPE);
    RunCommand("edit.transform.warp",L""); Preview(); Button(L"Split crosswise"); Drain(); Preview();
    { auto mesh = TransformState()["params"]["mesh"]; if (mesh["us"].size() != 5 || mesh["vs"].size() != 5) throw std::runtime_error("the cross split must add a column and a row to the default 3x3 grid: " + JsonText(mesh["us"]) + " " + JsonText(mesh["vs"])); }
    Key(VK_ESCAPE);

    NewDocument(depth); Command("edit.fill",R"({"color":"#ff0000"})"); Tool("crop"); Button(L"Clear");
    Stroke(0,0,-10,-10); ToggleOption(L"Content-Aware",true); auto cropHistory = docInfo["history"].size(); Key(VK_RETURN);
    Check(docInfo["width"].Int() == 210 && docInfo["height"].Int() == 170,"content-aware crop must extend the document");
    Check(Sample(1,1)[0] > .99 && Sample(1,1)[3] > .99,"content-aware crop must fill the newly exposed corner");
    Check(docInfo["history"].size() == cropHistory+1 && docInfo["layers"].size() == 2,"crop and completion must preserve layers in one history step");
    Undo(); Drain(); Check(docInfo["width"].Int() == 200 && docInfo["layers"].size() == 1,"Undo must restore the original crop and remove completion"); ToggleOption(L"Content-Aware",false);
    // Output size fixes the frame ratio; Content-Aware Crop dispatches its own engine command.
    NewDocument(depth); Patch(); Tool("crop"); Choose(L"Unconstrained",0); Button(L"Clear"); Stroke(20,20,120,100);
    Value(L"Output W",50); Value(L"Output H",25);
    Check(std::abs(CropState()["width"].Num() / CropState()["height"].Num() - 2) < .02 && Number(L"Ratio W")->value == 50 && Number(L"Ratio H")->value == 25,"an output size must fix the frame's ratio");
    Choose(L"Unconstrained",2); Check(Number(L"Output W")->value == 0 && Number(L"Output H")->value == 0,"choosing a ratio must clear the output size");
    Value(L"Output W",50); Value(L"Output H",25); Button(L"Clear"); Check(Number(L"Output W")->value == 0 && Number(L"Output H")->value == 0 && Choice(L"Unconstrained")->index == 0,"Clear must forget the output size and the ratio");
    Key(VK_ESCAPE); Stroke(20,20,120,100); Value(L"Output W",40); Value(L"Output H",20); Key(VK_RETURN);
    Check(docInfo["width"].Int() == 40 && docInfo["height"].Int() == 20,"a crop with an output size must resample the frame, never stretch it");
    Undo(); Drain(); Tool("crop"); Button(L"Clear"); Value(L"Output W",0); Value(L"Output H",0);

    // Delete Cropped Pixels: off keeps the pixels outside the new canvas.
    NewDocument(depth); Command("edit.fill",R"({"color":"#00ff00"})"); Tool("crop"); ToggleOption(L"Delete Cropped Pixels",false); Stroke(20,20,120,100); Key(VK_RETURN);
    Check(docInfo["width"].Int() == 100 && Bounds()[2].Int() > 100,"with Delete Cropped Pixels off the layer must keep what is outside the canvas");
    Undo(); Drain(); Tool("crop"); ToggleOption(L"Delete Cropped Pixels",true); Stroke(20,20,120,100); Key(VK_RETURN);
    Check(docInfo["width"].Int() == 100 && Bounds()[2].Int() == 100,"with Delete Cropped Pixels on the layer must be trimmed to the canvas"); Undo(); Drain();

    // A gradient click draws nothing and moves nothing; a new gradient layer is named in the interface language, in one step.
    NewDocument(depth); Command("tools.setColors","{\"foreground\":\"#ff0000\",\"background\":\"#0000ff\"}"); Tool("gradient"); ToggleOption(L"Classic Gradient",false);
    i18n::Load("ru"); auto history = docInfo["history"].size(); Stroke(20,40,140,40); Preview();
    Check((*ActiveLayer())["name"].Str() == "Градиент 1" && docInfo["history"].size() == history + 1,"a new gradient layer must be named in Russian in one history step");
    i18n::Load("en"); auto before = GradientState(); Stroke(80,100,80,100); Preview(); auto after = GradientState();
    Check(JsonText(before["from"]) == JsonText(after["from"]) && JsonText(before["to"]) == JsonText(after["to"]) && JsonText(Command("gradient.fill.get")["to"]) == JsonText(after["to"]),"a plain click on the canvas must not move the gradient handles");

    // The pen continues an open path from either end instead of deleting the end anchor; Esc takes it back.
    NewDocument(depth); Tool("pen"); Stroke(20,30,20,30); Stroke(80,30,80,30); Stroke(80,90,80,90); Key(VK_RETURN);
    auto work = [] { return Command("path.info",R"({"name":"work"})")["path"]; };
    auto path = work(); Check(path["subpaths"].size() == 1 && path["subpaths"][0]["knots"].size() == 3,"the pen must commit an open path");
    Stroke(80,90,80,90); Stroke(130,90,130,90); Key(VK_RETURN); path = work();
    Check(path["subpaths"].size() == 1 && path["subpaths"][0]["knots"].size() == 4 && path["subpaths"][0]["knots"][3]["anchor"][0].Int() == 130,"clicking the last anchor must continue the path from it");
    Stroke(20,30,20,30); Stroke(20,5,20,5); Key(VK_RETURN); path = work(); const auto& knots = path["subpaths"][0]["knots"];
    Check(path["subpaths"].size() == 1 && knots.size() == 5 && knots[0]["anchor"][0].Int() == 130 && knots[4]["anchor"][1].Int() == 5,"clicking the first anchor must continue the path from its start");
    auto kept = JsonText(path); Stroke(20,5,20,5); Key(VK_ESCAPE); Drain(); Check(JsonText(work()) == kept,"Esc must give the continued path back unchanged");
  }
}
void WandBenchmark(const std::filesystem::path& root) {
  Tool("move"); Command("file.new",R"({"width":6000,"height":4000,"background":"transparent","depth":8})"); Command("edit.fill",R"({"color":"#ffffff"})");
  Layout(true); canvas->Fit(false);
  double deadline = ui::NowMs()+20000; while ((jobs::Busy() || canvas->Busy()) && ui::NowMs() < deadline) { Frame(); Sleep(1); }
  Check(!jobs::Busy() && !canvas->Busy(),"large Wand fixture must finish rendering");
  std::ofstream report(root/L"wand-perf.txt");
  report << "6000x4000 RGB8, uniform white, one synthetic run; command time includes completion/refresh.\n";
  for (int size : {1,3}) {
    bool done = false,ok = false; std::string error; double begin = ui::NowMs(),elapsed = 0,longest = 0; unsigned frames = 0;
    Exec("select.magicWand",JsonObject().Num("x",100).Num("y",100).Num("sampleSize",size).Num("tolerance",0).Bool("contiguous",true).Bool("antiAlias",false).Text(),[&](bool success,const std::string& result) { ok = success; error = result; elapsed = ui::NowMs()-begin; done = true; },false);
    while (!done && ui::NowMs()-begin < 30000) { double t = ui::NowMs(); jobs::Pump(); Frame(); longest = std::max(longest,ui::NowMs()-t); frames++; Sleep(1); }
    if (!done || !ok) { Drain(); throw std::runtime_error("24 MP Wand: " + error); } Drain();
    Check(docInfo["selectionBounds"][2].Int() == 6000 && docInfo["selectionBounds"][3].Int() == 4000,"large averaged Wand must select the entire uniform fixture");
    report << "sample " << size << ": " << elapsed << " ms, UI frames " << frames << ", longest frame " << longest << " ms\n";
    Undo(); Drain();
  }
  report.close(); Check(std::filesystem::exists(root/L"wand-perf.txt"),"Wand benchmark report must be saved");
}
void CropBenchmark(const std::filesystem::path& root) {
  Tool("move"); Command("file.new",R"({"width":5500,"height":4000,"background":"transparent","depth":8})"); Command("edit.fill",R"({"color":"#447799"})");
  Layout(true); canvas->Fit(false);
  double deadline = ui::NowMs()+20000; while ((jobs::Busy() || canvas->Busy()) && ui::NowMs() < deadline) { jobs::Pump(); Frame(); Sleep(1); }
  std::ofstream report(root/L"crop-perf.txt"); report << "24 MP crop output, RGB8 uniform colour, one run; command and completion/refresh time.\n";
  for (auto id : {"image.crop","image.cropContentAware"}) {
    bool done = false,ok = false; std::string error; double begin = ui::NowMs(),elapsed = 0,longest = 0; unsigned frames = 0;
    Exec(id,R"({"x":-250,"y":0,"width":6000,"height":4000,"deleteCroppedPixels":false})",[&](bool success,const std::string& result) { ok = success; error = result; elapsed = ui::NowMs()-begin; done = true; },false);
    while (!done && ui::NowMs()-begin < 60000) { double t = ui::NowMs(); jobs::Pump(); Frame(); longest = std::max(longest,ui::NowMs()-t); frames++; Sleep(1); }
    if (!done || !ok) { Drain(); throw std::runtime_error("24 MP crop: " + error); } Drain();
    Check(docInfo["width"].Int() == 6000 && docInfo["height"].Int() == 4000,"benchmark crop must produce 24 MP");
    if (std::string(id) == "image.cropContentAware") Check(Sample(0,0)[3] > .99,"large crop must fill the extension");
    report << id << ": " << elapsed << " ms, UI frames " << frames << ", longest frame " << longest << " ms\n"; Undo(); Drain();
  }
}
}
int main(int argc, char** argv) {
  int status = 0; SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2); CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED); SetConsoleOutputCP(CP_UTF8);
  WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = GetModuleHandle(nullptr); wc.lpszClassName = L"VetroParameterTests"; RegisterClassW(&wc);
  hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP,wc.lpszClassName,L"Vetro parameter tests",WS_OVERLAPPEDWINDOW,0,0,1600,1000,nullptr,nullptr,wc.hInstance,nullptr);
  ui::Init(hwnd); ui::SetBounds(R(0,0,1584,961)); if (!GfxCreate(hwnd,1)) return 1;
  engine = pc_new(); jobs::Init(hwnd,WM_JOBS); LoadCatalog(); canvas = new Canvas(engine); ui::AddLayer(canvas); ui::AddLayer(new SelectionOverlay());
  if (GetEnvironmentVariableW(L"VETRO_TEST_GPU",nullptr,0)) { uint32_t low = 0; int32_t high = 0; if (GfxAdapterLuid(&low,&high)) canvas->UseGpu(low,high); }
  BuildTools(); BuildDocks(); BuildShortcuts(); ui::SetGate(InputAllowed);
  try {
    Transformations(); Warps(); ShapesAndAlignment(); DistributeAll(); SelectionMenus(); SamplingAndGradients(); CroppingAndPaths(); ReviewFixes();
    if (argc > 1) {
      auto root = std::filesystem::path(Widen(argv[1])); std::filesystem::create_directories(root); i18n::Load("ru");
      bool cropBenchmark = argc > 2 && std::string(argv[2]) == "--crop-benchmark-only";
      bool benchmarkOnly = cropBenchmark || (argc > 2 && std::string(argv[2]) == "--benchmark-only");
      if (!benchmarkOnly) {
      auto shot = [&](const wchar_t* name,bool red = true) { Layout(true); canvas->Fit(false); Sleep(3000); Capture(root/name,red); };
      NewDocument(); Patch(); Tool("move"); StartTransform(); shot(L"milestone3-transform-ru.png");
      Layout(true); auto* n = Number(L"W"); PointerEvent e; e.x = MidX(n->rect); e.y = MidY(n->rect); n->OnDown(e); n->OnUp(e,true); shot(L"milestone3-numeric-ru.png",false); Key(VK_ESCAPE); Key(VK_ESCAPE);
      RunCommand("edit.transform.warp",L""); Preview(); Choose(L"Custom",1); shot(L"milestone3-warp-ru.png",false); Key(VK_ESCAPE);
      Tool("roundedRectangle"); Value(L"Radius",12); FinishParameterPreviews(); Drain(); propsTabs->index = 0; propsTabs->onChange(0); Preview(); shot(L"milestone3-shape-ru.png");
      Tool("eyedropper"); e = Pointer(40,40); canvas->OnDown(e); Preview(); shot(L"milestone3-sampler-ru.png",false); canvas->OnUp(e,true); Drain();
      Tool("crop"); Choose(L"Unconstrained",0); Stroke(20,20,150,130); Value(L"Angle",15); Choose(L"Rule of Thirds",4); shot(L"milestone3-crop-ru.png",false); Key(VK_ESCAPE);
      NewDocument(); Command("tools.setColors","{\"foreground\":\"#ff0000\",\"background\":\"#0000ff\"}"); Tool("gradient"); Choose(L"Linear",0); Value(L"Opacity",100); FinishGradientPreview(); ToggleOption(L"Reverse",false); Stroke(20,60,180,100); Preview(); shot(L"milestone3-gradient-ru.png",false);
      Button(L"Edit Gradient…"); shot(L"milestone3-gradient-editor-ru.png",false); Key(VK_ESCAPE);
      }
      if (cropBenchmark) CropBenchmark(root);
      else if (argc > 2 && (std::string(argv[2]) == "--benchmark" || benchmarkOnly)) WandBenchmark(root);
    }
    std::cout << checks << " parameter checks passed\n";
  } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; status = 1; }
  if (Mode()) Mode()->Cancel(); FinishNudge(); Drain(); jobs::Shutdown(); delete canvas; canvas = nullptr; pc_free(engine); engine = nullptr; GfxDestroy(); DestroyWindow(hwnd); CoUninitialize(); return status;
}
