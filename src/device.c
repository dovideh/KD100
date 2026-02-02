#include "device.h"
#include "config.h"
#include "leader.h"
#include "handler.h"
#include "utils.h"
#include "compat.h"
#include "osd.h"
#include "profiles.h"
#include "window.h"
#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

void device_run(libusb_context* ctx, config_t* config, int debug, int accept, int dry) {
    int err = 0;
    int wheelFunction = 0;
    int c = 0;
    char indi[] = "|/-\\";
    event prevEvent;
    prevEvent.function = "";
    prevEvent.type = 0;

    // Multi-click detection state for button 18
    struct timeval last_button18_time = {0, 0};
    int button18_click_count = 0;
    int wheel_current_set = 0;      // Current set: 0 (functions 0-1), 1 (functions 2-3), 2 (functions 4-5)
    int wheel_position_in_set = 0;  // Position within set: 0 or 1

    // OSD and profile manager state
    osd_state_t* osd = NULL;
    profile_manager_t* profile_manager = NULL;
    struct timeval last_profile_check = {0, 0};
    config_t* active_config = config;  // Currently active configuration

    system("clear");

    if (debug > 0) {
        if (debug > 2)
            debug = 2;
        printf("Version 1.5.1 - Wheel Toggle Modes\n");
        printf("Debug level: %d\n", debug);
    }

    // Print configuration if debug
    config_print(config, debug);

    // Initialize OSD if enabled
    if (config->osd.enabled) {
        osd = osd_create(config);
        if (osd != NULL) {
            // Apply OSD settings from config
            osd->pos_x = config->osd.pos_x;
            osd->pos_y = config->osd.pos_y;
            osd->opacity = config->osd.opacity;
            osd->display_duration_ms = config->osd.display_duration_ms;
            osd->min_width = config->osd.min_width;
            osd->min_height = config->osd.min_height;
            osd->expanded_width = config->osd.expanded_width;
            osd->expanded_height = config->osd.expanded_height;

            // Initialize X11 display
            if (osd_init_display(osd) == 0) {
                // Set initial key descriptions from config
                for (int i = 0; i < 19; i++) {
                    if (config->key_descriptions[i]) {
                        osd_set_key_description(osd, i, config->key_descriptions[i]);
                    }
                }

                // Show OSD if start_visible is set
                if (config->osd.start_visible) {
                    osd_show(osd);
                }
                printf("OSD: Initialized successfully\n");
            } else {
                printf("OSD: Failed to initialize X11 display\n");
                osd_destroy(osd);
                osd = NULL;
            }
        } else {
            printf("OSD: Failed to create OSD state\n");
        }
    }

    // Initialize profile manager if profiles file is configured
    if (config->profile.profiles_file) {
        profile_manager = profile_manager_create(config);
        if (profile_manager != NULL) {
            profile_manager_set_debug(profile_manager, debug);

            // Initialize with shared display if OSD is enabled
            void* shared_display = osd ? osd->display : NULL;
            if (profile_manager_init(profile_manager, shared_display, osd) == 0) {
                // Load profiles from file
                if (profile_manager_load(profile_manager, config->profile.profiles_file) == 0) {
                    printf("Profiles: Loaded from %s\n", config->profile.profiles_file);
                    if (debug) {
                        profile_manager_print(profile_manager);
                    }
                } else {
                    printf("Profiles: Failed to load from %s\n", config->profile.profiles_file);
                }
            } else {
                printf("Profiles: Failed to initialize manager\n");
                profile_manager_destroy(profile_manager);
                profile_manager = NULL;
            }
        }
    }

    // Check module state
    int uclogic_loaded = is_module_loaded("hid_uclogic");

    if (debug) {
        printf("Module status: hid_uclogic=%s\n",
               uclogic_loaded ? "loaded" : "not loaded");
        printf("Config: enable_uclogic=%s\n", config->enable_uclogic ? "true" : "false");
    }

    if (uclogic_loaded && !config->enable_uclogic) {
        print_compatibility_warning();
        printf("hid_uclogic is loaded but enable_uclogic is false.\n");
        printf("Attempting alternative access methods...\n");
    }

    int devI = 0;
    int use_hidraw_fallback = 0;

    while (err == 0 || err == LIBUSB_ERROR_NO_DEVICE) {
        libusb_device **devs;
        libusb_device *dev;
        struct libusb_config_descriptor *desc;
        libusb_device_handle *handle = NULL;
        int hidraw_fd = -1;

        if (!config->enable_uclogic && uclogic_loaded && use_hidraw_fallback == 0) {
            hidraw_fd = try_hidraw_access();
            if (hidraw_fd >= 0) {
                printf("Using hidraw interface (bypassing hid_uclogic)\n");
                use_hidraw_fallback = 1;
            } else {
                printf("hidraw access failed, trying libusb with workarounds...\n");
            }
        }

        err = libusb_get_device_list(ctx, &devs);
        if (err < 0) {
            printf("Unable to retrieve USB devices. Exiting...\n");
            return;
        }

        int d = 0, found = 0;
        devI = 0;
        libusb_device *savedDevs[sizeof(devs)];

        while ((dev = devs[d++]) != NULL) {
            struct libusb_device_descriptor devDesc;
            unsigned char info[200] = "";
            err = libusb_get_device_descriptor(dev, &devDesc);

            if (err < 0) {
                if (debug > 0) {
                    printf("Unable to retrieve info from device #%d. Ignoring...\n", d);
                }
            } else if (devDesc.idVendor == DEVICE_VID && devDesc.idProduct == DEVICE_PID) {
                if (accept == 1) {
                    if (getuid() != 0) {
                        err = libusb_open(dev, &handle);
                        if (err < 0) {
                            if (err == LIBUSB_ERROR_ACCESS && !config->enable_uclogic) {
                                printf("\nPermission denied - hid_uclogic may be claiming the device.\n");
                                printf("Try: sudo rmmod hid_uclogic\n");
                                printf("Or set enable_uclogic: true in config\n");
                            }
                            handle = NULL;
                            if (err == LIBUSB_ERROR_ACCESS) {
                                printf("Error: Permission denied\n");
                                libusb_free_device_list(devs, 1);
                                return;
                            }
                        }
                        if (debug > 0) {
                            printf("\nUsing: %04x:%04x (Bus: %03d Device: %03d)\n",
                                   DEVICE_VID, DEVICE_PID,
                                   libusb_get_bus_number(dev),
                                   libusb_get_device_address(dev));
                        }
                        break;
                    } else {
                        err = libusb_open(dev, &handle);
                        if (err < 0) {
                            printf("\nUnable to open device. Error: %d\n", err);
                            handle = NULL;
                        }
                        err = libusb_get_string_descriptor_ascii(handle, devDesc.iProduct, info, 200);
                        if (debug > 0) {
                            printf("\n#%d | %04x:%04x : %s\n", d, DEVICE_VID, DEVICE_PID, (char*)info);
                        }
                        if (strlen((char*)info) == 0 || strcmp("Huion Tablet_KD100", (char*)info) == 0) {
                            break;
                        } else {
                            libusb_close(handle);
                            handle = NULL;
                            found++;
                        }
                    }
                } else {
                    savedDevs[devI] = dev;
                    devI++;
                }
            }
        }

        if (accept == 0) {
            int in = -1;
            while (in == -1) {
                char buf[64];
                printf("\n");
                system("lsusb");
                printf("\n");
                for (d = 0; d < devI; d++) {
                    printf("%d) %04x:%04x (Bus: %03d Device: %03d)\n", d,
                           DEVICE_VID, DEVICE_PID,
                           libusb_get_bus_number(savedDevs[d]),
                           libusb_get_device_address(savedDevs[d]));
                }
                printf("Select a device to use: ");
                fflush(stdout);
                fgets(buf, 10, stdin);
                in = atoi(buf);
                if (in >= devI || in < 0) {
                    in = -1;
                }
                system("clear");
            }
            err = libusb_open(savedDevs[in], &handle);
            if (err < 0) {
                printf("Unable to open device. Error: %d\n", err);
                handle = NULL;
                if (err == LIBUSB_ERROR_ACCESS) {
                    printf("Error: Permission denied\n");
                    if (!config->enable_uclogic) {
                        printf("hid_uclogic may be claiming the device.\n");
                        printf("Solutions:\n");
                        printf("  1. Unload: sudo rmmod hid_uclogic\n");
                        printf("  2. Set enable_uclogic: true in config\n");
                        printf("  3. Run driver as root (not recommended)\n");
                    }
                    libusb_free_device_list(devs, 1);
                    return;
                }
            }
        } else if (found > 0) {
            printf("Error: Found device does not appear to be the keydial\n");
            printf("Try running without the -a flag\n");
            libusb_free_device_list(devs, 1);
            return;
        }

        int interfaces = 0;
        if (handle == NULL && hidraw_fd < 0) {
            printf("\rWaiting for a device %c", indi[c]);
            fflush(stdout);
            usleep(250000);
            c++;
            if (c == 4) {
                c = 0;
            }
            err = LIBUSB_ERROR_NO_DEVICE;
        } else {
            if (debug == 0) {
                system("clear");
            }

            if (hidraw_fd >= 0) {
                printf("Starting driver via hidraw...\n");
                printf("Driver is running!\n");

                while (1) {
                    unsigned char data[64];
                    ssize_t bytes_read = read(hidraw_fd, data, sizeof(data));

                    if (bytes_read < 0) {
                        printf("Error reading from hidraw\n");
                        break;
                    }

                    if (bytes_read > 0) {
                        if (debug == 2 || dry) {
                            printf("HIDRAW DATA: [%d", data[0]);
                            for (int i = 1; i < bytes_read; i++) {
                                printf(", %d", data[i]);
                            }
                            printf("]\n");
                        }
                    }

                    usleep(10000);
                }

                close(hidraw_fd);
                return;
            } else {
                interfaces = 0;
                printf("Starting driver via libusb...\n");

                dev = libusb_get_device(handle);
                libusb_get_config_descriptor(dev, 0, &desc);
                interfaces = desc->bNumInterfaces;
                libusb_free_config_descriptor(desc);

                if (config->enable_uclogic) {
                    libusb_set_auto_detach_kernel_driver(handle, 1);
                    if (debug == 1)
                        printf("Using auto-detach (hid_uclogic compatible mode)\n");
                } else {
                    for (int x = 0; x < interfaces; x++) {
                        if (libusb_kernel_driver_active(handle, x) == 1) {
                            printf("Detaching kernel driver from interface %d...\n", x);
                            err = libusb_detach_kernel_driver(handle, x);
                            if (err != 0 && debug == 1) {
                                printf("Failed to detach kernel driver: %s\n", libusb_error_name(err));
                            }
                        }
                    }
                }

                if (debug == 1)
                    printf("Claiming interfaces... \n");

                for (int x = 0; x < interfaces; x++) {
                    int err = libusb_claim_interface(handle, x);
                    if (err != LIBUSB_SUCCESS && debug == 1)
                        printf("Failed to claim interface %d: %s\n", x, libusb_error_name(err));
                }

                printf("Driver is running!\n");
                printf("Enhanced Leader Key System v1.5.1\n");
                printf("Mode: %s | Timeout: %d ms\n",
                       leader_mode_to_string(config->leader.mode), config->leader.timeout_ms);
                printf("Wheel Mode: %s", config->wheel_mode == WHEEL_MODE_SEQUENTIAL ? "sequential" : "sets");
                if (config->wheel_mode == WHEEL_MODE_SETS) {
                    printf(" | Click Timeout: %d ms", config->wheel_click_timeout_ms);
                }
                printf("\n");
                printf("Press leader button first, then eligible buttons for combinations.\n");

                err = 0;

                while (err >= 0) {
                    unsigned char data[40];
                    int keycode = 0;

                    // Update OSD (process X11 events)
                    if (osd && osd->mode != OSD_MODE_HIDDEN) {
                        osd_update(osd);
                    }

                    // Check for profile switches periodically
                    if (profile_manager && config->profile.auto_switch) {
                        struct timeval now;
                        gettimeofday(&now, NULL);
                        long time_since_check =
                            (now.tv_sec - last_profile_check.tv_sec) * 1000 +
                            (now.tv_usec - last_profile_check.tv_usec) / 1000;

                        if (time_since_check >= config->profile.check_interval_ms) {
                            last_profile_check = now;
                            int profile_changed = profile_manager_update(profile_manager);
                            if (profile_changed > 0) {
                                // Get new active config
                                config_t* new_config = profile_manager_get_config(profile_manager);
                                if (new_config != NULL) {
                                    active_config = new_config;
                                    if (debug) {
                                        printf("Switched to profile config\n");
                                    }
                                }
                            }
                        }
                    }

                    err = libusb_interrupt_transfer(handle, 0x81, data, sizeof(data), NULL, 0);

                    if (err == LIBUSB_ERROR_TIMEOUT)
                        printf("\nTIMEDOUT\n");
                    if (err == LIBUSB_ERROR_PIPE)
                        printf("\nPIPE ERROR\n");
                    if (err == LIBUSB_ERROR_NO_DEVICE)
                        printf("\nDEVICE DISCONNECTED\n");
                    if (err == LIBUSB_ERROR_OVERFLOW)
                        printf("\nOVERFLOW ERROR\n");
                    if (err == LIBUSB_ERROR_INVALID_PARAM)
                        printf("\nINVALID PARAMETERS\n");
                    if (err == -1)
                        printf("\nDEVICE IS ALREADY IN USE\n");
                    if (err < 0) {
                        if (debug == 1) {
                            printf("Unable to retrieve data: %d\n", err);
                        }
                        break;
                    }

                    // Check if we have pending button 18 clicks that need to be processed
                    if (config->wheel_mode == WHEEL_MODE_SETS && button18_click_count > 0 &&
                        (last_button18_time.tv_sec != 0 || last_button18_time.tv_usec != 0)) {
                        struct timeval now;
                        gettimeofday(&now, NULL);
                        long time_since_last_click =
                            (now.tv_sec - last_button18_time.tv_sec) * 1000 +
                            (now.tv_usec - last_button18_time.tv_usec) / 1000;

                        // If timeout has expired, process the accumulated clicks
                        if (time_since_last_click >= config->wheel_click_timeout_ms) {
                            int final_click_count = button18_click_count;
                            button18_click_count = 0;
                            last_button18_time.tv_sec = 0;
                            last_button18_time.tv_usec = 0;

                            if (debug == 1) {
                                printf("Sets mode - Button 18 clicks: %d\n", final_click_count);
                            }

                            // Process based on click count
                            if (final_click_count == 1) {
                                // Single-click: toggle within current set
                                wheel_position_in_set = 1 - wheel_position_in_set;
                            } else if (final_click_count == 2) {
                                // Double-click: toggle between Set 0 and Set 1
                                if (wheel_current_set == 0) {
                                    wheel_current_set = 1;
                                } else if (wheel_current_set == 1) {
                                    wheel_current_set = 0;
                                } else {
                                    // From Set 2, go to Set 1 (not Set 0)
                                    wheel_current_set = 1;
                                }
                                wheel_position_in_set = 0;  // Start at first function in new set
                            } else if (final_click_count >= 3) {
                                // Triple-click: toggle to/from Set 2
                                if (wheel_current_set == 2) {
                                    // From Set 2, go back to Set 0
                                    wheel_current_set = 0;
                                } else {
                                    // From Set 0 or 1, go to Set 2
                                    wheel_current_set = 2;
                                }
                                wheel_position_in_set = 0;  // Start at first function in new set
                            }

                            // Calculate actual wheel function index
                            // Note: wheelFunction may be >= totalWheels if incomplete sets exist
                            // That's OK - wheel turn handler checks bounds before executing
                            wheelFunction = (wheel_current_set * 2) + wheel_position_in_set;

                            if (debug == 1) {
                                printf("Set: %d | Position: %d | Wheel Function: %d\n",
                                       wheel_current_set, wheel_position_in_set, wheelFunction);
                                if (wheelFunction >= 0 && wheelFunction < config->totalWheels) {
                                    printf("Function: %s | %s\n",
                                           config->wheelEvents[wheelFunction].left ? config->wheelEvents[wheelFunction].left : "(null)",
                                           config->wheelEvents[wheelFunction].right ? config->wheelEvents[wheelFunction].right : "(null)");
                                } else {
                                    printf("Function: (not defined - incomplete set)\n");
                                }
                            }
                        }
                    }

                    // Convert data to keycodes
                    if (data[4] != 0)
                        keycode = data[4];
                    else if (data[5] != 0)
                        keycode = data[5] + 128;
                    else if (data[6] != 0)
                        keycode = data[6] + 256;
                    if (data[1] == 241)
                        keycode += 512;
                    if (dry)
                        keycode = 0;

                    if (debug == 1 && keycode != 0) {
                        printf("Keycode: %d\n", keycode);
                    }

                    // Handle wheel events
                    if (keycode == 641) {
                        if (wheelFunction >= 0 && wheelFunction < config->totalWheels &&
                            config->wheelEvents[wheelFunction].right != NULL) {
                            Handler(config->wheelEvents[wheelFunction].right, -1, debug);
                        }
                    } else if (keycode == 642) {
                        if (wheelFunction >= 0 && wheelFunction < config->totalWheels &&
                            config->wheelEvents[wheelFunction].left != NULL) {
                            Handler(config->wheelEvents[wheelFunction].left, -1, debug);
                        }
                    } else {
                        int button_index = find_button_index(keycode);

                        if (button_index != -1) {
                            // Check for OSD toggle button
                            if (config->osd.enabled && button_index == config->osd.osd_toggle_button && osd) {
                                osd_toggle_mode(osd);
                                if (debug) {
                                    printf("OSD mode toggled\n");
                                }
                            }

                            // Record action to OSD
                            if (osd && active_config && button_index < active_config->totalButtons) {
                                const char* action = active_config->events[button_index].function;
                                if (action && strcmp(action, "NULL") != 0) {
                                    osd_record_action(osd, button_index, action);
                                }
                            }

                            // Process button press with leader system
                            process_leader_combination(&active_config->leader, active_config->events, button_index, debug);

                            // Also handle legacy single-button events for compatibility
                            if (active_config->events[button_index].function != NULL) {
                                if (strcmp(active_config->events[button_index].function, "NULL") == 0) {
                                    if (prevEvent.type != 0) {
                                        Handler(prevEvent.function, prevEvent.type, debug);
                                        prevEvent.type = 0;
                                        prevEvent.function = "";
                                    }
                                } else if (strcmp(active_config->events[button_index].function, "swap") == 0) {
                                    // Check wheel mode
                                    if (config->wheel_mode == WHEEL_MODE_SEQUENTIAL) {
                                        // Sequential mode: simple cycling through all functions
                                        if (wheelFunction != config->totalWheels - 1) {
                                            wheelFunction++;
                                        } else {
                                            wheelFunction = 0;
                                        }
                                        if (debug == 1) {
                                            printf("Sequential mode - Wheel Function: %d\n", wheelFunction);
                                            printf("Function: %s | %s\n",
                                                   config->wheelEvents[wheelFunction].left ? config->wheelEvents[wheelFunction].left : "(null)",
                                                   config->wheelEvents[wheelFunction].right ? config->wheelEvents[wheelFunction].right : "(null)");
                                        }
                                    } else {
                                        // Sets mode: multi-click detection for set-based navigation
                                        // Just record the click - processing happens at the top of the event loop
                                        struct timeval now;
                                        gettimeofday(&now, NULL);

                                        long time_since_last_click = 0;
                                        if (last_button18_time.tv_sec != 0 || last_button18_time.tv_usec != 0) {
                                            time_since_last_click =
                                                (now.tv_sec - last_button18_time.tv_sec) * 1000 +
                                                (now.tv_usec - last_button18_time.tv_usec) / 1000;
                                        }

                                        // If click is within timeout window, increment count
                                        if (time_since_last_click > 0 && time_since_last_click < config->wheel_click_timeout_ms) {
                                            button18_click_count++;
                                            if (debug == 1) {
                                                printf("Button 18 click recorded (count now: %d)\n", button18_click_count);
                                            }
                                        } else {
                                            // This is a new click sequence
                                            button18_click_count = 1;
                                            if (debug == 1) {
                                                printf("Button 18 new click sequence started\n");
                                            }
                                        }

                                        last_button18_time = now;
                                        // Don't process yet - wait for timeout (checked at top of loop)
                                    }
                                } else if (strcmp(active_config->events[button_index].function, "mouse1") == 0 ||
                                           strcmp(active_config->events[button_index].function, "mouse2") == 0 ||
                                           strcmp(active_config->events[button_index].function, "mouse3") == 0 ||
                                           strcmp(active_config->events[button_index].function, "mouse4") == 0 ||
                                           strcmp(active_config->events[button_index].function, "mouse5") == 0) {
                                    if (strcmp(active_config->events[button_index].function, prevEvent.function)) {
                                        if (prevEvent.type != 0) {
                                            Handler(prevEvent.function, prevEvent.type, debug);
                                        }
                                        prevEvent.function = active_config->events[button_index].function;
                                        prevEvent.type = 3;
                                    }
                                    Handler(active_config->events[button_index].function, 2, debug);
                                }
                            }
                        }
                    }

                    if (debug == 2 || dry) {
                        printf("DATA: [%d", data[0]);
                        for (size_t i = 1; i < sizeof(data); i++) {
                            printf(", %d", data[i]);
                        }
                        printf("]\n");

                        if (active_config->leader.toggle_state) {
                            printf("Leader toggle: ON (mode: %s)\n", leader_mode_to_string(active_config->leader.mode));
                        } else if (active_config->leader.leader_active) {
                            struct timeval now;
                            gettimeofday(&now, NULL);
                            long elapsed = time_diff_ms(active_config->leader.leader_press_time, now);
                            printf("Leader active: YES (%ld ms elapsed, mode: %s)\n",
                                   elapsed, leader_mode_to_string(active_config->leader.mode));
                        } else {
                            printf("Leader active: NO\n");
                        }
                    }

                    // Small delay to prevent CPU hogging
                    usleep(10000);
                }

                // Cleanup
                for (int x = 0; x < interfaces; x++) {
                    if (debug == 1) {
                        printf("Releasing interface %d...\n", x);
                    }
                    libusb_release_interface(handle, x);
                }
                printf("Closing device...\n");
                libusb_close(handle);
                interfaces = 0;
                sleep(1);
            }
        }
        libusb_free_device_list(devs, 1);
    }

    // Cleanup OSD and profile manager
    if (osd) {
        osd_destroy(osd);
    }
    if (profile_manager) {
        profile_manager_destroy(profile_manager);
    }
}
