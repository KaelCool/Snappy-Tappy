#include "snap_tap/layout.h"
#include "test_util.h"

#include <cstddef>
#include <vector>

using namespace snaptap;

namespace {

// The display scalings this actually has to survive: 100%, 125%, 150%, 200%.
const std::vector<int> kDpis = {96, 120, 144, 192};

// Scaled coordinates are rounded, so a rect can land a pixel outside a bound
// derived from a separately rounded value. One pixel is invisible; a real
// layout mistake is not, and would be far larger than this.
bool withinTolerance(const int value, const int bound) {
    return value <= bound + 1;
}

}  // namespace

int main() {
    {
        TEST_CASE("dpi scaling rounds rather than truncates");
        CHECK(scaleForDpi(100, 96) == 100);  // 100% leaves values alone
        CHECK(scaleForDpi(0, 144) == 0);
        CHECK(scaleForDpi(100, 120) == 125);
        CHECK(scaleForDpi(100, 144) == 150);
        CHECK(scaleForDpi(100, 192) == 200);
        CHECK(scaleForDpi(18, 120) == 23);   // 22.5 rounds up, not down to 22
        CHECK(scaleForDpi(1, 120) == 1);     // a hairline never scales away
    }

    {
        TEST_CASE("the window is the width the design calls for");
        const WindowLayout layout = computeLayout(96, 2);
        CHECK(layout.windowWidth == 460);
        CHECK(layout.margin == 18);
        CHECK(layout.pairRowHeight == 62);
        CHECK(layout.titleBar.width == 460);   // the title bar is full-bleed
        CHECK(layout.titleBar.x == 0);
        CHECK(layout.windowHeight > 300 && layout.windowHeight < 600);
    }

    {
        TEST_CASE("no two top-level bands overlap, at any scaling or pair count");
        for (const int dpi : kDpis) {
            for (std::size_t pairs = 0; pairs <= 8; ++pairs) {
                const WindowLayout layout = computeLayout(dpi, pairs);
                const std::vector<Rect> rects = layout.topLevelRects();
                for (std::size_t i = 0; i < rects.size(); ++i) {
                    for (std::size_t j = i + 1; j < rects.size(); ++j) {
                        CHECK(!overlaps(rects.at(i), rects.at(j)));
                    }
                }
            }
        }
    }

    {
        TEST_CASE("everything stays inside the window and off the margins");
        for (const int dpi : kDpis) {
            const WindowLayout layout = computeLayout(dpi, 3);
            const Rect window{0, 0, layout.windowWidth, layout.windowHeight};

            for (const Rect& rect : layout.topLevelRects()) {
                CHECK(!rect.isEmpty());
                CHECK(contains(window, rect));

                if (rect.width == layout.titleBar.width) {
                    continue;  // the title bar is deliberately full-bleed
                }
                CHECK(rect.x >= layout.margin - 1);
                CHECK(withinTolerance(rect.right(), layout.windowWidth - layout.margin));
            }
        }
    }

    {
        TEST_CASE("nested pieces sit inside the band that owns them");
        for (const int dpi : kDpis) {
            const WindowLayout layout = computeLayout(dpi, 2);

            CHECK(contains(layout.titleBar, layout.titleMark));
            CHECK(contains(layout.titleBar, layout.titleText));
            CHECK(contains(layout.titleBar, layout.minimiseButton));
            CHECK(contains(layout.titleBar, layout.closeButton));

            CHECK(contains(layout.statusCard, layout.statusDot));
            CHECK(contains(layout.statusCard, layout.statusHeading));
            CHECK(contains(layout.statusCard, layout.statusDetail));
            CHECK(contains(layout.statusCard, layout.enableToggle));

            // The caption buttons sit side by side without colliding.
            CHECK(!overlaps(layout.minimiseButton, layout.closeButton));
            CHECK(layout.minimiseButton.right() <= layout.closeButton.x);
            CHECK(!overlaps(layout.titleText, layout.minimiseButton));

            // The status text must not run under the toggle.
            CHECK(layout.statusHeading.right() <= layout.enableToggle.x);
            CHECK(layout.statusDetail.right() <= layout.enableToggle.x);
        }
    }

    {
        TEST_CASE("the add row reads left to right without collisions");
        for (const int dpi : kDpis) {
            const WindowLayout layout = computeLayout(dpi, 2);
            CHECK(layout.firstPicker.right() <= layout.addArrow.x);
            CHECK(layout.addArrow.right() <= layout.secondPicker.x);
            CHECK(layout.secondPicker.right() <= layout.addButton.x);
            CHECK(!overlaps(layout.addArrow, layout.firstPicker));
            CHECK(!overlaps(layout.addArrow, layout.secondPicker));

            // The pickers, arrow and button share one row.
            CHECK(layout.firstPicker.y == layout.secondPicker.y);
            CHECK(layout.firstPicker.y == layout.addButton.y);
        }
    }

    {
        TEST_CASE("the list grows with the pairs, then stops");
        const WindowLayout none = computeLayout(96, 0);
        const WindowLayout two = computeLayout(96, 2);
        const WindowLayout three = computeLayout(96, 3);
        const WindowLayout six = computeLayout(96, 6);
        const WindowLayout many = computeLayout(96, 50);

        // An empty list still reserves room, so the window never collapses.
        CHECK(none.pairList.height == two.pairList.height);
        CHECK(two.pairList.height == 2 * two.pairRowHeight);
        CHECK(three.pairList.height > two.pairList.height);
        CHECK(three.windowHeight > two.windowHeight);

        // Past the cap the list scrolls instead of the window growing forever.
        CHECK(many.pairList.height == six.pairList.height);
        CHECK(many.windowHeight == six.windowHeight);
        CHECK(six.pairList.height == 6 * six.pairRowHeight);
    }

    {
        TEST_CASE("the whole layout scales with the display");
        const WindowLayout base = computeLayout(96, 3);
        const WindowLayout scaled = computeLayout(192, 3);
        CHECK(scaled.windowWidth == 2 * base.windowWidth);
        CHECK(scaled.windowHeight == 2 * base.windowHeight);
        CHECK(scaled.pairRowHeight == 2 * base.pairRowHeight);
        CHECK(scaled.pairList.y == 2 * base.pairList.y);
        CHECK(scaled.addButton.width == 2 * base.addButton.width);
    }

    {
        TEST_CASE("a pair row lays out left to right inside its card");
        for (const int dpi : kDpis) {
            const WindowLayout layout = computeLayout(dpi, 2);
            const Rect row{layout.pairList.x, layout.pairList.y, layout.pairList.width,
                           layout.pairRowHeight};
            const PairRowGeometry geometry = computePairRowGeometry(row, dpi);

            // The card leaves a gap below it, which is what separates the rows.
            CHECK(contains(row, geometry.card));
            CHECK(geometry.card.height < row.height);
            CHECK(geometry.card.width == row.width);

            for (const Rect& piece :
                 {geometry.firstCap, geometry.arrows, geometry.secondCap, geometry.removeGlyph}) {
                CHECK(contains(geometry.card, piece));
                CHECK(!piece.isEmpty());
            }

            CHECK(geometry.firstCap.right() < geometry.arrows.x);
            CHECK(geometry.arrows.right() < geometry.secondCap.x);
            CHECK(geometry.secondCap.right() < geometry.removeGlyph.x);

            // The keycaps match and are square, so the pair reads as a pair.
            CHECK(geometry.firstCap.width == geometry.secondCap.width);
            CHECK(geometry.firstCap.width == geometry.firstCap.height);
            CHECK(geometry.firstCap.y == geometry.secondCap.y);

            // A lit cap paints a glow around itself; it must stay in the card.
            const int glow = scaleForDpi(4, dpi);
            const Rect lit{geometry.firstCap.x - glow, geometry.firstCap.y - glow,
                           geometry.firstCap.width + (2 * glow),
                           geometry.firstCap.height + (2 * glow)};
            CHECK(contains(geometry.card, lit));
        }
    }

    {
        TEST_CASE("rect helpers behave at the edges");
        const Rect a{0, 0, 10, 10};
        const Rect touching{10, 0, 10, 10};
        const Rect overlapping{9, 0, 10, 10};
        const Rect empty{0, 0, 0, 10};

        CHECK(!overlaps(a, touching));   // shared edge is not an overlap
        CHECK(overlaps(a, overlapping));
        CHECK(!overlaps(a, empty));
        CHECK(contains(a, Rect{0, 0, 10, 10}));
        CHECK(contains(a, Rect{2, 2, 3, 3}));
        CHECK(!contains(a, overlapping));
        CHECK(a.right() == 10 && a.bottom() == 10);
        CHECK(empty.isEmpty());
    }

    return ::testing::summarize("test_layout");
}
