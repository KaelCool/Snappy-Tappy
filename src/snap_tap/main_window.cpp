#include "snap_tap/main_window.h"

#include "snap_tap/config.h"
#include "snap_tap/key_codes.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <utility>

// Ask for the version 6 common controls, so the window uses current widget
// styling instead of the Windows 95 look. Doing it here avoids adding a
// resource file and a resource-compiler step to the build.
#pragma comment(linker,                                                    \
                "\"/manifestdependency:type='win32' "                      \
                "name='Microsoft.Windows.Common-Controls' "                \
                "version='6.0.0.0' processorArchitecture='*' "             \
                "publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace snaptap {
namespace {

const wchar_t* const kWindowClassName = L"SnapTapMainWindow";
const wchar_t* const kWindowTitle = L"Snap Tap";

enum ControlId : int {
    kIdEnableCheck = 1001,
    kIdPairsLabel,
    kIdPairList,
    kIdRemoveButton,
    kIdAddLabel,
    kIdFirstCombo,
    kIdArrowLabel,
    kIdSecondCombo,
    kIdAddButton,
    kIdStatusLabel,
    kIdConfigLabel,
};

enum TrayMenuId : int {
    kMenuShow = 2001,
    kMenuEnable,
    kMenuQuit,
};

constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT kTrayIconId = 1;
constexpr UINT_PTR kRefreshTimerId = 1;
constexpr UINT kRefreshIntervalMs = 100;

// Layout in 96-dpi units; every value passes through MainWindow::scaled().
constexpr int kMargin = 12;
constexpr int kClientWidth = 420;
constexpr int kClientHeight = 348;
constexpr int kContentWidth = kClientWidth - (2 * kMargin);
constexpr int kRowHeight = 26;
constexpr int kLabelHeight = 18;

// Key names are ASCII, so widening one character at a time is enough.
std::wstring widen(const std::string& text) {
    return std::wstring(text.begin(), text.end());
}

std::wstring pairLabel(const PairView& pair) {
    return widen(keyNameFromCode(pair.first)) + L"   <->   " + widen(keyNameFromCode(pair.second));
}

bool samePairs(const std::vector<PairView>& lhs, const std::vector<PairView>& rhs) {
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                      [](const PairView& a, const PairView& b) {
                          return a.first == b.first && a.second == b.second;
                      });
}

HWND control(const HWND parent, const int id) {
    return GetDlgItem(parent, id);
}

}  // namespace

MainWindow::MainWindow(KeyboardHook& hook, std::filesystem::path configPath)
    : hook_(hook), configPath_(std::move(configPath)) {}

MainWindow::~MainWindow() {
    removeTrayIcon();
    if (font_ != nullptr) {
        DeleteObject(static_cast<HFONT>(font_));
        font_ = nullptr;
    }
}

int MainWindow::scaled(const int value) const {
    return MulDiv(value, dpi_, 96);
}

namespace {

// The window procedure cannot be a member, so it recovers the instance from the
// window and forwards to it.
LRESULT CALLBACK windowProc(const HWND hwnd, const UINT message, const WPARAM wParam,
                            const LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* const create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    auto* const window =
        reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (window != nullptr) {
        long long result = 0;
        if (window->handleMessage(message, wParam, lParam, result)) {
            return result;
        }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace

bool MainWindow::create() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    windowClass.lpszClassName = kWindowClassName;
    if (RegisterClassExW(&windowClass) == 0) {
        return false;
    }

    const HDC screen = GetDC(nullptr);
    if (screen != nullptr) {
        dpi_ = GetDeviceCaps(screen, LOGPIXELSX);
        ReleaseDC(nullptr, screen);
    }

    // Size the frame so the client area is exactly the layout we drew.
    RECT frame{0, 0, scaled(kClientWidth), scaled(kClientHeight)};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&frame, style, FALSE);

    const HWND hwnd = CreateWindowExW(0, kWindowClassName, kWindowTitle, style, CW_USEDEFAULT,
                                      CW_USEDEFAULT, frame.right - frame.left,
                                      frame.bottom - frame.top, nullptr, nullptr, instance, this);
    if (hwnd == nullptr) {
        return false;
    }
    hwnd_ = hwnd;

    createControls();
    refreshEnabledCheck();
    refreshPairList(true);
    refreshStatus();
    addTrayIcon();

    SetTimer(hwnd, kRefreshTimerId, kRefreshIntervalMs, nullptr);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return true;
}

void MainWindow::createControls() {
    const HWND hwnd = static_cast<HWND>(hwnd_);
    const HINSTANCE instance = GetModuleHandleW(nullptr);

    // Use the same font the rest of the system uses for UI text.
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        font_ = CreateFontIndirectW(&metrics.lfMessageFont);
    }

    const auto make = [&](const wchar_t* const className, const wchar_t* const text,
                          const DWORD style, const int id) {
        return CreateWindowExW(0, className, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, hwnd,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance,
                               nullptr);
    };

    make(L"BUTTON", L"Enable Snap Tap", BS_AUTOCHECKBOX | WS_TABSTOP, kIdEnableCheck);
    make(L"STATIC", L"Managed pairs:", SS_LEFT, kIdPairsLabel);
    make(L"LISTBOX", nullptr, LBS_NOTIFY | WS_BORDER | WS_VSCROLL | WS_TABSTOP, kIdPairList);
    make(L"BUTTON", L"Remove selected", BS_PUSHBUTTON | WS_TABSTOP, kIdRemoveButton);
    make(L"STATIC", L"Add a pair:", SS_LEFT, kIdAddLabel);
    make(L"COMBOBOX", nullptr, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, kIdFirstCombo);
    make(L"STATIC", L"<->", SS_CENTER, kIdArrowLabel);
    make(L"COMBOBOX", nullptr, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, kIdSecondCombo);
    make(L"BUTTON", L"Add pair", BS_PUSHBUTTON | WS_TABSTOP, kIdAddButton);
    make(L"STATIC", L"", SS_LEFT, kIdStatusLabel);
    make(L"STATIC", (L"Config: " + configPath_.wstring()).c_str(), SS_PATHELLIPSIS, kIdConfigLabel);

    // Offer every key the engine understands, in the order key_codes lists them.
    const HWND firstCombo = control(hwnd, kIdFirstCombo);
    const HWND secondCombo = control(hwnd, kIdSecondCombo);
    for (const KeyCode key : allKeys()) {
        const std::wstring name = widen(keyNameFromCode(key));
        SendMessageW(firstCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        SendMessageW(secondCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    }

    // W/S is the obvious next pair once A/D is in place.
    const std::vector<KeyCode>& keys = allKeys();
    const auto indexOf = [&keys](const KeyCode key) {
        const auto it = std::find(keys.begin(), keys.end(), key);
        return it == keys.end() ? 0 : static_cast<int>(std::distance(keys.begin(), it));
    };
    SendMessageW(firstCombo, CB_SETCURSEL, indexOf('W'), 0);
    SendMessageW(secondCombo, CB_SETCURSEL, indexOf('S'), 0);

    if (font_ != nullptr) {
        EnumChildWindows(
            hwnd,
            [](const HWND child, const LPARAM fontParam) -> BOOL {
                SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(fontParam), TRUE);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(font_));
    }

    layoutControls();
}

void MainWindow::layoutControls() {
    const HWND hwnd = static_cast<HWND>(hwnd_);
    const auto place = [&](const int id, const int x, const int y, const int width,
                           const int height) {
        SetWindowPos(control(hwnd, id), nullptr, scaled(x), scaled(y), scaled(width),
                     scaled(height), SWP_NOZORDER);
    };

    int y = kMargin;
    place(kIdEnableCheck, kMargin, y, 200, 20);
    y += 32;
    place(kIdPairsLabel, kMargin, y, 200, kLabelHeight);
    y += kLabelHeight + 4;
    place(kIdPairList, kMargin, y, kContentWidth, 130);
    y += 130 + 6;
    place(kIdRemoveButton, kMargin + kContentWidth - 130, y, 130, kRowHeight);
    y += kRowHeight + 14;
    place(kIdAddLabel, kMargin, y, 200, kLabelHeight);
    y += kLabelHeight + 4;

    // A combo box takes its dropped-down height here, not its closed height.
    place(kIdFirstCombo, kMargin, y, 110, 200);
    place(kIdArrowLabel, kMargin + 116, y + 4, 30, kLabelHeight);
    place(kIdSecondCombo, kMargin + 152, y, 110, 200);
    place(kIdAddButton, kMargin + kContentWidth - 100, y - 1, 100, kRowHeight);
    y += kRowHeight + 12;

    place(kIdStatusLabel, kMargin, y, kContentWidth, kLabelHeight);
    y += kLabelHeight + 2;
    place(kIdConfigLabel, kMargin, y, kContentWidth, kLabelHeight);
}

void MainWindow::refreshPairList(const bool force) {
    std::vector<PairView> pairs;
    hook_.readEngine([&pairs](const SnapTapEngine& engine) { pairs = engine.pairs(); });

    // Rebuilding on every tick would reset the user's selection and flicker, so
    // only touch the list when the pairs themselves changed.
    if (!force && samePairs(pairs, shownPairs_)) {
        shownPairs_ = pairs;
        return;
    }

    const HWND hwnd = static_cast<HWND>(hwnd_);
    const HWND list = control(hwnd, kIdPairList);
    const LRESULT selected = SendMessageW(list, LB_GETCURSEL, 0, 0);

    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const PairView& pair : pairs) {
        const std::wstring label = pairLabel(pair);
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    if (selected != LB_ERR && selected < static_cast<LRESULT>(pairs.size())) {
        SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(selected), 0);
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);

    shownPairs_ = pairs;
    EnableWindow(control(hwnd, kIdRemoveButton), !pairs.empty());
}

void MainWindow::refreshStatus() {
    bool enabled = true;
    std::vector<PairView> pairs;
    hook_.readEngine([&](const SnapTapEngine& engine) {
        enabled = engine.isEnabled();
        pairs = engine.pairs();
    });

    std::wstring text;
    if (!enabled) {
        text = L"Snap Tap is off - keys behave normally.";
    } else if (pairs.empty()) {
        text = L"No pairs configured - add one below.";
    } else {
        std::wstring held;
        for (const PairView& pair : pairs) {
            if (pair.active != kNoKey) {
                if (!held.empty()) {
                    held += L", ";
                }
                held += widen(keyNameFromCode(pair.active));
            }
        }
        text = held.empty() ? L"Watching - nothing held." : L"Holding: " + held;
    }

    // Only touch the control when the text changed, so the label does not flicker
    // ten times a second.
    if (text != lastStatusText_) {
        lastStatusText_ = text;
        SetWindowTextW(control(static_cast<HWND>(hwnd_), kIdStatusLabel), text.c_str());
    }
}

void MainWindow::refreshEnabledCheck() {
    bool enabled = true;
    hook_.readEngine([&enabled](const SnapTapEngine& engine) { enabled = engine.isEnabled(); });
    SendMessageW(control(static_cast<HWND>(hwnd_), kIdEnableCheck), BM_SETCHECK,
                 enabled ? BST_CHECKED : BST_UNCHECKED, 0);
}

void MainWindow::onAddPair() {
    const HWND hwnd = static_cast<HWND>(hwnd_);
    const LRESULT firstIndex = SendMessageW(control(hwnd, kIdFirstCombo), CB_GETCURSEL, 0, 0);
    const LRESULT secondIndex = SendMessageW(control(hwnd, kIdSecondCombo), CB_GETCURSEL, 0, 0);
    if (firstIndex == CB_ERR || secondIndex == CB_ERR) {
        return;
    }

    const std::vector<KeyCode>& keys = allKeys();
    const KeyCode first = keys.at(static_cast<std::size_t>(firstIndex));
    const KeyCode second = keys.at(static_cast<std::size_t>(secondIndex));

    bool added = false;
    hook_.withEngine([&](SnapTapEngine& engine) {
        added = engine.addPair(first, second);
        return std::vector<OutputAction>{};
    });

    if (!added) {
        const std::wstring message =
            first == second
                ? L"Pick two different keys."
                : L"One of those keys already belongs to another pair. Remove that pair first.";
        MessageBoxW(hwnd, message.c_str(), kWindowTitle, MB_OK | MB_ICONINFORMATION);
        return;
    }

    refreshPairList(true);
    saveConfig();
}

void MainWindow::onRemovePair() {
    const HWND hwnd = static_cast<HWND>(hwnd_);
    const LRESULT selected = SendMessageW(control(hwnd, kIdPairList), LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR || selected >= static_cast<LRESULT>(shownPairs_.size())) {
        MessageBoxW(hwnd, L"Select a pair to remove.", kWindowTitle, MB_OK | MB_ICONINFORMATION);
        return;
    }

    const KeyCode key = shownPairs_.at(static_cast<std::size_t>(selected)).first;
    // Anything the pair still holds is released as part of the same call.
    hook_.withEngine([key](SnapTapEngine& engine) {
        RemoveResult result = engine.removePair(key);
        return result.released;
    });

    refreshPairList(true);
    saveConfig();
}

void MainWindow::onToggleEnabled(const bool enabled) {
    hook_.withEngine([enabled](SnapTapEngine& engine) { return engine.setEnabled(enabled); });
    refreshEnabledCheck();
    refreshStatus();
    saveConfig();
}

void MainWindow::saveConfig() {
    Config config;
    hook_.readEngine([&config](const SnapTapEngine& engine) {
        config.enabled = engine.isEnabled();
        for (const PairView& pair : engine.pairs()) {
            config.pairs.push_back(KeyPairConfig{pair.first, pair.second});
        }
    });

    if (!saveConfigFile(configPath_.string(), config)) {
        MessageBoxW(static_cast<HWND>(hwnd_),
                    (L"Could not write the config file:\n" + configPath_.wstring()).c_str(),
                    kWindowTitle, MB_OK | MB_ICONWARNING);
    }
}

void MainWindow::addTrayIcon() {
    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = static_cast<HWND>(hwnd_);
    icon.uID = kTrayIconId;
    icon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    icon.uCallbackMessage = kTrayCallbackMessage;
    icon.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(icon.szTip, kWindowTitle);
    trayIconAdded_ = Shell_NotifyIconW(NIM_ADD, &icon) != FALSE;
}

void MainWindow::removeTrayIcon() {
    if (!trayIconAdded_) {
        return;
    }
    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = static_cast<HWND>(hwnd_);
    icon.uID = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &icon);
    trayIconAdded_ = false;
}

void MainWindow::showTrayMenu() {
    const HWND hwnd = static_cast<HWND>(hwnd_);

    bool enabled = true;
    hook_.readEngine([&enabled](const SnapTapEngine& engine) { enabled = engine.isEnabled(); });

    const HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }
    AppendMenuW(menu, MF_STRING, kMenuShow, L"Show window");
    AppendMenuW(menu, MF_STRING | (enabled ? MF_CHECKED : MF_UNCHECKED), kMenuEnable,
                L"Enable Snap Tap");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuQuit, L"Quit");

    POINT cursor{};
    GetCursorPos(&cursor);
    // Required, or the menu will not dismiss when clicked away from.
    SetForegroundWindow(hwnd);
    const int choice = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                      cursor.x, cursor.y, 0, hwnd, nullptr);
    DestroyMenu(menu);

    switch (choice) {
        case kMenuShow:
            restoreWindow();
            break;
        case kMenuEnable:
            onToggleEnabled(!enabled);
            break;
        case kMenuQuit:
            DestroyWindow(hwnd);
            break;
        default:
            break;
    }
}

void MainWindow::restoreWindow() {
    const HWND hwnd = static_cast<HWND>(hwnd_);
    ShowWindow(hwnd, SW_SHOW);
    ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
}

bool MainWindow::handleMessage(const unsigned int message, const unsigned long long wParam,
                               const long long lParam, long long& result) {
    const HWND hwnd = static_cast<HWND>(hwnd_);
    result = 0;

    switch (message) {
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int notification = HIWORD(wParam);
            if (notification != BN_CLICKED) {
                return false;
            }
            if (id == kIdEnableCheck) {
                const LRESULT checked =
                    SendMessageW(control(hwnd, kIdEnableCheck), BM_GETCHECK, 0, 0);
                onToggleEnabled(checked == BST_CHECKED);
                return true;
            }
            if (id == kIdAddButton) {
                onAddPair();
                return true;
            }
            if (id == kIdRemoveButton) {
                onRemovePair();
                return true;
            }
            return false;
        }

        case WM_CTLCOLORSTATIC: {
            // Keep static text on the window background, and grey out the path.
            const auto dc = reinterpret_cast<HDC>(wParam);
            SetBkMode(dc, TRANSPARENT);
            if (GetDlgCtrlID(reinterpret_cast<HWND>(lParam)) == kIdConfigLabel) {
                SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
            }
            result = reinterpret_cast<long long>(GetSysColorBrush(COLOR_BTNFACE));
            return true;
        }

        case WM_TIMER:
            if (wParam == kRefreshTimerId) {
                refreshPairList(false);
                refreshStatus();
                return true;
            }
            return false;

        case WM_SIZE:
            // Minimising tucks the window into the tray instead of the taskbar.
            if (wParam == SIZE_MINIMIZED && trayIconAdded_) {
                ShowWindow(hwnd, SW_HIDE);
                return true;
            }
            return false;

        case kTrayCallbackMessage:
            if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
                restoreWindow();
                return true;
            }
            if (LOWORD(lParam) == WM_RBUTTONUP) {
                showTrayMenu();
                return true;
            }
            return false;

        case WM_DESTROY:
            KillTimer(hwnd, kRefreshTimerId);
            removeTrayIcon();
            PostQuitMessage(0);
            return true;

        default:
            return false;
    }
}

int MainWindow::run() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        // Lets Tab and the arrow keys move between the controls.
        if (!IsDialogMessageW(static_cast<HWND>(hwnd_), &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return static_cast<int>(message.wParam);
}

}  // namespace snaptap
