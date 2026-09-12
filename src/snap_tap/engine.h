#pragma once

#include "snap_tap/key_codes.h"

#include <array>
#include <cstddef>
#include <vector>

namespace snaptap {

// Sentinel for "no key": no virtual-key code is 0.
inline constexpr KeyCode kNoKey = 0;

enum class EventType {
    KeyDown,
    KeyUp,
};

// A physical key event observed from the keyboard.
struct InputEvent {
    KeyCode key = kNoKey;
    EventType type = EventType::KeyDown;
};

// A synthetic key event the caller should inject on our behalf.
struct OutputAction {
    KeyCode key = kNoKey;
    EventType type = EventType::KeyDown;
};

inline bool operator==(const OutputAction& lhs, const OutputAction& rhs) {
    return lhs.key == rhs.key && lhs.type == rhs.type;
}

// Handling one event emits at most a release of the outgoing key followed by a
// press of the incoming one.
inline constexpr std::size_t kMaxActionsPerEvent = 2;

// Result of feeding one event to the engine. Deliberately fixed-size: this is
// produced inside a low-level keyboard hook, where allocating would add latency.
class ProcessResult {
public:
    // True when the original physical event must be swallowed rather than
    // passed on to the rest of the system.
    bool suppressOriginal = false;

    void add(KeyCode key, EventType type);

    std::size_t size() const { return count_; }
    bool empty() const { return count_ == 0; }
    const OutputAction& operator[](const std::size_t index) const { return actions_[index]; }
    const OutputAction* begin() const { return actions_.data(); }
    const OutputAction* end() const { return actions_.data() + count_; }

private:
    std::array<OutputAction, kMaxActionsPerEvent> actions_{};
    std::size_t count_ = 0;
};

// A read-only snapshot of one configured pair, for status display and saving.
struct PairView {
    KeyCode first = kNoKey;
    KeyCode second = kNoKey;
    KeyCode active = kNoKey;
};

struct RemoveResult {
    bool removed = false;
    std::vector<OutputAction> released;
};

// The Snap Tap priority state machine.
//
// For each configured pair of opposite keys, the most recently pressed key wins:
// pressing one releases the other, and releasing the winner reactivates its
// partner if that partner is still physically held. Pairs are independent, and
// any number of them can be active at once.
//
// This class is deliberately free of any OS dependency so it can be tested in
// full; the caller translates real key events in and synthetic ones out.
class SnapTapEngine {
public:
    // Registers a pair. Fails if the keys are equal, either is kNoKey, or
    // either already belongs to another pair.
    bool addPair(KeyCode first, KeyCode second);

    // Removes the pair containing the given key, releasing its active key if one is held.
    RemoveResult removePair(KeyCode key);

    bool hasPair(KeyCode key) const;
    std::size_t pairCount() const { return pairs_.size(); }
    std::vector<PairView> pairs() const;

    bool isEnabled() const { return enabled_; }

    // Turning Snap Tap off releases anything it is currently holding; turning it
    // back on starts from a clean slate, since key state may have changed while
    // events were passing through untouched.
    std::vector<OutputAction> setEnabled(bool enabled);

    // Feeds one physical key event through the state machine.
    ProcessResult process(const InputEvent& event);

    // Releases every key the engine is currently holding down. Used when
    // shutting down so no key is left stuck in another application.
    std::vector<OutputAction> releaseAll();

private:
    struct KeyPair {
        KeyCode first = kNoKey;
        KeyCode second = kNoKey;
        bool firstDown = false;
        bool secondDown = false;
        KeyCode active = kNoKey;
    };

    KeyPair* findPair(KeyCode key);
    const KeyPair* findPair(KeyCode key) const;

    std::vector<KeyPair> pairs_;
    bool enabled_ = true;
};

}  // namespace snaptap
