#include "snap_tap/key_codes.h"
#include "test_util.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace snaptap;

int main() {
    TEST_CASE("letters map to their ASCII value");
    CHECK(keyCodeFromName("A") == 0x41);
    CHECK(keyCodeFromName("D") == 0x44);
    CHECK(keyCodeFromName("W") == 0x57);
    CHECK(keyCodeFromName("S") == 0x53);

    TEST_CASE("lookup is case-insensitive");
    CHECK(keyCodeFromName("a") == keyCodeFromName("A"));
    CHECK(keyCodeFromName("left") == keyCodeFromName("LEFT"));
    CHECK(keyCodeFromName("LeFt") == keyCodeFromName("LEFT"));

    TEST_CASE("digits and named keys");
    CHECK(keyCodeFromName("0") == 0x30);
    CHECK(keyCodeFromName("9") == 0x39);
    CHECK(keyCodeFromName("LEFT") == 0x25);
    CHECK(keyCodeFromName("RIGHT") == 0x27);
    CHECK(keyCodeFromName("SPACE") == 0x20);
    CHECK(keyCodeFromName("LSHIFT") == 0xA0);

    TEST_CASE("unknown names are rejected");
    CHECK(keyCodeFromName("") == std::nullopt);
    CHECK(keyCodeFromName("NOPE") == std::nullopt);
    CHECK(keyCodeFromName("-") == std::nullopt);
    CHECK(keyCodeFromName("AA") == std::nullopt);

    TEST_CASE("codes round-trip back to names");
    CHECK(keyNameFromCode(0x41) == "A");
    CHECK(keyNameFromCode(0x25) == "LEFT");
    CHECK(keyNameFromCode(0x20) == "SPACE");
    CHECK(keyNameFromCode(0x00).empty());
    CHECK(keyCodeFromName(keyNameFromCode(0xA2)) == 0xA2);

    TEST_CASE("allKeys is complete, ordered and free of duplicates");
    {
        const std::vector<KeyCode>& keys = allKeys();
        CHECK(keys.size() == 26 + 10 + 22);

        // Letters first, then digits, then the named keys.
        CHECK(keys.at(0) == 'A');
        CHECK(keys.at(25) == 'Z');
        CHECK(keys.at(26) == '0');
        CHECK(keys.at(35) == '9');

        // Every entry must be usable: it names a key, and that name resolves back.
        for (const KeyCode key : keys) {
            const std::string name = keyNameFromCode(key);
            CHECK(!name.empty());
            CHECK(keyCodeFromName(name) == key);
        }

        std::vector<KeyCode> sorted = keys;
        std::sort(sorted.begin(), sorted.end());
        CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());

        // The pairs the app ships with are all offered.
        for (const char expected : {'A', 'D', 'W', 'S'}) {
            CHECK(std::find(keys.begin(), keys.end(), expected) != keys.end());
        }
    }

    return ::testing::summarize("test_key_codes");
}
