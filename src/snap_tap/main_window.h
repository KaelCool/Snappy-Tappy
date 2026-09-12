#pragma once

#include "snap_tap/engine.h"
#include "snap_tap/keyboard_hook.h"

#include <filesystem>
#include <string>
#include <vector>

namespace snaptap {

// The Snap Tap window: an enable toggle, the list of managed pairs, dropdowns to
// add a pair, and a tray icon so it can sit out of the way.
//
// It owns no Snap Tap logic of its own. Every read and every change goes through
// KeyboardHook, which serialises access to the engine it shares with the hook
// thread, so nothing here runs on the latency-critical path.
class MainWindow {
public:
    MainWindow(KeyboardHook& hook, std::filesystem::path configPath);
    ~MainWindow();

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    // Registers the window class and shows the window.
    bool create();

    // Pumps messages until the user quits; returns the process exit code.
    int run();

    // Handles one window message. Public only because the Win32 window procedure
    // is a free function; not for general use. Returns true when the message was
    // handled and `result` should be returned to Windows.
    bool handleMessage(unsigned int message, unsigned long long wParam, long long lParam,
                       long long& result);

private:
    void createControls();
    void layoutControls();
    int scaled(int value) const;

    // Rebuilds the pair list, but only when the set of pairs actually changed,
    // so a periodic refresh cannot steal the selection or flicker.
    void refreshPairList(bool force);
    void refreshStatus();
    void refreshEnabledCheck();

    void onAddPair();
    void onRemovePair();
    void onToggleEnabled(bool enabled);
    void saveConfig();

    void addTrayIcon();
    void removeTrayIcon();
    void showTrayMenu();
    void restoreWindow();

    KeyboardHook& hook_;
    const std::filesystem::path configPath_;

    void* hwnd_ = nullptr;
    void* font_ = nullptr;
    int dpi_ = 96;
    bool trayIconAdded_ = false;

    // Pair order shown in the list, mirroring the engine order.
    std::vector<PairView> shownPairs_;
    std::wstring lastStatusText_;
};

}  // namespace snaptap
