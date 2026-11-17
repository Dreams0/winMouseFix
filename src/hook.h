#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <cstdint>
#include <vector>
#include <string>

enum class WheelAxis { Vertical, Horizontal };

struct WheelEvent {
    short delta = 0;
    WheelAxis axis = WheelAxis::Vertical;
    double timeMs = 0.0;
    bool precise = false;  // target window understands sub-notch deltas
};

bool StartInputHook(const std::wstring& logPath, bool captureInjected, bool testLogOnly);
void StopInputHook();

HANDLE GetWheelWakeEvent();
std::vector<WheelEvent> TakeWheelEvents();

void InjectWheelDelta(int units, bool horizontal);