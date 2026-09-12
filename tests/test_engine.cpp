#include "snap_tap/engine.h"
#include "snap_tap/key_codes.h"
#include "test_util.h"

#include <algorithm>
#include <initializer_list>
#include <vector>

using namespace snaptap;

namespace {

// The default pair from SPEC.md, plus a second pair for independence tests.
constexpr KeyCode kA = 'A';
constexpr KeyCode kD = 'D';
constexpr KeyCode kW = 'W';
constexpr KeyCode kS = 'S';
constexpr KeyCode kQ = 'Q';  // deliberately unmanaged

OutputAction down(const KeyCode key) { return OutputAction{key, EventType::KeyDown}; }
OutputAction up(const KeyCode key) { return OutputAction{key, EventType::KeyUp}; }

ProcessResult press(SnapTapEngine& engine, const KeyCode key) {
    return engine.process(InputEvent{key, EventType::KeyDown});
}

ProcessResult release(SnapTapEngine& engine, const KeyCode key) {
    return engine.process(InputEvent{key, EventType::KeyUp});
}

// Order matters: a swap must release the outgoing key before pressing the
// incoming one, so these comparisons are strictly sequential.
bool emitted(const ProcessResult& result, const std::initializer_list<OutputAction> expected) {
    return std::equal(result.begin(), result.end(), expected.begin(), expected.end());
}

bool emitted(const std::vector<OutputAction>& actions,
             const std::initializer_list<OutputAction> expected) {
    return std::equal(actions.begin(), actions.end(), expected.begin(), expected.end());
}

SnapTapEngine makeEngine() {
    SnapTapEngine engine;
    engine.addPair(kA, kD);
    return engine;
}

}  // namespace

int main() {
    {
        TEST_CASE("pair registration rejects bad input");
        SnapTapEngine engine;
        CHECK(engine.addPair(kA, kD));
        CHECK(engine.pairCount() == 1);
        CHECK(!engine.addPair(kA, kA));      // same key twice
        CHECK(!engine.addPair(kA, kW));      // A already paired
        CHECK(!engine.addPair(kW, kD));      // D already paired
        CHECK(!engine.addPair(kNoKey, kW));  // no such key
        CHECK(engine.addPair(kW, kS));
        CHECK(engine.pairCount() == 2);
        CHECK(engine.hasPair(kA) && engine.hasPair(kS));
        CHECK(!engine.hasPair(kQ));
    }

    {
        TEST_CASE("a lone key presses and releases normally");
        SnapTapEngine engine = makeEngine();
        const ProcessResult pressed = press(engine, kD);
        CHECK(pressed.suppressOriginal);
        CHECK(emitted(pressed, {down(kD)}));

        const ProcessResult released = release(engine, kD);
        CHECK(released.suppressOriginal);
        CHECK(emitted(released, {up(kD)}));
        CHECK(engine.pairs().at(0).active == kNoKey);
    }

    {
        TEST_CASE("pressing A while D is held hands priority to A");
        SnapTapEngine engine = makeEngine();
        press(engine, kD);
        const ProcessResult result = press(engine, kA);
        CHECK(result.suppressOriginal);
        CHECK(emitted(result, {up(kD), down(kA)}));  // release first, then press
        CHECK(engine.pairs().at(0).active == kA);
    }

    {
        TEST_CASE("releasing A reactivates D while D is still held");
        SnapTapEngine engine = makeEngine();
        press(engine, kD);
        press(engine, kA);
        const ProcessResult result = release(engine, kA);
        CHECK(emitted(result, {up(kA), down(kD)}));
        CHECK(engine.pairs().at(0).active == kD);
    }

    {
        TEST_CASE("releasing the shadowed key emits nothing");
        SnapTapEngine engine = makeEngine();
        press(engine, kD);
        press(engine, kA);  // D is now shadowed
        const ProcessResult result = release(engine, kD);
        CHECK(result.suppressOriginal);
        CHECK(result.empty());
        CHECK(engine.pairs().at(0).active == kA);

        // A is now alone: releasing it must not resurrect D.
        const ProcessResult afterA = release(engine, kA);
        CHECK(emitted(afterA, {up(kA)}));
        CHECK(engine.pairs().at(0).active == kNoKey);
    }

    {
        TEST_CASE("priority can swap back and forth repeatedly");
        SnapTapEngine engine = makeEngine();
        CHECK(emitted(press(engine, kA), {down(kA)}));
        CHECK(emitted(press(engine, kD), {up(kA), down(kD)}));
        CHECK(emitted(release(engine, kD), {up(kD), down(kA)}));
        CHECK(emitted(press(engine, kD), {up(kA), down(kD)}));
        CHECK(emitted(release(engine, kA), {}));  // A was shadowed
        CHECK(emitted(release(engine, kD), {up(kD)}));
        CHECK(engine.pairs().at(0).active == kNoKey);
    }

    {
        TEST_CASE("auto-repeat does not re-emit a held key");
        SnapTapEngine engine = makeEngine();
        CHECK(emitted(press(engine, kA), {down(kA)}));
        for (int repeat = 0; repeat < 5; ++repeat) {
            const ProcessResult result = press(engine, kA);
            CHECK(result.suppressOriginal);
            CHECK(result.empty());
        }
        CHECK(engine.pairs().at(0).active == kA);

        // A repeat of the shadowed key must not steal priority back.
        press(engine, kD);
        const ProcessResult shadowRepeat = press(engine, kA);
        CHECK(shadowRepeat.empty());
        CHECK(engine.pairs().at(0).active == kD);
    }

    {
        TEST_CASE("pairs operate independently and simultaneously");
        SnapTapEngine engine = makeEngine();
        engine.addPair(kW, kS);

        CHECK(emitted(press(engine, kD), {down(kD)}));
        CHECK(emitted(press(engine, kW), {down(kW)}));  // other pair untouched
        CHECK(emitted(press(engine, kA), {up(kD), down(kA)}));
        CHECK(emitted(press(engine, kS), {up(kW), down(kS)}));

        const std::vector<PairView> views = engine.pairs();
        CHECK(views.at(0).active == kA);
        CHECK(views.at(1).active == kS);

        CHECK(emitted(release(engine, kA), {up(kA), down(kD)}));
        CHECK(engine.pairs().at(1).active == kS);  // W/S unaffected
        CHECK(emitted(release(engine, kS), {up(kS), down(kW)}));
        CHECK(engine.pairs().at(0).active == kD);  // A/D unaffected
    }

    {
        TEST_CASE("unmanaged keys pass through untouched");
        SnapTapEngine engine = makeEngine();
        const ProcessResult result = press(engine, kQ);
        CHECK(!result.suppressOriginal);
        CHECK(result.empty());
        CHECK(!release(engine, kQ).suppressOriginal);
    }

    {
        TEST_CASE("disabling releases held keys and passes events through");
        SnapTapEngine engine = makeEngine();
        press(engine, kD);
        press(engine, kA);

        CHECK(emitted(engine.setEnabled(false), {up(kA)}));
        CHECK(!engine.isEnabled());

        const ProcessResult whileOff = press(engine, kA);
        CHECK(!whileOff.suppressOriginal);
        CHECK(whileOff.empty());

        // Re-enabling starts clean: A is treated as not held.
        CHECK(engine.setEnabled(true).empty());
        CHECK(engine.isEnabled());
        CHECK(engine.pairs().at(0).active == kNoKey);
        CHECK(emitted(press(engine, kD), {down(kD)}));

        // Toggling to the state it is already in changes nothing.
        CHECK(engine.setEnabled(true).empty());
        CHECK(engine.pairs().at(0).active == kD);
    }

    {
        TEST_CASE("removing a pair releases whatever it held");
        SnapTapEngine engine = makeEngine();
        engine.addPair(kW, kS);
        press(engine, kD);
        press(engine, kW);

        const RemoveResult removed = engine.removePair(kA);  // named by its partner
        CHECK(removed.removed);
        CHECK(emitted(removed.released, {up(kD)}));
        CHECK(engine.pairCount() == 1);
        CHECK(!engine.hasPair(kD));

        // The surviving pair keeps its state.
        CHECK(engine.pairs().at(0).active == kW);

        const RemoveResult missing = engine.removePair(kQ);
        CHECK(!missing.removed);
        CHECK(missing.released.empty());

        // A removed key is now unmanaged.
        CHECK(!press(engine, kD).suppressOriginal);
    }

    {
        TEST_CASE("releaseAll leaves no key stuck down");
        SnapTapEngine engine = makeEngine();
        engine.addPair(kW, kS);
        press(engine, kD);
        press(engine, kA);
        press(engine, kW);

        const std::vector<OutputAction> released = engine.releaseAll();
        CHECK(emitted(released, {up(kA), up(kW)}));
        CHECK(engine.pairs().at(0).active == kNoKey);
        CHECK(engine.pairs().at(1).active == kNoKey);

        // Nothing held means nothing to release.
        CHECK(engine.releaseAll().empty());

        // State was cleared, so D is treated as a fresh press.
        CHECK(emitted(press(engine, kD), {down(kD)}));
    }

    return ::testing::summarize("test_engine");
}
