# PC Xbox 360 UI (Virtual Keyboard + NXE Friends List)

Win32 recreations of classic Xbox 360 / NXE UI overlays:

- **Virtual Keyboard** – Xbox Live Guide style on-screen keyboard
- **Friends List** – NXE Guide-style friends list with presence, gamerscore, and controller navigation

Designed as drop-in overlays for games and applications that need authentic Xbox 360-era UI on PC.

![Virtual Keyboard](preview.png)

## Features

- **Xbox 360 aesthetic** – matching layout, colors, face-button badges, and side controls
- **Full controller support** via XInput (D-pad, left stick, A/B/X/Y, LB/RB, LT/RT, Start, Left Stick click)
- **Mouse + keyboard** support as well
- **Transparent overlay** – uses layered window + color-key so only the UI is visible
- **Scales to any resolution** – all layout is defined in a 1280×720 reference space and scaled with `MulDiv`
- **Overlapped / async API** – similar style to the original Xbox Online Guide virtual keyboard calls
- **Caps lock**, caret movement, backspace, space, and Done
- Symbols / Accents buttons are present (currently disabled / non-functional placeholders)

## Controls

| Input | Action |
|-------|--------|
| **A** / Enter / Left Click | Select / activate focused key |
| **B** / Esc | Cancel / close |
| **X** | Backspace |
| **Y** | Space |
| **LB** / **RB** | Move caret left / right |
| **Left Stick Click** | Toggle Caps |
| **Start** | Done |
| **D-pad / Left Stick** | Move focus between keys |
| **Arrow keys** | Move focus (Ctrl+Left/Right moves caret) |


## Friends List

`pc_friends_list.exe` opens a transparent overlay that mimics the classic Xbox 360 Guide friends list (the text/list version available from the Guide button during NXE):

- Online / Away / Busy / Offline status with colored indicators
- Presence strings ("Playing Halo 3", "Away", etc.)
- Gamerscore display
- Favorites sorted to the top of each presence group
- Scrollable list with mouse wheel, D-pad, left stick, and keyboard
- **A** Select (shows a simple options dialog for the demo)
- **B** / Esc Back

Sample friends data is hard-coded for demonstration. In a real integration you would feed live presence from Xbox Live / XInput / your own backend.


## Quick Launch

`pc_quick_launch.exe` recreates the Guide **Quick Launch** / Game Library list:

- Installed, Arcade, Indie, Demo, and Disc games
- Box-art placeholder, last played, achievement progress
- **DISC** badge when a disc is in the tray
- **A** Launch · **Y** Details · **B** Back
- Controller, mouse, and keyboard navigation


## Gamercard / Profile

`pc_gamercard.exe` recreates the classic Xbox 360 Guide **Gamercard** view:

- Gamerpic placeholder, gamertag, motto, location
- Stats row: Gamerscore, Reputation, Zone, Games played
- Bio text
- Recent achievements list
- **B** / Esc Back


## Party

`pc_party.exe` recreates the NXE **Party** panel:

- Party leader + members with activity ("In party chat", "Playing ...")
- Talking indicator (bright green dot)
- Invite online friends into the party
- Mute toggle per member
- Capacity display (e.g. 4 / 8)
- **A** Invite / Select · **Y** Mute · **B** Back


## Players Met

`pc_players_met.exe` recreates the Guide **Players Met** list:

- Recent players from multiplayer sessions
- Game they were met in + when
- Online / Away / Offline presence
- **FRIEND** and **FEEDBACK** badges
- **A** Add Friend · **X** Submit Feedback · **B** Back


## Message Center

`pc_message_center.exe` recreates the Guide **Message Center**:

- Text messages, voice messages, game invites, friend requests, party invites
- Unread indicator bar + unread count in header
- Color-coded type badges
- **A** Open · **X** Delete · **B** Back


## Unified Guide

`pc_guide.exe` stitches every panel into one **Xbox 360 Guide** overlay:

- Left **blade** navigation: Home, Friends, Messages, Party, Players Met, Quick Launch, Gamercard
- Right **content** pane switches with the selected section
- Unread badge on Messages
- **D-pad / stick** move within blade or list
- **A** Select / enter section
- **B** Back to blade (or close Guide from blade)
- **Y** Toggle focus between blade and content
- **Messages → A → Reply** opens the virtual keyboard to compose a reply
- **Left / Right** also switch focus blade <-> content


## Shared library

Common drawing, scaling, fonts, and overlay window helpers live in a static library:

- `xbox360_ui_common.h` / `xbox360_ui_common.cpp` → **libxbox360_ui_common**
- Reference resolution 1280×720, color-key magenta, GDI primitives, presence colors, footer buttons

All panel executables and `pc_guide` link against it.


## Configuration

Each app loads an **INI file** from the same folder as the executable:

| File | App |
|------|-----|
| `quick_launch.ini` | Quick Launch + Guide games |
| `friends_list.ini` | Friends |
| `party.ini` | Party |
| `players_met.ini` | Players Met |
| `message_center.ini` | Message Center |
| `gamercard.ini` | Gamercard / profile |
| `guide.ini` | Guide options |

### Quick Launch — real launches

```ini
[game.0]
title=Halo 3
type=disc
path=D:\Games\Halo3\halo3.exe
args=
last_played=Today
achievements=42
achievements_total=79
disc_in_tray=1
```

**A** on a game runs `path` via `CreateProcess`.  
Set each `path=` to your game install. Empty or invalid paths show **Failed to launch**.

CMake copies all `config/*.ini` into `build/vs2026-x64/Release` and `Debug`.

## Guide button / hotkey listener

`pc_guide_listener.exe` is a background process (no window) that opens the Guide when:

| Input | Action |
|-------|--------|
| **Guide (Xbox) button** | Launch / focus Guide — via undocumented `XInputGetStateEx` when the driver supports it |
| **Start + Back** | Always works with standard XInput |

Put it in the **same folder** as `pc_guide.exe` and run it at login (or from a shortcut). Only one instance runs at a time. If the Guide is already open, it is focused instead of starting a second copy.

## Building

### Requirements

- Windows 10/11
- CMake ≥ 3.20
- Visual Studio 2019/2022 (or any C++17 compiler that can target Win32)
- Windows SDK (for `user32`, `gdi32`, `msimg32`, `xinput`)

### Build (CMake Presets)

```bash
# Configure + build Release (Ninja)
cmake --preset x64-release
cmake --build --preset x64-release

# Or Debug
cmake --preset x64-debug
cmake --build --preset x64-debug

# Visual Studio 2026 solution
cmake --preset vs2026-x64
cmake --build --preset vs2026-x64-release

# Visual Studio 2022 solution
cmake --preset vs2022-x64
cmake --build --preset vs2022-x64-release
```

Available configure presets: `x64-debug`, `x64-release`, `x64-relwithdebinfo`, `vs2026-x64`, `vs2022-x64`, `vs2019-x64`.

You can also open the folder in Visual Studio / VS Code and select a preset from the CMake Tools UI.

The CMakeLists produces two executables:

```
pc_virtual_keyboard.exe
pc_friends_list.exe
pc_quick_launch.exe
pc_gamercard.exe
pc_party.exe
pc_players_met.exe
pc_message_center.exe
pc_guide.exe          # unified Guide
```

## Usage / API

The main entry point is:

```cpp
unsigned long online_guide_show_virtual_keyboard_ui(
    int controller_index,               // 0–3
    unsigned long character_flags,      // currently unused
    wchar_t const* default_text,
    wchar_t const* title_text,
    wchar_t const* description_text,
    wchar_t* result_text,               // output buffer
    unsigned long maximum_character_count,
    OVERLAPPED* platform_handle);       // async completion
```

- Returns `ERROR_IO_PENDING` on success (keyboard is shown).
- When the user presses **Done** the result is written into `result_text` and the overlapped event is signaled with `ERROR_SUCCESS`.
- When the user presses **B** / Esc the overlapped is completed with `ERROR_CANCELLED`.

A simple `wWinMain` demo is included that opens the keyboard with the title “Change Gamertag”.

## Layout Notes

All coordinates are defined against a **1280 × 720** reference resolution and scaled at runtime. Key constants live at the top of `virtual_keyboard_overlapped.cpp`:

- Panel, input field, letter keys, side buttons (Cursor / Caps / Symbols / Accents), Backspace, Space, and Done
- Left and right side columns use matching outer padding (30 px) and inner gaps (10 px)
- Backspace + Space together span the full width of the letter-key area

## Project Structure

```
├── CMakeLists.txt
├── CMakePresets.json
├── virtual_keyboard_overlapped.cpp   # Virtual Keyboard
├── friends_list.cpp                  # NXE Friends List
├── quick_launch.cpp                   # Quick Launch / Game Library
├── gamercard.cpp                      # Profile / Gamercard
├── party.cpp                          # Party
├── players_met.cpp                    # Players Met
├── message_center.cpp                 # Message Center
├── xbox360_ui_common.h / .cpp          # Shared static library
├── controller_message_box.h / .cpp     # Controller-friendly modal dialog
├── guide.cpp                          # Unified Guide (all sections)
├── build-vs2026.bat
└── README.md
```

## License

This project is provided as-is for educational and integration purposes.  
Xbox, Xbox 360, and related terms are trademarks of Microsoft Corporation.
