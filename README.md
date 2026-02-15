# Huion KD100 Linux Driver
Originally forked from [mckset/KD100](https://github.com/mckset/KD100), now independently maintained with major enhancements.


![KD100-OSD](KD100-osd.gif)

A simple driver for the Huion KD100 mini Keydial written in C to give the device some usability while waiting for Huion to fix their Linux drivers. Each button can be configured to either act as a key/multiple keys or to execute a program/command.

**Version 1.7.2** adds per-application profiles with overlay semantics, directory-based profile configuration (`apps.profiles.d/`), inotify hot reload, OSD profile switch notifications, and consistency checking.

> **NOTICE:** When updating from **v1.31** or below, make sure you updated your config file to follow the new format shown in the default config file.

## Features

| Feature | Version | Description |
|---------|---------|-------------|
| Per-App Profiles (apps.profiles.d/) | v1.7.2 | Directory-based profile configs, one `.cfg` per app, overlay semantics |
| Profile Hot Reload | v1.7.2 | inotify-based live reload when profile files change |
| Profile Switch Notifications | v1.7.2 | OSD shows "Profile: name" when switching configurations |
| Consistency Checking | v1.7.2 | Validates profiles on load (bounds, duplicates, type checks) |
| Sticky Profiles | v1.7.2 | Unmatched windows keep current profile active |
| Leader Key Descriptions | v1.7.1 | Per-button labels that swap in when leader is active |
| Wheel Function Descriptions | v1.7.0 | Human-readable names for wheel functions shown in OSD |
| Active Button Highlighting | v1.7.0 | Pressed buttons flash green in expanded keyboard layout |
| Leader Key Visual Feedback | v1.7.0 | Orange highlight for active leader, purple tint for eligible keys |
| Wheel Set Indicator | v1.7.0 | Numbered set boxes with green active highlight next to mode display |
| Wheel Action Aggregation | v1.7.0 | Repeated wheel turns show as single action, not per-tick |
| Mode & Leader Status Display | v1.7.0 | Mode, leader, and set info shown at top of both OSD views |
| Input Validation | v1.7.0 | Description fields sanitized (max 64 chars, printable ASCII only) |
| On-Screen Display | v1.6.0 | Semi-transparent overlay showing recent key actions and full keyboard layout |
| Profile System | v1.6.0 | Automatic configuration switching based on active window title |
| Scalable UI | v1.6.0 | Font size setting scales entire OSD proportionally |
| Configurable Wheel Toggle Modes | v1.5.1 | Choose between sequential or set-based navigation |
| Multi-Click Detection | v1.5.1 | Single, double, and triple-click support for wheel button |
| Set-Based Navigation | v1.5.1 | Organize up to 6 wheel functions into 3 sets of 2 |
| Modular Architecture | v1.5.0 | Clean separation into focused modules for easy maintenance |
| Enhanced Leader Key System | v1.4.9 | Three configurable modes (one_shot, sticky, toggle) |
| Per-button Leader Eligibility | v1.4.9 | Control which buttons can be modified by leader |
| hid_uclogic Compatibility | v1.4.5 | Work with or without the hid_uclogic kernel module |
| Advanced Debugging | v1.4.5 | Stack traces and line numbers on crashes (debug builds) |
| Multiple Build Targets | v1.4.5 | Debug, release, sanitizer builds |
| Clean Compilation | v1.5.0 | Zero warnings with strict compiler flags |

## Architecture (v1.7.2)

The codebase is organized into focused modules:

```
src/
├── main.c       - Application entry point and orchestration
├── config.c/h   - Configuration file parsing and management
├── device.c/h   - USB device discovery and event loop
├── leader.c/h   - Leader key system implementation
├── handler.c/h  - Event handling and key execution
├── utils.c/h    - Utility functions (time, string, parsing)
├── compat.c/h   - Hardware compatibility layer
├── osd.c/h      - On-screen display overlay (v1.6.0)
├── window.c/h   - Active window tracking (v1.6.0)
└── profiles.c/h - Profile management system (v1.6.0+, overlay/hot-reload v1.7.2)
```

Each module has a single, clear responsibility, making the code easier to understand, test, and extend.

## Pre-Installation
**Arch Linux/Manjaro:**
```bash
sudo pacman -S libusb xdotool libx11 libxrender libxext
```

**Ubuntu/Debian/Pop OS:**
```bash
sudo apt-get install libusb-1.0-0-dev xdotool libx11-dev libxrender-dev libxext-dev
```

> **NOTE:** Some distros label libusb as "libusb-1.0-0" and others might require the separate "libusb-1.0-dev" package. The X11 libraries are required for the OSD overlay feature.

## Installation
You can either download the latest release or run the following:
```bash
git clone https://github.com/mckset/KD100.git
cd KD100
make
```

> Running `sudo make install` will install the driver system-wide and create a folder in `~/.config` to store config files.

### Build Options
```bash
# Standard build (optimized, no debug symbols)
make

# Debug build (with crash handler and symbols)
make debug

# Release build (optimized, stripped)
make release

# Install system-wide
sudo make install

# Advanced debugging tools
make gdb      # Run with GDB debugger
make valgrind # Memory checking
make strace   # System call tracing
make asan     # AddressSanitizer build
make ubsan    # UndefinedBehaviorSanitizer build

# Clean build artifacts
make clean
```

## Usage
```bash
sudo ./KD100 [options]
```

**Options:**
- `-a` - Assume that the first device that matches the vid and pid is the keydial (skips prompt to select a device)
- `-c [path]` - Specify a config file to use after the flag (`./default.cfg` or `~/.config/KD100/default.cfg` is used normally)
- `-d` - Enable debug output (can be used twice to output the full packet of data received from the device)
- `-dry` - Display data sent from the keydial and ignore events
- `-h` - Displays help message
- `--uclogic` - Force hid_uclogic compatibility mode
- `--no-uclogic` - Disable hid_uclogic compatibility (OpenTabletDriver mode)

## Profile System (v1.7.2)

### Overview
The profile system provides automatic configuration switching based on the active window. Profiles use **overlay semantics** — they override only the parameters you specify, while everything else falls back to `default.cfg`.

See `docs/PROFILES_DESIGN.md` for the full design rationale.

### What Profiles Can Change

| Parameter | Per-Profile? | Notes |
|-----------|:------------:|-------|
| Button keys/functions | Yes | Override any of buttons 0-18 |
| Button types | Yes | Key (0), function (1), mouse (2) |
| Wheel functions | Yes | Clockwise and counter-clockwise |
| Key descriptions | Yes | Shown in OSD expanded view |
| Leader descriptions | Yes | Shown when leader is active |
| Wheel descriptions | Yes | Shown in wheel set indicator |
| Leader config | No | Shared across all profiles |
| OSD settings | No | Shared across all profiles |
| Wheel mode/timeout | No | Shared across all profiles |
| Hardware settings | No | Shared across all profiles |

### Profile Switching Behavior

| Scenario | Behavior |
|----------|----------|
| Switch to window matching a profile | Profile activates, OSD shows "Profile: name" |
| Switch to window with no matching profile | Current profile stays active (sticky) |
| Default profile defined (pattern `*`) | Falls back to default profile for unmatched windows |
| Profile file modified on disk | Hot-reloaded via inotify, OSD shows reload message |

### File Structure
Profiles are configured as individual `.cfg` files in the `apps.profiles.d/` directory:

```
apps.profiles.d/
├── krita.cfg
├── blender.cfg
└── gimp.cfg
```

### Profile File Format
Each `.cfg` file defines one profile:

```bash
# apps.profiles.d/krita.cfg
name: Krita
pattern: *krita*
priority: 0

# Override only the buttons you want to change
Button 0
type: 0
function: b

Button 7
type: 0
function: e

# Per-profile descriptions
description_0: Brush
description_7: Eraser

# Per-profile leader descriptions
leader_description_0: Shift+Brush

# Per-profile wheel descriptions
wheel_description_0: Brush Size
wheel_description_1: Opacity
```

Only the buttons and descriptions you specify are overridden. Everything else comes from `default.cfg`.

### Configuration
```bash
# In default.cfg

# Preferred: directory-based profiles (one .cfg per app)
profiles_dir: apps.profiles.d

# Backward compatible: monolithic profiles file
profiles_file: profiles.cfg

# If both are set, profiles_dir takes precedence

# Auto-switch when active window changes
profile_auto_switch: true

# How often to check active window (milliseconds)
profile_check_interval: 500
```

### Pattern Matching
- `*krita*` - Matches windows containing "krita"
- `krita*` - Matches windows starting with "krita"
- `*photoshop*` - Matches any window with "photoshop" in the title
- `*` - Matches all windows (default/fallback profile)
- Matching is case-insensitive

## Enhanced Leader Key System

### Leader Modes
The enhanced leader key system supports three modes:

1. **one_shot** (default): Leader + 1 key = combination, then reset
   ```
   Press Leader -> Press Key -> Send combination -> Reset
   ```

2. **sticky**: Leader stays active for multiple keys until timeout
   ```
   Press Leader -> Press Key1 -> Send combo -> Press Key2 -> Send combo -> Timeout -> Reset
   ```

3. **toggle**: Leader toggles on/off (press to enable, press again to disable)
   ```
   Press Leader -> Enable -> Press Key1 -> Send combo -> Press Key2 -> Send combo -> Press Leader -> Disable
   ```

### Per-Button Eligibility
Each button can be configured as leader-eligible or not:
```bash
Button 0
type: 0
function: b
leader_eligible: true  # Can be modified by leader

Button 1
type: 0
function: p
leader_eligible: false # Cannot be modified by leader
```

### Leader Key Descriptions (v1.7.1)
Each button can have a separate description shown when the leader key is active. This lets you display the modified action in the OSD expanded view.

```bash
# Format: leader_description_<button_number>: <description>
# Maximum 64 characters, printable ASCII only
# Only define for leader-eligible buttons

leader_description_0: Shift+Brush
leader_description_2: Shift+Transform
leader_description_3: Shift+Lasso
```

When the leader key is active, eligible buttons in the OSD keyboard layout swap their label to the leader description (shown with a purple tint). Non-eligible buttons keep their normal description.

### Configuration Example
```bash
# Leader configuration
leader_button: 16          # Which button acts as leader
leader_function: shift     # Which modifier key to use
leader_timeout: 1000       # Timeout in milliseconds (sticky mode)
leader_mode: toggle        # one_shot, sticky, or toggle

# Button configuration with eligibility
Button 0
type: 0
function: b
leader_eligible: true

Button 18                  # Wheel toggle button
type: 1
function: swap
leader_eligible: false    # Typically not eligible

# Leader descriptions (shown in OSD when leader is active)
leader_description_0: Shift+Brush
leader_description_2: Shift+Transform
```

## On-Screen Display (OSD)

### Overview
Version 1.6.0 introduces a semi-transparent on-screen display overlay, similar to Blender's screencast keys feature. The OSD shows recent key actions and can expand to show the full keyboard layout.

### Features
- **Minimal Mode**: Shows mode/set info, active wheel function pair, and 3 recent actions
- **Expanded Mode**: Full keyboard layout with button highlighting, wheel set indicator, and 3 recent actions
- **Active Button Highlighting**: Pressed buttons flash green in the keyboard layout
- **Leader Key Feedback**: Leader button glows orange when active; eligible keys tinted purple
- **Wheel Set Indicator**: Numbered boxes `[1][2][3]` next to mode, green = active set (grayed in sequential mode)
- **Wheel Function Display**: Shows both functions in current set with `>` on the active one
- **Wheel Action Aggregation**: Repeated wheel turns displayed as a single action
- **Profile Switch Notifications**: Shows "Profile: name" when configuration changes (v1.7.2)
- **Auto-Show/Hide**: Appears when keys are pressed, hides after timeout
- **Hover Detection**: Stays visible while cursor hovers over it
- **Draggable**: Click and drag anywhere on the window to move it
- **Scalable UI**: Font size setting scales entire interface proportionally
- **Profile Support**: Automatic key descriptions based on active window

### Configuration
```bash
# Enable/disable OSD
osd_enabled: true

# Show OSD on startup (or wait for key press)
osd_start_visible: false

# Auto-show when keys are pressed (recommended with start_visible: false)
osd_auto_show: true

# Initial position on screen
osd_position: 50,50

# Background opacity (0.0 = transparent, 1.0 = opaque)
osd_opacity: 0.67

# How long OSD stays visible after last action (milliseconds)
osd_display_duration: 3000

# Font size (8-32) - scales entire UI proportionally
osd_font_size: 13

# Button to toggle OSD visibility (-1 = disabled)
osd_toggle_button: -1
```

### Modes
- **Click title bar** to toggle between minimal and expanded modes
- **Drag anywhere** on the window to reposition
- **Hover** over the window to prevent auto-hide

### Scaling
The `osd_font_size` setting acts as a scaling factor:
- `osd_font_size: 13` - Default size (scale = 1.0)
- `osd_font_size: 26` - Double size (scale = 2.0)
- All UI elements (padding, buttons, text) scale proportionally

## Wheel Toggle Modes

### Overview
Version 1.5.1 introduces configurable wheel toggle modes, allowing you to choose how button 18 cycles through wheel functions.

### Sequential Mode (Default)
Classic behavior where single-click cycles through all wheel functions sequentially.

```bash
wheel_mode: sequential

# Example with 5 functions:
# Click -> Function 0 -> Click -> Function 1 -> Click -> Function 2 -> ... -> Click -> Function 0
```

**Benefits:**
- Simple and straightforward
- No limit on number of functions
- No click detection delay
- Backward compatible with existing configs

### Sets Mode
Advanced mode using multi-click detection to organize wheel functions into sets.

```bash
wheel_mode: sets
wheel_click_timeout: 300  # Time window for multi-click detection (20-990ms)
```

**Click Behavior:**
- **Single-click**: Toggle between the two functions in the current set
- **Double-click**: Switch between Set 1 and Set 2
- **Triple-click**: Toggle to/from Set 3

**Set Organization:**
- **Set 1**: Wheel functions 0-1 (primary tools)
- **Set 2**: Wheel functions 2-3 (secondary tools)
- **Set 3**: Wheel functions 4-5 (occasional tools)

**Set Navigation:**
```
Set 1 -> double-click -> Set 2
Set 1 -> triple-click -> Set 3
Set 2 -> double-click -> Set 1
Set 2 -> triple-click -> Set 3
Set 3 -> double-click -> Set 2
Set 3 -> triple-click -> Set 1
```

**Example Configuration:**
```bash
wheel_mode: sets
wheel_click_timeout: 300

# Clockwise wheel functions
Wheel
function: bracketright
function: o
function: shift+bracketright
function: ctrl+bracketright
function: alt+bracketright
function: super+bracketright

# Counter-clockwise wheel functions
Wheel
function: bracketleft
function: i
function: shift+bracketleft
function: ctrl+bracketleft
function: alt+bracketleft
function: super+bracketleft

# Wheel function descriptions (shown in OSD)
wheel_description_0: Brush Size
wheel_description_1: Opacity
wheel_description_2: Flow
wheel_description_3: Rotation
wheel_description_4: Scatter
wheel_description_5: Spacing
```

### Wheel Function Descriptions (v1.7.0)
Each wheel function index can have a human-readable description shown in the OSD. Descriptions are displayed in the wheel set indicator, the function pair line, and in action messages.

```bash
# Format: wheel_description_<index>: <description>
# Maximum 64 characters, printable ASCII only
wheel_description_0: Brush Size
wheel_description_1: Opacity
```

The OSD displays the current set's function pair with an active indicator:
```
Mode: Sets  [1][2][3]
Leader: OFF
Set 1:  > Brush Size  |    Opacity
```

**Workflow Example:**
1. Start in Set 1 (brush size / opacity)
2. Single-click to toggle between brush size and opacity
3. Double-click to jump to Set 2 (flow / rotation)
4. Triple-click to access Set 3 (scatter / spacing)
5. Double-click from Set 3 to return to Set 2, or triple-click to return to Set 1

**Benefits:**
- Quick access to frequently-used functions (Set 1)
- Organized workflow by grouping related tools
- No need to cycle through unused functions
- Maximum 6 wheel functions (3 sets x 2 functions)

### Configuration Options

```bash
# Wheel mode selection
wheel_mode: sequential        # Classic cycling (default)
wheel_mode: sets              # Set-based navigation

# Multi-click timeout (sets mode only)
wheel_click_timeout: 300      # Milliseconds (range: 20-990)
```

## Configuring
Edit or copy **default.cfg** to add your own keys/commands and use the `-c` flag to specify the location of the config file. New config files do not need to end in ".cfg".

### Configuration Format
```bash
# Button layout (0-17 are regular buttons, 18 is wheel button)
#
# +-------------------+
# |  .---.            |
# | ( 18  )    HUION  |
# |  '---'            |
# |-------------------|
# |  0 |  1 |  2 | 3  |
# |----+----+----+----|
# |  4 |  5 |  6 | 7  |
# |----+----+----+----|
# |  8 |  9 | 10 | 11 |
# |----+----+----+----|
# | 12 | 13 | 14 |    |
# |----+----+----| 15 |
# |    16   | 17 |    |
# +-------------------+
#

# Button Types:
# 0: Key - Acts as a key or key combination (e.g., "a", "ctrl+a")
# 1: Function - Runs a bash command/script (e.g., "krita", "echo Hello")
# 2: Mouse - Simulates mouse buttons ("mouse1", "mouse2", etc.)

# Special functions:
# "swap" - Changes wheel button function (type: 1, function: swap)
#          Behavior depends on wheel_mode setting (sequential or sets)
# "leader" - Marks button as leader key (type: 0, function: leader)

# Wheel toggle configuration (v1.5.1):
# wheel_mode: sequential       # Classic cycling through all functions (default)
# wheel_mode: sets             # Set-based navigation with multi-click
# wheel_click_timeout: 300     # Multi-click detection timeout in ms (20-990)
```

## hid_uclogic Compatibility

### With OpenTabletDriver
By default, the driver is compatible with OpenTabletDriver (which requires hid_uclogic to be unloaded).

**Default mode:** `enable_uclogic: false` (OpenTabletDriver compatible)

### Configuring Compatibility
Add to your config file:
```bash
enable_uclogic: true   # Use with hid_uclogic loaded
enable_uclogic: false  # Use without hid_uclogic (default)
```

### Solutions for Conflicts
If you encounter permission errors or device conflicts:

1. **Unload hid_uclogic module:**
   ```bash
   sudo rmmod hid_uclogic
   ```

2. **Blacklist hid_uclogic (prevents auto-loading):**
   ```bash
   echo 'blacklist hid_uclogic' | sudo tee /etc/modprobe.d/kd100-blacklist.conf
   sudo update-initramfs -u  # On Debian/Ubuntu
   ```

3. **Set enable_uclogic: true** in config and use `--uclogic` flag:
   ```bash
   sudo ./KD100 --uclogic
   ```

## Caveats
- This only works on X11 based desktops (because it relies on xdotool) but can be patched for Wayland desktops by altering the handler module in `src/handler.c`.
- You do not need to run this with sudo if you set a udev rule for the device. Create/edit a rule file in `/etc/udev/rules.d/` and add the following, then save and reboot or reload your udev rules:
  ```bash
  SUBSYSTEM=="usb",ATTRS{idVendor}=="256c",ATTRS{idProduct}=="006d",MODE="0666",GROUP="plugdev"
  ```
- Technically speaking, this can support other devices, especially if they send the same type of byte information. Otherwise, the code should be easy enough to edit and add support for other USB devices. If you want to see the information sent by different devices, change the device constants in `src/device.h` and run it with two debug flags.

## Tested Distros
- Arch Linux
- Manjaro
- Ubuntu
- Pop OS

## Known Issues
- Setting shortcuts like "ctrl+c" will close the driver if it ran from a terminal and it's active.
- The driver cannot trigger keyboard shortcuts from combining multiple buttons on the device (due to how the data is packaged).

## Version History

### v1.7.2 (Current) - Per-App Profiles & Hot Reload
- **Directory-Based Profiles**: New `apps.profiles.d/` directory with one `.cfg` per application
  - Each file defines name, pattern, priority, and button/wheel/description overrides
  - Preferred over monolithic `profiles.cfg` (backward compatible)
- **Overlay Semantics**: Profiles override only explicitly set parameters on top of `default.cfg`
  - Keys, wheel functions, and descriptions are per-profile
  - Leader, OSD, wheel mode, and hardware settings remain global
- **Hot Reload**: inotify-based file watching on `apps.profiles.d/`
  - Profile files are automatically reloaded when modified, created, or deleted
  - No driver restart required
- **OSD Profile Notifications**: OSD shows "Profile: name" on profile switch
- **Sticky Profiles**: Switching to a window without a matching profile keeps the current profile active
- **Consistency Checking**: Validates profiles on load (duplicate names, duplicate patterns, button bounds, type validation)
- **Configuration**: `profiles_dir: apps.profiles.d` in config file

### v1.7.1 - Leader Key Descriptions
- **Leader Key Descriptions**: New `leader_description_N` config fields for per-button labels when leader is active
  - Descriptions swap in on eligible buttons in the expanded OSD keyboard layout
  - Purple-tinted buttons show the leader description instead of the normal one
  - Input validation: max 64 chars, printable ASCII only

### v1.7.0 - Enhanced OSD Feedback & Wheel Descriptions
- **Wheel Function Descriptions**: New `wheel_description_N` config fields for human-readable names
  - Shown in OSD set indicator, function pair display, and action messages
- **Active Button Highlighting**: Pressed buttons flash green for 500ms in expanded view
- **Leader Key Visual Feedback**: Orange highlight on active leader button, purple tint on eligible keys
- **Wheel Set Indicator**: Numbered `[1][2][3]` boxes next to mode line, green = active set
- **Wheel Action Aggregation**: Repeated wheel turns coalesced into single display message
- **Mode & Leader Display**: Dedicated lines for mode+sets and leader state in both views
- **3-Command History**: Recent actions shown in both minimal and expanded views
- **Input Validation**: `sanitize_description()` enforces max 64 chars, printable ASCII only

### v1.6.0 - On-Screen Display & Profiles
- **On-Screen Display**: Semi-transparent overlay showing key actions
  - Minimal mode: Recent actions with key descriptions
  - Expanded mode: Full keyboard layout view
  - Auto-show on key press, auto-hide after timeout
  - Hover detection prevents unwanted hiding
  - Draggable window positioning
- **Profile System**: Automatic configuration per application
  - Window title pattern matching (wildcards supported)
  - Per-profile key descriptions
  - Automatic switching based on active window
- **Scalable UI**: Font size scales entire interface proportionally
- **New Modules**: osd.c/h, window.c/h, profiles.c/h

### v1.5.1 - Wheel Toggle Modes
- **Configurable Wheel Modes**: Choose between sequential or set-based navigation
- **Multi-Click Detection**: Single, double, and triple-click support for button 18
- **Set-Based Navigation**: Organize up to 6 wheel functions into 3 sets of 2
- **Configurable Timeout**: Adjustable multi-click detection window (20-990ms)
- **Backward Compatible**: Defaults to sequential mode (classic behavior)

### v1.5.0 - Modular Architecture
- **Complete Refactoring**: Transformed from monolithic 1,520-line file to 7 focused modules
- **Improved Maintainability**: Clean separation of concerns across modules
- **Clean Compilation**: Zero warnings with `-Wall -Wextra -Wpedantic` flags
- **Preserved Features**: 100% feature parity with v1.4.9

### v1.4.9
- **Enhanced Leader Key System**: Three configurable modes (one_shot, sticky, toggle)
- **Per-button Leader Eligibility**: Control which buttons can be modified by leader
- **Fixed Toggle Mode**: Now properly persists until explicitly disabled

### v1.4.5
- **Crash Handler**: Advanced debugging with stack traces and line numbers
- **hid_uclogic Compatibility**: Configurable compatibility mode
- **Build System**: Multiple build targets (debug, release, sanitizers)

### v1.4
- Basic leader key functionality
- Wheel button support
- Basic config file parsing

## Troubleshooting

### Common Issues
1. **Permission denied errors:**
   - Check if hid_uclogic is loaded: `lsmod | grep hid_uclogic`
   - Unload it: `sudo rmmod hid_uclogic`
   - Or set `enable_uclogic: true` in config

2. **Leader key not working:**
   - Verify leader button is configured correctly
   - Check leader mode setting
   - Ensure button eligibility is set properly

3. **Config file not loading:**
   - Use `-c` flag to specify path: `./KD100 -c /path/to/config.cfg`
   - Check config file syntax (comments must start at beginning of line)

### Debugging
```bash
# Level 1 debug (basic info)
sudo ./KD100 -d

# Level 2 debug (full data packets)
sudo ./KD100 -d -d

# Dry run (see data without sending)
sudo ./KD100 -dry

# Debug build with crash handler
make debug
sudo ./KD100-debug -d
```

## Contributing
Feel free to submit issues and pull requests. When contributing code:
- Follow the modular architecture in `src/`
- Use the debug build for testing: `make debug`
- Run memory checks: `make valgrind`
- Test with AddressSanitizer: `make asan`
- Ensure clean compilation: no warnings with `-Wall -Wextra -Wpedantic`

## Development

### Code Structure
The modular design makes it easy to add new features:
- Add device support -> Modify `src/device.c`
- Change key handling -> Modify `src/handler.c`
- Add config options -> Modify `src/config.c`
- Extend leader modes -> Modify `src/leader.c`
- Add profile support -> Add `.cfg` files to `apps.profiles.d/`

### Adding New Features
1. Identify the appropriate module for your feature
2. Add necessary data structures to the module's `.h` file
3. Implement functionality in the module's `.c` file
4. Test with `make debug` and `make asan`
5. Submit a pull request

## License
This project is licensed under the GPL-3.0 License - see the LICENSE file for details.

## Acknowledgments
- Original driver by mckset
- Enhanced features and debugging by community contributors
- OpenTabletDriver compatibility improvements
- Modular architecture refactoring (v1.5.0)
