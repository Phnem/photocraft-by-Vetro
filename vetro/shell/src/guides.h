#pragma once
#include "shell.h"

void InitializeGuides();
void LayoutGuides(Rect& free);
void GuideToolChanged(const std::string& key);
bool GuideCommand(const std::string& id);
bool IsGuideCommand(const std::string& id);
bool GuideCommandChecked(const std::string& id);
bool GuideKey(UINT vk, Mods mods);
bool ExtrasVisible();
void SyncGuidePreferences();
Json GuideState();
Widget* GuideOverlay();
void BeginMoveSnap();
void EndViewSnap();
D2D_POINT_2F SnapMoveOffset(double dx, double dy, Mods mods);
void BeginPointSnap(bool excludeSelected = false);
D2D_POINT_2F SnapViewPoint(D2D_POINT_2F p, bool horizontal = true, bool vertical = true);
D2D_POINT_2F SnapViewRay(D2D_POINT_2F p,D2D_POINT_2F origin);
