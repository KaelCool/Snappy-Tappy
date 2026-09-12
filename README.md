# Snappy-Tappy

A Windows utility that adds **Snap Tap** key priority to any keyboard.

Snap Tap is a feature of the Razer Huntsman V3 Pro: when two opposite keys are
held at once, the most recently pressed one wins, and releasing it hands control
back to the key still being held. This program does the same thing in software,
on hardware that does not have it.

## What it does

Hold <kbd>D</kbd>, then press <kbd>A</kbd> without letting go of <kbd>D</kbd>:

- <kbd>D</kbd> is released and <kbd>A</kbd> activates, instantly.
- Release <kbd>A</kbd> while <kbd>D</kbd> is still held, and <kbd>D</kbd>
  reactivates on its own.

Without it, most keyboards either send both keys or ignore the second one. The
same logic applies to any pair you configure, and pairs work independently, so
<kbd>A</kbd>/<kbd>D</kbd> and <kbd>W</kbd>/<kbd>S</kbd> can both be live at once.

<kbd>A</kbd>/<kbd>D</kbd> is the only pair enabled on a fresh install.

## A note on how it works, and on anti-cheat

This program installs a low-level keyboard hook (`WH_KEYBOARD_LL`) so it can see
key presses before the focused application does, and synthesises key events to
perform the swap. That is the same Windows API a keylogger uses, so some
antivirus software flags it on sight. It records nothing and sends nothing
anywhere — the whole program is the source in this repository — but you should
expect the warning, and you should not take my word for it over reading
[`src/snap_tap/keyboard_hook.cpp`](src/snap_tap/keyboard_hook.cpp).

**Several competitive games have banned Snap Tap and similar "SOCD" input
handling**, whether it comes from software like this or from the keyboard's own
firmware, and some enforce it with bans. Check the current rules for any game
you play before using it. Treat this as a single-player and general-purpose
tool unless you have confirmed otherwise yourself.

## Installing

Download `SnapTap.exe` from the [Releases page](../../releases) and run it. There
is no installer and nothing is written outside the folder you put it in.

Windows SmartScreen will warn that the publisher is unknown, because the binary
is not code-signed. *More info* → *Run anyway*.

## Using it

The window lists your key pairs, each drawn as two keycaps with the currently
winning key lit. Add a pair by picking two keys and pressing **Add**; remove one
with the cross on its row. The toggle at the top enables and disables the whole
thing without closing it.

Minimising sends the program to the notification area rather than the taskbar.
Double-click the tray icon to bring it back, or right-click it for a menu.

## Configuring by hand

Settings live in `snaptap.cfg`, in the same folder as the executable. It is
written whenever you change something in the window, and you can also edit it
yourself:

```
# Lines starting with # are ignored.
enabled true
pair A D
pair W S
```

Key names are case-insensitive. Letters `A`-`Z` and digits `0`-`9` are written
as themselves; the rest are named: `SPACE`, `LEFT`, `UP`, `RIGHT`, `DOWN`,
`SHIFT`, `CTRL`, `ALT`, `LSHIFT`, `RSHIFT`, `LCTRL`, `RCTRL`, `LALT`, `RALT`,
`TAB`, `ENTER`, `ESC`, `BACKSPACE`, `INSERT`, `DELETE`, `HOME`, `END`.

A line that does not parse is reported and skipped; the rest of the file still
loads. A missing config file is not an error — you get the defaults.

## Building from source

Needs CMake and the Visual Studio C++ toolchain (MSVC).

```
git clone https://github.com/KaelCool/Snappy-Tappy.git
cd Snappy-Tappy
cmake -S . -B build
cmake --build build --config Release
```

The executable lands in `build\Release\SnapTap.exe`.

To run the tests:

```
cmake --build build
ctest --test-dir build -C Debug --output-on-failure
```

`scripts\check.ps1` does both in one step.

## Layout

| Path | Contents |
| --- | --- |
| `src/snap_tap/engine.*` | The key priority logic. No Windows dependency. |
| `src/snap_tap/keyboard_hook.*` | The low-level hook, and the thread it runs on. |
| `src/snap_tap/config.*` | Reading and writing `snaptap.cfg`. |
| `src/snap_tap/main_window.*` | The window, the tray icon and the controls. |
| `src/snap_tap/layout.*`, `painting.*`, `theme.*` | Geometry, drawing and the palette. |
| `tests/` | Unit tests for everything that does not need a window. |

The engine, config and layout are deliberately free of `windows.h` so they can
be unit-tested directly.
