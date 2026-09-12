#include "snap_tap/config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <sstream>

namespace snaptap {
namespace {

std::string toLower(const std::string_view text) {
    std::string lower(text);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lower;
}

std::vector<std::string> tokenize(const std::string& line) {
    std::istringstream stream(line);
    std::vector<std::string> tokens;
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

std::optional<bool> parseBool(const std::string_view text) {
    const std::string lower = toLower(text);
    if (lower == "true" || lower == "on" || lower == "yes" || lower == "1") {
        return true;
    }
    if (lower == "false" || lower == "off" || lower == "no" || lower == "0") {
        return false;
    }
    return std::nullopt;
}

std::string errorAt(const int lineNumber, const std::string& message) {
    return "line " + std::to_string(lineNumber) + ": " + message;
}

bool configUsesKey(const Config& config, const KeyCode key) {
    return std::any_of(config.pairs.begin(), config.pairs.end(), [key](const KeyPairConfig& pair) {
        return pair.first == key || pair.second == key;
    });
}

// Strips a trailing carriage return so files saved with CRLF endings parse the
// same as LF ones.
std::string stripCarriageReturn(std::string line) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line;
}

}  // namespace

Config defaultConfig() {
    Config config;
    config.enabled = true;
    config.pairs.push_back(KeyPairConfig{'A', 'D'});
    return config;
}

ParseResult parseConfig(const std::string_view text) {
    ParseResult result;
    std::istringstream stream{std::string(text)};
    std::string rawLine;
    int lineNumber = 0;

    while (std::getline(stream, rawLine)) {
        ++lineNumber;
        const std::string line = stripCarriageReturn(rawLine);
        const std::vector<std::string> tokens = tokenize(line);
        if (tokens.empty() || tokens.front().front() == '#') {
            continue;  // blank line or comment
        }

        const std::string directive = toLower(tokens.front());
        if (directive == "pair") {
            if (tokens.size() != 3) {
                result.errors.push_back(errorAt(lineNumber, "pair needs exactly two key names"));
                continue;
            }
            const std::optional<KeyCode> first = keyCodeFromName(tokens[1]);
            const std::optional<KeyCode> second = keyCodeFromName(tokens[2]);
            if (!first.has_value()) {
                result.errors.push_back(errorAt(lineNumber, "unknown key name: " + tokens[1]));
                continue;
            }
            if (!second.has_value()) {
                result.errors.push_back(errorAt(lineNumber, "unknown key name: " + tokens[2]));
                continue;
            }
            if (*first == *second) {
                result.errors.push_back(errorAt(lineNumber, "a key cannot be paired with itself"));
                continue;
            }
            if (configUsesKey(result.config, *first) || configUsesKey(result.config, *second)) {
                result.errors.push_back(
                    errorAt(lineNumber, "key already belongs to another pair"));
                continue;
            }
            result.config.pairs.push_back(KeyPairConfig{*first, *second});
        } else if (directive == "enabled") {
            if (tokens.size() != 2) {
                result.errors.push_back(errorAt(lineNumber, "enabled needs one value"));
                continue;
            }
            const std::optional<bool> value = parseBool(tokens[1]);
            if (!value.has_value()) {
                result.errors.push_back(errorAt(lineNumber, "not a boolean: " + tokens[1]));
                continue;
            }
            result.config.enabled = *value;
        } else {
            result.errors.push_back(errorAt(lineNumber, "unknown directive: " + tokens.front()));
        }
    }

    return result;
}

std::string serializeConfig(const Config& config) {
    std::ostringstream out;
    out << "# Snap Tap configuration\n";
    out << "enabled " << (config.enabled ? "true" : "false") << "\n";
    for (const KeyPairConfig& pair : config.pairs) {
        out << "pair " << keyNameFromCode(pair.first) << " " << keyNameFromCode(pair.second)
            << "\n";
    }
    return out.str();
}

ParseResult loadConfigFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        ParseResult result;
        result.config = defaultConfig();
        return result;
    }

    std::ostringstream contents;
    contents << file.rdbuf();
    return parseConfig(contents.str());
}

bool saveConfigFile(const std::string& path, const Config& config) {
    std::ofstream file(path, std::ios::trunc);
    if (!file) {
        return false;
    }
    file << serializeConfig(config);
    return file.good();
}

}  // namespace snaptap
