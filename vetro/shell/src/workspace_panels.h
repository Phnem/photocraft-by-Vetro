#pragma once
#include "panels.h"
void InitializeWorkspacePanels();
void LayoutWorkspacePanels(bool snap);
void SyncWorkspacePanels();
void TickWorkspacePanels();
bool WorkspacePanelCommand(const std::string&);
bool IsWorkspacePanelCommand(const std::string&);
bool WorkspacePanelChecked(const std::string&);
ToolPanel* WorkspacePanel(const std::string& key);
Json WorkspacePanelState(const std::string& key);
void ImportPanelAssets(const std::string& key,const std::wstring& path);
void ExportPanelAssets(const std::string& key,const std::wstring& path,const std::string& group="");
void BuildHistoryExtras(Container&);
void PaintHistorySource(const Rect&,int state);
bool HistorySourceClick(const PointerEvent&,const Rect&,int state);
void AddHistoryBrushSource(JsonObject&);
std::string SelectedPanelPath();
bool WorkspacePanelsBusy();
constexpr UINT WM_WORKSPACE_PANELS=WM_APP+0x58;
