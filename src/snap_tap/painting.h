#pragma once

#include "snap_tap/layout.h"
#include "snap_tap/theme.h"

#include <memory>
#include <string>

namespace snaptap {

// Paints into an off-screen bitmap and blits it in one go on destruction.
// Dark custom drawing flickers badly without this.
class BufferedDC {
public:
    BufferedDC(HDC target, const Rect& bounds);
    ~BufferedDC();

    BufferedDC(const BufferedDC&) = delete;
    BufferedDC& operator=(const BufferedDC&) = delete;

    HDC get() const { return memory_ != nullptr ? memory_ : target_; }

private:
    HDC target_ = nullptr;
    HDC memory_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previousBitmap_ = nullptr;
    Rect bounds_;
};

// The app mark from the design - two offset rounded squares, one key handing
// over to the other - rendered to icons at runtime, so the build still needs no
// resource file. Owns the icons and destroys them with the window.
class AppIcon {
public:
    AppIcon();
    ~AppIcon();

    AppIcon(const AppIcon&) = delete;
    AppIcon& operator=(const AppIcon&) = delete;

    // Not small()/large(): rpcndr.h, via windows.h, defines small as a macro.
    HICON smallIcon() const { return smallIcon_; }
    HICON largeIcon() const { return largeIcon_; }

private:
    HICON smallIcon_ = nullptr;
    HICON largeIcon_ = nullptr;
};

// The drawing vocabulary the Command Deck design repeats. Shapes go through
// GDI+ so the 9px radii are antialiased; text stays on GDI, whose ClearType is
// noticeably crisper than GDI+ text rendering.
class Canvas {
public:
    Canvas(HDC dc, const Fonts& fonts, int dpi);
    ~Canvas();

    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    HDC dc() const { return dc_; }
    int scaled(int value) const;

    void fill(const Rect& rect, COLORREF color);
    void roundedRect(const Rect& rect, int radius, COLORREF fill);
    void roundedRectOutlined(const Rect& rect, int radius, COLORREF fill, COLORREF border);
    void circle(const Rect& rect, COLORREF fill);
    void horizontalHairline(const Rect& rect, COLORREF color);

    // A keycap, lit when its key currently owns the pair. A lit cap also gets
    // the surrounding glow, so leave a few pixels of room around it.
    void keycap(const Rect& rect, const std::wstring& letter, bool lit);

    void togglePill(const Rect& rect, bool on);
    void swapArrows(const Rect& rect, COLORREF leading, COLORREF trailing);
    void chevronDown(const Rect& rect, COLORREF color);
    void crossGlyph(const Rect& rect, COLORREF color, double thickness = 1.4);
    void minimiseGlyph(const Rect& rect, COLORREF color);
    void appMark(const Rect& rect, COLORREF front, COLORREF back);

    // `tracking` is extra space per character, for the letter-spaced labels;
    // GDI has no real tracking control.
    void text(const Rect& rect, const std::wstring& value, HFONT font, COLORREF color,
              UINT format, int tracking = 0);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    HDC dc_;
    const Fonts& fonts_;
    int dpi_;
};

}  // namespace snaptap
