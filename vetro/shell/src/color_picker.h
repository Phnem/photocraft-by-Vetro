#pragma once
#include "shell.h"
// Shared by the picker and Color panel. Changes are local until a gesture is released.
class ColorSpectrum : public Widget {
 public:
  double hue=0,saturation=0,brightness=0;
  std::function<void(D2D1_COLOR_F,bool)> change;
  void Set(D2D1_COLOR_F);
  D2D1_COLOR_F Color() const;
  void Paint() override;
  void OnDown(const PointerEvent&) override;
  void OnDrag(const PointerEvent&) override;
  void OnUp(const PointerEvent&,bool) override;
 private:
  bool strip=false,dragging=false;
  double paintedHue=-1;
  Microsoft::WRL::ComPtr<ID2D1Bitmap1> field;
  Rect Field() const;
};
void OpenColorPicker(D2D1_COLOR_F color,std::function<void(D2D1_COLOR_F)> done,const Rect& anchor);
Json ColorPickerState();
