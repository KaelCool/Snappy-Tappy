#pragma once

#include <cstddef>
#include <vector>

namespace snaptap {

// A rectangle in client coordinates. Deliberately not Win32's RECT: keeping this
// header free of windows.h is what lets the layout maths be unit-tested.
struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    int right() const { return x + width; }
    int bottom() const { return y + height; }
    bool isEmpty() const { return width <= 0 || height <= 0; }
};

// Touching edges do not count as overlapping.
bool overlaps(const Rect& lhs, const Rect& rhs);

// True when `inner` sits entirely within `outer`.
bool contains(const Rect& outer, const Rect& inner);

// Where every piece of the Command Deck window goes. All values are already
// scaled for the target DPI, so the caller just places controls.
struct WindowLayout {
    int dpi = 96;
    int windowWidth = 0;
    int windowHeight = 0;
    int margin = 0;

    Rect titleBar;
    Rect titleMark;      // the two-square app mark
    Rect titleText;
    Rect minimiseButton;
    Rect closeButton;

    Rect statusCard;     // the master toggle card
    Rect statusDot;
    Rect statusHeading;
    Rect statusDetail;
    Rect enableToggle;

    Rect pairsLabel;     // the letter-spaced "PAIRS" heading
    Rect pairList;
    int pairRowHeight = 0;

    Rect firstPicker;
    Rect addArrow;
    Rect secondPicker;
    Rect addButton;
    Rect removeButton;

    Rect configLabel;

    // The bands the window is divided into. These are peers: none may overlap
    // another, and all must sit inside the window. Rects nested inside a band
    // (the status card's contents, the title bar's contents) are not included.
    std::vector<Rect> topLevelRects() const;
};

// Scales a 96-dpi design value to the given dpi.
int scaleForDpi(int value, int dpi);

// Builds the layout. `pairCount` sizes the list; it is clamped to a sensible
// range so the window neither collapses nor grows without limit.
WindowLayout computeLayout(int dpi, std::size_t pairCount);

// Where the pieces of one pair row sit inside that row. Drawing and click
// hit-testing both derive from this, so the remove glyph cannot drift away
// from the pixels the user is aiming at.
struct PairRowGeometry {
    Rect card;
    Rect firstCap;
    Rect arrows;
    Rect secondCap;
    Rect removeGlyph;
};

PairRowGeometry computePairRowGeometry(const Rect& row, int dpi);

// The pair list shows this many rows before it starts scrolling.
inline constexpr std::size_t kMinVisiblePairRows = 2;
inline constexpr std::size_t kMaxVisiblePairRows = 6;

}  // namespace snaptap
