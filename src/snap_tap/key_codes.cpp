#include "snap_tap/key_codes.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace snaptap {
namespace {

struct NamedKey {
    std::string_view name;
    KeyCode code;
};

// Keys whose names are not simply their character. Virtual-key values match the
// VK_* constants in WinUser.h; they are spelled out so this file needs no Windows header.
constexpr std::array<NamedKey, 22> kNamedKeys = {{
    {"SPACE", 0x20},
    {"LEFT", 0x25},
    {"UP", 0x26},
    {"RIGHT", 0x27},
    {"DOWN", 0x28},
    {"SHIFT", 0x10},
    {"CTRL", 0x11},
    {"ALT", 0x12},
    {"LSHIFT", 0xA0},
    {"RSHIFT", 0xA1},
    {"LCTRL", 0xA2},
    {"RCTRL", 0xA3},
    {"LALT", 0xA4},
    {"RALT", 0xA5},
    {"TAB", 0x09},
    {"ENTER", 0x0D},
    {"ESC", 0x1B},
    {"BACKSPACE", 0x08},
    {"INSERT", 0x2D},
    {"DELETE", 0x2E},
    {"HOME", 0x24},
    {"END", 0x23},
}};

std::string toUpper(const std::string_view text) {
    std::string upper(text);
    std::transform(upper.begin(), upper.end(), upper.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return upper;
}

}  // namespace

std::optional<KeyCode> keyCodeFromName(const std::string_view name) {
    const std::string upper = toUpper(name);
    if (upper.empty()) {
        return std::nullopt;
    }

    // Letters and digits map directly onto their ASCII values.
    if (upper.size() == 1) {
        const char ch = upper.front();
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            return static_cast<KeyCode>(ch);
        }
        return std::nullopt;
    }

    for (const NamedKey& key : kNamedKeys) {
        if (key.name == upper) {
            return key.code;
        }
    }
    return std::nullopt;
}

std::string keyNameFromCode(const KeyCode code) {
    if ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9')) {
        return std::string(1, static_cast<char>(code));
    }

    for (const NamedKey& key : kNamedKeys) {
        if (key.code == code) {
            return std::string(key.name);
        }
    }
    return std::string();
}

}  // namespace snaptap
