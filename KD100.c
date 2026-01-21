/*
	V1.4.5
	https://github.com/mckset/KD100.git
	KD100 Linux driver for X11 desktops
	Enhanced version with hid_uclogic compatibility mode
	Fixed config parsing bug and crash handler
*/

#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <dirent.h>
#include <fcntl.h>

/* ===== CRASH HANDLER - Fixed version with proper address translation ===== */
#ifdef DEBUG
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dlfcn.h>

// Function to get base address of executable
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
    
    // Get stack trace
    size = backtrace(array, 50);
    strings = backtrace_symbols(array, size);
    
    if (strings != NULL) {
        fprintf(stderr, "Stack trace (%zu frames):\n", size);
        for (size_t i = 0; i < size; i++) {
            fprintf(stderr, "  #%zu: %s\n", i, strings[i]);
        }
        free(strings);
    }
    
    // Get executable path
    char exe_path[1024];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path)-1);
    if (len == -1) {
        strcpy(exe_path, "./KD100-debug");
    } else {
        exe_path[len] = '\0';
    }
    
    // Get base address to calculate offsets
    void *base_addr = get_base_address();
    
    fprintf(stderr, "\n=== Resolving line numbers ===\n");
    fprintf(stderr, "Executable: %s\n", exe_path);
    fprintf(stderr, "Base address: %p\n", base_addr);
    
    for (size_t i = 0; i < size; i++) {
        // Calculate file offset from runtime address
        void *file_offset = array[i];
        if (base_addr) {
            file_offset = (void*)((unsigned long)array[i] - (unsigned long)base_addr);
        }
        
        fprintf(stderr, "\nFrame #%zu:\n", i);
        fprintf(stderr, "  Runtime address: %p\n", array[i]);
        fprintf(stderr, "  File offset:     %p\n", file_offset);
        
        // Try addr2line with file offset
        char cmd[512];
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
                // Try with raw address as fallback
                snprintf(cmd, sizeof(cmd), "addr2line -e '%s' -f -C -p %p 2>&1", 
                        exe_path, array[i]);
                system(cmd);
            }
        }
    }
    
    // Alternative: Use gdb directly
    fprintf(stderr, "\n=== Using gdb to get line info ===\n");
    char gdb_cmd[512];
    snprintf(gdb_cmd, sizeof(gdb_cmd), 
            "gdb -q '%s' -ex 'info line *%p' -ex 'quit' 2>/dev/null | grep -E \"Line|at\"", 
            exe_path, array[3]); // Try frame 3 (Handler)
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



int keycodes[] = {1, 2, 4, 8, 16, 32, 64, 128, 129, 130, 132, 136, 144, 160, 192, 256, 257, 258, 260, 641, 642};
char* file = "default.cfg";
int enable_uclogic = 0; // Default: don't use hid_uclogic (compatible with OpenTabletDriver)

typedef struct event event;
typedef struct wheel wheel;

struct event{
	int type;
	char* function;
};

struct wheel {
	char* right;
	char* left;
};

void GetDevice(libusb_context*, int, int, int);
void Handler(char*, int, int);  // Added debug parameter
char* Substring(const char*, int, int);
int is_module_loaded(const char* module_name);
int try_hidraw_access();
void print_compatibility_warning();

const int vid = 0x256c;
const int pid = 0x006d;

// Check if a kernel module is loaded
int is_module_loaded(const char* module_name) {
    char path[256];
    snprintf(path, sizeof(path), "/sys/module/%s", module_name);
    return (access(path, F_OK) == 0);
}

// Try to access device via hidraw (alternative to libusb)
int try_hidraw_access() {
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
                    // Check if this is our device (256c:006d)
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
void print_compatibility_warning() {
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

void GetDevice(libusb_context *ctx, int debug, int accept, int dry){
	int err=0, wheelFunction=0, button=-1, totalButtons=0, wheelType=0, leftWheels=0, rightWheels=0, totalWheels=0;
	char* data = malloc(512*sizeof(char)); // Data received from the config file and the USB
	event* events = malloc(1*sizeof(event)); // Stores key events and functions
	wheel* wheelEvents = malloc(1*sizeof(wheel)); // Stores wheel functions
	event prevEvent;
	uid_t uid=getuid(); // Used to check if the driver was ran as root

	// Not important
	int c=0; // Index of the loading character to display when waiting for a device

	system("clear");

	if (debug > 0){
		if (debug > 2)
			debug=2;
		printf("Version 1.4.5\nDebug level: %d\n", debug);
	}		

	// Load config file
	if (debug == 1){
		printf("Loading config...\n");
	}
	
	FILE *f;
	if (strcmp(file, "default.cfg")){
		f = fopen(file, "r");
		if (f == NULL){
                        char* home = getpwuid(getuid())->pw_dir;
                        char* config = "/.config/KD100/";
                        char temp[strlen(home)+strlen(config)+strlen(file)+1];
                        for (int i = 0; i < strlen(home); i++)
                                temp[i] = home[i];
                        for (int i = 0; i < strlen(config); i++)
                                temp[i+strlen(home)] = config[i];
                        for (int i = 0; i < strlen(file); i++)
                                temp[i+strlen(home)+strlen(config)] = file[i];
                        temp[strlen(home)+strlen(config)+strlen(file)] = '\0';
                        f = fopen(temp, "r");
                        if (f == NULL){
                                printf("CONFIG FILE NOT FOUND\n");
                                free(data);
                                free(events);
                                free(wheelEvents);
                                return;
                        }
                }
	}else{
		f = fopen(file, "r");
		if (f == NULL){
			char* home = getpwuid(getuid())->pw_dir;
			file = "/.config/KD100/default.cfg";
			char temp[strlen(file)+strlen(home)+1];
			for (int i = 0; i < strlen(home); i++)
				temp[i] = home[i];
			for (int i = 0; i < strlen(file); i++)
				temp[i+strlen(home)] = file[i];
			temp[strlen(home) + strlen(file)] = '\0';

			f = fopen(temp, "r");
			if (f == NULL){
				printf("DEFAULT CONFIGS ARE MISSING!\n");
				printf("Please add default.cfg to %s/.config/KD100/ or specify a file to use with -c\n", home);
				free(data);
				free(events);
				free(wheelEvents);
				return;
			}
		}
	}
	
	// Initialize all events to NULL
	for (int i = 0; i < totalButtons; i++) {
		events[i].function = NULL;
		events[i].type = 0;
	}
	
	// Initialize wheel events
	wheelEvents[0].right = NULL;
	wheelEvents[0].left = NULL;
	
	// Parse config file
	while (fgets(data, 512, f) != NULL) {
	    // Remove trailing newline
	    data[strcspn(data, "\n")] = 0;
	    
	    // Skip comment lines
	    if (strstr(data, "//") == data) {
		continue;
	    }
	    
	    // Skip empty lines
	    if (strlen(data) == 0) {
		continue;
	    }
	    
	    // Trim leading whitespace
	    char *line = data;
	    while (*line == ' ' || *line == '\t') line++;
	    
	    // Check for enable_uclogic setting
	    if (strncasecmp(line, "enable_uclogic:", 15) == 0) {
		char* value = line + 15;
		// Skip spaces after colon
		while (*value == ' ') value++;
		if (strncasecmp(value, "true", 4) == 0) {
		    enable_uclogic = 1;
		    if (debug) printf("Config: enable_uclogic = true\n");
		} else if (strncasecmp(value, "false", 5) == 0) {
		    enable_uclogic = 0;
		    if (debug) printf("Config: enable_uclogic = false\n");
		}
		continue;
	    }
	    
	    // Check for Button definition
	    if (strncasecmp(line, "button ", 7) == 0) {
		char* num_str = line + 7;
		// Skip spaces after "button"
		while (*num_str == ' ') num_str++;
		button = atoi(num_str);
		
		if (button >= totalButtons){
		    event* temp = realloc(events, (button+1)*sizeof(*events));
		    if (temp == NULL) {
			printf("Memory allocation failed!\n");
			free(data);
			free(events);
			free(wheelEvents);
			fclose(f);
			return;
		    }
		    events = temp;
		    // Initialize new entries
		    for (int j = totalButtons; j <= button; j++) {
			events[j].function = NULL;
			events[j].type = 0;
		    }
		    totalButtons = button+1;
		}
		continue;
	    }
	    
	    // Check for type definition
	    if (strncasecmp(line, "type:", 5) == 0 && button != -1) {
		char* type_str = line + 5;
		// Skip spaces after colon
		while (*type_str == ' ') type_str++;
		events[button].type = atoi(type_str);
		continue;
	    }
	    
	    // Check for function definition
	    if (strncasecmp(line, "function:", 9) == 0) {
		char* func_str = line + 9;
		// Skip spaces after colon
		while (*func_str == ' ') func_str++;
		
		// Duplicate the function string
		char* func_copy = strdup(func_str);
		if (func_copy == NULL) {
		    printf("Memory allocation failed!\n");
		    free(data);
		    free(events);
		    free(wheelEvents);
		    fclose(f);
		    return;
		}
		
		if (!wheelType) {
		    // Button function
		    if (button >= 0 && button < totalButtons) {
			if (events[button].function != NULL) {
			    free(events[button].function);
			}
			events[button].function = func_copy;
		    } else {
			// Invalid button index
			free(func_copy);
			if (debug) printf("Warning: function without valid button definition\n");
		    }
		} else if (wheelType == 1){
		    // Right wheel function (clockwise)
		    if (rightWheels != 0){
			wheel* temp = realloc(wheelEvents, (rightWheels+1)*sizeof(*wheelEvents));
			if (temp == NULL) {
			    printf("Memory allocation failed!\n");
			    free(data);
			    free(events);
			    free(wheelEvents);
			    free(func_copy);
			    fclose(f);
			    return;
			}
			wheelEvents = temp;
			// Initialize the new entry
			wheelEvents[rightWheels].right = func_copy;
			wheelEvents[rightWheels].left = NULL;
		    }else{
			wheelEvents[0].right = func_copy;
			wheelEvents[0].left = NULL;
		    }
		    rightWheels++;
		}else{
		    // Left wheel function (counter-clockwise)
		    if (leftWheels < rightWheels) {
			if (wheelEvents[leftWheels].left != NULL) {
			    free(wheelEvents[leftWheels].left);
			}
			wheelEvents[leftWheels].left = func_copy;
		    } else {
			wheel* temp = realloc(wheelEvents, (leftWheels+1)*sizeof(*wheelEvents));
			if (temp == NULL) {
			    printf("Memory allocation failed!\n");
			    free(data);
			    free(events);
			    free(wheelEvents);
			    free(func_copy);
			    fclose(f);
			    return;
			}
			wheelEvents = temp;
			wheelEvents[leftWheels].left = func_copy;
			wheelEvents[leftWheels].right = NULL;
		    }
		    leftWheels++;
		}
		continue;
	    }
	    
	    // Check for Wheel definition
	    if (strncasecmp(line, "wheel", 5) == 0) {
		wheelType++;
		continue;
	    }
	    
	    // If we get here, the line is unrecognized
	    if (debug > 1) {
		printf("Skipping unrecognized line: %s\n", line);
	    }
	}
	
	fclose(f);
	
	wheelFunction=0;
	if (rightWheels > leftWheels)
		totalWheels = rightWheels;
	else
		totalWheels = leftWheels;

	if (debug > 0){
		for (int i = 0; i < totalButtons; i++) {
			if (events[i].function != NULL) {
				printf("Button: %d | Type: %d | Function: %s\n", i, events[i].type, events[i].function);
			} else {
				printf("Button: %d | Type: %d | Function: (not set)\n", i, events[i].type);
			}
		}
		printf("\n");
		for (int i = 0; i < totalWheels; i++) {
			printf("Wheel %d: Right: %s | Left: %s\n", i, 
			       wheelEvents[i].right ? wheelEvents[i].right : "(null)",
			       wheelEvents[i].left ? wheelEvents[i].left : "(null)");
		}
		printf("\n");
	}
	
	// Check module state and print warnings
	int uclogic_loaded = is_module_loaded("hid_uclogic");
	int wacom_loaded = is_module_loaded("wacom");
	
	if (debug) {
		printf("Module status: hid_uclogic=%s, wacom=%s\n",
		       uclogic_loaded ? "loaded" : "not loaded",
		       wacom_loaded ? "loaded" : "not loaded");
		printf("Config: enable_uclogic=%s\n", enable_uclogic ? "true" : "false");
	}
	
	if (uclogic_loaded && !enable_uclogic) {
		print_compatibility_warning();
		printf("hid_uclogic is loaded but enable_uclogic is false.\n");
		printf("Attempting alternative access methods...\n");
	}
	
	free(data);
	int devI = 0;
	char indi[] = "|/-\\";
	int use_hidraw_fallback = 0;
	
	while (err == 0 || err == LIBUSB_ERROR_NO_DEVICE){
		libusb_device **devs; // List of USB devices
		libusb_device *dev; // Selected USB device
		struct libusb_config_descriptor *desc; // USB description (For claiming interfaces)
		libusb_device_handle *handle = NULL; // USB handle
		int hidraw_fd = -1;

		// Try hidraw fallback if enabled and modules are interfering
		if (!enable_uclogic && uclogic_loaded && use_hidraw_fallback == 0) {
			hidraw_fd = try_hidraw_access();
			if (hidraw_fd >= 0) {
				printf("Using hidraw interface (bypassing hid_uclogic)\n");
				use_hidraw_fallback = 1;
			} else {
				printf("hidraw access failed, trying libusb with workarounds...\n");
			}
		}

		err = libusb_get_device_list(ctx, &devs);
		if (err < 0){
			printf("Unable to retrieve USB devices. Exiting...\n");
			// Clean up allocated memory
			for (int i = 0; i < totalButtons; i++) {
				if (events[i].function != NULL) {
					free(events[i].function);
				}
			}
			free(events);
			
			for (int i = 0; i < totalWheels; i++) {
				if (wheelEvents[i].right != NULL) {
					free(wheelEvents[i].right);
				}
				if (wheelEvents[i].left != NULL) {
					free(wheelEvents[i].left);
				}
			}
			free(wheelEvents);
			return;
		}

		// Gets a list of devices and looks for ones that have the same vid and pid
		int d=0, found=0;
		devI=0;
		libusb_device *savedDevs[sizeof(devs)];
		while ((dev = devs[d++]) != NULL){
			struct libusb_device_descriptor devDesc;
			unsigned char info[200] = "";
			err = libusb_get_device_descriptor(dev, &devDesc);
			if (err < 0){
				if (debug > 0){
					printf("Unable to retrieve info from device #%d. Ignoring...\n", d);
				}
			}else if (devDesc.idVendor == vid && devDesc.idProduct == pid){
				if (accept == 1){
					if (uid != 0){
						err=libusb_open(dev, &handle);
						if (err < 0){
							if (err == LIBUSB_ERROR_ACCESS && !enable_uclogic){
								printf("\nPermission denied - hid_uclogic may be claiming the device.\n");
								printf("Try: sudo rmmod hid_uclogic\n");
								printf("Or set enable_uclogic: true in config\n");
							}
							handle=NULL;
							if (err == LIBUSB_ERROR_ACCESS){
								printf("Error: Permission denied\n");
								libusb_free_device_list(devs, 1);
								// Clean up allocated memory
								for (int i = 0; i < totalButtons; i++) {
									if (events[i].function != NULL) {
										free(events[i].function);
									}
								}
								free(events);
								
								for (int i = 0; i < totalWheels; i++) {
									if (wheelEvents[i].right != NULL) {
										free(wheelEvents[i].right);
									}
									if (wheelEvents[i].left != NULL) {
										free(wheelEvents[i].left);
									}
								}
								free(wheelEvents);
								return;
							}
						}
						if (debug > 0){
							printf("\nUsing: %04x:%04x (Bus: %03d Device: %03d)\n", vid, pid, libusb_get_bus_number(dev), libusb_get_device_address(dev));
						}
						break;
					}else{ // If the driver is ran as root, it can safely execute the following
						err = libusb_open(dev, &handle);
						if (err < 0){
							printf("\nUnable to open device. Error: %d\n", err);
							handle=NULL;
						}
						err = libusb_get_string_descriptor_ascii(handle, devDesc.iProduct, info, 200);
						if (debug > 0){
							printf("\n#%d | %04x:%04x : %s\n", d, vid, pid, info);
						}
						if (strlen(info) == 0 || strcmp("Huion Tablet_KD100", info) == 0){
							break;
						}else{
							libusb_close(handle);
							handle = NULL;
							found++;
						}
					}
				}else{
					savedDevs[devI] = dev;
					devI++;
				}
			}
		}
		if (accept == 0){
			int in=-1;
			while(in == -1){
				char buf[64];
				printf("\n");
				system("lsusb");
				printf("\n");
				for(d=0; d < devI; d++){
					printf("%d) %04x:%04x (Bus: %03d Device: %03d)\n", d, vid, pid, libusb_get_bus_number(savedDevs[d]), libusb_get_device_address(savedDevs[d]));
				}
				printf("Select a device to use: ");
				fflush(stdout);
				fgets(buf, 10, stdin);
				in = atoi(buf);
				if (in >= devI || in < 0){
					in=-1;
				}
				system("clear");
			}
			err=libusb_open(savedDevs[in], &handle);
			if (err < 0){
				printf("Unable to open device. Error: %d\n", err);
				handle=NULL;
				if (err == LIBUSB_ERROR_ACCESS){
					printf("Error: Permission denied\n");
					if (!enable_uclogic){
						printf("hid_uclogic may be claiming the device.\n");
						printf("Solutions:\n");
						printf("  1. Unload: sudo rmmod hid_uclogic\n");
						printf("  2. Set enable_uclogic: true in config\n");
						printf("  3. Run driver as root (not recommended)\n");
					}
					libusb_free_device_list(devs, 1);
					// Clean up allocated memory
					for (int i = 0; i < totalButtons; i++) {
						if (events[i].function != NULL) {
							free(events[i].function);
						}
					}
					free(events);
					
					for (int i = 0; i < totalWheels; i++) {
						if (wheelEvents[i].right != NULL) {
							free(wheelEvents[i].right);
						}
						if (wheelEvents[i].left != NULL) {
							free(wheelEvents[i].left);
						}
					}
					free(wheelEvents);
					return;
				}
			}
		}else if (found > 0){
			printf("Error: Found device does not appear to be the keydial\n");
			printf("Try running without the -a flag\n");
			libusb_free_device_list(devs, 1);
			// Clean up allocated memory
			for (int i = 0; i < totalButtons; i++) {
				if (events[i].function != NULL) {
					free(events[i].function);
				}
			}
			free(events);
			
			for (int i = 0; i < totalWheels; i++) {
				if (wheelEvents[i].right != NULL) {
					free(wheelEvents[i].right);
				}
				if (wheelEvents[i].left != NULL) {
					free(wheelEvents[i].left);
				}
			}
			free(wheelEvents);
			return;
		}

		int interfaces=0;
		if (handle == NULL && hidraw_fd < 0){
			printf("\rWaiting for a device %c", indi[c]);
			fflush(stdout);
			usleep(250000); // Buffer
			c++;
			if (c == 4){
				c=0;
			}
			err = LIBUSB_ERROR_NO_DEVICE;
		}else{ // Claims the device and starts the driver
			if (debug == 0){
				system("clear");
			}

			if (hidraw_fd >= 0) {
				// Using hidraw interface
				printf("Starting driver via hidraw...\n");
				printf("Driver is running!\n");
				
				// Main loop for hidraw
				while (1) {
					unsigned char data[64];
					ssize_t bytes_read = read(hidraw_fd, data, sizeof(data));
					
					if (bytes_read < 0) {
						printf("Error reading from hidraw\n");
						break;
					}
					
					if (bytes_read > 0) {
						// Process hidraw data (you'll need to adapt this based on actual hidraw data format)
						if (debug == 2 || dry) {
							printf("HIDRAW DATA: [%d", data[0]);
							for (int i = 1; i < bytes_read; i++) {
								printf(", %d", data[i]);
							}
							printf("]\n");
						}
						
						// TODO: Map hidraw data to keycodes and trigger events
						// This will depend on the actual hidraw data format
					}
					
					usleep(10000); // Small delay to prevent CPU hogging
				}
				
				close(hidraw_fd);
				// Clean up allocated memory before returning
				for (int i = 0; i < totalButtons; i++) {
					if (events[i].function != NULL) {
						free(events[i].function);
					}
				}
				free(events);
				
				for (int i = 0; i < totalWheels; i++) {
					if (wheelEvents[i].right != NULL) {
						free(wheelEvents[i].right);
					}
					if (wheelEvents[i].left != NULL) {
						free(wheelEvents[i].left);
					}
				}
				free(wheelEvents);
				return;
			} else {
				// Using libusb interface
				interfaces = 0;
				printf("Starting driver via libusb...\n");

				// Read device and claim interfaces
				dev = libusb_get_device(handle);
				libusb_get_config_descriptor(dev, 0, &desc);
				interfaces = desc->bNumInterfaces;
				libusb_free_config_descriptor(desc);
				
				// Set auto-detach based on enable_uclogic setting
				if (enable_uclogic) {
					libusb_set_auto_detach_kernel_driver(handle, 1);
					if (debug == 1)
						printf("Using auto-detach (hid_uclogic compatible mode)\n");
				} else {
					// Try to detach kernel driver if it's active
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

				for (int x = 0; x < interfaces; x++){
					int err = libusb_claim_interface(handle, x);
					if (err != LIBUSB_SUCCESS && debug == 1)
						printf("Failed to claim interface %d: %s\n", x, libusb_error_name(err));
				}

				printf("Driver is running!\n");

				err = 0;
				prevEvent.function = "";
				prevEvent.type = 0;
				while (err >=0){ // Listen for events
					unsigned char data[40]; // Stores device input
					int keycode = 0; // Keycode read from the device
					err = libusb_interrupt_transfer(handle, 0x81, data, sizeof(data), NULL, 0); // Get data

					// Potential errors
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
					if (err < 0){
						if (debug == 1){
							printf("Unable to retrieve data: %d\n", err);
						}
						break;
					}

					// Convert data to keycodes
					if (data[4] != 0)
						keycode = data[4];
					else if (data[5] != 0)
						keycode = data[5] + 128;
					else if (data[6] != 0)
						keycode = data[6] + 256;
					if (data[1] == 241)
						keycode+=512;
					if (dry)
						keycode = 0;

					// Compare keycodes to data and trigger events
					if (debug == 1 && keycode != 0){
						printf("Keycode: %d\n", keycode);
					}
					if (keycode == 0 && prevEvent.type != 0){ // Reset key held
						Handler(prevEvent.function, prevEvent.type, debug);
						prevEvent.function = "";
						prevEvent.type = 0;
					}
					if (keycode == 641){ // Wheel clockwise
						if (wheelFunction >= 0 && wheelFunction < totalWheels && 
							wheelEvents[wheelFunction].right != NULL) {
							Handler(wheelEvents[wheelFunction].right, -1, debug);
						} else {
							if (debug == 1) {
								printf("Wheel function %d.right is NULL or out of bounds\n", wheelFunction);
							}
						}
					}else if (keycode == 642){ // Counter clockwise
						if (wheelFunction >= 0 && wheelFunction < totalWheels && 
							wheelEvents[wheelFunction].left != NULL) {
							Handler(wheelEvents[wheelFunction].left, -1, debug);
						} else {
							if (debug == 1) {
								printf("Wheel function %d.left is NULL or out of bounds\n", wheelFunction);
							}
						}
					}else{
						for (int k = 0; k < 19; k++){
							if (keycodes[k] == keycode){
								if (events[k].function != NULL){
									if (strcmp(events[k].function, "NULL") == 0){
										if (prevEvent.type != 0){
											Handler(prevEvent.function, prevEvent.type, debug);
											prevEvent.type = 0;
											prevEvent.function = "";
										}
										break;
									}
									if (events[k].type == 0){
										if (strcmp(events[k].function, prevEvent.function)){
											if (prevEvent.type != 0){
												Handler(prevEvent.function, prevEvent.type, debug);
											}
											prevEvent.function = events[k].function;
											prevEvent.type=1;
										}
										Handler(events[k].function, 0, debug);
									}else if (strcmp(events[k].function, "swap") == 0){
										if (wheelFunction != totalWheels-1){
											wheelFunction++;
										}else
											wheelFunction=0;
										if (debug == 1){
											printf("Function: %s | %s\n", 
											       wheelEvents[wheelFunction].left ? wheelEvents[wheelFunction].left : "(null)",
											       wheelEvents[wheelFunction].right ? wheelEvents[wheelFunction].right : "(null)");
										}
									}else if (strcmp(events[k].function, "mouse1") == 0 || strcmp(events[k].function, "mouse2") == 0 || strcmp(events[k].function, "mouse3") == 0 || strcmp(events[k].function, "mouse4") == 0 || strcmp(events[k].function, "mouse5") == 0){
										if (strcmp(events[k].function, prevEvent.function)){
											if (prevEvent.type != 0){
												Handler(prevEvent.function, prevEvent.type, debug);
											}
											prevEvent.function = events[k].function;
											prevEvent.type=3;
										}
										Handler(events[k].function, 2, debug);
									}else{
										system(events[k].function);
									}
									break;
								}
							}
						}
					}

					if(debug == 2 || dry){
						printf("DATA: [%d", data[0]);
						for (int i = 1; i < sizeof(data); i++){
							printf(", %d", data[i]);
						}
						printf("]\n");
					}
				}

				// Cleanup
				for (int x = 0; x<interfaces; x++) {
					if (debug == 1){
						printf("Releasing interface %d...\n", x);
					}
					libusb_release_interface(handle, x);
				}
				printf("Closing device...\n");
				libusb_close(handle);
				interfaces=0;
				sleep(1); // Buffer to wait in case the device was disconnected
			}
		}
		libusb_free_device_list(devs, 1);
	}
	
	// Clean up allocated memory
	for (int i = 0; i < totalButtons; i++) {
		if (events[i].function != NULL) {
			free(events[i].function);
		}
	}
	free(events);
	
	for (int i = 0; i < totalWheels; i++) {
		if (wheelEvents[i].right != NULL) {
			free(wheelEvents[i].right);
		}
		if (wheelEvents[i].left != NULL) {
			free(wheelEvents[i].left);
		}
	}
	free(wheelEvents);
}

void Handler(char* key, int type, int debug){
	// Safety checks
	if (key == NULL) {
		if (debug == 1) printf("Handler called with NULL key\n");
		return;
	}
	
	// Check for "NULL" string
	if (strcmp(key, "NULL") == 0) {
		return;
	}
	
	char* cmd = NULL;
	int is_mouse = 0;
	char mouse_char = '1';  // Default to left mouse button
	
	// Determine the command based on type
	switch(type) {
		case -1:  // Wheel function
			cmd = "xdotool key ";
			break;
		case 0:   // Key down
			cmd = "xdotool keydown ";
			break;
		case 1:   // Key up
			cmd = "xdotool keyup ";
			break;
		case 2:   // Mouse down
			is_mouse = 1;
			cmd = "xdotool mousedown ";
			break;
		case 3:   // Mouse up
			is_mouse = 1;
			cmd = "xdotool mouseup ";
			break;
		default:
			if (debug == 1) printf("Handler called with unknown type: %d\n", type);
			return;
	}
	
	if (cmd == NULL) {
		return;
	}
	
	// For mouse functions, extract the button number
	if (is_mouse) {
		// Check if key is long enough and starts with "mouse"
		if (strlen(key) >= 6 && strncmp(key, "mouse", 5) == 0) {
			mouse_char = key[5];  // "mouse1" -> '1'
			// Validate it's a digit 1-5
			if (mouse_char < '1' || mouse_char > '5') {
				mouse_char = '1';  // Default to left button
			}
		}
		
		char temp[strlen(cmd) + 3];  // cmd + space + digit + null
		snprintf(temp, sizeof(temp), "%s %c", cmd, mouse_char);
		if (debug == 1) printf("Executing: %s\n", temp);
		system(temp);
	} else {
		// For key functions
		char temp[strlen(cmd) + strlen(key) + 1];
		snprintf(temp, sizeof(temp), "%s%s", cmd, key);
		if (debug == 1) printf("Executing: %s\n", temp);
		system(temp);
	}
}

char* Substring(const char* in, int start, int end) {
    // Input validation
    if (in == NULL) {
        char* out = malloc(1);
        if (out) out[0] = '\0';
        return out;
    }
    
    int len = strlen(in);
    
    // Validate bounds
    if (start < 0) start = 0;
    if (start >= len) {
        // Return empty string for invalid start
        char* out = malloc(1);
        if (out) out[0] = '\0';
        return out;
    }
    
    if (end <= 0) {
        // Return empty string for invalid length
        char* out = malloc(1);
        if (out) out[0] = '\0';
        return out;
    }
    
    // Adjust end if it goes beyond string length
    if (start + end > len) {
        end = len - start;
    }
    
    // Allocate memory (end + 1 for null terminator)
    char* out = malloc(end + 1);
    if (out == NULL) {
        // Memory allocation failed
        return NULL;
    }
    
    // Copy substring
    for (int i = 0; i < end; i++) {
        out[i] = in[start + i];
    }
    out[end] = '\0';
    
    return out;
}

int main(int args, char *in[]){
#ifdef DEBUG
    setup_crash_handler();
    printf("Debug mode enabled. Crash handler active.\n");
#endif
	int debug=0, accept=0, dry=0, err;

	err = system("xdotool sleep 0.01");
	if (err != 0){
		printf("xdotool not found. Please install xdotool for key simulation.\n");
		printf("Exiting...\n");
		return -9;
	}

	for (int arg = 1; arg < args; arg++){
		if (strcmp(in[arg],"-h") == 0 || strcmp(in[arg],"--help") == 0){
			printf("Usage: KD100 [option]...\n");
			printf("\t-a\t\tAssume the first device that matches %04x:%04x is the Keydial\n", vid, pid);
			printf("\t-c [path]\tSpecifies a config file to use\n");
			printf("\t-d [-d]\t\tEnable debug outputs (use twice to view data sent by the device)\n");
			printf("\t-dry \t\tDisplay data sent by the device without sending events\n");
			printf("\t-h\t\tDisplays this message\n");
			printf("\nConfiguration:\n");
			printf("\tAdd 'enable_uclogic: true' to config to work with hid_uclogic loaded\n");
			printf("\tDefault: enable_uclogic: false (compatible with OpenTabletDriver)\n\n");
			return 0;
		}
		if (strcmp(in[arg],"-d") == 0){
			debug++;
		}
		if (strcmp(in[arg],"-dry") == 0){
			dry=1;
		}
		if (strcmp(in[arg],"-a") == 0){
			accept=1;
		}
		if (strcmp(in[arg], "-c") == 0){
			if (in[arg+1]){
				file = in[arg+1];
				arg++;
			}else{
				printf("No config file specified. Exiting...\n");
				return -8;
			}
		}
		if (strcmp(in[arg], "--uclogic") == 0){
			enable_uclogic = 1;
			printf("Forcing hid_uclogic compatibility mode\n");
		}
		if (strcmp(in[arg], "--no-uclogic") == 0){
			enable_uclogic = 0;
			printf("Disabling hid_uclogic compatibility (OpenTabletDriver mode)\n");
		}
	}

	libusb_context *ctx = NULL;
	err = libusb_init(&ctx);
	if (err < 0){
		printf("Error initializing libusb: %d\n", err);
		return err;
	}
	
	// Print mode info
	if (enable_uclogic) {
		printf("Mode: hid_uclogic compatibility enabled\n");
	} else {
		printf("Mode: OpenTabletDriver compatible (hid_uclogic disabled)\n");
		if (is_module_loaded("hid_uclogic")) {
			print_compatibility_warning();
		}
	}
	
	GetDevice(ctx, debug, accept, dry);
	libusb_exit(ctx);
	return 0;
}

