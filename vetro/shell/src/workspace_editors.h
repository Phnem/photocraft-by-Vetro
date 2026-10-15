#pragma once
#include "panels.h"
bool WorkspaceEditorCommand(const std::string& id);
ToolPanel* WorkspaceEditorPanel();
Json WorkspaceEditorState();
void OpenPathStrokeDialog(const std::string& name="work");
