#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

HWND CreateMainWindow(HINSTANCE instance);
void ShowMainWindow();
void SaveSettingsNow();
void RemoveTrayIcon();

constexpr UINT kMessageShowWindow = WM_APP + 1;
constexpr UINT kMessageTray = WM_APP + 5;