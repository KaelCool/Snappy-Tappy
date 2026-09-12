#pragma once

#include <filesystem>

namespace snaptap {

// Directory holding the running executable.
//
// Config lives next to the exe rather than in the current directory, so that
// launching Snap Tap from Explorer, from a shortcut, or from a shell all reach
// the same settings file. Falls back to the current directory if Windows will
// not report the executable path.
std::filesystem::path executableDirectory();

}  // namespace snaptap
