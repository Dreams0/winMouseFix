#include "smoother.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <atomic>
#include <thread>
#include <vector>

#include "engine.h"
#include "hook.h"
#include "settings.h"
#include "util.h"

namespace {

std::thread g_worker;
std::atomic<bool> g_running{false};

void WorkerMain() {
    ScrollEngine vertical;
    ScrollEngine horizontal;

    while (g_running.load(std::memory_order_relaxed)) {
        const bool active = vertical.active() || horizontal.active();
        HANDLE wake = GetWheelWakeEvent();
        DWORD waitMs = active ? 4 : INFINITE;
        if (wake) {
            WaitForSingleObject(wake, waitMs);
        } else {
            Sleep(active ? waitMs : 100);
        }
        if (!g_running.load(std::memory_order_relaxed)) break;
    
        std::vector<WheelEvent> events = TakeWheelEvents();
        const bool ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool enabled = app::g_runtimeEnabled.load(std::memory_order_relaxed);
        const double now = NowMs();
    
        for (const WheelEvent& event : events) {
            if (event.axis == WheelAxis::Vertical) {
                vertical.addNotch(static_cast<double>(event.delta),
                                  static_cast<double>(event.timeMs), event.precise);
            } else if (app::g_horizontal.load(std::memory_order_relaxed)) {
                horizontal.addNotch(static_cast<double>(event.delta),
                                    static_cast<double>(event.timeMs), event.precise);
            }
        }
    
        if (ctrlDown || !enabled) {
            // Zooming (Ctrl+wheel) or a pause requested from the UI must stop
            // any glide immediately instead of injecting extra movement.
            vertical.reset();
            horizontal.reset();
            continue;
        }
    
        if (vertical.active()) {
            int step = vertical.tick(now);
            if (step != 0) InjectWheelDelta(step, false);
        }
        if (horizontal.active()) {
            int step = horizontal.tick(now);
            if (step != 0) InjectWheelDelta(step, true);
        }
    }
}

}  // namespace

bool StartSmoother() {
    if (g_worker.joinable()) return true;
    g_running.store(true, std::memory_order_relaxed);
    g_worker = std::thread(WorkerMain);
    return true;
}

void StopSmoother() {
    if (!g_worker.joinable()) return;
    g_running.store(false, std::memory_order_relaxed);
    if (HANDLE wake = GetWheelWakeEvent()) SetEvent(wake);
    g_worker.join();
}