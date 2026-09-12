#include "snap_tap/app_paths.h"
#include "snap_tap/config.h"
#include "snap_tap/engine.h"
#include "snap_tap/key_codes.h"
#include "snap_tap/keyboard_hook.h"
#include "snap_tap/main_window.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <commctrl.h>

#include <filesystem>
#include <string>

using namespace snaptap;

namespace {

const wchar_t* const kAppTitle = L"Snap Tap";
const std::string kConfigFileName = "snaptap.cfg";

std::wstring widen(const std::string& text) {
    return std::wstring(text.begin(), text.end());
}

// Per-monitor v2, so the window is laid out for whichever monitor it is on
// rather than bitmap-stretched from the primary monitor's scaling. Loaded
// dynamically: the call is Windows 10 1703 and later, and the older
// SetProcessDPIAware still gets the primary monitor right before that.
void enableHighDpiAwareness() {
    using SetContext = BOOL(WINAPI*)(HANDLE);
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 != nullptr) {
        const auto setContext =
            reinterpret_cast<SetContext>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        // -4 is DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2.
        if (setContext != nullptr && setContext(reinterpret_cast<HANDLE>(-4)) != FALSE) {
            return;
        }
    }
    SetProcessDPIAware();
}

void reportProblem(const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), kAppTitle, MB_OK | MB_ICONERROR);
}

// Bad lines in the config are not fatal: the good ones still load, so say what
// was skipped and carry on.
void reportConfigWarnings(const ParseResult& loaded, const std::filesystem::path& path) {
    if (loaded.ok()) {
        return;
    }
    std::wstring message = L"Some lines in the config file were skipped:\n\n";
    for (const std::string& error : loaded.errors) {
        message += L"  " + widen(error) + L"\n";
    }
    message += L"\n" + path.wstring();
    MessageBoxW(nullptr, message.c_str(), kAppTitle, MB_OK | MB_ICONWARNING);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    enableHighDpiAwareness();

    INITCOMMONCONTROLSEX commonControls{};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&commonControls);

    const std::filesystem::path configPath = executableDirectory() / kConfigFileName;
    const ParseResult loaded = loadConfigFile(configPath.string());
    reportConfigWarnings(loaded, configPath);

    SnapTapEngine engine;
    for (const KeyPairConfig& pair : loaded.config.pairs) {
        if (!engine.addPair(pair.first, pair.second)) {
            reportProblem(L"Ignoring an unusable pair in the config: " +
                          widen(keyNameFromCode(pair.first)) + L"/" +
                          widen(keyNameFromCode(pair.second)));
        }
    }
    engine.setEnabled(loaded.config.enabled);

    // The hook is scoped so its destructor releases every held key and uninstalls
    // the hook before the process exits, however the window is closed.
    KeyboardHook hook(engine);
    if (!hook.isInstalled()) {
        reportProblem(L"Could not install the keyboard hook (Windows error " +
                      std::to_wstring(hook.installError()) + L").");
        return 1;
    }

    MainWindow window(hook, configPath);
    if (!window.create()) {
        reportProblem(L"Could not create the Snap Tap window.");
        return 1;
    }
    return window.run();
}
