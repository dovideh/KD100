# Huion KD100 Linux Driver
A simple driver for the Huion KD100 mini Keydial written in C to give the device some usability while waiting for Huion to fix their Linux drivers. Each button can be configured to either act as a key/multiple keys or to execute a program/command

> **_NOTICE:_**  When updating from **v1.31** or below, make sure you updated your config file to follow the new format shown in the default config file

Pre-Installation
------------
Arch Linux/Manjaro:
```
sudo pacman -S libusb-1.0 xdotool
```
Ubuntu/Debian/Pop OS:
```
sudo apt-get install libusb-1.0-0-dev xdotool
```
> **_NOTE:_**  Some distros label libusb as "libusb-1.0-0" and others might require the separate "libusb-1.0-dev" package

Installation
------------
You can either download the latest release or run the following:
```
git clone https://github.com/mckset/KD100.git
cd KD100
make
```

> Running make as root will install the driver as a command and create a folder in ~/.config to store config files

Usage
-----
```
sudo ./KD100 [options]
```
**-a**  Assume that the first device that matches the vid and pid is the keydial (skips prompt to select a device)

**-c**  Specify a config file to use after the flag (./default.cfg or ~/.config/KD100/default.cfg is used normally)

**-d**  Enable debug output (can be used twice to output the full packet of data recieved from the device)

**-dry**  Display data sent from the keydial and ignore events

**-h**  Displays a help message

Configuring
----------
Edit or copy **default.cfg** to add your own keys/commands and use the '-c' flag to specify the location of the config file. New config files do not need to end in ".cfg".

Caveats
-------
- This only works on X11 based desktops (because it relies on xdotool) but can be patched for wayland desktops by altering the "handler" function
- You do not need to run this with sudo if you set a udev rule for the device. Create/edit a rule file in /etc/udev/rules.d/ and add the following, then save and reboot or reload your udev rules
```
SUBSYSTEM=="usb",ATTRS{idVendor}=="256c",ATTRS{idProduct}=="006d",MODE="0666",GROUP="plugdev"
```
- Technically speaking, this can support other devices, especially if they send the same type of byte information, otherwise the code should be easy enough to edit and add support for other usb devices. If you want to see the information sent by different devices, change the vid and pid in the program and run it with two debug flags

Tested Distros
--------------
- Arch linux
- Manjaro
- Ubuntu
- Pop OS

Known Issues
------------
- Setting shortcuts like "ctrl+c" will close the driver if it ran from a terminal and it's active
- The driver cannot trigger keyboard shortcuts from combining multiple buttons on the device
> Because of how the data is packaged, there currently is no work around for this



# KD100 Driver - Changes and Improvements Document

## Overview
This document outlines the changes made between the original KD100 driver (v1.4) and the enhanced version (v1.4.5). The new version introduces significant improvements in stability, debugging capabilities, and compatibility with other tablet drivers.

## Version History
- **Original**: v1.4
- **Enhanced**: v1.4.5

## Major Changes

### 1. **Crash Handler & Debugging Enhancements**
```c
#ifdef DEBUG
#include <execinfo.h>
#include <signal.h>
// ... crash handler implementation
void setup_crash_handler(void);
#endif
```
**New Features:**
- Advanced crash handler with stack trace resolution
- Line number resolution using addr2line and gdb
- Base address calculation for accurate debugging
- Signal handling for SEGV, ABRT, BUS, ILL, FPE, TERM

**Benefits:**
- Easier debugging of crashes
- Detailed stack traces with line numbers
- Better crash reporting for issue diagnosis

### 2. **hid_uclogic Compatibility Mode**
```c
int enable_uclogic = 0; // Default: don't use hid_uclogic
int is_module_loaded(const char* module_name);
void print_compatibility_warning();
```

**New Features:**
- Configurable compatibility with hid_uclogic kernel module
- Automatic detection of loaded modules
- Alternative hidraw access method when modules conflict
- Clear compatibility warnings and solutions

**Benefits:**
- Coexistence with OpenTabletDriver
- Workarounds for kernel module conflicts
- Better error messages for permission issues

### 3. **Memory Management Improvements**
```c
// Proper initialization
for (int i = 0; i < totalButtons; i++) {
    events[i].function = NULL;
    events[i].type = 0;
}

// Proper cleanup
for (int i = 0; i < totalButtons; i++) {
    if (events[i].function != NULL) {
        free(events[i].function);
    }
}
```

**Fixes:**
- Memory leaks in config parsing
- Proper NULL initialization of structures
- Comprehensive cleanup on exit
- Error handling for memory allocation failures

### 4. **Config File Parsing Enhancements**
```c
// New parsing approach
while (fgets(data, 512, f) != NULL) {
    data[strcspn(data, "\n")] = 0;
    // Skip comments and empty lines
    if (strstr(data, "//") == data || strlen(data) == 0) {
        continue;
    }
    // ... improved parsing logic
}
```

**Improvements:**
- Support for `enable_uclogic:` config option
- Better comment handling (lines starting with `//`)
- Trim leading whitespace
- More robust error handling
- Support for `strdup()` for proper string management

### 5. **Safety and Validation**
```c
// Added parameter validation
void Handler(char* key, int type, int debug) {
    if (key == NULL) {
        if (debug == 1) printf("Handler called with NULL key\n");
        return;
    }
    // ... additional checks
}
```

**Additions:**
- NULL pointer checks throughout
- Bounds checking for array accesses
- Improved error messages
- Parameter validation in functions

### 6. **Build System Overhaul (Makefile)**
```makefile
# Multiple build targets
all: $(TARGET)
debug: CFLAGS += $(DEBUG_FLAGS)
release: CFLAGS += $(RELEASE_FLAGS)

# Advanced debugging tools
gdb: debug
valgrind: debug
strace: debug
asan: CFLAGS += -fsanitize=address
```

**New Targets:**
- `make debug` - Full debug symbols and crash handler
- `make release` - Optimized release build
- `make gdb` - Auto-run in GDB debugger
- `make valgrind` - Memory checking
- `make asan` - AddressSanitizer build
- `make ubsan` - UndefinedBehaviorSanitizer

### 7. **Command Line Interface Enhancements**
```c
// New command line options
if (strcmp(in[arg], "--uclogic") == 0) {
    enable_uclogic = 1;
}
if (strcmp(in[arg], "--no-uclogic") == 0) {
    enable_uclogic = 0;
}
```

**New Options:**
- `--uclogic` - Force hid_uclogic compatibility mode
- `--no-uclogic` - Disable hid_uclogic compatibility
- Enhanced help text with compatibility information

### 8. **Config File Updates**
**Original Config:**
- Basic key mappings for general use
- Simple wheel functions

**New Config:**
- Optimized for graphic design (Krita/Photoshop shortcuts)
- More logical button layout for creative work
- Enhanced wheel functions for brush size/zoom control

## Technical Improvements Summary

| Area | Original | Enhanced |
|------|----------|----------|
| **Debugging** | Basic printf statements | Full crash handler with stack traces |
| **Memory Management** | Potential leaks | Proper allocation/cleanup |
| **Config Parsing** | Fragile string parsing | Robust line-by-line parsing |
| **Module Compatibility** | None | hid_uclogic detection & workarounds |
| **Build System** | Simple install target | Multiple build configurations |
| **Error Handling** | Minimal | Comprehensive validation |
| **CLI Options** | Basic flags | Extended with compatibility controls |

## Compatibility Notes

### With OpenTabletDriver
- **Default mode**: Compatible (enable_uclogic: false)
- **Conflicts**: hid_uclogic module may interfere
- **Solution**: Unload module or use alternative access methods

### Kernel Module Interactions
```c
// Module state checking
int uclogic_loaded = is_module_loaded("hid_uclogic");
int wacom_loaded = is_module_loaded("wacom");
```

The driver now:
1. Detects loaded kernel modules
2. Provides clear warnings about conflicts
3. Offers alternative access methods (hidraw)
4. Allows configurable compatibility modes

## Installation Changes

### New Build Options:
```bash
# Standard build
make

# Debug build (with crash handler)
make debug
./KD100-debug

# Release build (optimized)
make release
./KD100-release

# Install system-wide
sudo make install
```

### Dependencies Added:
- `libdl` for dynamic loading (debug builds)
- Advanced debugging tools (optional): gdb, valgrind, strace

## Migration Guide

### For Existing Users:
1. **Config Files**: New config format is backward compatible but adds `enable_uclogic:` option
2. **Build**: Use `make` as before, or `make debug` for better crash reporting
3. **Running**: Add `--uclogic` flag if using with hid_uclogic module loaded

### For OpenTabletDriver Users:
1. Keep `enable_uclogic: false` in config
2. Unload hid_uclogic module: `sudo rmmod hid_uclogic`
3. Or blacklist it to prevent auto-loading

## Performance Impact

- **Debug builds**: Larger binaries with full symbols (~2-3x size)
- **Release builds**: Optimized, similar to original performance
- **Memory usage**: Slightly higher due to safety checks
- **Runtime**: Minimal overhead from additional validation

## Known Issues Resolved

1. **Config parsing crashes** - Fixed with better memory management
2. **Module conflicts** - Added detection and workarounds
3. **Poor crash reporting** - Enhanced with stack traces
4. **Memory leaks** - Proper cleanup implemented

## Future Compatibility

The new architecture allows for:
- Easier addition of new device support
- Better integration with other input systems
- Simplified debugging for community contributions
- Configurable compatibility modes for different environments

---

**Note**: The enhanced version maintains full backward compatibility with existing config files and usage patterns while providing significant improvements in stability and debugging capabilities.
