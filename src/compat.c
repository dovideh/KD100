#include "compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>

// Check if a kernel module is loaded
int is_module_loaded(const char* module_name) {
    char path[256];
    snprintf(path, sizeof(path), "/sys/module/%s", module_name);
    return (access(path, F_OK) == 0);
}

// Try to access device via hidraw (alternative to libusb)
int try_hidraw_access(void) {
    DIR *dir;
    struct dirent *ent;
    char path[256];
    char buf[256];

    dir = opendir("/dev");
    if (dir == NULL) return -1;

    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "hidraw", 6) == 0) {
            snprintf(path, sizeof(path), "/sys/class/hidraw/%s/device/uevent",
                     ent->d_name);
            FILE *f = fopen(path, "r");
            if (f) {
                while (fgets(buf, sizeof(buf), f)) {
                    if (strstr(buf, "VID=256C") && strstr(buf, "PID=006D")) {
                        snprintf(path, sizeof(path), "/dev/%s", ent->d_name);
                        fclose(f);
                        closedir(dir);
                        int fd = open(path, O_RDWR);
                        if (fd >= 0) {
                            printf("Successfully opened device via hidraw\n");
                            return fd;
                        }
                    }
                }
                fclose(f);
            }
        }
    }
    closedir(dir);
    return -1;
}

// Print compatibility warning
void print_compatibility_warning(void) {
    printf("\n=========================================================\n");
    printf("COMPATIBILITY WARNING\n");
    printf("=========================================================\n");
    printf("The hid_uclogic kernel module may interfere with this driver.\n");
    printf("\nIf you're using OpenTabletDriver:\n");
    printf("  • Keep enable_uclogic: false (default)\n");
    printf("  • Unload hid_uclogic: sudo rmmod hid_uclogic\n");
    printf("  • Blacklist it: echo 'blacklist hid_uclogic' | sudo tee /etc/modprobe.d/kd100-blacklist.conf\n");
    printf("\nIf you need hid_uclogic for other tablet functions:\n");
    printf("  • Set enable_uclogic: true in config\n");
    printf("  • Driver will attempt to work around the module\n");
    printf("=========================================================\n\n");
}
