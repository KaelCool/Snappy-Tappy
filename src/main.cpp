#include "snap_tap/app_paths.h"
#include "snap_tap/config.h"
#include "snap_tap/engine.h"
#include "snap_tap/key_codes.h"
#include "snap_tap/keyboard_hook.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace snaptap;

namespace {

const std::string kConfigFileName = "snaptap.cfg";

std::string toLower(const std::string& text) {
    std::string lower = text;
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

void printHelp() {
    std::cout << "Commands:\n"
              << "  status                 show pairs and current state\n"
              << "  pair add <k1> <k2>     manage a new opposite-key pair, e.g. pair add W S\n"
              << "  pair remove <key>      stop managing the pair containing <key>\n"
              << "  on | off | toggle      enable or disable Snap Tap\n"
              << "  save                   write the current pairs to the config file\n"
              << "  help                   show this list\n"
              << "  quit                   release every held key and exit\n";
}

void printStatus(const KeyboardHook& hook) {
    hook.readEngine([](const SnapTapEngine& engine) {
        std::cout << "Snap Tap is " << (engine.isEnabled() ? "ENABLED" : "disabled") << "\n";
        const std::vector<PairView> pairs = engine.pairs();
        if (pairs.empty()) {
            std::cout << "  no pairs configured (try: pair add A D)\n";
            return;
        }
        for (const PairView& pair : pairs) {
            std::cout << "  " << keyNameFromCode(pair.first) << " <-> "
                      << keyNameFromCode(pair.second);
            if (pair.active != kNoKey) {
                std::cout << "   [active: " << keyNameFromCode(pair.active) << "]";
            }
            std::cout << "\n";
        }
    });
}

// Rebuilds a saveable Config from whatever the engine is currently running.
Config currentConfig(const KeyboardHook& hook) {
    Config config;
    hook.readEngine([&config](const SnapTapEngine& engine) {
        config.enabled = engine.isEnabled();
        for (const PairView& pair : engine.pairs()) {
            config.pairs.push_back(KeyPairConfig{pair.first, pair.second});
        }
    });
    return config;
}

void handlePairCommand(KeyboardHook& hook, const std::vector<std::string>& tokens) {
    const std::string action = tokens.size() > 1 ? toLower(tokens[1]) : std::string();

    if (action == "add") {
        if (tokens.size() != 4) {
            std::cout << "Usage: pair add <key> <key>\n";
            return;
        }
        const std::optional<KeyCode> first = keyCodeFromName(tokens[2]);
        const std::optional<KeyCode> second = keyCodeFromName(tokens[3]);
        if (!first.has_value()) {
            std::cout << "Unknown key: " << tokens[2] << "\n";
            return;
        }
        if (!second.has_value()) {
            std::cout << "Unknown key: " << tokens[3] << "\n";
            return;
        }

        bool added = false;
        hook.withEngine([&](SnapTapEngine& engine) {
            added = engine.addPair(*first, *second);
            return std::vector<OutputAction>{};
        });
        if (added) {
            std::cout << "Managing " << keyNameFromCode(*first) << " <-> "
                      << keyNameFromCode(*second) << "\n";
        } else {
            std::cout << "Could not add that pair: the keys must differ and neither may already "
                         "belong to a pair.\n";
        }
        return;
    }

    if (action == "remove") {
        if (tokens.size() != 3) {
            std::cout << "Usage: pair remove <key>\n";
            return;
        }
        const std::optional<KeyCode> key = keyCodeFromName(tokens[2]);
        if (!key.has_value()) {
            std::cout << "Unknown key: " << tokens[2] << "\n";
            return;
        }

        bool removed = false;
        // Any key the pair still holds is released as part of the same call.
        hook.withEngine([&](SnapTapEngine& engine) {
            RemoveResult result = engine.removePair(*key);
            removed = result.removed;
            return result.released;
        });
        std::cout << (removed ? "Pair removed.\n" : "No pair contains that key.\n");
        return;
    }

    std::cout << "Usage: pair add <key> <key> | pair remove <key>\n";
}

void setEnabled(KeyboardHook& hook, const bool enabled) {
    hook.withEngine([enabled](SnapTapEngine& engine) { return engine.setEnabled(enabled); });
    std::cout << "Snap Tap is now " << (enabled ? "ENABLED" : "disabled") << "\n";
}

}  // namespace

int main() {
    const std::filesystem::path configPath = executableDirectory() / kConfigFileName;
    const ParseResult loaded = loadConfigFile(configPath.string());
    for (const std::string& error : loaded.errors) {
        std::cout << "Config warning (" << configPath.string() << ") " << error << "\n";
    }

    SnapTapEngine engine;
    for (const KeyPairConfig& pair : loaded.config.pairs) {
        if (!engine.addPair(pair.first, pair.second)) {
            std::cout << "Config warning: ignoring unusable pair " << keyNameFromCode(pair.first)
                      << "/" << keyNameFromCode(pair.second) << "\n";
        }
    }
    engine.setEnabled(loaded.config.enabled);

    // The hook is scoped so that its destructor releases every held key and
    // uninstalls the hook before main returns, however the loop ends.
    KeyboardHook hook(engine);
    if (!hook.isInstalled()) {
        std::cerr << "Failed to install the keyboard hook (Windows error "
                  << hook.installError() << ").\n";
        return 1;
    }

    std::cout << "Snap Tap\n"
              << "Config: " << configPath.string() << "\n\n";
    printStatus(hook);
    std::cout << "\n";
    printHelp();

    std::string line;
    while (true) {
        std::cout << "\n> " << std::flush;
        if (!std::getline(std::cin, line)) {
            break;  // stdin closed
        }

        const std::vector<std::string> tokens = tokenize(line);
        if (tokens.empty()) {
            continue;
        }

        const std::string command = toLower(tokens.front());
        if (command == "quit" || command == "exit") {
            break;
        } else if (command == "help") {
            printHelp();
        } else if (command == "status" || command == "list") {
            printStatus(hook);
        } else if (command == "pair") {
            handlePairCommand(hook, tokens);
        } else if (command == "on") {
            setEnabled(hook, true);
        } else if (command == "off") {
            setEnabled(hook, false);
        } else if (command == "toggle") {
            bool enabled = false;
            hook.readEngine([&enabled](const SnapTapEngine& e) { enabled = e.isEnabled(); });
            setEnabled(hook, !enabled);
        } else if (command == "save") {
            if (saveConfigFile(configPath.string(), currentConfig(hook))) {
                std::cout << "Saved to " << configPath.string() << "\n";
            } else {
                std::cout << "Could not write " << configPath.string() << "\n";
            }
        } else {
            std::cout << "Unknown command: " << tokens.front() << " (try: help)\n";
        }
    }

    std::cout << "Releasing held keys and exiting.\n";
    return 0;
}
