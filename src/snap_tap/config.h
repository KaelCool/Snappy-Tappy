#pragma once

#include "snap_tap/key_codes.h"

#include <string>
#include <string_view>
#include <vector>

namespace snaptap {

struct KeyPairConfig {
    KeyCode first = 0;
    KeyCode second = 0;
};

struct Config {
    bool enabled = true;
    std::vector<KeyPairConfig> pairs;
};

// The launch default required by SPEC.md: enabled, with A/D as the only pair.
Config defaultConfig();

struct ParseResult {
    Config config;
    // One message per rejected line, each naming the line number. Valid lines
    // are still applied, so a single typo does not discard a whole config.
    std::vector<std::string> errors;

    bool ok() const { return errors.empty(); }
};

// Parses the line-based config format:
//
//     # comments and blank lines are ignored
//     enabled true
//     pair A D
//     pair W S
//
// Key names are resolved with keyCodeFromName, so they are case-insensitive.
ParseResult parseConfig(std::string_view text);

// Renders a config back into the format parseConfig accepts.
std::string serializeConfig(const Config& config);

// Reads a config file. A missing or unreadable file is not an error: the
// defaults are returned so a first run behaves exactly as the spec describes.
ParseResult loadConfigFile(const std::string& path);

// Writes a config file, returning false if it could not be written.
bool saveConfigFile(const std::string& path, const Config& config);

}  // namespace snaptap
