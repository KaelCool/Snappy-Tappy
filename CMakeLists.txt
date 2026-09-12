cmake_minimum_required(VERSION 3.20)
project(MyProject)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Link the C++ runtime into the executable rather than against the Visual C++
# Redistributable, so a downloaded build starts on a machine that has never had
# Visual Studio installed. Costs a few hundred KB.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Portable Snap Tap logic: no windows.h, so the unit tests can link it directly.
add_library(snaptap_core STATIC
    src/snap_tap/key_codes.cpp
    src/snap_tap/engine.cpp
    src/snap_tap/config.cpp
    src/snap_tap/layout.cpp
)
target_include_directories(snaptap_core PUBLIC src)

# The Win32 layer is linked only into the app, never into the tests.
# WIN32 makes this a windowed app (wWinMain, no console window).
add_executable(SnapTap WIN32
    src/main.cpp
    src/snap_tap/app_paths.cpp
    src/snap_tap/keyboard_hook.cpp
    src/snap_tap/main_window.cpp
    src/snap_tap/painting.cpp
    src/snap_tap/theme.cpp
)
# gdiplus: antialiased rounded corners, which GDI's RoundRect cannot draw.
# dwmapi: dark title bar and Windows 11 rounded window corners.
target_link_libraries(SnapTap PRIVATE snaptap_core comctl32 gdiplus dwmapi uxtheme)
# The app calls the wide-character Win32 API throughout; this also makes the
# resource macros (IDC_ARROW, IDI_APPLICATION) resolve to their wide forms.
target_compile_definitions(SnapTap PRIVATE UNICODE _UNICODE)

enable_testing()
add_subdirectory(tests)
