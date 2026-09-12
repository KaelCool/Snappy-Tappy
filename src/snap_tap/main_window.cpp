#include "snap_tap/main_window.h"

#include "snap_tap/config.h"
#include "snap_tap/key_codes.h"
#include "snap_tap/painting.h"
#include "snap_tap/theme.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <utility>

namespace snaptap {
namespace {

const wchar_t* const kWindowClassName = L"SnapTapCommandDeck";
const wchar_t* const kWindowTitle = L"Snap Tap";

enum ControlId : int {
    kIdEnableToggle = 1001,
    kIdPairList,
    kIdRemoveButton,
    kIdFirstPicker,
    kIdSecondPicker,
    kIdAddButton,
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
constexpr UINT_PTR kListSubclassId = 1;
constexpr UINT_PTR kPickerSubclassId = 2;

// DWM attributes, spelled out because the SDK guards them behind newer targets.
constexpr DWORD kUseImmersiveDarkMode = 20;
constexpr DWORD kUseImmersiveDarkModeLegacy = 19;
constexpr DWORD kWindowCornerPreference = 33;
constexpr DWORD kCornerPreferenceRound = 2;

Rect toRect(const RECT& rect) {
    return Rect{rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
}

bool hit(const Rect& rect, const int x, const int y) {
    return x >= rect.x && x < rect.right() && y >= rect.y && y < rect.bottom();
}

std::wstring widen(const std::string& text) {
    return std::wstring(text.begin(), text.end());
}

HWND control(const HWND parent, const int id) {
    return GetDlgItem(parent, id);
}

MainWindow* fromHwnd(const HWND hwnd) {
    return reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

LRESULT CALLBACK windowProc(const HWND hwnd, const UINT message, const WPARAM wParam,
                            const LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* const create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    MainWindow* const window = fromHwnd(hwnd);
    if (window != nullptr) {
        long long result = 0;
        if (window->handleMessage(message, wParam, lParam, result)) {
            return result;
        }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

// The list knows about rows, not about the remove glyph drawn inside one.
LRESULT CALLBACK listSubclassProc(const HWND hwnd, const UINT message, const WPARAM wParam,
                                  const LPARAM lParam, const UINT_PTR id,
                                  const DWORD_PTR reference) {
    MainWindow* const window = reinterpret_cast<MainWindow*>(reference);
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, listSubclassProc, id);
    } else if (message == WM_LBUTTONDOWN && window != nullptr) {
        if (window->handleListClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam))) {
            return 0;
        }
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

// Owner-draw covers a combo box's items but not its frame, which the system
// would paint in the light theme, so the closed state is painted here instead.
LRESULT CALLBACK pickerSubclassProc(const HWND hwnd, const UINT message, const WPARAM wParam,
                                    const LPARAM lParam, const UINT_PTR id,
                                    const DWORD_PTR reference) {
    MainWindow* const window = reinterpret_cast<MainWindow*>(reference);
    switch (message) {
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, pickerSubclassProc, id);
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            if (window != nullptr) {
                window->paintPicker(hwnd);
                return 0;
            }
            break;
        default:
            break;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

}  // namespace

MainWindow::MainWindow(KeyboardHook& hook, std::filesystem::path configPath)
    : hook_(hook), configPath_(std::move(configPath)) {}

MainWindow::~MainWindow() {
    removeTrayIcon();
}

bool MainWindow::create() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = nullptr;  // WM_PAINT covers every pixel
    windowClass.lpszClassName = kWindowClassName;
    if (RegisterClassExW(&windowClass) == 0) {
        return false;
    }

    const HDC screen = GetDC(nullptr);
    if (screen != nullptr) {
        dpi_ = GetDeviceCaps(screen, LOGPIXELSX);
        ReleaseDC(nullptr, screen);
    }

    gdiPlus_ = std::make_unique<GdiPlusSession>();
    fonts_ = std::make_unique<Fonts>(dpi_);
    brushes_ = std::make_unique<Brushes>();
    icon_ = std::make_unique<AppIcon>();

    std::size_t pairCount = 0;
    hook_.readEngine([&pairCount](const SnapTapEngine& engine) { pairCount = engine.pairCount(); });
    layout_ = computeLayout(dpi_, pairCount);

    // WM_NCCALCSIZE hands the whole window to the client area, so the created
    // size is the layout size exactly, with no frame to adjust for.
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    const HWND hwnd = CreateWindowExW(0, kWindowClassName, kWindowTitle, style, CW_USEDEFAULT,
                                      CW_USEDEFAULT, layout_.windowWidth, layout_.windowHeight,
                                      nullptr, nullptr, instance, this);
    if (hwnd == nullptr) {
        return false;
    }
    hwnd_ = hwnd;

    // Belt and braces: the caption is ours, but a dark one behind it means no
    // white flash while the window is being created.
    const BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(hwnd, kUseImmersiveDarkMode, &dark, sizeof(dark)))) {
        DwmSetWindowAttribute(hwnd, kUseImmersiveDarkModeLegacy, &dark, sizeof(dark));
    }
    const DWORD corners = kCornerPreferenceRound;
    DwmSetWindowAttribute(hwnd, kWindowCornerPreference, &corners, sizeof(corners));

    // One pixel of frame keeps the drop shadow that WM_NCCALCSIZE would remove.
    const MARGINS shadow{0, 0, 1, 0};
    DwmExtendFrameIntoClientArea(hwnd, &shadow);

    // The DWM attributes above land on an already-created window, and the dark
    // caption in particular is not applied until the frame is recalculated.
    // Without this the first show wears the pre-DWM frame; minimising to the
    // tray and back forced the recalculation and quietly fixed it.
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    if (icon_->largeIcon() != nullptr) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG,
                     reinterpret_cast<LPARAM>(icon_->largeIcon()));
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL,
                     reinterpret_cast<LPARAM>(icon_->smallIcon()));
    }

    createControls();
    refreshFromEngine(true);
    addTrayIcon();

    SetTimer(hwnd, kRefreshTimerId, kRefreshIntervalMs, nullptr);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return true;
}

void MainWindow::createControls() {
    const HWND hwnd = static_cast<HWND>(hwnd_);
    const HINSTANCE instance = GetModuleHandleW(nullptr);

    const auto make = [&](const wchar_t* const className, const DWORD style, const int id) {
        return CreateWindowExW(0, className, nullptr, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
                               hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance,
                               nullptr);
    };

    make(L"BUTTON", BS_OWNERDRAW | WS_TABSTOP, kIdEnableToggle);
    const HWND list =
        make(L"LISTBOX", LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP,
             kIdPairList);
    make(L"BUTTON", BS_OWNERDRAW | WS_TABSTOP, kIdRemoveButton);
    const HWND first =
        make(L"COMBOBOX", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL |
                              WS_TABSTOP,
             kIdFirstPicker);
    const HWND second =
        make(L"COMBOBOX", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL |
                              WS_TABSTOP,
             kIdSecondPicker);
    make(L"BUTTON", BS_OWNERDRAW | WS_TABSTOP, kIdAddButton);

    // Undocumented, cosmetic: without it the list scrollbar is a white stripe
    // down the side of a dark window. It only appears past six pairs.
    SetWindowTheme(list, L"DarkMode_Explorer", nullptr);
    SetWindowSubclass(list, listSubclassProc, kListSubclassId,
                      reinterpret_cast<DWORD_PTR>(this));

    for (const HWND picker : {first, second}) {
        SetWindowTheme(picker, L"DarkMode_CFD", nullptr);
        SetWindowSubclass(picker, pickerSubclassProc, kPickerSubclassId,
                          reinterpret_cast<DWORD_PTR>(this));
        for (const KeyCode key : allKeys()) {
            const std::wstring name = widen(keyNameFromCode(key));
            SendMessageW(picker, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        }
    }

    // W/S is the obvious next pair once A/D is in place.
    const std::vector<KeyCode>& keys = allKeys();
    const auto indexOf = [&keys](const KeyCode key) {
        const auto it = std::find(keys.begin(), keys.end(), key);
        return it == keys.end() ? 0 : static_cast<int>(std::distance(keys.begin(), it));
    };
    SendMessageW(first, CB_SETCURSEL, indexOf('W'), 0);
    SendMessageW(second, CB_SETCURSEL, indexOf('S'), 0);

    applyLayout();
}

void MainWindow::applyLayout() {
    const HWND hwnd = static_cast<HWND>(hwnd_);
    const auto place = [&](const int id, const Rect& rect, const int heightOverride = 0) {
        const int height = heightOverride > 0 ? heightOverride : rect.height;
        SetWindowPos(control(hwnd, id), nullptr, rect.x, rect.y, rect.width, height, SWP_NOZORDER);
    };

    place(kIdEnableToggle, layout_.enableToggle);
    place(kIdPairList, layout_.pairList);
    place(kIdRemoveButton, layout_.removeButton);
    place(kIdAddButton, layout_.addButton);

    // A combo box takes its dropped-down height here, not its closed height.
    const int droppedHeight = layout_.firstPicker.height + scaleForDpi(220, dpi_);
    place(kIdFirstPicker, layout_.firstPicker, droppedHeight);
    place(kIdSecondPicker, layout_.secondPicker, droppedHeight);
}

void MainWindow::rebuildLayout() {
    const HWND hwnd = static_cast<HWND>(hwnd_);
    const WindowLayout updated = computeLayout(dpi_, shownPairs_.size());
    const bool resized = updated.windowHeight != layout_.windowHeight;
    layout_ = updated;

    if (resized) {
        SetWindowPos(hwnd, nullptr, 0, 0, layout_.windowWidth, layout_.windowHeight,
                     SWP_NOZORDER | SWP_NOMOVE);
    }
    applyLayout();
    InvalidateRect(hwnd, nullptr, TRUE);
}

// ------------------------------------------------------------------- state

void MainWindow::refreshFromEngine(const bool force) {
    const HWND hwnd = static_cast<HWND>(hwnd_);

    bool enabled = true;
    std::vector<PairView> pairs;
    hook_.readEngine([&](const SnapTapEngine& engine) {
        enabled = engine.isEnabled();
        pairs = engine.pairs();
    });

    const bool sameKeys =
        std::equal(pairs.begin(), pairs.end(), shownPairs_.begin(), shownPairs_.end(),
                   [](const PairView& a, const PairView& b) {
                       return a.first == b.first && a.second == b.second;
                   });
    const bool sameActive =
        std::equal(pairs.begin(), pairs.end(), shownPairs_.begin(), shownPairs_.end(),
                   [](const PairView& a, const PairView& b) { return a.active == b.active; });

    // Which key is winning changes constantly while typing; the set of pairs
    // almost never does. Only rebuild the list for the latter.
    const bool rebuild = force || !sameKeys;
    shownPairs_ = pairs;

    if (rebuild) {
        const HWND list = control(hwnd, kIdPairList);
        const LRESULT selected = SendMessageW(list, LB_GETCURSEL, 0, 0);

        SendMessageW(list, WM_SETREDRAW, FALSE, 0);
        SendMessageW(list, LB_RESETCONTENT, 0, 0);
        for (const PairView& pair : pairs) {
            const std::wstring label = widen(keyNameFromCode(pair.first) + "/" +
                                             keyNameFromCode(pair.second));
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        if (selected != LB_ERR && selected < static_cast<LRESULT>(pairs.size())) {
            SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(selected), 0);
        }
        SendMessageW(list, WM_SETREDRAW, TRUE, 0);

        EnableWindow(control(hwnd, kIdRemoveButton), !pairs.empty());
        rebuildLayout();
    } else if (!sameActive) {
        InvalidateRect(control(hwnd, kIdPairList), nullptr, FALSE);
    }

    // --- status line ---
    std::wstring heading;
    std::wstring detail;
    if (!enabled) {
        heading = L"Snap Tap is off";
        detail = L"Your keys behave normally.";
    } else {
        heading = L"Snap Tap is on";

        const PairView* held = nullptr;
        for (const PairView& pair : pairs) {
            if (pair.active != kNoKey) {
                held = &pair;
                break;
            }
        }
        if (pairs.empty()) {
            detail = L"No pairs yet - add one below.";
        } else if (held == nullptr) {
            detail = L"Watching - nothing held.";
        } else {
            const KeyCode waiting = held->active == held->first ? held->second : held->first;
            detail = L"Holding " + widen(keyNameFromCode(held->active)) + L"  -  " +
                     widen(keyNameFromCode(waiting)) + L" is waiting";
        }
    }

    if (force || heading != statusHeading_ || detail != statusDetail_ || enabled != shownEnabled_) {
        statusHeading_ = heading;
        statusDetail_ = detail;
        const bool toggleChanged = enabled != shownEnabled_;
        shownEnabled_ = enabled;

        const RECT card{layout_.statusCard.x, layout_.statusCard.y, layout_.statusCard.right(),
                        layout_.statusCard.bottom()};
        InvalidateRect(hwnd, &card, FALSE);
        if (toggleChanged) {
            InvalidateRect(control(hwnd, kIdEnableToggle), nullptr, FALSE);
        }
    }
}

// ------------------------------------------------------------------ actions

void MainWindow::onAddPair() {
    const HWND hwnd = static_cast<HWND>(hwnd_);
    const LRESULT firstIndex = SendMessageW(control(hwnd, kIdFirstPicker), CB_GETCURSEL, 0, 0);
    const LRESULT secondIndex = SendMessageW(control(hwnd, kIdSecondPicker), CB_GETCURSEL, 0, 0);
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
        const wchar_t* const message =
            first == second
                ? L"Pick two different keys."
                : L"One of those keys already belongs to another pair. Remove that pair first.";
        MessageBoxW(hwnd, message, kWindowTitle, MB_OK | MB_ICONINFORMATION);
        return;
    }

    refreshFromEngine(true);
    saveConfig();
}

void MainWindow::onRemovePairAt(const std::size_t index) {
    if (index >= shownPairs_.size()) {
        return;
    }
    const KeyCode key = shownPairs_.at(index).first;

    // Anything the pair still holds is released as part of the same call.
    hook_.withEngine([key](SnapTapEngine& engine) {
        RemoveResult result = engine.removePair(key);
        return result.released;
    });

    refreshFromEngine(true);
    saveConfig();
}

void MainWindow::onRemoveSelected() {
    const HWND hwnd = static_cast<HWND>(hwnd_);
    const LRESULT selected = SendMessageW(control(hwnd, kIdPairList), LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR || selected >= static_cast<LRESULT>(shownPairs_.size())) {
        MessageBoxW(hwnd, L"Select a pair to remove.", kWindowTitle, MB_OK | MB_ICONINFORMATION);
        return;
    }
    onRemovePairAt(static_cast<std::size_t>(selected));
}

void MainWindow::onToggleEnabled(const bool enabled) {
    hook_.withEngine([enabled](SnapTapEngine& engine) { return engine.setEnabled(enabled); });
    refreshFromEngine(true);
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

bool MainWindow::handleListClick(const int x, const int y) {
    const HWND hwnd = static_cast<HWND>(hwnd_);
    const HWND list = control(hwnd, kIdPairList);

    const LRESULT itemInfo = SendMessageW(list, LB_ITEMFROMPOINT, 0, MAKELPARAM(x, y));
    if (HIWORD(itemInfo) != 0) {
        return false;  // the click was outside every item
    }

    const std::size_t index = static_cast<std::size_t>(LOWORD(itemInfo));
    if (index >= shownPairs_.size()) {
        return false;
    }

    RECT itemRect{};
    if (SendMessageW(list, LB_GETITEMRECT, index, reinterpret_cast<LPARAM>(&itemRect)) == LB_ERR) {
        return false;
    }

    const PairRowGeometry geometry = computePairRowGeometry(toRect(itemRect), dpi_);
    if (!hit(geometry.removeGlyph, x, y)) {
        return false;  // an ordinary click: let the list select the row
    }

    onRemovePairAt(index);
    return true;
}

// ------------------------------------------------------------------ drawing

void MainWindow::paintWindow(void* const deviceContext) {
    const Palette& colors = palette();
    const Rect client{0, 0, layout_.windowWidth, layout_.windowHeight};

    BufferedDC buffer(static_cast<HDC>(deviceContext), client);
    Canvas canvas(buffer.get(), *fonts_, dpi_);

    canvas.fill(client, colors.bg);

    // --- title bar ---
    canvas.fill(layout_.titleBar, colors.surface);
    canvas.horizontalHairline(Rect{0, layout_.titleBar.bottom() - 1, client.width, 1},
                              colors.border);
    canvas.appMark(layout_.titleMark, colors.accent, colors.muted);
    canvas.text(layout_.titleText, kWindowTitle, fonts_->title(), colors.text,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    canvas.minimiseGlyph(layout_.minimiseButton, colors.muted);
    canvas.crossGlyph(layout_.closeButton, colors.muted, 1.2);

    // --- status card ---
    canvas.roundedRectOutlined(layout_.statusCard, canvas.scaled(10), colors.surface,
                               colors.border);
    if (shownEnabled_) {
        const int glow = canvas.scaled(4);
        canvas.circle(Rect{layout_.statusDot.x - glow, layout_.statusDot.y - glow,
                           layout_.statusDot.width + (2 * glow),
                           layout_.statusDot.height + (2 * glow)},
                      colors.glow);
        canvas.circle(layout_.statusDot, colors.accent);
    } else {
        canvas.circle(layout_.statusDot, colors.border);
    }
    canvas.text(layout_.statusHeading, statusHeading_, fonts_->body(),
                shownEnabled_ ? colors.text : colors.muted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    canvas.text(layout_.statusDetail, statusDetail_, fonts_->caption(), colors.muted,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    // --- section label and footer ---
    canvas.text(layout_.pairsLabel, L"PAIRS", fonts_->sectionLabel(), colors.muted,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE, canvas.scaled(2));
    canvas.swapArrows(layout_.addArrow, colors.muted, colors.muted);
    canvas.text(layout_.configLabel, configPath_.wstring(), fonts_->caption(), colors.muted,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
}

void MainWindow::drawPairRow(void* const drawItem) {
    const auto& item = *static_cast<const DRAWITEMSTRUCT*>(drawItem);
    const Palette& colors = palette();
    const Rect row = toRect(item.rcItem);

    Canvas canvas(item.hDC, *fonts_, dpi_);
    canvas.fill(row, colors.bg);

    const std::size_t index = static_cast<std::size_t>(item.itemID);
    if (index >= shownPairs_.size()) {
        return;
    }
    const PairView& pair = shownPairs_.at(index);
    const PairRowGeometry geometry = computePairRowGeometry(row, dpi_);

    const bool selected = (item.itemState & ODS_SELECTED) != 0;
    canvas.roundedRectOutlined(geometry.card, canvas.scaled(10), colors.surface,
                               selected ? colors.accent : colors.border);

    canvas.keycap(geometry.firstCap, widen(keyNameFromCode(pair.first)),
                  pair.active == pair.first);
    canvas.keycap(geometry.secondCap, widen(keyNameFromCode(pair.second)),
                  pair.active == pair.second);

    // The leading arrow lights up on whichever side currently owns the pair.
    const bool firstWins = pair.active == pair.first;
    canvas.swapArrows(geometry.arrows, firstWins ? colors.accent : colors.muted,
                      firstWins ? colors.muted : colors.accent);
    canvas.crossGlyph(geometry.removeGlyph, colors.muted);
}

void MainWindow::drawOwnerButton(void* const drawItem) {
    const auto& item = *static_cast<const DRAWITEMSTRUCT*>(drawItem);
    const Palette& colors = palette();
    const Rect bounds = toRect(item.rcItem);

    Canvas canvas(item.hDC, *fonts_, dpi_);

    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool focused = (item.itemState & ODS_FOCUS) != 0;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;

    if (item.CtlID == kIdEnableToggle) {
        // The toggle sits on the status card, not on the window ground.
        canvas.fill(bounds, colors.surface);
        canvas.togglePill(bounds, shownEnabled_);
        if (focused) {
            canvas.roundedRectOutlined(bounds, bounds.height / 2, colors.surface, colors.accent);
            canvas.togglePill(bounds, shownEnabled_);
        }
        return;
    }

    canvas.fill(bounds, colors.bg);

    const bool primary = item.CtlID == kIdAddButton;
    const int radius = canvas.scaled(9);
    COLORREF fill = primary ? colors.accent : colors.raised;
    COLORREF ink = primary ? colors.accentInk : colors.text;
    if (disabled) {
        fill = colors.surface;
        ink = colors.border;
    } else if (pressed) {
        fill = primary ? colors.glow : colors.hover;
    }

    canvas.roundedRectOutlined(bounds, radius, fill,
                               focused ? colors.accent : (primary ? fill : colors.border));
    canvas.text(bounds, item.CtlID == kIdAddButton ? L"Add pair" : L"Remove selected",
                fonts_->body(), ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void MainWindow::drawComboItem(void* const drawItem) {
    const auto& item = *static_cast<const DRAWITEMSTRUCT*>(drawItem);
    if (item.itemID == static_cast<UINT>(-1)) {
        return;
    }

    const Palette& colors = palette();
    const Rect bounds = toRect(item.rcItem);
    Canvas canvas(item.hDC, *fonts_, dpi_);

    const bool selected = (item.itemState & ODS_SELECTED) != 0;
    canvas.fill(bounds, selected ? colors.raised : colors.bg);

    wchar_t text[64] = {};
    SendMessageW(item.hwndItem, CB_GETLBTEXT, item.itemID, reinterpret_cast<LPARAM>(text));

    const Rect textRect{bounds.x + canvas.scaled(10), bounds.y, bounds.width, bounds.height};
    canvas.text(textRect, text, fonts_->body(), selected ? colors.accent : colors.text,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

void MainWindow::paintPicker(void* const comboHwnd) {
    const HWND picker = static_cast<HWND>(comboHwnd);
    const Palette& colors = palette();

    PAINTSTRUCT paint{};
    const HDC dc = BeginPaint(picker, &paint);

    RECT clientRect{};
    GetClientRect(picker, &clientRect);
    const Rect bounds = toRect(clientRect);

    {
        BufferedDC buffer(dc, bounds);
        Canvas canvas(buffer.get(), *fonts_, dpi_);

        const bool focused = GetFocus() == picker;
        canvas.fill(bounds, colors.bg);
        canvas.roundedRectOutlined(bounds, canvas.scaled(9), colors.raised,
                                   focused ? colors.accent : colors.border);

        wchar_t text[64] = {};
        const LRESULT selected = SendMessageW(picker, CB_GETCURSEL, 0, 0);
        if (selected != CB_ERR) {
            SendMessageW(picker, CB_GETLBTEXT, static_cast<WPARAM>(selected),
                         reinterpret_cast<LPARAM>(text));
        }

        const int padding = canvas.scaled(12);
        canvas.text(Rect{bounds.x + padding, bounds.y, bounds.width, bounds.height}, text,
                    fonts_->keycap(), colors.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        const int chevron = canvas.scaled(16);
        canvas.chevronDown(Rect{bounds.right() - padding - chevron,
                                bounds.y + ((bounds.height - chevron) / 2), chevron, chevron},
                           colors.muted);
    }

    EndPaint(picker, &paint);
}

// --------------------------------------------------------------------- tray

void MainWindow::addTrayIcon() {
    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = static_cast<HWND>(hwnd_);
    icon.uID = kTrayIconId;
    icon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    icon.uCallbackMessage = kTrayCallbackMessage;
    icon.hIcon = icon_->smallIcon() != nullptr ? icon_->smallIcon()
                                              : LoadIconW(nullptr, IDI_APPLICATION);
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

    const HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }
    AppendMenuW(menu, MF_STRING, kMenuShow, L"Show window");
    AppendMenuW(menu, MF_STRING | (shownEnabled_ ? MF_CHECKED : MF_UNCHECKED), kMenuEnable,
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
            onToggleEnabled(!shownEnabled_);
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

// ----------------------------------------------------------------- messages

bool MainWindow::handleMessage(const unsigned int message, const unsigned long long wParam,
                               const long long lParam, long long& result) {
    const HWND hwnd = static_cast<HWND>(hwnd_);
    result = 0;

    switch (message) {
        case WM_NCCALCSIZE:
            // Hand the entire window to the client area: the title bar is ours.
            // The window is neither resizable nor maximisable, so there are no
            // resize borders or snap layouts to preserve.
            if (wParam == TRUE) {
                return true;
            }
            return false;

        case WM_NCHITTEST: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &point);
            if (hit(layout_.minimiseButton, point.x, point.y) ||
                hit(layout_.closeButton, point.x, point.y)) {
                result = HTCLIENT;
            } else if (point.y < layout_.titleBar.bottom()) {
                result = HTCAPTION;  // drag the window by its title bar
            } else {
                result = HTCLIENT;
            }
            return true;
        }

        case WM_LBUTTONUP: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            if (hit(layout_.closeButton, x, y)) {
                DestroyWindow(hwnd);
                return true;
            }
            if (hit(layout_.minimiseButton, x, y)) {
                ShowWindow(hwnd, SW_MINIMIZE);
                return true;
            }
            return false;
        }

        case WM_ERASEBKGND:
            result = 1;  // WM_PAINT covers every pixel
            return true;

        case WM_PAINT: {
            PAINTSTRUCT paint{};
            const HDC dc = BeginPaint(hwnd, &paint);
            paintWindow(dc);
            EndPaint(hwnd, &paint);
            return true;
        }

        case WM_MEASUREITEM: {
            auto& measure = *reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            measure.itemHeight = measure.CtlID == kIdPairList
                                     ? static_cast<UINT>(layout_.pairRowHeight)
                                     : static_cast<UINT>(scaleForDpi(28, dpi_));
            result = TRUE;
            return true;
        }

        case WM_DRAWITEM: {
            const auto& item = *reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (item.CtlType == ODT_LISTBOX && item.CtlID == kIdPairList) {
                drawPairRow(const_cast<DRAWITEMSTRUCT*>(&item));
            } else if (item.CtlType == ODT_BUTTON) {
                drawOwnerButton(const_cast<DRAWITEMSTRUCT*>(&item));
            } else if (item.CtlType == ODT_COMBOBOX) {
                drawComboItem(const_cast<DRAWITEMSTRUCT*>(&item));
            } else {
                return false;
            }
            result = TRUE;
            return true;
        }

        case WM_CTLCOLORLISTBOX:
            // Covers both the pair list and the pickers' dropdowns.
            SetBkColor(reinterpret_cast<HDC>(wParam), palette().bg);
            result = reinterpret_cast<long long>(brushes_->background());
            return true;

        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int notification = HIWORD(wParam);

            if (id == kIdEnableToggle && notification == BN_CLICKED) {
                onToggleEnabled(!shownEnabled_);
                return true;
            }
            if (id == kIdAddButton && notification == BN_CLICKED) {
                onAddPair();
                return true;
            }
            if (id == kIdRemoveButton && notification == BN_CLICKED) {
                onRemoveSelected();
                return true;
            }
            if ((id == kIdFirstPicker || id == kIdSecondPicker) &&
                notification == CBN_SELCHANGE) {
                InvalidateRect(control(hwnd, id), nullptr, FALSE);
                return true;
            }
            return false;
        }

        case WM_TIMER:
            if (wParam == kRefreshTimerId) {
                refreshFromEngine(false);
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
