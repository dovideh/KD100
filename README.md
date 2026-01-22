# Huion KD100 Linux Driver
A simple driver for the Huion KD100 mini Keydial written in C to give the device some usability while waiting for Huion to fix their Linux drivers. Each button can be configured to either act as a key/multiple keys or to execute a program/command.

**Version 1.5.0** introduces a complete modular architecture refactoring for improved maintainability, while preserving all features from v1.4.9.

> **NOTICE:** When updating from **v1.31** or below, make sure you updated your config file to follow the new format shown in the default config file.

## Features
- **Modular Architecture (v1.5.0)**: Clean separation into 7 focused modules for easy maintenance
- **Enhanced Leader Key System**: Three configurable modes (one_shot, sticky, toggle)
- **Per-button Leader Eligibility**: Control which buttons can be modified by leader
- **hid_uclogic Compatibility**: Work with or without the hid_uclogic kernel module
- **Advanced Debugging**: Stack traces and line numbers on crashes (debug builds)
- **Multiple Build Targets**: Debug, release, sanitizer builds
- **Clean Compilation**: Zero warnings with strict compiler flags
- **Better Code Organization**: Easy to extend and maintain

## Architecture (v1.5.0)

The codebase is organized into focused modules:

```
src/
├── main.c       - Application entry point and orchestration
├── config.c/h   - Configuration file parsing and management
├── device.c/h   - USB device discovery and event loop
├── leader.c/h   - Leader key system implementation
├── handler.c/h  - Event handling and key execution
├── utils.c/h    - Utility functions (time, string, parsing)
└── compat.c/h   - Hardware compatibility layer
```

Each module has a single, clear responsibility, making the code easier to understand, test, and extend.

## Pre-Installation
**Arch Linux/Manjaro:**
```bash
sudo pacman -S libusb xdotool
```

**Ubuntu/Debian/Pop OS:**
```bash
sudo apt-get install libusb-1.0-0-dev xdotool
```

> **NOTE:** Some distros label libusb as "libusb-1.0-0" and others might require the separate "libusb-1.0-dev" package.

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

## Enhanced Leader Key System

### Leader Modes
The enhanced leader key system supports three modes:

1. **one_shot** (default): Leader + 1 key = combination, then reset
   ```
   Press Leader → Press Key → Send combination → Reset
   ```

2. **sticky**: Leader stays active for multiple keys until timeout
   ```
   Press Leader → Press Key1 → Send combo → Press Key2 → Send combo → Timeout → Reset
   ```

3. **toggle**: Leader toggles on/off (press to enable, press again to disable)
   ```
   Press Leader → Enable → Press Key1 → Send combo → Press Key2 → Send combo → Press Leader → Disable
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
# "leader" - Marks button as leader key (type: 0, function: leader)
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

### v1.5.0 (Current) - Modular Architecture
- **Complete Refactoring**: Transformed from monolithic 1,520-line file to 7 focused modules
- **Improved Maintainability**: Clean separation of concerns across modules
- **Better Code Organization**: Each module has a single, clear responsibility
- **Enhanced Testability**: Modules can be tested independently
- **Clean Compilation**: Zero warnings with `-Wall -Wextra -Wpedantic` flags
- **Preserved Features**: 100% feature parity with v1.4.9
- **Module Structure**:
  - `main.c` (269 lines) - Entry point and orchestration
  - `config.c/h` (352 lines) - Configuration parsing
  - `device.c/h` (402 lines) - USB device handling
  - `leader.c/h` (198 lines) - Leader key system
  - `handler.c/h` (65 lines) - Event handling
  - `utils.c/h` (122 lines) - Utility functions
  - `compat.c/h` (66 lines) - Compatibility layer

### v1.4.9
- **Enhanced Leader Key System**: Three configurable modes (one_shot, sticky, toggle)
- **Per-button Leader Eligibility**: Control which buttons can be modified by leader
- **Fixed Toggle Mode**: Now properly persists until explicitly disabled
- **Fixed Config Parsing**: Each button's eligibility is now properly tracked
- **Improved Debug Output**: Shows toggle state and mode information

### v1.4.5
- **Crash Handler**: Advanced debugging with stack traces and line numbers
- **hid_uclogic Compatibility**: Configurable compatibility mode
- **Memory Management**: Proper cleanup and error handling
- **Build System**: Multiple build targets (debug, release, sanitizers)
- **Config Parsing**: More robust handling of config files

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
- Add device support → Modify `src/device.c`
- Change key handling → Modify `src/handler.c`
- Add config options → Modify `src/config.c`
- Extend leader modes → Modify `src/leader.c`

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
