#include "gui.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <commctrl.h>
#include <mmsystem.h>
#include <shellapi.h>

#include <cwchar>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "engine.h"
#include "hook.h"
#include "settings.h"
#include "smoother.h"
#include "util.h"

namespace {

struct CommandLine {
    bool minimized = false;
    bool preview = false;
    std::wstring logPath;
    std::wstring mode;
    int number = 0;
    int interval = 0;
    int seconds = 0;
    std::wstring extra;
};

std::vector<std::wstring> GetArguments() {
    std::vector<std::wstring> args;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    LocalFree(argv);
    return args;
}

CommandLine ParseCommandLine(const std::vector<std::wstring>& args) {
    CommandLine parsed;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::wstring& arg = args[i];
        if (arg == L"--minimized") {
            parsed.minimized = true;
        } else if (arg == L"--preview") {
            parsed.preview = true;
        } else if (arg == L"--log" && i + 1 < args.size()) {
            parsed.logPath = args[++i];
        } else if (arg == L"--demo" && i + 2 < args.size()) {
            parsed.mode = L"demo";
            parsed.number = _wtoi(args[++i].c_str());
            parsed.interval = _wtoi(args[++i].c_str());
        } else if (arg == L"--test-capture" && i + 1 < args.size()) {
            parsed.mode = L"test-capture";
            parsed.seconds = _wtoi(args[++i].c_str());
        } else if (arg == L"--selftest") {
            parsed.mode = L"selftest";
        } else if (arg == L"--hooktest") {
            parsed.mode = L"hooktest";
        } else if (arg == L"--replace") {
            parsed.extra = L"replace";
        }
    }
    return parsed;
}

std::wstring EnvironmentLogPath() {
    wchar_t buffer[MAX_PATH * 2] = {};
    DWORD length = GetEnvironmentVariableW(L"FIXMOUSE_LOG", buffer,
                                           static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) return std::wstring();
    return std::wstring(buffer, length);
}

HANDLE OpenAppendFile(const std::wstring& path) {
    if (path.empty()) return INVALID_HANDLE_VALUE;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) SetFilePointer(file, 0, nullptr, FILE_END);
    return file;
}

void WriteDemoLog(HANDLE file, const std::wstring& line) {
    if (file == INVALID_HANDLE_VALUE) return;
    std::string utf8 = WideToUtf8(line) + "\r\n";
    DWORD written = 0;
    WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

int RunDemo(int notches, int intervalMs, const std::wstring& logPath) {
    if (notches <= 0) notches = 3;
    if (intervalMs < 1) intervalMs = 30;

    timeBeginPeriod(1);
    ApplySettings(LoadSettings());

    HANDLE logFile = OpenAppendFile(logPath);
    ScrollEngine engine;
    const double start = static_cast<double>(GetTickCount64());
    double nextNotchAt = 0.0;
    int added = 0;
    long long totalUnits = 0;

    WriteDemoLog(logFile, L"demo start notches=" + std::to_wstring(notches) +
                              L" intervalMs=" + std::to_wstring(intervalMs));

    while (added < notches || engine.active()) {
        const double now = static_cast<double>(GetTickCount64());

        if (added < notches && (now - start) >= nextNotchAt) {
            engine.addNotch(120.0, now, false);
            ++added;
            nextNotchAt += static_cast<double>(intervalMs);
        }

        int step = engine.tick(now);
        if (step != 0) {
            InjectWheelDelta(step, false);
            totalUnits += step;
            wchar_t line[128] = {};
            swprintf(line, std::size(line), L"t=%llu add=%d step=%d total=%lld",
                     static_cast<unsigned long long>(now - start), added, step, totalUnits);
            WriteDemoLog(logFile, line);
        }

        Sleep(4);
    }

    WriteDemoLog(logFile, L"demo end totalUnits=" + std::to_wstring(totalUnits));
    if (logFile != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(logFile);
        CloseHandle(logFile);
    }
    timeEndPeriod(1);
    return 0;
}

int RunCaptureTest(int seconds, const std::wstring& logPath) {
    if (seconds <= 0) seconds = 5;
    if (logPath.empty()) return 2;

    ApplySettings(LoadSettings());
    if (!StartInputHook(logPath, true, true)) return 1;
    Sleep(static_cast<DWORD>(seconds) * 1000);
    StopInputHook();
    return 0;
}

// ---------------------------------------------------------------------------
// Offline engine self test: exercises the smoothing / inertia math without
// touching the real mouse or installing a hook.
// ---------------------------------------------------------------------------

struct SimInput {
    double timeMs = 0.0;
    double notches = 0.0;
    bool precise = false;
};

struct SimResult {
    double totalUnits = 0.0;
    double tailUnits = 0.0;
    double lastInputTime = 0.0;
    double lastEmissionTime = -1e9;
    double firstEmissionTime = -1e9;
    int steps = 0;
    int subNotchSteps = 0;
    std::vector<std::pair<double, int>> emissions;
};

SimResult SimulateSchedule(const std::vector<SimInput>& inputs, double maxExtraMs) {
    ScrollEngine engine;
    SimResult result;
    double now = 0.0;
    size_t nextInput = 0;
    constexpr double kTickMs = 4.0;

    while (true) {
        if (nextInput < inputs.size() && now >= inputs[nextInput].timeMs - 1e-9) {
            const SimInput& input = inputs[nextInput];
            engine.addNotch(input.notches * 120.0, input.timeMs, input.precise);
            result.lastInputTime = input.timeMs;
            ++nextInput;
        }

        const int step = engine.tick(now);
        if (step != 0) {
            result.totalUnits += step;
            ++result.steps;
            if (std::abs(step) < 120) ++result.subNotchSteps;
            if (result.firstEmissionTime < 0.0) result.firstEmissionTime = now;
            result.lastEmissionTime = now;
            result.emissions.emplace_back(now, step);
        }

        if (nextInput == inputs.size() && !engine.active()) break;
        now += kTickMs;
        if (now > result.lastInputTime + maxExtraMs) break;
    }

    for (const auto& emission : result.emissions) {
        if (emission.first > result.lastInputTime + 1e-9) result.tailUnits += emission.second;
    }
    return result;
}

std::vector<SimInput> MakeBurst(int count, double intervalMs, bool precise) {
    std::vector<SimInput> inputs;
    if (count <= 0) return inputs;
    inputs.reserve(static_cast<size_t>(count) + 1);
    for (int i = 0; i < count; ++i) {
        SimInput input;
        input.timeMs = i * intervalMs;
        input.notches = 1.0;
        input.precise = precise;
        inputs.push_back(input);
    }
    return inputs;
}

void AddBrakeNotch(std::vector<SimInput>* inputs, double gapMs, bool precise) {
    SimInput input;
    input.timeMs = inputs->empty() ? 0.0 : inputs->back().timeMs + gapMs;
    input.notches = 1.0;
    input.precise = precise;
    inputs->push_back(input);
}

SimResult RunSchedule(const std::vector<SimInput>& inputs, int smoothness, int inertia,
                      int sensitivity, bool acceleration) {
    app::g_smoothness.store(smoothness, std::memory_order_relaxed);
    app::g_inertia.store(inertia, std::memory_order_relaxed);
    app::g_sensitivity.store(sensitivity, std::memory_order_relaxed);
    app::g_acceleration.store(acceleration, std::memory_order_relaxed);
    return SimulateSchedule(inputs, 5000.0);
}

void WriteReport(const std::wstring& logPath, const std::wstring& text) {
    if (logPath.empty()) return;
    HANDLE file = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    std::string utf8 = WideToUtf8(text);
    DWORD written = 0;
    WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(file);
}

int RunSelfTest(const std::wstring& logPath) {
    const std::vector<SimInput> singleSchedule = MakeBurst(1, 200.0, false);
    const std::vector<SimInput> slowSchedule = MakeBurst(5, 200.0, false);
    const std::vector<SimInput> fastSchedule = MakeBurst(8, 35.0, false);
    const std::vector<SimInput> fastPreciseSchedule = MakeBurst(8, 35.0, true);
    const std::vector<SimInput> longFastSchedule = MakeBurst(45, 30.0, false);

    std::vector<SimInput> brakeSchedule = MakeBurst(25, 30.0, false);
    AddBrakeNotch(&brakeSchedule, 120.0, false);

    const SimResult single = RunSchedule(singleSchedule, 55, 60, 100, true);
    const SimResult slow = RunSchedule(slowSchedule, 55, 60, 100, true);
    const SimResult fast = RunSchedule(fastSchedule, 55, 60, 100, true);
    const SimResult fastNoInertia = RunSchedule(fastSchedule, 55, 0, 100, false);
    const SimResult fastPrecise = RunSchedule(fastPreciseSchedule, 55, 60, 100, true);
    const SimResult fastAccel = RunSchedule(longFastSchedule, 55, 60, 100, true);
    const SimResult fastNoAccel = RunSchedule(longFastSchedule, 55, 60, 100, false);
    const SimResult brakeOn = RunSchedule(brakeSchedule, 55, 60, 100, true);
    const SimResult brakeOff = RunSchedule(brakeSchedule, 55, 60, 100, false);

    const double singleNotches = single.totalUnits / 120.0;
    const double slowNotches = slow.totalUnits / 120.0;
    const double fastNotches = fast.totalUnits / 120.0;
    const double noInertiaNotches = fastNoInertia.totalUnits / 120.0;
    const double preciseNotches = fastPrecise.totalUnits / 120.0;
    const double accelNotches = fastAccel.totalUnits / 120.0;
    const double noAccelNotches = fastNoAccel.totalUnits / 120.0;
    const double brakeOnTail = brakeOn.tailUnits / 120.0;
    const double brakeOffTail = brakeOff.tailUnits / 120.0;

    const bool passSlow = std::abs(slowNotches - 5.0) < 0.6;
    const bool passFastExtra = (fastNotches - 8.0) > 6.0;
    const bool passNoInertia = std::abs(noInertiaNotches - 8.0) < 0.6;
    const bool passPrecise = fastPrecise.subNotchSteps > 5 && fast.subNotchSteps == 0;
    const bool passSingle = singleNotches >= 0.99 && singleNotches <= 1.01 &&
                            (single.firstEmissionTime >= 0.0) && single.firstEmissionTime < 80.0;
    const bool passTail = (fast.tailUnits / 120.0) > (slow.tailUnits / 120.0) + 4.0;
    const bool passAccel = (accelNotches - noAccelNotches) > 6.0;
    const bool passBrake = brakeOnTail < 2.5 && brakeOffTail >= brakeOnTail + 3.0;
    const bool pass = passSlow && passFastExtra && passNoInertia && passPrecise && passSingle &&
                      passTail && passAccel && passBrake;

    std::wstring report;
    wchar_t line[512] = {};

    report += L"winMouseFix V1.0 engine self-test\r\n";
    report += L"params: smoothness=55 inertia=60 sensitivity=100 tick=4ms\r\n";
    report += L"quantum: whole notch (native/compat) unless marked precise\r\n\r\n";

    swprintf(line, std::size(line), L"slow  5 notches @200ms : total=%.2f tail=%.2f settle=%.0fms\r\n",
             slowNotches, slow.tailUnits / 120.0, slow.lastEmissionTime - slow.lastInputTime);
    report += line;
    swprintf(line, std::size(line), L"fast  8 notches @35ms  : total=%.2f tail=%.2f settle=%.0fms\r\n",
             fastNotches, fast.tailUnits / 120.0, fast.lastEmissionTime - fast.lastInputTime);
    report += line;
    swprintf(line, std::size(line),
             L"fast  inertia=0, accel off : total=%.2f tail=%.2f\r\n",
             noInertiaNotches, fastNoInertia.tailUnits / 120.0);
    report += line;
    swprintf(line, std::size(line),
             L"fast  precise output   : total=%.2f notches sub-notch steps=%d\r\n", preciseNotches,
             fastPrecise.subNotchSteps);
    report += line;
    swprintf(line, std::size(line),
             L"accel 45 notches @30ms : on total=%.2f  off total=%.2f  (+%.2f)\r\n", accelNotches,
             noAccelNotches, accelNotches - noAccelNotches);
    report += line;
    swprintf(line, std::size(line),
             L"brake after 25 fast    : accel-on tail=%.2f  accel-off tail=%.2f\r\n", brakeOnTail,
             brakeOffTail);
    report += line;
    swprintf(line, std::size(line), L"single notch           : total=%.2f first output=%.0fms\r\n",
             singleNotches, single.firstEmissionTime);
    report += line;

    swprintf(line, std::size(line),
             L"slow %ls | fast extra %ls | no-inertia %ls | precision %ls | accel %ls | brake %ls | "
             L"single %ls | tail %ls\r\n",
             passSlow ? L"OK" : L"FAIL", passFastExtra ? L"OK" : L"FAIL",
             passNoInertia ? L"OK" : L"FAIL", passPrecise ? L"OK" : L"FAIL",
             passAccel ? L"OK" : L"FAIL", passBrake ? L"OK" : L"FAIL",
             passSingle ? L"OK" : L"FAIL", passTail ? L"OK" : L"FAIL");
    report += line;
    report += pass ? L"SELFTEST PASS\r\n" : L"SELFTEST FAIL\r\n";

    WriteReport(logPath, report);
    fwprintf(stdout, L"%ls", report.c_str());
    std::fflush(stdout);
    return pass ? 0 : 1;
}

int RunHookTest(const std::wstring& logPath) {
    const std::wstring path = logPath.empty() ? L"hooktest.log" : logPath;
    if (!StartInputHook(path, true, true)) return 1;
    Sleep(250);
    for (int i = 0; i < 3; ++i) {
        InjectWheelDelta(120, false);
        Sleep(40);
    }
    Sleep(350);
    StopInputHook();
    return 0;
}

int RunApplication(HINSTANCE instance, const CommandLine& commandLine) {
    ApplySettings(LoadSettings());

    HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\winMouseFix.SingleInstance");
    if (instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (commandLine.extra == L"replace") {
            // Started to replace a running instance (for example "restart as
            // administrator"): wait for the old process to release the mutex.
            const DWORD wait = WaitForSingleObject(instanceMutex, 10000);
            if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
                CloseHandle(instanceMutex);
                return 0;
            }
        } else {
            HWND existing = FindWindowW(app::kWndClass, nullptr);
            if (existing) {
                ShowWindow(existing, SW_SHOW);
                PostMessageW(existing, kMessageShowWindow, 0, 0);
            }
            CloseHandle(instanceMutex);
            return 0;
        }
    }

    INITCOMMONCONTROLSEX controls = {};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    HWND window = CreateMainWindow(instance);
    if (!window) {
        if (instanceMutex) CloseHandle(instanceMutex);
        return 1;
    }

    const std::wstring logPath = commandLine.logPath.empty() ? EnvironmentLogPath()
                                                             : commandLine.logPath;
    if (!commandLine.preview) {
        if (!StartInputHook(logPath, false, false)) {
            MessageBoxW(nullptr, L"无法安装全局鼠标钩子，滚轮平滑将不可用。", L"winMouseFix",
                        MB_ICONERROR | MB_OK);
        }
        StartSmoother();
    }

    if (!commandLine.minimized) {
        ShowMainWindow();
    }

    timeBeginPeriod(1);

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    StopSmoother();
    StopInputHook();
    timeEndPeriod(1);

    if (instanceMutex) CloseHandle(instanceMutex);
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        SetProcessDPIAware();
    }

    CommandLine commandLine = ParseCommandLine(GetArguments());

    if (commandLine.mode == L"demo") {
        return RunDemo(commandLine.number, commandLine.interval, commandLine.logPath);
    }
    if (commandLine.mode == L"test-capture") {
        return RunCaptureTest(commandLine.seconds, commandLine.logPath);
    }
    if (commandLine.mode == L"selftest") {
        return RunSelfTest(commandLine.logPath);
    }
    if (commandLine.mode == L"hooktest") {
        return RunHookTest(commandLine.logPath);
    }

    return RunApplication(instance, commandLine);
}