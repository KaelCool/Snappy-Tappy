#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <memory>
#include <type_traits>

namespace snaptap {

// The Nord Frost palette from the approved Command Deck design.
struct Palette {
    COLORREF bg;         // window ground
    COLORREF surface;    // cards and the title bar
    COLORREF raised;     // idle keycaps and pickers
    COLORREF border;     // hairlines
    COLORREF text;       // primary text
    COLORREF muted;      // secondary text and icon strokes
    COLORREF accent;     // the lit keycap, the toggle, the Add button
    COLORREF accentInk;  // text drawn on top of the accent
    COLORREF glow;       // accent at 22% over surface, pre-blended: GDI has no alpha
    COLORREF hover;      // surface lifted slightly, for hovered controls
};

const Palette& palette();

// GDI handles are freed with DeleteObject, so one deleter covers brushes, pens
// and fonts alike.
struct GdiDeleter {
    void operator()(void* handle) const {
        if (handle != nullptr) {
            DeleteObject(static_cast<HGDIOBJ>(handle));
        }
    }
};

template <typename Handle>
using GdiHandle = std::unique_ptr<std::remove_pointer_t<Handle>, GdiDeleter>;

using BrushHandle = GdiHandle<HBRUSH>;
using PenHandle = GdiHandle<HPEN>;
using FontHandle = GdiHandle<HFONT>;

// The four faces the design uses, built once at the window's DPI.
class Fonts {
public:
    explicit Fonts(int dpi);

    HFONT body() const { return body_.get(); }
    HFONT title() const { return title_.get(); }
    HFONT sectionLabel() const { return sectionLabel_.get(); }
    HFONT keycap() const { return keycap_.get(); }
    // Not named small(): rpcndr.h, via windows.h, defines small as a macro.
    HFONT caption() const { return caption_.get(); }

private:
    FontHandle body_;
    FontHandle title_;
    FontHandle sectionLabel_;
    FontHandle keycap_;
    FontHandle caption_;
};

// Cached brushes for the WM_CTLCOLOR* replies, which must hand back a brush
// rather than paint themselves.
class Brushes {
public:
    Brushes();

    HBRUSH background() const { return background_.get(); }
    HBRUSH surface() const { return surface_.get(); }

private:
    BrushHandle background_;
    BrushHandle surface_;
};

// Starts GDI+ for the lifetime of the object. GDI+ gives antialiased rounded
// rectangles, which GDI's RoundRect cannot do and this design needs.
class GdiPlusSession {
public:
    GdiPlusSession();
    ~GdiPlusSession();

    GdiPlusSession(const GdiPlusSession&) = delete;
    GdiPlusSession& operator=(const GdiPlusSession&) = delete;

    bool ok() const { return token_ != 0; }

private:
    ULONG_PTR token_ = 0;
};

}  // namespace snaptap
