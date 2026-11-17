#include "settings.h"

#include "util.h"

#include <algorithm>
#include <cwchar>

namespace app {

std::mutex g_stateMutex;
Settings g_settings;

std::atomic<bool> g_runtimeEnabled{true};
std::atomic<bool> g_horizontal{true};
std::atomic<bool> g_fullscreenBypass{true};
std::atomic<bool> g_highPrecision{true};
std::atomic<bool> g_acceleration{true};
std::atomic<int> g_smoothness{55};
std::atomic<int> g_inertia{60};
std::atomic<int> g_sensitivity{100};

std::vector<std::wstring> g_exclusions;
std::atomic<bool> g_selfElevated{false};

HWND g_mainWnd = nullptr;
HINSTANCE g_hInst = nullptr;

const wchar_t kWndClass[] = L"winMouseFixMainWindow";
const wchar_t kAppTitle[] = L"winMouseFix V1.0 · Dreams0";

}  // namespace app

namespace {

const wchar_t* const kRegPath = L"Software\\winMouseFix";
const wchar_t* const kLegacyRegPath = L"Software\\FixMouse";
const wchar_t* const kRunPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* const kRunValue = L"winMouseFix";
const wchar_t* const kLegacyRunValue = L"FixMouse";

int ClampInt(int value, int low, int high) {
    return value < low ? low : (value > high ? high : value);
}

DWORD ReadDword(HKEY key, const wchar_t* name, DWORD fallback) {
    DWORD type = 0;
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(&value), &size) ==
            ERROR_SUCCESS &&
        type == REG_DWORD) {
        return value;
    }
    return fallback;
}

std::wstring ReadString(HKEY key, const wchar_t* name, const std::wstring& fallback) {
    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
        type != REG_SZ || size == 0) {
        return fallback;
    }

    std::wstring out(size / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(out.data()), &size) !=
        ERROR_SUCCESS) {
        return fallback;
    }
    out.resize(size / sizeof(wchar_t));
    while (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

void WriteDword(HKEY key, const wchar_t* name, DWORD value) {
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

bool RunValueExists(const wchar_t* name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunPath, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }

    DWORD type = 0;
    DWORD size = 0;
    bool exists = RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) == ERROR_SUCCESS;
    RegCloseKey(key);
    return exists;
}

}  // namespace

Settings LoadSettings() {
    Settings s;
    bool usedLegacy = false;

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegPath, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kLegacyRegPath, 0, KEY_READ, &key) == ERROR_SUCCESS) {
            usedLegacy = true;
        }
    }
    if (key) {
        s.enabled = ReadDword(key, L"Enabled", 1) != 0;
        s.horizontal = ReadDword(key, L"Horizontal", 1) != 0;
        s.fullscreenBypass = ReadDword(key, L"FullscreenBypass", 1) != 0;
        s.highPrecision = ReadDword(key, L"HighPrecision", 1) != 0;
        s.acceleration = ReadDword(key, L"Acceleration", 1) != 0;
        s.smoothness = static_cast<int>(ReadDword(key, L"Smoothness", 55));
        s.inertia = static_cast<int>(ReadDword(key, L"Inertia", 60));
        s.sensitivity = static_cast<int>(ReadDword(key, L"Sensitivity", 100));
        s.exclusions = ReadString(key, L"Exclusions", L"");
        RegCloseKey(key);
    }

    s.smoothness = ClampInt(s.smoothness, 0, 100);
    s.inertia = ClampInt(s.inertia, 0, 100);
    s.sensitivity = ClampInt(s.sensitivity, 25, 300);

    // Horizontal smoothing, smart precision and acceleration are part of the
    // core experience and are always on in this build.
    s.horizontal = true;
    s.highPrecision = true;
    s.acceleration = true;
    s.autostart = IsAutostartEnabled();
    if (s.autostart && !RunValueExists(kRunValue)) {
        // Point a legacy startup entry at the renamed executable.
        SetAutostart(true);
    }
    if (usedLegacy) {
        SaveSettings(s);
    }
    return s;
}


bool SaveSettings(const Settings& s) {
    HKEY key = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegPath, 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_WRITE, nullptr, &key, &disposition) != ERROR_SUCCESS) {
        return false;
    }

    WriteDword(key, L"Enabled", s.enabled ? 1 : 0);
    WriteDword(key, L"Horizontal", s.horizontal ? 1 : 0);
    WriteDword(key, L"FullscreenBypass", s.fullscreenBypass ? 1 : 0);
    WriteDword(key, L"HighPrecision", s.highPrecision ? 1 : 0);
    WriteDword(key, L"Acceleration", s.acceleration ? 1 : 0);
    WriteDword(key, L"Smoothness", static_cast<DWORD>(ClampInt(s.smoothness, 0, 100)));
    WriteDword(key, L"Inertia", static_cast<DWORD>(ClampInt(s.inertia, 0, 100)));
    WriteDword(key, L"Sensitivity", static_cast<DWORD>(ClampInt(s.sensitivity, 25, 300)));

    DWORD bytes = static_cast<DWORD>((s.exclusions.size() + 1) * sizeof(wchar_t));
    RegSetValueExW(key, L"Exclusions", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(s.exclusions.c_str()), bytes);

    RegCloseKey(key);
    return true;
}

Settings GetSettings() {
    std::lock_guard<std::mutex> lock(app::g_stateMutex);
    return app::g_settings;
}

void ApplySettings(const Settings& input) {
    Settings s = input;
    s.smoothness = ClampInt(s.smoothness, 0, 100);
    s.inertia = ClampInt(s.inertia, 0, 100);
    s.sensitivity = ClampInt(s.sensitivity, 25, 300);

    // Horizontal smoothing, smart precision and acceleration are part of the
    // core experience and are always on in this build.
    s.horizontal = true;
    s.highPrecision = true;
    s.acceleration = true;

    std::vector<std::wstring> exclusions;
    exclusions.reserve(32);

    std::wstring current;
    auto flushLine = [&]() {
        std::wstring line = TrimW(current);
        current.clear();
        if (line.empty() || line[0] == L'#') return;
        line = LowerW(line);
        if (line.size() > 4 && line.compare(line.size() - 4, 4, L".exe") == 0) {
            line.resize(line.size() - 4);
        }
        if (line.empty()) return;
        if (std::find(exclusions.begin(), exclusions.end(), line) == exclusions.end()) {
            exclusions.push_back(line);
        }
    };

    for (wchar_t c : s.exclusions) {
        if (c == L'\r' || c == L'\n') {
            flushLine();
        } else {
            current.push_back(c);
        }
    }
    flushLine();

    std::lock_guard<std::mutex> lock(app::g_stateMutex);
    app::g_settings = s;
    app::g_exclusions = std::move(exclusions);

    app::g_runtimeEnabled.store(s.enabled, std::memory_order_relaxed);
    app::g_horizontal.store(s.horizontal, std::memory_order_relaxed);
    app::g_fullscreenBypass.store(s.fullscreenBypass, std::memory_order_relaxed);
    app::g_highPrecision.store(s.highPrecision, std::memory_order_relaxed);
    app::g_acceleration.store(s.acceleration, std::memory_order_relaxed);
    app::g_smoothness.store(s.smoothness, std::memory_order_relaxed);
    app::g_inertia.store(s.inertia, std::memory_order_relaxed);
    app::g_sensitivity.store(s.sensitivity, std::memory_order_relaxed);
}

bool IsAutostartEnabled() {
    return RunValueExists(kRunValue) || RunValueExists(kLegacyRunValue);
}

bool SetAutostart(bool enable) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunPath, 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    bool ok = false;
    if (enable) {
        std::wstring exe = GetExecutablePath();
        if (!exe.empty()) {
            std::wstring command = L"\"" + exe + L"\" --minimized";
            ok = RegSetValueExW(key, kRunValue, 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(command.c_str()),
                                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))) ==
                 ERROR_SUCCESS;
        }
    } else {
        LONG result = RegDeleteValueW(key, kRunValue);
        ok = (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
    }

    RegDeleteValueW(key, kLegacyRunValue);
    RegCloseKey(key);
    return ok;
}