#include "shell.h"
#include "icons_phosphor.h"
#include "i18n.h"
#include <tuple>

namespace { int alignTo = 0; }
void MoveAlignmentOptions(Container& dock, std::vector<Widget*>& out) {
  auto* reference = dock.Add<SelectList>(std::vector<std::wstring>{L"Auto",L"Selection",L"Canvas"},alignTo);
  reference->onChange = [](int i) { alignTo = i; }; out.push_back(reference);
  for (auto [key,icon,label] : {std::tuple{"leftEdges",L"M40 28V228 M72 68H212 M72 128H160 M72 188H192",L"Align Left Edges"},std::tuple{"horizontalCenters",L"M128 28V228 M48 68H208 M72 128H184 M88 188H168",L"Align Horizontal Centers"},
                                std::tuple{"rightEdges",L"M216 28V228 M44 68H184 M96 128H184 M64 188H184",L"Align Right Edges"},std::tuple{"topEdges",L"M28 40H228 M68 72V212 M128 72V160 M188 72V192",L"Align Top Edges"},
                                std::tuple{"verticalCenters",L"M28 128H228 M68 48V208 M128 72V184 M188 88V168",L"Align Vertical Centers"},std::tuple{"bottomEdges",L"M28 216H228 M68 44V184 M128 96V184 M188 64V184",L"Align Bottom Edges"}}) {
    auto* b = dock.Add<IconButton>(icon,label,[key] { const char* targets[] = {"auto","selection","canvas"}; Exec(std::string("layer.align.") + key,JsonObject().Str("to",targets[std::clamp(alignTo,0,2)]).Text()); });
    b->stroked = true; out.push_back(b);
  }
  // Distribute: three layers' edges or centres evenly spaced, then equal gaps between them.
  for (auto [key,icon,label] : {std::tuple{"topEdges",L"M36 36H220 M36 116H220 M36 196H220 M72 36V76 M128 116V156 M184 196V236",L"Distribute Top Edges"},std::tuple{"verticalCenters",L"M36 40H220 M36 128H220 M36 216H220 M72 20V60 M128 108V148 M184 196V236",L"Distribute Vertical Centers"},
                                std::tuple{"bottomEdges",L"M36 60H220 M36 140H220 M36 220H220 M72 20V60 M128 100V140 M184 180V220",L"Distribute Bottom Edges"},std::tuple{"leftEdges",L"M36 36V220 M116 36V220 M196 36V220 M36 72H76 M116 128H156 M196 184H236",L"Distribute Left Edges"},
                                std::tuple{"horizontalCenters",L"M40 36V220 M128 36V220 M216 36V220 M20 72H60 M108 128H148 M196 184H236",L"Distribute Horizontal Centers"},std::tuple{"rightEdges",L"M60 36V220 M140 36V220 M220 36V220 M20 72H60 M100 128H140 M180 184H220",L"Distribute Right Edges"},
                                std::tuple{"horizontally",L"M40 44V212 M128 76V180 M216 44V212 M48 128H208",L"Distribute Horizontally"},std::tuple{"vertically",L"M44 40H212 M76 128H180 M44 216H212 M128 48V208",L"Distribute Vertically"}}) {
    auto* b = dock.Add<IconButton>(icon,label,[key] { Exec(std::string("layer.distribute.") + key); }); b->stroked = true; out.push_back(b);
  }
}
