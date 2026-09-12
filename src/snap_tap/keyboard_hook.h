#pragma once

#include "snap_tap/engine.h"

#include <atomic>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace snaptap {

// Installs a global low-level keyboard hook and drives a SnapTapEngine with it,
// injecting the engine output back into the system with SendInput.
//
// The hook runs on its own thread with its own message loop, because Windows
// only delivers low-level hook events to a thread that is pumping messages --
// that leaves the main thread free to read console commands. Engine access is
// serialised by a mutex, so commands can reconfigure pairs while keys are live.
//
// This is the only part of Snap Tap that touches the OS, and the only part the
// unit tests cannot reach; keep it thin.
class KeyboardHook {
public:
    // Mutates the engine under the lock and returns any keys that must be
    // injected as a result (for example releases when a pair is removed).
    using EngineMutator = std::function<std::vector<OutputAction>(SnapTapEngine&)>;
    using EngineReader = std::function<void(const SnapTapEngine&)>;

    // Starts the hook thread. Ownership of `engine` stays with the caller, which
    // must outlive this object.
    explicit KeyboardHook(SnapTapEngine& engine);

    // Stops the hook thread and releases any key the engine still holds, so
    // quitting can never leave a key stuck down in another application.
    ~KeyboardHook();

    KeyboardHook(const KeyboardHook&) = delete;
    KeyboardHook& operator=(const KeyboardHook&) = delete;

    // True once the hook is installed. Blocks until the hook thread has tried.
    bool isInstalled() const;

    // Windows error code from SetWindowsHookEx when installation failed.
    unsigned long installError() const { return installError_; }

    void withEngine(const EngineMutator& mutator);
    void readEngine(const EngineReader& reader) const;

    // Feeds one key event through the engine and injects whatever it emits.
    // Returns true when the original event must be swallowed. Public only
    // because the Windows hook callback is a free function; not for general use.
    bool handleEvent(KeyCode key, EventType type);

private:
    void run();

    SnapTapEngine& engine_;
    mutable std::mutex mutex_;
    std::thread thread_;
    std::promise<void> ready_;
    std::future<void> readyFuture_ = ready_.get_future();
    std::atomic<bool> installed_{false};
    std::atomic<unsigned long> installError_{0};
    std::atomic<unsigned long> threadId_{0};
};

}  // namespace snaptap
