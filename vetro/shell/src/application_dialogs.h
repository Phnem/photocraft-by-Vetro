#pragma once
#include "panels.h"
bool ApplicationDialogCommand(const std::string& id);
Popover* ApplicationDialog();
Json ApplicationDialogState();
void InitializeApplicationPreferences(const std::wstring& file=L"");
void PersistApplicationPreferences();
void ApplyShortcutMetadata(const Json& metadata);
bool CustomShortcutKey(UINT vk,Mods mods);
void ImportShortcutFile(const std::wstring& path);
std::wstring FileDialog(bool save,const std::wstring& suggested);
