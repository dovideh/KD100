#ifndef DEVICE_H
#define DEVICE_H

#include <libusb-1.0/libusb.h>
#include "config.h"

// Device identifiers
#define DEVICE_VID 0x256c
#define DEVICE_PID 0x006d

// Device management functions
void device_run(libusb_context* ctx, config_t* config, int debug, int accept, int dry);

#endif // DEVICE_H
