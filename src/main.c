/*
	V1.7.0 - Enhanced OSD Feedback & Wheel Descriptions
	https://github.com/mckset/KD100.git
	KD100 Linux driver for X11 desktops
	Features:
	- On-screen display (OSD) for key actions
	- Profile system with window title matching
	- Collapsible OSD (minimal/expanded modes)
	- Draggable OSD overlay
	- Wildcard pattern matching for profiles
	- Modular code structure for better maintainability
	- Configurable leader modes (one_shot, sticky, toggle)
	- Per-button leader eligibility
*/

#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "device.h"
#include "compat.h"

/* ===== CRASH HANDLER ===== */
#ifdef DEBUG
#include <execinfo.h>
#include <signal.h>
#include <dlfcn.h>

static void* get_base_address(void) {
    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps) return NULL;

    char line[256];
    void *base_addr = NULL;

    while (fgets(line, sizeof(line), maps)) {
        if (strstr(line, "r-xp") && strstr(line, "KD100-debug")) {
            unsigned long start;
            sscanf(line, "%lx", &start);
            base_addr = (void*)start;
            break;
        }
    }

    fclose(maps);
    return base_addr;
}

void crash_handler(int sig) {
    void *array[50];
    size_t size;
    char **strings;

    fprintf(stderr, "\n⚠️  PROGRAM CRASHED! Signal: %d\n", sig);
    fprintf(stderr, "════════════════════════════════════════\n");

    size = backtrace(array, 50);
    strings = backtrace_symbols(array, size);

    if (strings != NULL) {
        fprintf(stderr, "Stack trace (%zu frames):\n", size);
        for (size_t i = 0; i < size; i++) {
            fprintf(stderr, "  #%zu: %s\n", i, strings[i]);
        }
        free(strings);
    }

    char exe_path[1024];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path)-1);
    if (len == -1) {
        strcpy(exe_path, "./KD100-debug");
    } else {
        exe_path[len] = '\0';
    }

    void *base_addr = get_base_address();

    fprintf(stderr, "\n=== Resolving line numbers ===\n");
    fprintf(stderr, "Executable: %s\n", exe_path);
    fprintf(stderr, "Base address: %p\n", base_addr);

    for (size_t i = 0; i < size; i++) {
        void *file_offset = array[i];
        if (base_addr) {
            file_offset = (void*)((unsigned long)array[i] - (unsigned long)base_addr);
        }

        fprintf(stderr, "\nFrame #%zu:\n", i);
        fprintf(stderr, "  Runtime address: %p\n", array[i]);
        fprintf(stderr, "  File offset:     %p\n", file_offset);

        char cmd[2048];
        snprintf(cmd, sizeof(cmd), "addr2line -e '%s' -f -C -p %p 2>&1",
                exe_path, file_offset);

        FILE *fp = popen(cmd, "r");
        if (fp) {
            char line[256];
            int has_output = 0;
            while (fgets(line, sizeof(line), fp)) {
                line[strcspn(line, "\n")] = 0;
                if (strstr(line, "??") == NULL) {
                    fprintf(stderr, "  %s\n", line);
                    has_output = 1;
                }
            }
            pclose(fp);

            if (!has_output) {
                fprintf(stderr, "  Could not resolve (trying raw address)\n");
                snprintf(cmd, sizeof(cmd), "addr2line -e '%s' -f -C -p %p 2>&1",
                        exe_path, array[i]);
                system(cmd);
            }
        }
    }

    fprintf(stderr, "\n=== Using gdb to get line info ===\n");
    char gdb_cmd[2048];
    snprintf(gdb_cmd, sizeof(gdb_cmd),
            "gdb -q '%s' -ex 'info line *%p' -ex 'quit' 2>/dev/null | grep -E \"Line|at\"",
            exe_path, array[3]);
    system(gdb_cmd);

    fprintf(stderr, "\n════════════════════════════════════════\n");
    exit(1);
}

void setup_crash_handler(void) {
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGBUS, crash_handler);
    signal(SIGILL, crash_handler);
    signal(SIGFPE, crash_handler);
    signal(SIGTERM, crash_handler);
}
#endif

// Global keycodes array (used by utils module)
int keycodes[] = {1, 2, 4, 8, 16, 32, 64, 128, 129, 130, 132, 136, 144, 160, 192, 256, 257, 258, 260, 641, 642};

int main(int args, char *in[]) {
#ifdef DEBUG
    setup_crash_handler();
    printf("Debug mode enabled. Crash handler active.\n");
#endif

    int debug = 0, accept = 0, dry = 0, err;
    char* file = "default.cfg";
    int enable_uclogic = 0;

    // Check for xdotool
    err = system("xdotool sleep 0.01");
    if (err != 0) {
        printf("xdotool not found. Please install xdotool for key simulation.\n");
        printf("Exiting...\n");
        return -9;
    }

    // Parse command-line arguments
    for (int arg = 1; arg < args; arg++) {
        if (strcmp(in[arg], "-h") == 0 || strcmp(in[arg], "--help") == 0) {
            printf("Usage: KD100 [option]...\n");
            printf("\t-a\t\tAssume the first device that matches %04x:%04x is the Keydial\n", DEVICE_VID, DEVICE_PID);
            printf("\t-c [path]\tSpecifies a config file to use\n");
            printf("\t-d [-d]\t\tEnable debug outputs (use twice to view data sent by the device)\n");
            printf("\t-dry \t\tDisplay data sent by the device without sending events\n");
            printf("\t-h\t\tDisplays this message\n");
            printf("\nNew in v1.7.0 - ENHANCED OSD FEEDBACK:\n");
            printf("\t• Wheel function descriptions (wheel_description_N: name)\n");
            printf("\t• Active button highlighting in expanded keyboard layout\n");
            printf("\t• Leader key visual feedback (orange/purple indicators)\n");
            printf("\t• Wheel set indicator with active set highlight\n");
            printf("\t• Wheel action aggregation (no repeated messages on turn)\n");
            printf("\t• Mode and leader status display in both OSD views\n");
            printf("\t• 3-command history in both minimal and expanded views\n");
            printf("\t• Input validation for description fields (max 64 chars)\n");
            printf("\nConfiguration:\n");
            printf("\tAdd 'enable_uclogic: true' to config to work with hid_uclogic loaded\n");
            printf("\tDefault: enable_uclogic: false (compatible with OpenTabletDriver)\n\n");
            return 0;
        }
        if (strcmp(in[arg], "-d") == 0) {
            debug++;
        }
        if (strcmp(in[arg], "-dry") == 0) {
            dry = 1;
        }
        if (strcmp(in[arg], "-a") == 0) {
            accept = 1;
        }
        if (strcmp(in[arg], "-c") == 0) {
            if (in[arg + 1]) {
                file = in[arg + 1];
                arg++;
            } else {
                printf("No config file specified. Exiting...\n");
                return -8;
            }
        }
        if (strcmp(in[arg], "--uclogic") == 0) {
            enable_uclogic = 1;
            printf("Forcing hid_uclogic compatibility mode\n");
        }
        if (strcmp(in[arg], "--no-uclogic") == 0) {
            enable_uclogic = 0;
            printf("Disabling hid_uclogic compatibility (OpenTabletDriver mode)\n");
        }
    }

    // Initialize libusb
    libusb_context *ctx = NULL;
    err = libusb_init(&ctx);
    if (err < 0) {
        printf("Error initializing libusb: %d\n", err);
        return err;
    }

    // Load configuration
    config_t* config = config_create();
    if (config == NULL) {
        printf("Failed to create configuration\n");
        libusb_exit(ctx);
        return -1;
    }

    if (config_load(config, file, debug) < 0) {
        printf("Failed to load configuration from %s\n", file);
        config_destroy(config);
        libusb_exit(ctx);
        return -1;
    }

    // Override enable_uclogic from command line if specified
    if (enable_uclogic) {
        config->enable_uclogic = 1;
    }

    // Print startup information
    if (config->enable_uclogic) {
        printf("Mode: hid_uclogic compatibility enabled\n");
    } else {
        printf("Mode: OpenTabletDriver compatible (hid_uclogic disabled)\n");
        if (is_module_loaded("hid_uclogic")) {
            print_compatibility_warning();
        }
    }

    printf("\nKD100 Driver v1.7.0 - Enhanced OSD Feedback & Wheel Descriptions\n");
    printf("Features: OSD overlay | Profile switching | Wheel descriptions | Button highlighting\n\n");

    // Run device handler
    device_run(ctx, config, debug, accept, dry);

    // Cleanup
    config_destroy(config);
    libusb_exit(ctx);
    return 0;
}
