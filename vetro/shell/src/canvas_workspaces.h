#pragma once
#include "panels.h"
bool CanvasWorkspaceCommand(const std::string& id);
ToolPanel* CanvasWorkspacePanel();
Json CanvasWorkspaceState();
void ReadClipboardForCanvas(std::function<void()> ready);
