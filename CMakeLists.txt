cmake_minimum_required(VERSION 3.20)
project(MyProject)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Portable Snap Tap logic: no windows.h, so the unit tests can link it directly.
add_library(snaptap_core STATIC
    src/snap_tap/key_codes.cpp
    src/snap_tap/engine.cpp
    src/snap_tap/config.cpp
)
target_include_directories(snaptap_core PUBLIC src)

# The Win32 layer is linked only into the app, never into the tests.
# WIN32 makes this a windowed app (wWinMain, no console window).
add_executable(myapp WIN32
    src/main.cpp
    src/snap_tap/app_paths.cpp
    src/snap_tap/keyboard_hook.cpp
    src/snap_tap/main_window.cpp
)
target_link_libraries(myapp PRIVATE snaptap_core comctl32)
# The app calls the wide-character Win32 API throughout; this also makes the
# resource macros (IDC_ARROW, IDI_APPLICATION) resolve to their wide forms.
target_compile_definitions(myapp PRIVATE UNICODE _UNICODE)

enable_testing()
add_subdirectory(tests)
