#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

struct Settings {
    bool enabled = true;
    bool horizontal = true;
    bool fullscreenBypass = true;
    bool highPrecision = true;   // smart precision: only for compatible windows
    bool acceleration = true;    // speed up while the wheel keeps spinning fast
    bool autostart = false;

    int smoothness = 55;   // 0..100
    int inertia = 60;      // 0..100
    int sensitivity = 100;  // 25..300 (percent)

    std::wstring exclusions;  // one process name per line
};

namespace app {

extern std::mutex g_stateMutex;
extern Settings g_settings;

extern std::atomic<bool> g_runtimeEnabled;
extern std::atomic<bool> g_horizontal;
extern std::atomic<bool> g_fullscreenBypass;
extern std::atomic<bool> g_highPrecision;
extern std::atomic<bool> g_acceleration;
extern std::atomic<int> g_smoothness;
extern std::atomic<int> g_inertia;
extern std::atomic<int> g_sensitivity;

extern std::vector<std::wstring> g_exclusions;  // guarded by g_stateMutex
extern std::atomic<bool> g_selfElevated;

extern HWND g_mainWnd;
extern HINSTANCE g_hInst;

extern const wchar_t kWndClass[];
extern const wchar_t kAppTitle[];

}  // namespace app

Settings LoadSettings();
bool SaveSettings(const Settings& s);
Settings GetSettings();
void ApplySettings(const Settings& s);

bool IsAutostartEnabled();
bool SetAutostart(bool enable);