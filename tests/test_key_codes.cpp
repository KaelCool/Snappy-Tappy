#include "snap_tap/key_codes.h"
#include "test_util.h"

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

    return ::testing::summarize("test_key_codes");
}
