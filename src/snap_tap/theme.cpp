#include "snap_tap/theme.h"

#include "snap_tap/layout.h"

// gdiplus.h expects unqualified min/max, which NOMINMAX removes.
#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <objidl.h>
#include <gdiplus.h>

#include <cwchar>

namespace snaptap {
namespace {

// Point sizes from the design, converted against the window's DPI.
constexpr int kBodyPoint = 9;
constexpr int kTitlePoint = 10;
constexpr int kSectionPoint = 7;
constexpr int kKeycapPoint = 13;
constexpr int kSmallPoint = 8;

int heightForPoint(const int points, const int dpi) {
    return -MulDiv(points, dpi, 72);
}

FontHandle makeFont(const LOGFONTW& base, const int points, const int dpi, const int weight,
                    const wchar_t* const face = nullptr) {
    LOGFONTW font = base;
    font.lfHeight = heightForPoint(points, dpi);
    font.lfWidth = 0;
    font.lfWeight = weight;
    font.lfQuality = CLEARTYPE_QUALITY;
    if (face != nullptr) {
        wcsncpy_s(font.lfFaceName, face, _TRUNCATE);
    }
    return FontHandle(CreateFontIndirectW(&font));
}

LOGFONTW systemUiFont() {
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        return metrics.lfMessageFont;
    }

    LOGFONTW fallback{};
    wcsncpy_s(fallback.lfFaceName, L"Segoe UI", _TRUNCATE);
    return fallback;
}

// Returns true if the named face is installed, so the keycap font can fall back
// from Cascadia Mono to Consolas without drawing in a proportional face.
bool fontExists(const wchar_t* const face) {
    const HDC screen = GetDC(nullptr);
    if (screen == nullptr) {
        return false;
    }

    LOGFONTW query{};
    query.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(query.lfFaceName, face, _TRUNCATE);

    bool found = false;
    EnumFontFamiliesExW(
        screen, &query,
        [](const LOGFONTW*, const TEXTMETRICW*, DWORD, const LPARAM param) -> int {
            *reinterpret_cast<bool*>(param) = true;
            return 0;
        },
        reinterpret_cast<LPARAM>(&found), 0);

    ReleaseDC(nullptr, screen);
    return found;
}

}  // namespace

const Palette& palette() {
    // COLORREF is 0x00BBGGRR, so RGB() reverses the hex from the design.
    static const Palette nordFrost{
        RGB(0x2E, 0x34, 0x40),  // bg
        RGB(0x3B, 0x42, 0x52),  // surface
        RGB(0x43, 0x4C, 0x5E),  // raised
        RGB(0x4C, 0x56, 0x6A),  // border
        RGB(0xEC, 0xEF, 0xF4),  // text
        RGB(0xA9, 0xB1, 0xC0),  // muted
        RGB(0x88, 0xC0, 0xD0),  // accent
        RGB(0x16, 0x20, 0x2B),  // accentInk
        RGB(0x4C, 0x5E, 0x6E),  // glow: accent at 22% over surface
        RGB(0x49, 0x51, 0x63),  // hover
    };
    return nordFrost;
}

Fonts::Fonts(const int dpi) {
    const LOGFONTW base = systemUiFont();

    body_ = makeFont(base, kBodyPoint, dpi, FW_NORMAL);
    title_ = makeFont(base, kTitlePoint, dpi, FW_SEMIBOLD);
    sectionLabel_ = makeFont(base, kSectionPoint, dpi, FW_BOLD);
    caption_ = makeFont(base, kSmallPoint, dpi, FW_NORMAL);

    const wchar_t* const monoFace = fontExists(L"Cascadia Mono") ? L"Cascadia Mono" : L"Consolas";
    keycap_ = makeFont(base, kKeycapPoint, dpi, FW_BOLD, monoFace);
}

Brushes::Brushes()
    : background_(CreateSolidBrush(palette().bg)), surface_(CreateSolidBrush(palette().surface)) {}

GdiPlusSession::GdiPlusSession() {
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&token_, &input, nullptr) != Gdiplus::Ok) {
        token_ = 0;
    }
}

GdiPlusSession::~GdiPlusSession() {
    if (token_ != 0) {
        Gdiplus::GdiplusShutdown(token_);
        token_ = 0;
    }
}

}  // namespace snaptap
