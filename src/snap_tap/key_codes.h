#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace snaptap {

// A Windows virtual-key code. Declared here as a plain integer so this header,
// and everything built on it, stays free of windows.h.
using KeyCode = int;

// Looks up a key by name, case-insensitively: "A", "d", "Left", "SPACE".
// Returns std::nullopt if the name is not recognised.
std::optional<KeyCode> keyCodeFromName(std::string_view name);

// Canonical display name for a key code, or an empty string if unknown.
std::string keyNameFromCode(KeyCode code);

// Every key the app understands, in the order they should be offered to the
// user: letters, then digits, then the named keys.
const std::vector<KeyCode>& allKeys();

}  // namespace snaptap
