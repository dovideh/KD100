#ifndef COMPAT_H
#define COMPAT_H

// Compatibility functions for hid_uclogic and device access

// Check if a kernel module is loaded
int is_module_loaded(const char* module_name);

// Try to access device via hidraw (alternative to libusb)
int try_hidraw_access(void);

// Print compatibility warning
void print_compatibility_warning(void);

#endif // COMPAT_H
