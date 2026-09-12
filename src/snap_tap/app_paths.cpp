#include "snap_tap/app_paths.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <vector>

namespace snaptap {
namespace {

// Longest path Windows will hand back, even with long paths enabled.
constexpr std::size_t kMaxPathLength = 32768;

}  // namespace

std::filesystem::path executableDirectory() {
    std::vector<wchar_t> buffer(MAX_PATH);

    while (true) {
        const DWORD written =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0) {
            break;  // the call failed outright
        }
        if (written < buffer.size()) {
            return std::filesystem::path(buffer.data(), buffer.data() + written).parent_path();
        }
        // The path was truncated to fit; grow the buffer and ask again.
        if (buffer.size() >= kMaxPathLength) {
            break;
        }
        buffer.resize(buffer.size() * 2);
    }

    std::error_code error;
    const std::filesystem::path fallback = std::filesystem::current_path(error);
    return error ? std::filesystem::path(".") : fallback;
}

}  // namespace snaptap
