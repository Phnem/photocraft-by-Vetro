#pragma once
#include "shell.h"

void InitializeGuides();
void LayoutGuides(Rect& free);
void GuideToolChanged(const std::string& key);
bool GuideCommand(const std::string& id);
bool IsGuideCommand(const std::string& id);
bool GuideCommandChecked(const std::string& id);
bool GuideKey(UINT vk, Mods mods);
void SyncGuidePreferences();
Json GuideState();
Widget* GuideOverlay();
void BeginMoveSnap();
void EndViewSnap();
D2D_POINT_2F SnapMoveOffset(double dx, double dy, Mods mods);
