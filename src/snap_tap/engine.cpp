#include "snap_tap/engine.h"

#include <algorithm>

namespace snaptap {

void ProcessResult::add(const KeyCode key, const EventType type) {
    if (count_ >= kMaxActionsPerEvent) {
        return;
    }
    actions_[count_] = OutputAction{key, type};
    ++count_;
}

SnapTapEngine::KeyPair* SnapTapEngine::findPair(const KeyCode key) {
    const auto it = std::find_if(pairs_.begin(), pairs_.end(), [key](const KeyPair& pair) {
        return pair.first == key || pair.second == key;
    });
    return it == pairs_.end() ? nullptr : &(*it);
}

const SnapTapEngine::KeyPair* SnapTapEngine::findPair(const KeyCode key) const {
    const auto it = std::find_if(pairs_.begin(), pairs_.end(), [key](const KeyPair& pair) {
        return pair.first == key || pair.second == key;
    });
    return it == pairs_.end() ? nullptr : &(*it);
}

bool SnapTapEngine::addPair(const KeyCode first, const KeyCode second) {
    if (first == kNoKey || second == kNoKey || first == second) {
        return false;
    }
    if (hasPair(first) || hasPair(second)) {
        return false;
    }

    KeyPair pair;
    pair.first = first;
    pair.second = second;
    pairs_.push_back(pair);
    return true;
}

RemoveResult SnapTapEngine::removePair(const KeyCode key) {
    RemoveResult result;
    const auto it = std::find_if(pairs_.begin(), pairs_.end(), [key](const KeyPair& pair) {
        return pair.first == key || pair.second == key;
    });
    if (it == pairs_.end()) {
        return result;
    }

    if (it->active != kNoKey) {
        result.released.push_back(OutputAction{it->active, EventType::KeyUp});
    }
    pairs_.erase(it);
    result.removed = true;
    return result;
}

bool SnapTapEngine::hasPair(const KeyCode key) const {
    return findPair(key) != nullptr;
}

std::vector<PairView> SnapTapEngine::pairs() const {
    std::vector<PairView> views;
    views.reserve(pairs_.size());
    for (const KeyPair& pair : pairs_) {
        views.push_back(PairView{pair.first, pair.second, pair.active});
    }
    return views;
}

std::vector<OutputAction> SnapTapEngine::setEnabled(const bool enabled) {
    if (enabled == enabled_) {
        return {};
    }

    enabled_ = enabled;
    if (enabled) {
        // Physical key state observed before the pause is no longer trustworthy,
        // because events passed through untouched while we were off.
        for (KeyPair& pair : pairs_) {
            pair.firstDown = false;
            pair.secondDown = false;
            pair.active = kNoKey;
        }
        return {};
    }
    return releaseAll();
}

ProcessResult SnapTapEngine::process(const InputEvent& event) {
    ProcessResult result;
    KeyPair* const pair = findPair(event.key);
    if (!enabled_ || pair == nullptr) {
        return result;  // Not ours: let the event through untouched.
    }

    const bool isFirst = (event.key == pair->first);
    bool& thisDown = isFirst ? pair->firstDown : pair->secondDown;
    const bool otherDown = isFirst ? pair->secondDown : pair->firstDown;
    const KeyCode otherKey = isFirst ? pair->second : pair->first;

    // From here on the engine owns this key: the physical event is swallowed and
    // replaced by whatever the state machine decides to emit.
    result.suppressOriginal = true;

    if (event.type == EventType::KeyDown) {
        if (thisDown) {
            return result;  // Auto-repeat of a key already held: nothing changes.
        }
        thisDown = true;
        if (pair->active != event.key) {
            if (pair->active != kNoKey) {
                // Release before press, so the target never sees both keys down.
                result.add(pair->active, EventType::KeyUp);
            }
            result.add(event.key, EventType::KeyDown);
            pair->active = event.key;
        }
        return result;
    }

    thisDown = false;
    if (pair->active != event.key) {
        return result;  // The shadowed key was never pressed downstream.
    }

    result.add(event.key, EventType::KeyUp);
    pair->active = kNoKey;
    if (otherDown) {
        // The partner is still held, so it takes over again.
        result.add(otherKey, EventType::KeyDown);
        pair->active = otherKey;
    }
    return result;
}

std::vector<OutputAction> SnapTapEngine::releaseAll() {
    std::vector<OutputAction> actions;
    for (KeyPair& pair : pairs_) {
        if (pair.active != kNoKey) {
            actions.push_back(OutputAction{pair.active, EventType::KeyUp});
        }
        pair.firstDown = false;
        pair.secondDown = false;
        pair.active = kNoKey;
    }
    return actions;
}

}  // namespace snaptap
