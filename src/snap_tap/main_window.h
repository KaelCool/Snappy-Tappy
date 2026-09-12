#pragma once

#include "snap_tap/engine.h"
#include "snap_tap/keyboard_hook.h"
#include "snap_tap/layout.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace snaptap {

// Declared in snap_tap/theme.h, which pulls in windows.h. Held by pointer so
// this header stays free of it, as the rest of the project's headers are.
class AppIcon;
class Brushes;
class Fonts;
class GdiPlusSession;

// The Snap Tap window, in the Command Deck design: a dark ground, each pair
// drawn as two keycaps with the winning one lit, and a tray icon so it can sit
// out of the way.
//
// It owns no Snap Tap logic. Every read and every change goes through
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

    // Handles one window message. Public only because the Win32 window
    // procedure and the list subclass are free functions; not for general use.
    // Returns true when handled and `result` should go back to Windows.
    bool handleMessage(unsigned int message, unsigned long long wParam, long long lParam,
                       long long& result);

    // Click inside the pair list. Returns true if it hit a row's remove glyph,
    // which the list itself knows nothing about.
    bool handleListClick(int x, int y);

    // Paints a key picker's closed state. The system would otherwise draw a
    // light themed combo box frame around our dark contents.
    void paintPicker(void* comboHwnd);

private:
    void createControls();
    void applyLayout();

    // Rebuilds the fonts and layout for a new DPI and resizes the window to
    // match. The suggested rect is what Windows offers on a DPI change, whose
    // position is honoured and whose size is not: the size comes from the
    // layout. Pass nullptr to keep the window where it is.
    void applyDpi(int newDpi, std::size_t pairCount, const void* suggested);

    // Recomputes the layout for the current pair count, resizes the window and
    // repositions every control.
    void rebuildLayout();

    // Pulls current state from the engine and updates only what changed.
    void refreshFromEngine(bool force);

    void onAddPair();
    void onRemovePairAt(std::size_t index);
    void onRemoveSelected();
    void onToggleEnabled(bool enabled);
    void saveConfig();

    void paintWindow(void* deviceContext);
    void drawPairRow(void* drawItem);
    void drawOwnerButton(void* drawItem);
    void drawComboItem(void* drawItem);

    void addTrayIcon();
    void removeTrayIcon();
    void showTrayMenu();
    void restoreWindow();

    KeyboardHook& hook_;
    const std::filesystem::path configPath_;

    void* hwnd_ = nullptr;
    int dpi_ = 96;
    bool trayIconAdded_ = false;

    std::unique_ptr<GdiPlusSession> gdiPlus_;
    std::unique_ptr<Fonts> fonts_;
    std::unique_ptr<Brushes> brushes_;
    std::unique_ptr<AppIcon> icon_;

    WindowLayout layout_;

    // What the window is currently showing, so a refresh can tell what changed.
    std::vector<PairView> shownPairs_;
    bool shownEnabled_ = true;
    std::wstring statusHeading_;
    std::wstring statusDetail_;
};

}  // namespace snaptap
