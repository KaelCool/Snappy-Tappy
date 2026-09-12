#include "snap_tap/config.h"
#include "snap_tap/key_codes.h"
#include "test_util.h"

#include <filesystem>
#include <string>

using namespace snaptap;

namespace {

constexpr KeyCode kA = 'A';
constexpr KeyCode kD = 'D';
constexpr KeyCode kW = 'W';
constexpr KeyCode kS = 'S';

}  // namespace

int main() {
    {
        TEST_CASE("the launch default is A/D only, enabled");
        const Config config = defaultConfig();
        CHECK(config.enabled);
        CHECK(config.pairs.size() == 1);
        CHECK(config.pairs.at(0).first == kA);
        CHECK(config.pairs.at(0).second == kD);
    }

    {
        TEST_CASE("a well-formed config parses");
        const ParseResult result = parseConfig(
            "enabled true\n"
            "pair A D\n"
            "pair W S\n");
        CHECK(result.ok());
        CHECK(result.config.enabled);
        CHECK(result.config.pairs.size() == 2);
        CHECK(result.config.pairs.at(0).first == kA);
        CHECK(result.config.pairs.at(0).second == kD);
        CHECK(result.config.pairs.at(1).first == kW);
        CHECK(result.config.pairs.at(1).second == kS);
    }

    {
        TEST_CASE("comments, blank lines, casing and stray whitespace are tolerated");
        const ParseResult result = parseConfig(
            "# Snap Tap configuration\n"
            "\n"
            "   \t  \n"
            "   ENABLED   off   \n"
            "   pair   a   d   \n"
            "  # pair W S\n"
            "pair LEFT RIGHT\r\n");
        CHECK(result.ok());
        CHECK(!result.config.enabled);
        CHECK(result.config.pairs.size() == 2);
        CHECK(result.config.pairs.at(0).first == kA);   // lowercase accepted
        CHECK(result.config.pairs.at(1).first == 0x25);  // LEFT, CRLF line ending
        CHECK(result.config.pairs.at(1).second == 0x27);
    }

    {
        TEST_CASE("every accepted boolean spelling");
        CHECK(parseConfig("enabled true").config.enabled);
        CHECK(parseConfig("enabled on").config.enabled);
        CHECK(parseConfig("enabled yes").config.enabled);
        CHECK(parseConfig("enabled 1").config.enabled);
        CHECK(!parseConfig("enabled false").config.enabled);
        CHECK(!parseConfig("enabled off").config.enabled);
        CHECK(!parseConfig("enabled no").config.enabled);
        CHECK(!parseConfig("enabled 0").config.enabled);
    }

    {
        TEST_CASE("bad lines are reported but valid ones still apply");
        const ParseResult result = parseConfig(
            "pair A D\n"
            "pair A NOPE\n"       // unknown key name
            "pair W\n"            // too few keys
            "pair W S T\n"        // too many keys
            "pair Q Q\n"          // paired with itself
            "pair A W\n"          // A is already paired
            "enabled maybe\n"     // not a boolean
            "enabled\n"           // missing value
            "frobnicate X\n"      // unknown directive
            "pair W S\n");
        CHECK(result.errors.size() == 8);
        CHECK(!result.ok());
        CHECK(result.config.pairs.size() == 2);  // the two good pair lines
        CHECK(result.config.pairs.at(1).first == kW);
        CHECK(result.config.enabled);  // untouched by the bad enabled lines

        // Errors name the offending line so they can be shown to the user.
        CHECK(result.errors.at(0).rfind("line 2:", 0) == 0);
        CHECK(result.errors.at(7).rfind("line 9:", 0) == 0);
    }

    {
        TEST_CASE("an empty config yields no pairs rather than the defaults");
        const ParseResult result = parseConfig("# nothing here\n");
        CHECK(result.ok());
        CHECK(result.config.pairs.empty());
    }

    {
        TEST_CASE("serialize round-trips back through the parser");
        Config config;
        config.enabled = false;
        config.pairs.push_back(KeyPairConfig{kA, kD});
        config.pairs.push_back(KeyPairConfig{0x25, 0x27});  // LEFT / RIGHT

        const ParseResult result = parseConfig(serializeConfig(config));
        CHECK(result.ok());
        CHECK(result.config.enabled == config.enabled);
        CHECK(result.config.pairs.size() == config.pairs.size());
        CHECK(result.config.pairs.at(0).first == kA);
        CHECK(result.config.pairs.at(1).second == 0x27);
    }

    {
        TEST_CASE("a missing file falls back to the defaults without erroring");
        const std::filesystem::path path = "no_such_snaptap_config.txt";
        std::filesystem::remove(path);

        const ParseResult result = loadConfigFile(path.string());
        CHECK(result.ok());
        CHECK(result.config.pairs.size() == 1);
        CHECK(result.config.pairs.at(0).first == kA);
        CHECK(result.config.pairs.at(0).second == kD);
        CHECK(result.config.enabled);
    }

    {
        TEST_CASE("a saved file loads back identically");
        const std::filesystem::path path = "snaptap_config_roundtrip.txt";
        std::filesystem::remove(path);

        Config config;
        config.enabled = false;
        config.pairs.push_back(KeyPairConfig{kW, kS});
        CHECK(saveConfigFile(path.string(), config));
        CHECK(std::filesystem::exists(path));

        const ParseResult result = loadConfigFile(path.string());
        CHECK(result.ok());
        CHECK(!result.config.enabled);
        CHECK(result.config.pairs.size() == 1);
        CHECK(result.config.pairs.at(0).first == kW);
        CHECK(result.config.pairs.at(0).second == kS);

        std::filesystem::remove(path);
    }

    return ::testing::summarize("test_config");
}
