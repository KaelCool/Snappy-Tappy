#include "snap_tap/painting.h"

// gdiplus.h expects unqualified min/max, which NOMINMAX removes.
#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <objidl.h>
#include <gdiplus.h>

namespace snaptap {
namespace {

Gdiplus::Color toGdiPlus(const COLORREF color) {
    return Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color));
}

// GDI+ fills are inclusive of the origin and exclusive of the far edge, so a
// rect is inset by a hair to keep antialiased edges inside the bounds.
Gdiplus::RectF toRectF(const Rect& rect) {
    return Gdiplus::RectF(static_cast<Gdiplus::REAL>(rect.x), static_cast<Gdiplus::REAL>(rect.y),
                          static_cast<Gdiplus::REAL>(rect.width),
                          static_cast<Gdiplus::REAL>(rect.height));
}

// GraphicsPath is non-copyable, so it is filled in place rather than returned.
void buildRoundedPath(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect,
                      const Gdiplus::REAL radius) {
    path.Reset();
    const Gdiplus::REAL diameter = radius * 2.0f;
    if (radius <= 0.0f) {
        path.AddRectangle(rect);
        path.CloseFigure();
        return;
    }

    path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(rect.GetRight() - diameter, rect.Y, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(rect.GetRight() - diameter, rect.GetBottom() - diameter, diameter, diameter, 0.0f,
                90.0f);
    path.AddArc(rect.X, rect.GetBottom() - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

Rect inflated(const Rect& rect, const int by) {
    return Rect{rect.x - by, rect.y - by, rect.width + (2 * by), rect.height + (2 * by)};
}

// Maps a point from the design's 22x22 icon grid into the target rect.
Gdiplus::PointF onGrid(const Rect& rect, const double x, const double y, const double grid = 22.0) {
    return Gdiplus::PointF(
        static_cast<Gdiplus::REAL>(rect.x + ((x / grid) * rect.width)),
        static_cast<Gdiplus::REAL>(rect.y + ((y / grid) * rect.height)));
}

}  // namespace

// ---------------------------------------------------------------- BufferedDC

BufferedDC::BufferedDC(const HDC target, const Rect& bounds) : target_(target), bounds_(bounds) {
    if (bounds.isEmpty()) {
        return;
    }
    memory_ = CreateCompatibleDC(target);
    if (memory_ == nullptr) {
        return;
    }
    bitmap_ = CreateCompatibleBitmap(target, bounds.width, bounds.height);
    if (bitmap_ == nullptr) {
        DeleteDC(memory_);
        memory_ = nullptr;
        return;
    }
    previousBitmap_ = SelectObject(memory_, bitmap_);
}

BufferedDC::~BufferedDC() {
    if (memory_ == nullptr) {
        return;
    }
    BitBlt(target_, bounds_.x, bounds_.y, bounds_.width, bounds_.height, memory_, 0, 0, SRCCOPY);
    SelectObject(memory_, previousBitmap_);
    DeleteObject(bitmap_);
    DeleteDC(memory_);
}

// ------------------------------------------------------------------- AppIcon

namespace {

// Renders the mark into a 32-bit alpha icon at the requested size.
HICON renderMark(const int size) {
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = size;
    header.bV5Height = -size;  // top-down rows
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;

    const HDC screen = GetDC(nullptr);
    if (screen == nullptr) {
        return nullptr;
    }
    void* bits = nullptr;
    const HBITMAP color =
        CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits,
                         nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (color == nullptr || bits == nullptr) {
        return nullptr;
    }

    {
        const Palette& colors = palette();
        Gdiplus::Bitmap bitmap(size, size, size * 4, PixelFormat32bppPARGB,
                               static_cast<BYTE*>(bits));
        Gdiplus::Graphics graphics(&bitmap);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));

        // Filled rather than outlined: strokes turn to mud at 16 pixels.
        const Gdiplus::REAL side = static_cast<Gdiplus::REAL>(size) * 0.58f;
        const Gdiplus::REAL radius = static_cast<Gdiplus::REAL>(size) * 0.11f;
        const Gdiplus::REAL offset = static_cast<Gdiplus::REAL>(size) - side;

        Gdiplus::GraphicsPath backPath;
        buildRoundedPath(backPath, Gdiplus::RectF(offset, offset, side, side), radius);
        Gdiplus::SolidBrush backBrush(toGdiPlus(colors.border));
        graphics.FillPath(&backBrush, &backPath);

        Gdiplus::GraphicsPath frontPath;
        buildRoundedPath(frontPath, Gdiplus::RectF(0.0f, 0.0f, side, side), radius);
        Gdiplus::SolidBrush frontBrush(toGdiPlus(colors.accent));
        graphics.FillPath(&frontBrush, &frontPath);
    }

    const HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmColor = color;
    info.hbmMask = mask;

    const HICON icon = CreateIconIndirect(&info);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

}  // namespace

AppIcon::AppIcon()
    : smallIcon_(renderMark(GetSystemMetrics(SM_CXSMICON))),
      largeIcon_(renderMark(GetSystemMetrics(SM_CXICON))) {}

AppIcon::~AppIcon() {
    if (smallIcon_ != nullptr) {
        DestroyIcon(smallIcon_);
    }
    if (largeIcon_ != nullptr) {
        DestroyIcon(largeIcon_);
    }
}

// -------------------------------------------------------------------- Canvas

struct Canvas::Impl {
    explicit Impl(const HDC dc) : graphics(dc) {
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    }
    Gdiplus::Graphics graphics;
};

Canvas::Canvas(const HDC dc, const Fonts& fonts, const int dpi)
    : impl_(std::make_unique<Impl>(dc)), dc_(dc), fonts_(fonts), dpi_(dpi) {}

Canvas::~Canvas() = default;

int Canvas::scaled(const int value) const {
    return scaleForDpi(value, dpi_);
}

void Canvas::fill(const Rect& rect, const COLORREF color) {
    if (rect.isEmpty()) {
        return;
    }
    Gdiplus::SolidBrush brush(toGdiPlus(color));
    impl_->graphics.FillRectangle(&brush, toRectF(rect));
}

void Canvas::roundedRect(const Rect& rect, const int radius, const COLORREF fillColor) {
    if (rect.isEmpty()) {
        return;
    }
    Gdiplus::GraphicsPath path;
    buildRoundedPath(path, toRectF(rect), static_cast<Gdiplus::REAL>(radius));
    Gdiplus::SolidBrush brush(toGdiPlus(fillColor));
    impl_->graphics.FillPath(&brush, &path);
}

void Canvas::roundedRectOutlined(const Rect& rect, const int radius, const COLORREF fillColor,
                                 const COLORREF borderColor) {
    if (rect.isEmpty()) {
        return;
    }
    // Inset by half the stroke so the hairline lands inside the rect.
    Gdiplus::RectF bounds = toRectF(rect);
    bounds.Inflate(-0.5f, -0.5f);

    Gdiplus::GraphicsPath path;
    buildRoundedPath(path, bounds, static_cast<Gdiplus::REAL>(radius));
    Gdiplus::SolidBrush brush(toGdiPlus(fillColor));
    impl_->graphics.FillPath(&brush, &path);

    Gdiplus::Pen pen(toGdiPlus(borderColor), 1.0f);
    impl_->graphics.DrawPath(&pen, &path);
}

void Canvas::circle(const Rect& rect, const COLORREF fillColor) {
    if (rect.isEmpty()) {
        return;
    }
    Gdiplus::SolidBrush brush(toGdiPlus(fillColor));
    impl_->graphics.FillEllipse(&brush, toRectF(rect));
}

void Canvas::horizontalHairline(const Rect& rect, const COLORREF color) {
    fill(Rect{rect.x, rect.y, rect.width, 1}, color);
}

void Canvas::keycap(const Rect& rect, const std::wstring& letter, const bool lit) {
    const Palette& colors = palette();
    const int radius = scaled(9);

    if (lit) {
        // The glow sits behind the cap, in the pre-blended colour.
        roundedRect(inflated(rect, scaled(4)), radius + scaled(4), colors.glow);
        roundedRect(rect, radius, colors.accent);
    } else {
        roundedRectOutlined(rect, radius, colors.raised, colors.border);
    }

    text(rect, letter, fonts_.keycap(), lit ? colors.accentInk : colors.text,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void Canvas::togglePill(const Rect& rect, const bool on) {
    const Palette& colors = palette();
    const int radius = rect.height / 2;

    if (on) {
        roundedRect(rect, radius, colors.accent);
    } else {
        roundedRectOutlined(rect, radius, colors.raised, colors.border);
    }

    const int inset = scaled(3);
    const int knob = rect.height - (2 * inset);
    const int knobX = on ? (rect.right() - inset - knob) : (rect.x + inset);
    circle(Rect{knobX, rect.y + inset, knob, knob}, on ? colors.accentInk : colors.muted);
}

void Canvas::swapArrows(const Rect& rect, const COLORREF leading, const COLORREF trailing) {
    const Gdiplus::REAL width = static_cast<Gdiplus::REAL>(std::max(1, scaled(1)) * 1.4);

    // Top arrow points right, bottom arrow points left: the swap, drawn.
    Gdiplus::Pen top(toGdiPlus(leading), width);
    top.SetStartCap(Gdiplus::LineCapRound);
    top.SetEndCap(Gdiplus::LineCapRound);
    top.SetLineJoin(Gdiplus::LineJoinRound);

    Gdiplus::Pen bottom(toGdiPlus(trailing), width);
    bottom.SetStartCap(Gdiplus::LineCapRound);
    bottom.SetEndCap(Gdiplus::LineCapRound);
    bottom.SetLineJoin(Gdiplus::LineJoinRound);

    impl_->graphics.DrawLine(&top, onGrid(rect, 4, 8), onGrid(rect, 18, 8));
    impl_->graphics.DrawLine(&top, onGrid(rect, 18, 8), onGrid(rect, 14.8, 4.8));
    impl_->graphics.DrawLine(&top, onGrid(rect, 18, 8), onGrid(rect, 14.8, 11.2));

    impl_->graphics.DrawLine(&bottom, onGrid(rect, 18, 15), onGrid(rect, 4, 15));
    impl_->graphics.DrawLine(&bottom, onGrid(rect, 4, 15), onGrid(rect, 7.2, 11.8));
    impl_->graphics.DrawLine(&bottom, onGrid(rect, 4, 15), onGrid(rect, 7.2, 18.2));
}

void Canvas::chevronDown(const Rect& rect, const COLORREF color) {
    Gdiplus::Pen pen(toGdiPlus(color), static_cast<Gdiplus::REAL>(std::max(1, scaled(1)) * 1.3));
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);

    const Gdiplus::PointF points[3] = {onGrid(rect, 4, 8.5, 22.0), onGrid(rect, 11, 15.0, 22.0),
                                       onGrid(rect, 18, 8.5, 22.0)};
    impl_->graphics.DrawLines(&pen, points, 3);
}

void Canvas::crossGlyph(const Rect& rect, const COLORREF color, const double thickness) {
    Gdiplus::Pen pen(toGdiPlus(color),
                     static_cast<Gdiplus::REAL>(std::max(1, scaled(1)) * thickness));
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);

    impl_->graphics.DrawLine(&pen, onGrid(rect, 6, 6), onGrid(rect, 16, 16));
    impl_->graphics.DrawLine(&pen, onGrid(rect, 16, 6), onGrid(rect, 6, 16));
}

void Canvas::minimiseGlyph(const Rect& rect, const COLORREF color) {
    Gdiplus::Pen pen(toGdiPlus(color), static_cast<Gdiplus::REAL>(std::max(1, scaled(1)) * 1.2));
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    impl_->graphics.DrawLine(&pen, onGrid(rect, 6, 11), onGrid(rect, 16, 11));
}

void Canvas::appMark(const Rect& rect, const COLORREF front, const COLORREF back) {
    // Two offset rounded squares: one key handing over to the other.
    Gdiplus::Pen backPen(toGdiPlus(back), static_cast<Gdiplus::REAL>(std::max(1, scaled(1)) * 1.5));
    Gdiplus::Pen frontPen(toGdiPlus(front),
                          static_cast<Gdiplus::REAL>(std::max(1, scaled(1)) * 1.5));

    const Gdiplus::REAL radius = static_cast<Gdiplus::REAL>(std::max(1, scaled(2)));
    const Gdiplus::REAL side = static_cast<Gdiplus::REAL>(rect.width) * 0.5f;

    Gdiplus::RectF backSquare(static_cast<Gdiplus::REAL>(rect.x) + (side * 0.75f),
                              static_cast<Gdiplus::REAL>(rect.y) + (side * 0.75f), side, side);
    Gdiplus::GraphicsPath backPath;
    buildRoundedPath(backPath, backSquare, radius);
    impl_->graphics.DrawPath(&backPen, &backPath);

    Gdiplus::RectF frontSquare(static_cast<Gdiplus::REAL>(rect.x) + (side * 0.1f),
                               static_cast<Gdiplus::REAL>(rect.y) + (side * 0.1f), side, side);
    Gdiplus::GraphicsPath frontPath;
    buildRoundedPath(frontPath, frontSquare, radius);
    impl_->graphics.DrawPath(&frontPen, &frontPath);
}

void Canvas::text(const Rect& rect, const std::wstring& value, const HFONT font,
                  const COLORREF color, const UINT format, const int tracking) {
    if (value.empty()) {
        return;
    }

    const HGDIOBJ previousFont = SelectObject(dc_, font);
    const int previousMode = SetBkMode(dc_, TRANSPARENT);
    const COLORREF previousColor = SetTextColor(dc_, color);
    const int previousExtra = GetTextCharacterExtra(dc_);
    if (tracking != 0) {
        SetTextCharacterExtra(dc_, tracking);
    }

    RECT target{rect.x, rect.y, rect.right(), rect.bottom()};
    DrawTextW(dc_, value.c_str(), static_cast<int>(value.size()), &target, format);

    SetTextCharacterExtra(dc_, previousExtra);
    SetTextColor(dc_, previousColor);
    SetBkMode(dc_, previousMode);
    SelectObject(dc_, previousFont);
}

}  // namespace snaptap
