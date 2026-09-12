#include "snap_tap/layout.h"

#include <algorithm>

namespace snaptap {
namespace {

// The design, in 96-dpi units, as drawn in the approved Command Deck mockup.
constexpr int kWindowWidth = 460;
constexpr int kTitleBarHeight = 40;
constexpr int kBodyPadding = 18;

constexpr int kMarkSize = 18;
constexpr int kCaptionButtonWidth = 34;
constexpr int kCaptionButtonHeight = 28;

constexpr int kStatusCardHeight = 64;
constexpr int kStatusDotSize = 10;
constexpr int kCardPadding = 16;
constexpr int kToggleWidth = 42;
constexpr int kToggleHeight = 24;

constexpr int kSectionLabelHeight = 14;
constexpr int kPairRowHeight = 62;

constexpr int kControlHeight = 38;
constexpr int kPickerWidth = 84;
constexpr int kArrowSize = 18;
constexpr int kAddButtonWidth = 104;
constexpr int kRemoveButtonWidth = 130;
constexpr int kRemoveButtonHeight = 30;

// Scales the edges, not the size. Rounding a position and a width separately
// lets two rects that touch at 96 dpi round into a one-pixel overlap; scaling
// both edges keeps adjacent rects exactly adjacent at every scaling.
Rect scaleRect(const Rect& rect, const int dpi) {
    const int left = scaleForDpi(rect.x, dpi);
    const int top = scaleForDpi(rect.y, dpi);
    const int right = scaleForDpi(rect.right(), dpi);
    const int bottom = scaleForDpi(rect.bottom(), dpi);
    return Rect{left, top, right - left, bottom - top};
}

}  // namespace

bool overlaps(const Rect& lhs, const Rect& rhs) {
    if (lhs.isEmpty() || rhs.isEmpty()) {
        return false;
    }
    return lhs.x < rhs.right() && rhs.x < lhs.right() && lhs.y < rhs.bottom() &&
           rhs.y < lhs.bottom();
}

bool contains(const Rect& outer, const Rect& inner) {
    return inner.x >= outer.x && inner.y >= outer.y && inner.right() <= outer.right() &&
           inner.bottom() <= outer.bottom();
}

int scaleForDpi(const int value, const int dpi) {
    // Rounded rather than truncated, so a column of scaled gaps does not drift.
    return ((value * dpi) + 48) / 96;
}

std::vector<Rect> WindowLayout::topLevelRects() const {
    return {titleBar,   statusCard,   pairsLabel, pairList,    removeButton,
            firstPicker, secondPicker, addButton,  configLabel};
}

PairRowGeometry computePairRowGeometry(const Rect& row, const int dpi) {
    // In 96-dpi units: a 54-tall card in a 62-tall row, leaving an 8px gap.
    const int cardHeight = scaleForDpi(54, dpi);
    const int padding = scaleForDpi(14, dpi);
    const int capSize = scaleForDpi(40, dpi);
    const int arrowSize = scaleForDpi(22, dpi);
    const int glyphSize = scaleForDpi(26, dpi);
    const int gap = scaleForDpi(12, dpi);

    PairRowGeometry out;
    out.card = Rect{row.x, row.y, row.width, cardHeight};

    const auto centred = [&out, cardHeight](const int x, const int size) {
        return Rect{x, out.card.y + ((cardHeight - size) / 2), size, size};
    };

    out.firstCap = centred(out.card.x + padding, capSize);
    out.arrows = centred(out.firstCap.right() + gap, arrowSize);
    out.secondCap = centred(out.arrows.right() + gap, capSize);
    out.removeGlyph = centred(out.card.right() - padding - glyphSize, glyphSize);
    return out;
}

WindowLayout computeLayout(const int dpi, const std::size_t pairCount) {
    WindowLayout out;
    out.dpi = dpi;

    const std::size_t visibleRows =
        std::clamp(pairCount, kMinVisiblePairRows, kMaxVisiblePairRows);
    const int listHeight = static_cast<int>(visibleRows) * kPairRowHeight;
    const int contentLeft = kBodyPadding;
    const int contentWidth = kWindowWidth - (2 * kBodyPadding);
    const int contentRight = contentLeft + contentWidth;

    // --- title bar -------------------------------------------------------
    Rect titleBar{0, 0, kWindowWidth, kTitleBarHeight};
    Rect titleMark{14, (kTitleBarHeight - kMarkSize) / 2, kMarkSize, kMarkSize};
    Rect titleText{titleMark.right() + 10, 11, 220, 18};
    Rect closeButton{kWindowWidth - 6 - kCaptionButtonWidth, 6, kCaptionButtonWidth,
                     kCaptionButtonHeight};
    Rect minimiseButton{closeButton.x - kCaptionButtonWidth, 6, kCaptionButtonWidth,
                        kCaptionButtonHeight};

    int y = kTitleBarHeight + kBodyPadding;

    // --- status card -----------------------------------------------------
    Rect statusCard{contentLeft, y, contentWidth, kStatusCardHeight};
    Rect statusDot{statusCard.x + kCardPadding,
                   statusCard.y + ((kStatusCardHeight - kStatusDotSize) / 2), kStatusDotSize,
                   kStatusDotSize};
    const int statusTextLeft = statusDot.right() + 14;
    Rect statusHeading{statusTextLeft, statusCard.y + 13, 240, 18};
    Rect statusDetail{statusTextLeft, statusCard.y + 33, 240, 16};
    Rect enableToggle{statusCard.right() - kCardPadding - kToggleWidth,
                      statusCard.y + ((kStatusCardHeight - kToggleHeight) / 2), kToggleWidth,
                      kToggleHeight};
    y = statusCard.bottom() + 16;

    // --- pair list -------------------------------------------------------
    Rect pairsLabel{contentLeft, y, 200, kSectionLabelHeight};
    y = pairsLabel.bottom() + 8;

    Rect pairList{contentLeft, y, contentWidth, listHeight};
    y = pairList.bottom() + 10;

    Rect removeButton{contentRight - kRemoveButtonWidth, y, kRemoveButtonWidth,
                      kRemoveButtonHeight};
    y = removeButton.bottom() + 16;

    // --- add row ---------------------------------------------------------
    Rect firstPicker{contentLeft, y, kPickerWidth, kControlHeight};
    Rect addArrow{firstPicker.right() + 10, y + ((kControlHeight - kArrowSize) / 2), kArrowSize,
                  kArrowSize};
    Rect secondPicker{addArrow.right() + 10, y, kPickerWidth, kControlHeight};
    Rect addButton{contentRight - kAddButtonWidth, y, kAddButtonWidth, kControlHeight};
    y = firstPicker.bottom() + 14;

    // --- footer ----------------------------------------------------------
    Rect configLabel{contentLeft, y, contentWidth, kSectionLabelHeight};
    y = configLabel.bottom() + kBodyPadding;

    out.windowWidth = scaleForDpi(kWindowWidth, dpi);
    out.windowHeight = scaleForDpi(y, dpi);
    out.margin = scaleForDpi(kBodyPadding, dpi);
    out.pairRowHeight = scaleForDpi(kPairRowHeight, dpi);

    out.titleBar = scaleRect(titleBar, dpi);
    out.titleMark = scaleRect(titleMark, dpi);
    out.titleText = scaleRect(titleText, dpi);
    out.minimiseButton = scaleRect(minimiseButton, dpi);
    out.closeButton = scaleRect(closeButton, dpi);

    out.statusCard = scaleRect(statusCard, dpi);
    out.statusDot = scaleRect(statusDot, dpi);
    out.statusHeading = scaleRect(statusHeading, dpi);
    out.statusDetail = scaleRect(statusDetail, dpi);
    out.enableToggle = scaleRect(enableToggle, dpi);

    out.pairsLabel = scaleRect(pairsLabel, dpi);
    out.pairList = scaleRect(pairList, dpi);

    out.firstPicker = scaleRect(firstPicker, dpi);
    out.addArrow = scaleRect(addArrow, dpi);
    out.secondPicker = scaleRect(secondPicker, dpi);
    out.addButton = scaleRect(addButton, dpi);
    out.removeButton = scaleRect(removeButton, dpi);

    out.configLabel = scaleRect(configLabel, dpi);
    return out;
}

}  // namespace snaptap
