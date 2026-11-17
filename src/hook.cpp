#include "hook.h"

#include "settings.h"
#include "util.h"

#include <windows.h>

#include <atomic>
#include <cwchar>
#include <mutex>
#include <thread>
#include <vector>

namespace {

constexpr int kStopMessage = WM_APP + 20;
constexpr uint32_t kProcCacheMs = 3000;

HHOOK g_hook = nullptr;
std::thread g_hookThread;
DWORD g_hookThreadId = 0;
HANDLE g_hookReadyEvent = nullptr;

std::atomic<bool> g_captureInjected{false};
std::atomic<bool> g_testLogOnly{false};

HANDLE g_logFile = INVALID_HANDLE_VALUE;
std::mutex g_logMutex;

std::mutex g_queueMutex;
std::vector<WheelEvent> g_queue;
HANDLE g_wakeEvent = nullptr;

DWORD g_ownPid = 0;
bool g_selfElevated = false;

struct CacheEntry {
    DWORD pid = 0;
    uint64_t checkedAt = 0;
    bool valid = false;
    ProcDetails details;
};

CacheEntry g_procCache[16];
int g_cacheCursor = 0;

enum class Decision {
    Capture,
    Disabled,
    CtrlDown,
    OwnProcess,
    NoWindow,
    ProcessUnavailable,
    Excluded,
    ElevatedTarget,
    Fullscreen,
    Injected,
};

struct EvalResult {
    Decision decision = Decision::Capture;
    DWORD pid = 0;
    ProcDetails details;
    bool precise = false;
};

const wchar_t* DecisionName(Decision d) {
    switch (d) {
        case Decision::Capture: return L"capture";
        case Decision::Disabled: return L"disabled";
        case Decision::CtrlDown: return L"ctrl";
        case Decision::OwnProcess: return L"own-process";
        case Decision::NoWindow: return L"no-window";
        case Decision::ProcessUnavailable: return L"process-unavailable";
        case Decision::Excluded: return L"excluded";
        case Decision::ElevatedTarget: return L"elevated-target";
        case Decision::Fullscreen: return L"fullscreen";
        case Decision::Injected: return L"injected-input";
    }
    return L"unknown";
}

void WriteLogLine(const std::wstring& line) {
    if (g_logFile == INVALID_HANDLE_VALUE) return;
    std::string utf8 = WideToUtf8(line);
    utf8 += "\r\n";
    std::lock_guard<std::mutex> lock(g_logMutex);
    DWORD written = 0;
    WriteFile(g_logFile, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

void OpenLogFile(const std::wstring& path) {
    if (path.empty()) return;
    g_logFile = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_logFile != INVALID_HANDLE_VALUE) {
        SetFilePointer(g_logFile, 0, nullptr, FILE_END);
    }
}

const CacheEntry* LookupProcess(DWORD pid, bool& ok) {
    const uint64_t now = GetTickCount64();
    for (const CacheEntry& entry : g_procCache) {
        if (entry.pid == pid && entry.valid && now - entry.checkedAt < kProcCacheMs) {
            ok = true;
            return &entry;
        }
    }

    CacheEntry& entry = g_procCache[g_cacheCursor];
    g_cacheCursor = (g_cacheCursor + 1) % static_cast<int>(std::size(g_procCache));
    entry.pid = pid;
    entry.checkedAt = now;
    entry.details = ProcDetails{};
    entry.valid = GetProcessDetails(pid, entry.details);
    ok = entry.valid;
    return &entry;
}

bool IsExcluded(const std::wstring& stem) {
    if (stem.empty()) return false;
    std::lock_guard<std::mutex> lock(app::g_stateMutex);
    for (const std::wstring& item : app::g_exclusions) {
        if (item == stem) return true;
    }
    return false;
}

bool IsFullscreenRoot(HWND root) {
    if (!root) return false;
    LONG style = GetWindowLongW(root, GWL_STYLE);
    if (style & WS_CAPTION) return false;

    RECT wr = {};
    if (!GetWindowRect(root, &wr)) return false;
    HMONITOR monitor = MonitorFromWindow(root, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return false;

    const LONG monitorW = info.rcMonitor.right - info.rcMonitor.left;
    const LONG monitorH = info.rcMonitor.bottom - info.rcMonitor.top;
    const LONG w = wr.right - wr.left;
    const LONG h = wr.bottom - wr.top;
    return w * 100 >= monitorW * 98 && h * 100 >= monitorH * 98;
}

// Chromium-based windows (Chrome, Edge, Electron apps, ...) honour sub-notch
// WHEEL_DELTA values, which allows a much more fluid stream. Traditional Win32
// controls divide by WHEEL_DELTA and silently drop smaller deltas, so those
// must receive whole notches instead — otherwise scrolling appears broken.
bool IsPrecisionSurface(HWND hwnd) {
    static const wchar_t* const kChromiumClasses[] = {
        L"Chrome_WidgetWin_1",
        L"Chrome_RenderWidgetHostHWND",
        L"Chrome_WidgetWin_0",
    };

    HWND window = hwnd;
    for (int depth = 0; window && depth < 8; ++depth) {
        wchar_t className[160] = {};
        if (GetClassNameW(window, className, static_cast<int>(std::size(className)) - 1) == 0) {
            break;
        }
        for (const wchar_t* candidate : kChromiumClasses) {
            if (wcscmp(className, candidate) == 0) return true;
        }
        window = GetParent(window);
    }
    return false;
}

EvalResult Evaluate(POINT pt) {
    EvalResult result;

    if (!app::g_runtimeEnabled.load(std::memory_order_relaxed)) {
        result.decision = Decision::Disabled;
        return result;
    }
    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) {
        result.decision = Decision::CtrlDown;
        return result;
    }

    HWND under = WindowFromPoint(pt);
    if (!under) {
        result.decision = Decision::NoWindow;
        return result;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(under, &pid);
    result.pid = pid;
    if (pid == 0) {
        result.decision = Decision::NoWindow;
        return result;
    }
    if (pid == g_ownPid) {
        result.decision = Decision::OwnProcess;
        return result;
    }

    bool processOk = false;
    const CacheEntry* entry = LookupProcess(pid, processOk);
    result.details = entry->details;
    if (!processOk || result.details.stem.empty()) {
        result.decision = Decision::ProcessUnavailable;
        return result;
    }
    if (IsExcluded(result.details.stem)) {
        result.decision = Decision::Excluded;
        return result;
    }
    if (result.details.elevated && !g_selfElevated) {
        // SendInput from a medium-integrity process cannot reach an elevated
        // window; swallowing here would leave the user with no scrolling at all.
        result.decision = Decision::ElevatedTarget;
        return result;
    }
    if (app::g_fullscreenBypass.load(std::memory_order_relaxed)) {
        HWND root = GetAncestor(under, GA_ROOT);
        if (IsFullscreenRoot(root)) {
            result.decision = Decision::Fullscreen;
            return result;
        }
    }

    result.decision = Decision::Capture;
    result.precise = app::g_highPrecision.load(std::memory_order_relaxed) &&
                     IsPrecisionSurface(under);
    return result;
}

void LogEvent(short delta, bool vertical, POINT pt, const EvalResult& result, bool injected) {
    if (g_logFile == INVALID_HANDLE_VALUE) return;

    wchar_t line[512] = {};
    swprintf(line, std::size(line),
             L"%llu delta=%d axis=%ls pid=%lu exe=%ls elevated=%d injected=%d decision=%ls "
             L"precise=%d pos=%ld,%ld",
             static_cast<unsigned long long>(GetTickCount64()), static_cast<int>(delta),
             vertical ? L"V" : L"H", static_cast<unsigned long>(result.pid),
             result.details.stem.c_str(), result.details.elevated ? 1 : 0, injected ? 1 : 0,
             DecisionName(result.decision), result.precise ? 1 : 0, pt.x, pt.y);
    WriteLogLine(line);
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode != HC_ACTION) return CallNextHookEx(g_hook, nCode, wParam, lParam);

    const bool vertical = wParam == WM_MOUSEWHEEL;
    const bool horizontal = wParam == WM_MOUSEHWHEEL;
    if (!vertical && !horizontal) return CallNextHookEx(g_hook, nCode, wParam, lParam);

    MSLLHOOKSTRUCT* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
    const short delta = static_cast<short>((info->mouseData >> 16) & 0xFFFF);
    if (delta == 0) return CallNextHookEx(g_hook, nCode, wParam, lParam);

    const bool injected = (info->flags & LLMHF_INJECTED) != 0;
    const bool captureInjected = g_captureInjected.load(std::memory_order_relaxed);
    const bool logging = g_logFile != INVALID_HANDLE_VALUE;

    if (injected && !captureInjected) {
        // Our own injected sub-notch wheel events (and those of other tools)
        // must pass through untouched to avoid feedback loops.
        if (logging) {
            EvalResult result;
            result.decision = Decision::Injected;
            LogEvent(delta, vertical, info->pt, result, injected);
        }
        return CallNextHookEx(g_hook, nCode, wParam, lParam);
    }

    EvalResult result = Evaluate(info->pt);
    if (logging) LogEvent(delta, vertical, info->pt, result, injected);

    if (result.decision != Decision::Capture) {
        return CallNextHookEx(g_hook, nCode, wParam, lParam);
    }

    if (g_testLogOnly.load(std::memory_order_relaxed)) {
        // Diagnostic mode: log everything, but never eat the user's physical
        // wheel input. Synthetic test input is swallowed so the test has no
        // side effects on the window below the cursor.
        return injected ? 1 : CallNextHookEx(g_hook, nCode, wParam, lParam);
    }

    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        WheelEvent event;
        event.delta = delta;
        event.axis = vertical ? WheelAxis::Vertical : WheelAxis::Horizontal;
        event.timeMs = NowMs();
        event.precise = result.precise;
        g_queue.push_back(event);
    }
    if (g_wakeEvent) SetEvent(g_wakeEvent);

    return 1;  // swallow the physical notch; the smooth worker reinjects it
}

void HookThreadMain() {
    g_hookThreadId = GetCurrentThreadId();
    g_hook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandleW(nullptr), 0);
    SetEvent(g_hookReadyEvent);

    if (!g_hook) return;

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == kStopMessage) break;
    }

    UnhookWindowsHookEx(g_hook);
    g_hook = nullptr;
}

}  // namespace

bool StartInputHook(const std::wstring& logPath, bool captureInjected, bool testLogOnly) {
    g_ownPid = GetCurrentProcessId();
    g_selfElevated = IsSelfElevated();
    g_captureInjected.store(captureInjected, std::memory_order_relaxed);
    g_testLogOnly.store(testLogOnly, std::memory_order_relaxed);

    OpenLogFile(logPath);

    g_wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_hookReadyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_hookThread = std::thread(HookThreadMain);

    WaitForSingleObject(g_hookReadyEvent, 3000);
    const bool ok = g_hook != nullptr;

    CloseHandle(g_hookReadyEvent);
    g_hookReadyEvent = nullptr;
    return ok;
}

void StopInputHook() {
    if (g_hookThread.joinable()) {
        if (g_hookThreadId) PostThreadMessageW(g_hookThreadId, kStopMessage, 0, 0);
        g_hookThread.join();
        g_hookThreadId = 0;
    }

    if (g_logFile != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(g_logFile);
        CloseHandle(g_logFile);
        g_logFile = INVALID_HANDLE_VALUE;
    }

    if (g_wakeEvent) {
        CloseHandle(g_wakeEvent);
        g_wakeEvent = nullptr;
    }
}

HANDLE GetWheelWakeEvent() {
    return g_wakeEvent;
}

std::vector<WheelEvent> TakeWheelEvents() {
    std::vector<WheelEvent> events;
    std::lock_guard<std::mutex> lock(g_queueMutex);
    events.swap(g_queue);
    return events;
}

void InjectWheelDelta(int units, bool horizontal) {
    if (units == 0) return;
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = horizontal ? MOUSEEVENTF_HWHEEL : MOUSEEVENTF_WHEEL;
    input.mi.mouseData = static_cast<DWORD>(static_cast<int>(units));
    SendInput(1, &input, sizeof(INPUT));
}