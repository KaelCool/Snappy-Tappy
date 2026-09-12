#include "snap_tap/keyboard_hook.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>

namespace snaptap {
namespace {

// Stamped onto every event we inject so the hook can recognise its own output
// and pass it straight through. Without this the engine would react to the keys
// it just synthesised and chase itself in a loop.
constexpr ULONG_PTR kSnapTapSignature = 0x534E4150;  // "SNAP"

// The hook callback carries no user data, so the active instance has to be
// reachable from file scope. Non-owning, and only ever one at a time.
KeyboardHook* g_activeHook = nullptr;

// Keys that live on the extended part of the keyboard must say so, or the
// target application sees the numeric-keypad key of the same scan code.
bool isExtendedKey(const KeyCode key) {
    switch (key) {
        case VK_LEFT:
        case VK_UP:
        case VK_RIGHT:
        case VK_DOWN:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_HOME:
        case VK_END:
        case VK_INSERT:
        case VK_DELETE:
        case VK_RCONTROL:
        case VK_RMENU:
        case VK_NUMLOCK:
        case VK_DIVIDE:
            return true;
        default:
            return false;
    }
}

INPUT makeInput(const OutputAction& action) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(action.key);
    // Games commonly read scan codes rather than virtual keys, so send both.
    input.ki.wScan = static_cast<WORD>(MapVirtualKeyW(static_cast<UINT>(action.key), MAPVK_VK_TO_VSC));
    input.ki.dwFlags = 0;
    if (action.type == EventType::KeyUp) {
        input.ki.dwFlags |= KEYEVENTF_KEYUP;
    }
    if (isExtendedKey(action.key)) {
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
    input.ki.dwExtraInfo = kSnapTapSignature;
    return input;
}

// Sends the whole batch in one SendInput call so a release and the press that
// replaces it cannot be separated by another application interleaving input.
void sendActions(const OutputAction* const actions, const std::size_t count) {
    if (count == 0) {
        return;
    }

    constexpr std::size_t kMaxBatch = 32;
    std::array<INPUT, kMaxBatch> batch{};
    std::size_t batched = 0;
    for (std::size_t index = 0; index < count; ++index) {
        batch[batched] = makeInput(actions[index]);
        ++batched;
        if (batched == kMaxBatch) {
            SendInput(static_cast<UINT>(batched), batch.data(), sizeof(INPUT));
            batched = 0;
        }
    }
    if (batched > 0) {
        SendInput(static_cast<UINT>(batched), batch.data(), sizeof(INPUT));
    }
}

LRESULT CALLBACK lowLevelKeyboardProc(const int code, const WPARAM wParam, const LPARAM lParam) {
    if (code != HC_ACTION || g_activeHook == nullptr) {
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    const auto* const info = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    if (info->dwExtraInfo == kSnapTapSignature) {
        return CallNextHookEx(nullptr, code, wParam, lParam);  // our own injection
    }

    const bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    const bool isUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
    if (!isDown && !isUp) {
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    const bool suppress = g_activeHook->handleEvent(static_cast<KeyCode>(info->vkCode),
                                                    isDown ? EventType::KeyDown : EventType::KeyUp);
    if (suppress) {
        return 1;  // swallow the physical event; the engine already replaced it
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

}  // namespace

KeyboardHook::KeyboardHook(SnapTapEngine& engine) : engine_(engine) {
    g_activeHook = this;
    thread_ = std::thread([this] { run(); });
}

KeyboardHook::~KeyboardHook() {
    // Let go of anything still held before the hook stops listening.
    withEngine([](SnapTapEngine& engine) { return engine.releaseAll(); });

    const unsigned long threadId = threadId_.load();
    if (threadId != 0) {
        PostThreadMessageW(threadId, WM_QUIT, 0, 0);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    g_activeHook = nullptr;
}

bool KeyboardHook::isInstalled() const {
    readyFuture_.wait();
    return installed_.load();
}

void KeyboardHook::withEngine(const EngineMutator& mutator) {
    std::vector<OutputAction> actions;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        actions = mutator(engine_);
    }
    // Injected outside the lock: SendInput re-enters our own hook.
    sendActions(actions.data(), actions.size());
}

void KeyboardHook::readEngine(const EngineReader& reader) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    reader(engine_);
}

bool KeyboardHook::handleEvent(const KeyCode key, const EventType type) {
    ProcessResult result;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        result = engine_.process(InputEvent{key, type});
    }
    sendActions(result.begin(), result.size());
    return result.suppressOriginal;
}

void KeyboardHook::run() {
    threadId_.store(GetCurrentThreadId());

    const HHOOK hook =
        SetWindowsHookExW(WH_KEYBOARD_LL, &lowLevelKeyboardProc, GetModuleHandleW(nullptr), 0);
    if (hook == nullptr) {
        installError_.store(GetLastError());
    }
    installed_.store(hook != nullptr);
    ready_.set_value();

    if (hook == nullptr) {
        return;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    UnhookWindowsHookEx(hook);
}

}  // namespace snaptap
