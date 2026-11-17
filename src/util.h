#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <string>

// High-resolution monotonic milliseconds (QueryPerformanceCounter).
double NowMs();

struct ProcDetails {
    std::wstring stem;  // lowercase executable name without .exe
    std::wstring path;
    bool elevated = true;
};

std::wstring GetExecutablePath();
bool GetProcessDetails(DWORD pid, ProcDetails& out);
bool IsSelfElevated();

std::string WideToUtf8(const std::wstring& s);
std::wstring TrimW(const std::wstring& s);
std::wstring LowerW(const std::wstring& s);