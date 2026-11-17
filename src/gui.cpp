#include "gui.h"

#include <commctrl.h>
#include <shellapi.h>

#include <cwchar>
#include <string>
#include <vector>

#include "resource.h"
#include "settings.h"
#include "util.h"

namespace {

enum ControlId {
    IDC_TITLE = 1000,
    IDC_SUBTITLE,
    IDC_STATUS,
    IDC_ENABLE,

    IDC_SMOOTH_TXT,
    IDC_SMOOTH_VAL,
    IDC_SMOOTH,

    IDC_INERTIA_TXT,
    IDC_INERTIA_VAL,
    IDC_INERTIA,

    IDC_SENS_TXT,
    IDC_SENS_VAL,
    IDC_SENS,

    IDC_HORIZONTAL,
    IDC_FULLSCREEN,
    IDC_AUTOSTART,
    IDC_HIGH_PRECISION,
    IDC_ACCELERATION,

    IDC_PREC_HINT,
    IDC_EXCL_TXT,
    IDC_EXCL,
    IDC_HINT,

    IDC_RESET,
    IDC_HIDE,
    IDC_EXIT,
    IDC_ABOUT,
};

enum TrayCommand {
    IDM_TRAY_SHOW = 2100,
    IDM_TRAY_TOGGLE,
    IDM_TRAY_ADMIN,
    IDM_TRAY_EXIT,
};

enum FontRole {
    FontUi = 0,
    FontHeading = 1,
    FontHint = 2,
};

constexpr int kBaseWidth = 540;
constexpr int kBaseHeight = 522;
constexpr int kHeaderHeight = 96;
constexpr UINT_PTR kSaveTimerId = 1;

struct Spec {
    int id;
    const wchar_t* cls;
    const wchar_t* text;
    DWORD style;
    DWORD exStyle;
    int x;
    int y;
    int w;
    int h;
    FontRole font;
};

struct Item {
    HWND hwnd;
    int id;
    RECT rc;
    FontRole font;
};

const Spec kSpecs[] = {
    {IDC_TITLE, L"STATIC", L"winMouseFix", SS_LEFT, 0, 24, 12, 300, 34, FontHeading},
    {IDC_SUBTITLE, L"STATIC", L"V1.0", SS_LEFT, 0, 24, 50, 420, 20,
     FontHint},
    {IDC_STATUS, L"STATIC", L"● 已启用", SS_RIGHT | SS_CENTERIMAGE, 0, 330, 20, 186, 24, FontUi},

    {IDC_ENABLE, L"BUTTON", L"启用滚轮平滑",
     BS_AUTOCHECKBOX | WS_TABSTOP, 0, 24, 110, 492, 24, FontUi},

    {IDC_SMOOTH_TXT, L"STATIC", L"平滑程度", SS_LEFT, 0, 24, 142, 260, 20, FontUi},
    {IDC_SMOOTH_VAL, L"STATIC", L"55", SS_RIGHT, 0, 416, 142, 100, 20, FontUi},
    {IDC_SMOOTH, L"msctls_trackbar32", L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, 24, 162, 492, 28,
     FontUi},

    {IDC_INERTIA_TXT, L"STATIC", L"惯性强度", SS_LEFT, 0, 24, 196, 260, 20, FontUi},
    {IDC_INERTIA_VAL, L"STATIC", L"60", SS_RIGHT, 0, 416, 196, 100, 20, FontUi},
    {IDC_INERTIA, L"msctls_trackbar32", L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, 24, 216, 492, 28,
     FontUi},

    {IDC_SENS_TXT, L"STATIC", L"滚动灵敏度", SS_LEFT, 0, 24, 250, 260, 20, FontUi},
    {IDC_SENS_VAL, L"STATIC", L"100%", SS_RIGHT, 0, 416, 250, 100, 20, FontUi},
    {IDC_SENS, L"msctls_trackbar32", L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, 24, 270, 492, 28,
     FontUi},

    {IDC_FULLSCREEN, L"BUTTON", L"全屏应用/游戏下关闭平滑", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 280, 318, 236,
     24, FontUi},
    {IDC_AUTOSTART, L"BUTTON", L"开机自动启动（最小化到托盘）", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 24,
     318, 236, 24, FontUi},

    {IDC_EXCL_TXT, L"STATIC", L"排除的程序（每行一个进程名，如 chrome；该程序内保持原生滚轮）",
     SS_LEFT, 0, 24, 360, 492, 20, FontUi},
    {IDC_EXCL, L"EDIT", L"", WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_LEFT | WS_TABSTOP,
     WS_EX_CLIENTEDGE, 24, 380, 492, 74, FontUi},

    {IDC_RESET, L"BUTTON", L"恢复默认", BS_PUSHBUTTON | WS_TABSTOP, 0, 24, 468, 110, 34, FontUi},
    {IDC_HIDE, L"BUTTON", L"隐藏到托盘", BS_PUSHBUTTON | WS_TABSTOP, 0, 146, 468, 128, 34, FontUi},
    {IDC_ABOUT, L"BUTTON", L"关于", BS_PUSHBUTTON | WS_TABSTOP, 0, 292, 468, 96, 34, FontUi},
    {IDC_EXIT, L"BUTTON", L"退出程序", BS_PUSHBUTTON | WS_TABSTOP, 0, 406, 468, 110, 34, FontUi},
};

HWND g_hwnd = nullptr;
HINSTANCE g_instance = nullptr;
int g_dpi = 96;
HFONT g_fontUi = nullptr;
HFONT g_fontHeading = nullptr;
HFONT g_fontHint = nullptr;
HBRUSH g_headerBrush = nullptr;
HBRUSH g_accentBrush = nullptr;
std::vector<Item> g_items;
bool g_loading = false;
bool g_trayAdded = false;
bool g_balloonShown = false;

int S(int value) {
    return MulDiv(value, g_dpi, 96);
}

DWORD WindowStyle() {
    return WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
}

DWORD WindowExStyle() {
    return WS_EX_APPWINDOW;
}

HFONT MakeFont(int points, int weight) {
    LOGFONTW lf = {};
    lf.lfHeight = -MulDiv(points, g_dpi, 72);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcsncpy(lf.lfFaceName, L"Microsoft YaHei UI", std::size(lf.lfFaceName) - 1);
    return CreateFontIndirectW(&lf);
}

void DestroyFonts() {
    if (g_fontUi) DeleteObject(g_fontUi);
    if (g_fontHeading) DeleteObject(g_fontHeading);
    if (g_fontHint) DeleteObject(g_fontHint);
    g_fontUi = g_fontHeading = g_fontHint = nullptr;

    if (g_headerBrush) DeleteObject(g_headerBrush);
    if (g_accentBrush) DeleteObject(g_accentBrush);
    g_headerBrush = g_accentBrush = nullptr;
}

void CreateFonts() {
    DestroyFonts();
    g_fontUi = MakeFont(9, FW_NORMAL);
    g_fontHeading = MakeFont(16, FW_SEMIBOLD);
    g_fontHint = MakeFont(8, FW_NORMAL);

    g_headerBrush = CreateSolidBrush(RGB(30, 37, 51));
    g_accentBrush = CreateSolidBrush(RGB(76, 141, 255));
}

void ApplyFonts() {
    for (const Item& item : g_items) {
        HFONT font = g_fontUi;
        if (item.font == FontHeading) font = g_fontHeading;
        if (item.font == FontHint) font = g_fontHint;
        SendMessageW(item.hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

void LayoutControls() {
    for (const Item& item : g_items) {
        SetWindowPos(item.hwnd, nullptr, S(item.rc.left), S(item.rc.top),
                     S(item.rc.right - item.rc.left), S(item.rc.bottom - item.rc.top),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

HWND GetItem(int id) {
    return GetDlgItem(g_hwnd, id);
}

std::wstring GetText(int id) {
    HWND control = GetItem(id);
    if (!control) return std::wstring();
    int length = GetWindowTextLengthW(control);
    if (length <= 0) return std::wstring();
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

void SetText(int id, const wchar_t* text) {
    SetDlgItemTextW(g_hwnd, id, text);
}

bool IsChecked(int id) {
    HWND control = GetItem(id);
    if (!control) return false;
    return SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void UpdateValueLabels(const Settings& s) {
    wchar_t buffer[32] = {};
    swprintf(buffer, std::size(buffer), L"%d", s.smoothness);
    SetDlgItemTextW(g_hwnd, IDC_SMOOTH_VAL, buffer);
    swprintf(buffer, std::size(buffer), L"%d", s.inertia);
    SetDlgItemTextW(g_hwnd, IDC_INERTIA_VAL, buffer);
    swprintf(buffer, std::size(buffer), L"%d%%", s.sensitivity);
    SetDlgItemTextW(g_hwnd, IDC_SENS_VAL, buffer);
}

void UpdateTrayTip(bool enabled) {
    if (!g_trayAdded) return;
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_TIP;
    std::wstring tip = enabled ? L"winMouseFix V1.0 · 滚轮平滑已启用" : L"winMouseFix V1.0 · 已暂停";
    wcsncpy(nid.szTip, tip.c_str(), std::size(nid.szTip) - 1);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void UpdateUIFromSettings(const Settings& s) {
    if (!g_hwnd) return;

    g_loading = true;
    auto setCheck = [&](int id, bool value) {
        SendDlgItemMessageW(g_hwnd, id, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
    };
    setCheck(IDC_ENABLE, s.enabled);
    setCheck(IDC_FULLSCREEN, s.fullscreenBypass);
    setCheck(IDC_AUTOSTART, s.autostart);

    SendDlgItemMessageW(g_hwnd, IDC_SMOOTH, TBM_SETPOS, TRUE, s.smoothness);
    SendDlgItemMessageW(g_hwnd, IDC_INERTIA, TBM_SETPOS, TRUE, s.inertia);
    SendDlgItemMessageW(g_hwnd, IDC_SENS, TBM_SETPOS, TRUE, s.sensitivity);
    UpdateValueLabels(s);

    if (GetText(IDC_EXCL) != s.exclusions) {
        SetText(IDC_EXCL, s.exclusions.c_str());
    }

    SetText(IDC_STATUS, s.enabled ? L"● 已启用" : L"● 已暂停");
    InvalidateRect(GetItem(IDC_STATUS), nullptr, TRUE);
    UpdateTrayTip(s.enabled);
    g_loading = false;
}

void ScheduleSave() {
    if (!g_hwnd) return;
    KillTimer(g_hwnd, kSaveTimerId);
    SetTimer(g_hwnd, kSaveTimerId, 600, nullptr);
}

void HideToTray() {
    if (!g_hwnd) return;
    ShowWindow(g_hwnd, SW_HIDE);
    if (g_trayAdded && !g_balloonShown) {
        g_balloonShown = true;
        NOTIFYICONDATAW nid = {};
        nid.cbSize = sizeof(nid);
        nid.hWnd = g_hwnd;
        nid.uID = 1;
        nid.uFlags = NIF_INFO;
        nid.dwInfoFlags = NIIF_INFO;
        wcsncpy(nid.szInfoTitle, L"winMouseFix V1.0 仍在运行", std::size(nid.szInfoTitle) - 1);
        wcsncpy(nid.szInfo, L"已最小化到系统托盘，双击图标可打开设置。", std::size(nid.szInfo) - 1);
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
}

void ApplyCheckboxChange(int id, bool checked) {
    Settings s = GetSettings();
    switch (id) {
        case IDC_ENABLE:
            s.enabled = checked;
            break;
        case IDC_FULLSCREEN:
            s.fullscreenBypass = checked;
            break;
        case IDC_AUTOSTART:
            if (!SetAutostart(checked)) {
                MessageBoxW(g_hwnd, L"无法写入开机启动项，请检查系统安全策略。", L"winMouseFix",
                            MB_ICONWARNING | MB_OK);
            }
            s.autostart = IsAutostartEnabled();
            ApplySettings(s);
            SaveSettings(s);
            UpdateUIFromSettings(s);
            return;
        default:
            return;
    }

    ApplySettings(s);
    UpdateUIFromSettings(s);
    ScheduleSave();
}

void ApplySliderChange(int id) {
    HWND control = GetItem(id);
    if (!control) return;
    int position = static_cast<int>(SendMessageW(control, TBM_GETPOS, 0, 0));
    Settings s = GetSettings();

    switch (id) {
        case IDC_SMOOTH:
            s.smoothness = position;
            break;
        case IDC_INERTIA:
            s.inertia = position;
            break;
        case IDC_SENS:
            s.sensitivity = position;
            break;
        default:
            return;
    }

    ApplySettings(s);
    UpdateValueLabels(s);
    ScheduleSave();
}

void ApplyExclusionsChange() {
    if (g_loading) return;
    Settings s = GetSettings();
    s.exclusions = GetText(IDC_EXCL);
    ApplySettings(s);
    ScheduleSave();
}

void ApplyDefaultSettings() {
    Settings s = GetSettings();
    Settings defaults;
    s.enabled = true;
    s.horizontal = defaults.horizontal;
    s.fullscreenBypass = defaults.fullscreenBypass;
    s.highPrecision = defaults.highPrecision;
    s.acceleration = defaults.acceleration;
    s.smoothness = defaults.smoothness;
    s.inertia = defaults.inertia;
    s.sensitivity = defaults.sensitivity;
    s.exclusions.clear();

    ApplySettings(s);
    UpdateUIFromSettings(GetSettings());
    ScheduleSave();
}

void ToggleEnabled() {
    Settings s = GetSettings();
    s.enabled = !s.enabled;
    ApplySettings(s);
    SaveSettings(s);
    UpdateUIFromSettings(s);
}

void AddTrayIcon() {
    HICON icon = LoadIconW(g_instance, MAKEINTRESOURCEW(IDI_APP));
    if (!icon) icon = LoadIconW(nullptr, IDI_APPLICATION);

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = kMessageTray;
    nid.hIcon = icon;
    wcsncpy(nid.szTip, L"winMouseFix V1.0 · 滚轮平滑已启用", std::size(nid.szTip) - 1);
    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
}

void ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    Settings s = GetSettings();
    AppendMenuW(menu, MF_STRING, IDM_TRAY_SHOW, L"显示设置");
    AppendMenuW(menu, MF_STRING | (s.enabled ? MF_CHECKED : 0), IDM_TRAY_TOGGLE,
                L"启用滚轮平滑");
    if (!IsSelfElevated()) {
        AppendMenuW(menu, MF_STRING, IDM_TRAY_ADMIN, L"以管理员身份重启");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT, L"退出程序");

    SetForegroundWindow(g_hwnd);
    POINT cursor = {};
    GetCursorPos(&cursor);
    UINT command = static_cast<UINT>(TrackPopupMenu(
        menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, cursor.x, cursor.y, 0, g_hwnd,
        nullptr));
    DestroyMenu(menu);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);

    switch (command) {
        case IDM_TRAY_SHOW:
            ShowMainWindow();
            break;
        case IDM_TRAY_TOGGLE:
            ToggleEnabled();
            break;
        case IDM_TRAY_ADMIN: {
            std::wstring exe = GetExecutablePath();
            if (!exe.empty()) {
                INT_PTR result = reinterpret_cast<INT_PTR>(
                    ShellExecuteW(nullptr, L"runas", exe.c_str(), L"--replace", nullptr,
                                  SW_SHOWNORMAL));
                if (result > 32) {
                    // The new elevated process waits for our mutex, so quit.
                    DestroyWindow(g_hwnd);
                }
            }
            break;
        }
        case IDM_TRAY_EXIT:
            DestroyWindow(g_hwnd);
            break;
        default:
            break;
    }
}

void CreateUi(HWND hwnd) {
    g_dpi = GetDpiForWindow(hwnd);
    CreateFonts();
    g_items.clear();
    g_items.reserve(std::size(kSpecs));

    for (const Spec& spec : kSpecs) {
        HWND control = CreateWindowExW(
            spec.exStyle, spec.cls, spec.text, WS_CHILD | WS_VISIBLE | spec.style, 0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(spec.id)), g_instance, nullptr);
        if (!control) continue;
        RECT rc = {spec.x, spec.y, spec.x + spec.w, spec.y + spec.h};
        g_items.push_back(Item{control, spec.id, rc, spec.font});
    }

    ApplyFonts();
    LayoutControls();

    SendDlgItemMessageW(hwnd, IDC_SMOOTH, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendDlgItemMessageW(hwnd, IDC_INERTIA, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendDlgItemMessageW(hwnd, IDC_SENS, TBM_SETRANGE, TRUE, MAKELPARAM(25, 300));
    SendDlgItemMessageW(hwnd, IDC_SMOOTH, TBM_SETPAGESIZE, 0, 10);
    SendDlgItemMessageW(hwnd, IDC_INERTIA, TBM_SETPAGESIZE, 0, 10);
    SendDlgItemMessageW(hwnd, IDC_SENS, TBM_SETPAGESIZE, 0, 25);

    UpdateUIFromSettings(GetSettings());
    AddTrayIcon();
}

const wchar_t kAboutClass[] = L"winMouseFixAboutWindow";
const wchar_t kProjectUrl[] = L"https://github.com/Dreams0/winMouseFix";
constexpr int kAboutOpenId = 9001;
constexpr int kAboutCloseId = 9002;
HWND g_aboutWnd = nullptr;

void OpenProjectPage() {
    ShellExecuteW(nullptr, L"open", kProjectUrl, nullptr, nullptr, SW_SHOWNORMAL);
}

LRESULT CALLBACK AboutWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            HICON icon = LoadIconW(g_instance, MAKEINTRESOURCEW(IDI_APP));
            HWND iconCtl = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ICON,
                                           S(24), S(20), S(40), S(40), hwnd, nullptr, g_instance,
                                           nullptr);
            if (iconCtl && icon) {
                SendMessageW(iconCtl, STM_SETICON, reinterpret_cast<WPARAM>(icon), 0);
            }

            HWND title = CreateWindowExW(0, L"STATIC", L"winMouseFix V1.0", WS_CHILD | WS_VISIBLE,
                                         S(80), S(18), S(330), S(34), hwnd, nullptr, g_instance,
                                         nullptr);
            if (title) SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontHeading), TRUE);

            HWND subtitle = CreateWindowExW(0, L"STATIC", L"Windows 滚轮平滑与惯性增强",
                                            WS_CHILD | WS_VISIBLE, S(80), S(54), S(330), S(18),
                                            hwnd, nullptr, g_instance, nullptr);
            if (subtitle) {
                SendMessageW(subtitle, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontHint), TRUE);
            }

            HWND author = CreateWindowExW(0, L"STATIC", L"作者：Dreams0", WS_CHILD | WS_VISIBLE,
                                          S(24), S(94), S(392), S(20), hwnd, nullptr, g_instance,
                                          nullptr);
            if (author) SendMessageW(author, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontUi), TRUE);

            HWND link = CreateWindowExW(0, L"STATIC",
                                        L"项目主页：https://github.com/Dreams0/winMouseFix",
                                        WS_CHILD | WS_VISIBLE, S(24), S(120), S(392), S(20), hwnd,
                                        nullptr, g_instance, nullptr);
            if (link) SendMessageW(link, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontUi), TRUE);

            CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ, S(24), S(158),
                            S(392), S(1), hwnd, nullptr, g_instance, nullptr);

            HWND openBtn = CreateWindowExW(
                0, L"BUTTON", L"打开项目主页", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                S(24), S(174), S(150), S(34), hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAboutOpenId)), g_instance, nullptr);
            HWND closeBtn = CreateWindowExW(
                0, L"BUTTON", L"关闭", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP, S(326),
                S(174), S(90), S(34), hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAboutCloseId)), g_instance, nullptr);
            if (openBtn) SendMessageW(openBtn, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontUi), TRUE);
            if (closeBtn) SendMessageW(closeBtn, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontUi), TRUE);
            SetFocus(closeBtn);
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(35, 40, 50));
            return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case kAboutOpenId:
                    OpenProjectPage();
                    return 0;
                case kAboutCloseId:
                    DestroyWindow(hwnd);
                    return 0;
                default:
                    break;
            }
            break;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (g_aboutWnd == hwnd) g_aboutWnd = nullptr;
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void ShowAboutWindow() {
    if (g_aboutWnd) {
        ShowWindow(g_aboutWnd, SW_SHOW);
        SetForegroundWindow(g_aboutWnd);
        return;
    }

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = AboutWndProc;
        wc.hInstance = g_instance;
        wc.hIcon = LoadIconW(g_instance, MAKEINTRESOURCEW(IDI_APP));
        if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
        wc.lpszClassName = kAboutClass;
        wc.hIconSm = wc.hIcon;
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    constexpr DWORD kAboutStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    constexpr DWORD kAboutExStyle = WS_EX_DLGMODALFRAME;
    RECT client = {0, 0, S(440), S(230)};
    AdjustWindowRectExForDpi(&client, kAboutStyle, FALSE, kAboutExStyle, g_dpi);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;

    RECT owner = {};
    GetWindowRect(g_hwnd, &owner);
    const int x = owner.left + ((owner.right - owner.left) - width) / 2;
    const int y = owner.top + ((owner.bottom - owner.top) - height) / 2;

    g_aboutWnd = CreateWindowExW(kAboutExStyle, kAboutClass, L"关于 winMouseFix", kAboutStyle, x, y,
                                 width, height, g_hwnd, nullptr, g_instance, nullptr);
    if (g_aboutWnd) {
        ShowWindow(g_aboutWnd, SW_SHOW);
        UpdateWindow(g_aboutWnd);
        SetForegroundWindow(g_aboutWnd);
    }
}


LRESULT WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            g_hwnd = hwnd;
            app::g_mainWnd = hwnd;
            CreateUi(hwnd);
            return 0;

        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int notification = HIWORD(wParam);

            if ((id == IDC_ENABLE || id == IDC_FULLSCREEN || id == IDC_AUTOSTART) &&
                notification == BN_CLICKED && !g_loading) {
                ApplyCheckboxChange(id, IsChecked(id));
                return 0;
            }

            if (id == IDC_EXCL && notification == EN_CHANGE) {
                ApplyExclusionsChange();
                return 0;
            }

            if (notification == BN_CLICKED && !g_loading) {
                switch (id) {
                    case IDC_RESET:
                        ApplyDefaultSettings();
                        return 0;
                    case IDC_HIDE:
                        HideToTray();
                        return 0;
                    case IDC_ABOUT:
                        ShowAboutWindow();
                        return 0;
                    case IDC_EXIT:
                        DestroyWindow(hwnd);
                        return 0;
                    default:
                        break;
                }
            }
            break;
        }

        case WM_HSCROLL: {
            HWND control = reinterpret_cast<HWND>(lParam);
            if (!control) break;
            int id = GetDlgCtrlID(control);
            if (id == IDC_SMOOTH || id == IDC_INERTIA || id == IDC_SENS) {
                ApplySliderChange(id);
                return 0;
            }
            break;
        }

        case WM_TIMER:
            if (wParam == kSaveTimerId) {
                KillTimer(hwnd, kSaveTimerId);
                SaveSettings(GetSettings());
                return 0;
            }
            break;

        case WM_CLOSE:
            HideToTray();
            return 0;

        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_MINIMIZE) {
                HideToTray();
                return 0;
            }
            break;

        case WM_ERASEBKGND: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            RECT client = {};
            GetClientRect(hwnd, &client);
            FillRect(dc, &client, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            RECT header = {0, 0, client.right, S(kHeaderHeight)};
            if (g_headerBrush) FillRect(dc, &header, g_headerBrush);
            RECT strip = {0, S(kHeaderHeight) - S(3), client.right, S(kHeaderHeight)};
            if (g_accentBrush) FillRect(dc, &strip, g_accentBrush);
            return 1;
        }

        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            HWND control = reinterpret_cast<HWND>(lParam);
            int id = GetDlgCtrlID(control);
            SetBkMode(dc, TRANSPARENT);

            if (id == IDC_TITLE) {
                SetTextColor(dc, RGB(255, 255, 255));
                return reinterpret_cast<LRESULT>(g_headerBrush);
            }
            if (id == IDC_SUBTITLE) {
                SetTextColor(dc, RGB(166, 177, 198));
                return reinterpret_cast<LRESULT>(g_headerBrush);
            }
            if (id == IDC_STATUS) {
                SetTextColor(dc, app::g_runtimeEnabled.load(std::memory_order_relaxed)
                                     ? RGB(92, 216, 158)
                                     : RGB(242, 182, 96));
                return reinterpret_cast<LRESULT>(g_headerBrush);
            }
            if (id == IDC_EXCL_TXT) {
                SetTextColor(dc, RGB(115, 118, 125));
            }
            return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
        }

        case WM_DPICHANGED: {
            g_dpi = LOWORD(wParam);
            CreateFonts();
            ApplyFonts();

            RECT desired = {0, 0, S(kBaseWidth), S(kBaseHeight)};
            AdjustWindowRectExForDpi(&desired, WindowStyle(), FALSE, WindowExStyle(), g_dpi);
            RECT* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         desired.right - desired.left, desired.bottom - desired.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            LayoutControls();
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }

        case kMessageShowWindow:
            ShowMainWindow();
            return 0;

        case kMessageTray:
            switch (LOWORD(lParam)) {
                case WM_LBUTTONUP:
                case WM_LBUTTONDBLCLK:
                    ShowMainWindow();
                    return 0;
                case WM_RBUTTONUP:
                    ShowTrayMenu();
                    return 0;
                default:
                    break;
            }
            break;

        case WM_DESTROY:
            SaveSettingsNow();
            RemoveTrayIcon();
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace

HWND CreateMainWindow(HINSTANCE instance) {
    g_instance = instance;
    app::g_hInst = instance;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    wc.lpszClassName = app::kWndClass;
    wc.hIconSm = wc.hIcon;

    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return nullptr;
    }

    const int systemDpi = GetDpiForSystem();
    const int previousDpi = g_dpi;
    g_dpi = systemDpi;
    RECT desired = {0, 0, S(kBaseWidth), S(kBaseHeight)};
    AdjustWindowRectExForDpi(&desired, WindowStyle(), FALSE, WindowExStyle(), systemDpi);
    const int width = desired.right - desired.left;
    const int height = desired.bottom - desired.top;

    RECT work = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + ((work.right - work.left) - width) / 2;
    const int y = work.top + ((work.bottom - work.top) - height) / 2;
    g_dpi = previousDpi;

    HWND hwnd = CreateWindowExW(WindowExStyle(), app::kWndClass, app::kAppTitle, WindowStyle(),
                                x, y, width, height, nullptr, nullptr, instance, nullptr);
    if (hwnd) {
        app::g_mainWnd = hwnd;
        g_hwnd = hwnd;
    }
    return hwnd;
}

void ShowMainWindow() {
    if (!g_hwnd) return;
    if (IsIconic(g_hwnd)) ShowWindow(g_hwnd, SW_RESTORE);
    ShowWindow(g_hwnd, SW_SHOW);
    SetForegroundWindow(g_hwnd);
}

void SaveSettingsNow() {
    if (!g_hwnd) return;
    KillTimer(g_hwnd, kSaveTimerId);
    SaveSettings(GetSettings());
}

void RemoveTrayIcon() {
    if (!g_trayAdded) return;
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_trayAdded = false;
}