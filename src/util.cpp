#include "util.h"

#include <wchar.h>
#include <cctype>

double NowMs() {
    static const double frequency = []() {
        LARGE_INTEGER freq = {};
        QueryPerformanceFrequency(&freq);
        return freq.QuadPart > 0 ? static_cast<double>(freq.QuadPart) : 1.0;
    }();
    LARGE_INTEGER counter = {};
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) * 1000.0 / frequency;
}

std::wstring GetExecutablePath() {
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD len = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if (len == 0) return std::wstring();
    return std::wstring(buf, len);
}

bool GetProcessDetails(DWORD pid, ProcDetails& out) {
    out = ProcDetails{};

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    auto closeProcess = [&]() { CloseHandle(process); };

    wchar_t path[MAX_PATH * 2] = {};
    DWORD pathLen = static_cast<DWORD>(std::size(path));
    if (!QueryFullProcessImageNameW(process, 0, path, &pathLen)) {
        closeProcess();
        return false;
    }

    out.path.assign(path, pathLen);
    std::wstring name = path;
    size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos) name = name.substr(slash + 1);
    if (name.size() > 4 && _wcsicmp(name.c_str() + name.size() - 4, L".exe") == 0) {
        name.resize(name.size() - 4);
    }
    out.stem = LowerW(name);

    HANDLE token = nullptr;
    if (OpenProcessToken(process, TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elevation = {};
        DWORD size = sizeof(elevation);
        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
            out.elevated = elevation.TokenIsElevated != 0;
        }
        CloseHandle(token);
    }
    // If the token cannot be queried (protected/system process) keep elevated = true,
    // so the hook passes input through instead of swallowing it.

    closeProcess();
    return true;
}

bool IsSelfElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;

    TOKEN_ELEVATION elevation = {};
    DWORD size = sizeof(elevation);
    bool elevated = false;
    if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
        elevated = elevation.TokenIsElevated != 0;
    }
    CloseHandle(token);
    return elevated;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                  nullptr, 0, nullptr, nullptr);
    if (len <= 0) return std::string();
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring TrimW(const std::wstring& s) {
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && (s[begin] == L' ' || s[begin] == L'\t' || s[begin] == L'\r' ||
                           s[begin] == L'\n' || s[begin] == L'﻿')) {
        ++begin;
    }
    while (end > begin && (s[end - 1] == L' ' || s[end - 1] == L'\t' || s[end - 1] == L'\r' ||
                           s[end - 1] == L'\n' || s[end - 1] == L'﻿')) {
        --end;
    }
    return s.substr(begin, end - begin);
}

std::wstring LowerW(const std::wstring& s) {
    std::wstring out = s;
    for (wchar_t& c : out) {
        c = static_cast<wchar_t>(towlower(c));
    }
    return out;
}