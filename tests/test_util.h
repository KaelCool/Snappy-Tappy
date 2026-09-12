#pragma once

#include <iostream>
#include <string_view>

// Minimal assertion helpers, in the same dependency-free spirit as the original
// placeholder test: no external framework, just a non-zero exit code on failure.
namespace testing {

inline int failureCount = 0;
inline std::string_view currentCase = "<unnamed>";

inline void beginCase(const std::string_view name) {
    currentCase = name;
}

inline void reportFailure(const std::string_view expr, const char* const file, const int line) {
    ++failureCount;
    std::cout << "FAIL [" << currentCase << "] " << file << ":" << line << ": " << expr << "\n";
}

inline int summarize(const std::string_view suite) {
    if (failureCount == 0) {
        std::cout << suite << ": all checks passed\n";
        return 0;
    }
    std::cout << suite << ": " << failureCount << " check(s) failed\n";
    return 1;
}

}  // namespace testing

#define TEST_CASE(name) ::testing::beginCase(name)

#define CHECK(expr)                                                    \
    do {                                                               \
        if (!(expr)) {                                                 \
            ::testing::reportFailure(#expr, __FILE__, __LINE__);       \
        }                                                              \
    } while (false)
