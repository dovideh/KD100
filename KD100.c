/*
	V1.4.9 - Enhanced Leader Key System
	https://github.com/mckset/KD100.git
	KD100 Linux driver for X11 desktops
	Features:
	- Configurable leader modes (one_shot, sticky, toggle)
	- Per-button leader eligibility
	- Fixed timing system
*/

#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <dirent.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/time.h>

/* ===== CRASH HANDLER ===== */
#ifdef DEBUG
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
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
                snprintf(cmd, sizeof(cmd), "addr2line -e '%s' -f -C -p %p 2>&1", 
                        exe_path, array[i]);
                system(cmd);
            }
        }
    }
    
    fprintf(stderr, "\n=== Using gdb to get line info ===\n");
    char gdb_cmd[512];
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

int keycodes[] = {1, 2, 4, 8, 16, 32, 64, 128, 129, 130, 132, 136, 144, 160, 192, 256, 257, 258, 260, 641, 642};
char* file = "default.cfg";
int enable_uclogic = 0;

typedef struct event event;
typedef struct wheel wheel;
typedef struct leader_state leader_state;

// Leader mode enumeration
typedef enum {
    LEADER_MODE_ONE_SHOT,    // Leader + 1 key = combination, then reset (default)
    LEADER_MODE_STICKY,      // Leader stays active for multiple keys until timeout
    LEADER_MODE_TOGGLE       // Leader toggles on/off (press to enable, press again to disable)
} leader_mode_t;

struct event{
	int type;
	char* function;
	int leader_eligible;  // 0 = not eligible, 1 = eligible, -1 = not set (default eligible)
};

struct wheel {
	char* right;
	char* left;
};

struct leader_state {
    int leader_button;        // Which button is the leader (e.g., Button 16 for shift)
    int leader_active;        // Is leader mode active?
    int last_button;          // Last button pressed (for timing)
    struct timeval leader_press_time; // When was leader pressed?
    char* leader_function;    // What function does the leader have?
    int timeout_ms;           // Leader timeout in milliseconds
    leader_mode_t mode;       // Leader mode (one_shot, sticky, toggle)
    int toggle_state;         // For toggle mode: 0 = off, 1 = on
};

void GetDevice(libusb_context*, int, int, int);
void Handler(char*, int, int);
void process_leader_combination(leader_state*, event*, int, int);
void reset_leader_state(leader_state*);
void send_leader_combination(leader_state*, char*, int);
int is_modifier_key(const char*);
int find_button_index(int keycode);
char* Substring(const char*, int, int);
int is_module_loaded(const char* module_name);
int try_hidraw_access();
void print_compatibility_warning();
long get_time_ms();
long time_diff_ms(struct timeval start, struct timeval end);
void trim_trailing_spaces(char* str);
leader_mode_t parse_leader_mode(const char* mode_str);
const char* leader_mode_to_string(leader_mode_t mode);

// Get current time in milliseconds
long get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

// Calculate time difference in milliseconds
long time_diff_ms(struct timeval start, struct timeval end) {
    long sec_diff = end.tv_sec - start.tv_sec;
    long usec_diff = end.tv_usec - start.tv_usec;
    return (sec_diff * 1000) + (usec_diff / 1000);
}

// Trim trailing spaces from a string
void trim_trailing_spaces(char* str) {
    if (str == NULL) return;
    
    int len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[len - 1] = '\0';
        len--;
    }
}

// Parse leader mode string
leader_mode_t parse_leader_mode(const char* mode_str) {
    if (mode_str == NULL) return LEADER_MODE_ONE_SHOT;
    
    if (strcasecmp(mode_str, "sticky") == 0) {
        return LEADER_MODE_STICKY;
    } else if (strcasecmp(mode_str, "toggle") == 0) {
        return LEADER_MODE_TOGGLE;
    } else if (strcasecmp(mode_str, "one_shot") == 0 || strcasecmp(mode_str, "oneshot") == 0) {
        return LEADER_MODE_ONE_SHOT;
    }
    
    return LEADER_MODE_ONE_SHOT; // Default
}

// Convert leader mode to string
const char* leader_mode_to_string(leader_mode_t mode) {
    switch (mode) {
        case LEADER_MODE_ONE_SHOT: return "one_shot";
        case LEADER_MODE_STICKY: return "sticky";
        case LEADER_MODE_TOGGLE: return "toggle";
        default: return "unknown";
    }
}

// Find button index from keycode
int find_button_index(int keycode) {
    for (int k = 0; k < 19; k++) {  // Only 0-17 for buttons, 18 is wheel button
        if (keycodes[k] == keycode) {
            return k;
        }
    }
    return -1;
}

// Check if a key is a modifier
int is_modifier_key(const char* key) {
    if (key == NULL) return 0;
    
    if (strcmp(key, "ctrl") == 0 || strcmp(key, "control") == 0 ||
        strcmp(key, "shift") == 0 ||
        strcmp(key, "alt") == 0 ||
        strcmp(key, "super") == 0 || strcmp(key, "meta") == 0) {
        return 1;
    }
    return 0;
}

// Reset leader state
void reset_leader_state(leader_state* state) {
    state->leader_active = 0;
    state->last_button = -1;
    state->leader_press_time.tv_sec = 0;
    state->leader_press_time.tv_usec = 0;
    state->toggle_state = 0; // Also reset toggle state
}

// Send leader combination
void send_leader_combination(leader_state* state, char* combination, int debug) {
    if (combination == NULL || strlen(combination) == 0) {
        return;
    }
    
    if (debug == 1) {
        printf("Sending leader combination: %s\n", combination);
    }
    
    // Send the combination
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "xdotool key %s", combination);
    system(cmd);
    
    // Reset leader state after sending (except for sticky mode)
    if (state->mode != LEADER_MODE_STICKY) {
        reset_leader_state(state);
    }
}

// Process leader key combinations
void process_leader_combination(leader_state* state, event* events, int button_index, int debug) {
    if (button_index < 0 || button_index >= 19) {
        return;
    }
    
    // Check if this is the wheel button (button 18) - should not be modified
    if (button_index == 18) {
        // Wheel button - handle normally without leader
        if (events[button_index].function != NULL && 
            strcmp(events[button_index].function, "swap") == 0) {
            // Handle swap function
            // (This would need wheel state tracking - implemented elsewhere)
        }
        reset_leader_state(state);
        return;
    }
    
    if (events[button_index].function == NULL) {
        return;
    }
    
    char* button_func = events[button_index].function;
    
    // Check if this button is configured as a leader
    if (strcmp(button_func, "leader") == 0) {
        // This button is the leader itself
        if (state->mode == LEADER_MODE_TOGGLE) {
            // Toggle mode: press to enable, press again to disable
            if (!state->toggle_state) {
                // Enable toggle mode
                state->toggle_state = 1;
                state->leader_active = 1;
                gettimeofday(&state->leader_press_time, NULL);
                state->last_button = button_index;
                
                if (debug == 1) {
                    printf("Leader toggle mode ENABLED by button %d\n", button_index);
                }
            } else {
                // Disable toggle mode
                state->toggle_state = 0;
                state->leader_active = 0;
                
                if (debug == 1) {
                    printf("Leader toggle mode DISABLED by button %d\n", button_index);
                }
            }
        } else {
            // One-shot or sticky mode
            if (!state->leader_active) {
                // Activate leader mode
                state->leader_active = 1;
                gettimeofday(&state->leader_press_time, NULL);
                state->last_button = button_index;
                
                if (debug == 1) {
                    printf("Leader mode activated by button %d\n", button_index);
                }
            } else {
                // Leader pressed again - cancel leader mode
                reset_leader_state(state);
                if (debug == 1) {
                    printf("Leader mode cancelled\n");
                }
            }
        }
        return;
    }
    
    // Check if we're in leader mode
    if (state->leader_active) {
        // Check eligibility first
        if (events[button_index].leader_eligible == 0) {
            // Button is not eligible for leader modifications
            if (debug == 1) {
                printf("Button %d not eligible for leader - handling normally\n", button_index);
            }
            
            // For sticky mode, we might want to keep leader active
            if (state->mode != LEADER_MODE_STICKY) {
                reset_leader_state(state);
            }
            
            // Fall through to normal button handling
        } else {
            // Button is eligible for leader modifications
            // Calculate time since leader was pressed (for timeout)
            struct timeval now;
            gettimeofday(&now, NULL);
            long elapsed = time_diff_ms(state->leader_press_time, now);
            
            if (elapsed > state->timeout_ms && state->mode != LEADER_MODE_TOGGLE) {
                // Leader timeout - reset (except for toggle mode)
                if (debug == 1) {
                    printf("Leader timeout (%ld ms > %d ms)\n", elapsed, state->timeout_ms);
                }
                reset_leader_state(state);
                // Fall through to normal button handling
            } else {
                // We have a leader combination!
                // Format: leader_function + button_function
                char combination[256] = "";
                
                if (state->leader_function != NULL && strlen(state->leader_function) > 0) {
                    strcpy(combination, state->leader_function);
                    strcat(combination, "+");
                }
                
                strcat(combination, button_func);
                
                // Send the combination
                send_leader_combination(state, combination, debug);
                
                // For sticky mode, update the timer to extend timeout
                if (state->mode == LEADER_MODE_STICKY) {
                    gettimeofday(&state->leader_press_time, NULL);
                }
                
                // For one-shot mode, reset after sending
                if (state->mode == LEADER_MODE_ONE_SHOT) {
                    reset_leader_state(state);
                }
                
                return; // Don't also handle as normal button
            }
        }
    }
    
    // Not in leader mode, leader timed out, or button not eligible - handle normal button press
    if (strcmp(button_func, "NULL") != 0 && strcmp(button_func, "swap") != 0 &&
        strcmp(button_func, "mouse1") != 0 && strcmp(button_func, "mouse2") != 0 &&
        strcmp(button_func, "mouse3") != 0 && strcmp(button_func, "mouse4") != 0 &&
        strcmp(button_func, "mouse5") != 0) {
        
        if (events[button_index].type == 0) {
            // Type 0: Key press
            Handler(button_func, 0, debug);
            usleep(10000); // Small delay
            Handler(button_func, 1, debug);
        } else if (events[button_index].type == 1) {
            // Type 1: Run program/script
            system(button_func);
        }
    }
}

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
	char* data = malloc(512*sizeof(char));
	event* events = malloc(1*sizeof(event));
	wheel* wheelEvents = malloc(1*sizeof(wheel));
	event prevEvent;
	uid_t uid=getuid();
	
	// Leader state tracking
	leader_state leader = {0};
	leader.leader_button = -1;  // Will be set from config
	leader.leader_active = 0;
	leader.last_button = -1;
	leader.leader_press_time.tv_sec = 0;
	leader.leader_press_time.tv_usec = 0;
	leader.leader_function = NULL;
	leader.timeout_ms = 1000;  // 1 second timeout for combinations
	leader.mode = LEADER_MODE_ONE_SHOT;  // Default mode
	leader.toggle_state = 0;

	int c=0;
	char indi[] = "|/-\\";

	system("clear");

	if (debug > 0){
		if (debug > 2)
			debug=2;
		printf("Version 1.4.9 - Enhanced Leader Key System\n");
		printf("Debug level: %d\n", debug);
	}		

	// Load config file
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
		events[i].leader_eligible = -1;  // -1 = not set (default to eligible)
	}
	
	// Initialize wheel events
	wheelEvents[0].right = NULL;
	wheelEvents[0].left = NULL;
	
	// Parse config file
	while (fgets(data, 512, f) != NULL) {
	    data[strcspn(data, "\n")] = 0;
	    
	    if (strstr(data, "//") == data) {
		continue;
	    }
	    
	    if (strlen(data) == 0) {
		continue;
	    }
	    
	    char *line = data;
	    while (*line == ' ' || *line == '\t') line++;
	    
	    if (strncasecmp(line, "enable_uclogic:", 15) == 0) {
		char* value = line + 15;
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
	    
	    if (strncasecmp(line, "leader_button:", 14) == 0) {
		char* value = line + 14;
		while (*value == ' ') value++;
		leader.leader_button = atoi(value);
		if (debug) printf("Config: leader_button = %d\n", leader.leader_button);
		continue;
	    }
	    
	    if (strncasecmp(line, "leader_function:", 16) == 0) {
		char* value = line + 16;
		while (*value == ' ') value++;
		leader.leader_function = strdup(value);
		if (leader.leader_function != NULL) {
		    trim_trailing_spaces(leader.leader_function);
		}
		if (debug) printf("Config: leader_function = '%s'\n", leader.leader_function);
		continue;
	    }
	    
	    if (strncasecmp(line, "leader_timeout:", 15) == 0) {
		char* value = line + 15;
		while (*value == ' ') value++;
		leader.timeout_ms = atoi(value);
		if (debug) printf("Config: leader_timeout = %d ms\n", leader.timeout_ms);
		continue;
	    }
	    
	    if (strncasecmp(line, "leader_mode:", 12) == 0) {
		char* value = line + 12;
		while (*value == ' ') value++;
		leader.mode = parse_leader_mode(value);
		if (debug) printf("Config: leader_mode = %s\n", leader_mode_to_string(leader.mode));
		continue;
	    }
	    
	    if (strncasecmp(line, "button ", 7) == 0) {
		char* num_str = line + 7;
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
		    for (int j = totalButtons; j <= button; j++) {
			events[j].function = NULL;
			events[j].type = 0;
			events[j].leader_eligible = -1;  // Default: not set
		    }
		    totalButtons = button+1;
		}
		continue;
	    }
	    
	    if (strncasecmp(line, "type:", 5) == 0 && button != -1) {
		char* type_str = line + 5;
		while (*type_str == ' ') type_str++;
		events[button].type = atoi(type_str);
		continue;
	    }
	    
	    if (strncasecmp(line, "leader_eligible:", 16) == 0 && button != -1) {
		char* value = line + 16;
		while (*value == ' ') value++;
		if (strcasecmp(value, "false") == 0) {
		    events[button].leader_eligible = 0;
		} else if (strcasecmp(value, "true") == 0) {
		    events[button].leader_eligible = 1;
		} else {
		    events[button].leader_eligible = atoi(value);
		}
		if (debug) printf("Config: button %d leader_eligible = %d\n", button, events[button].leader_eligible);
		continue;
	    }
	    
	    if (strncasecmp(line, "function:", 9) == 0) {
		char* func_str = line + 9;
		while (*func_str == ' ') func_str++;
		
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
		    if (button >= 0 && button < totalButtons) {
			if (events[button].function != NULL) {
			    free(events[button].function);
			}
			events[button].function = func_copy;
		    } else {
			free(func_copy);
			if (debug) printf("Warning: function without valid button definition\n");
		    }
		} else if (wheelType == 1){
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
			wheelEvents[rightWheels].right = func_copy;
			wheelEvents[rightWheels].left = NULL;
		    }else{
			wheelEvents[0].right = func_copy;
			wheelEvents[0].left = NULL;
		    }
		    rightWheels++;
		}else{
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
	    
	    if (strncasecmp(line, "wheel", 5) == 0) {
		wheelType++;
		continue;
	    }
	    
	    if (debug > 1) {
		printf("Skipping unrecognized line: %s\n", line);
	    }
	}
	
	fclose(f);
	
	// Set default eligibility for buttons that weren't explicitly configured
	for (int i = 0; i < totalButtons; i++) {
		if (events[i].leader_eligible == -1) {
			// Default: eligible for all buttons except wheel button (18)
			events[i].leader_eligible = (i == 18) ? 0 : 1;
		}
	}
	
	wheelFunction=0;
	if (rightWheels > leftWheels)
		totalWheels = rightWheels;
	else
		totalWheels = leftWheels;

	if (debug > 0){
		printf("\n=== Button Configuration ===\n");
		for (int i = 0; i < totalButtons; i++) {
			if (events[i].function != NULL) {
				printf("Button %2d: Type: %d | Function: '%s' | Leader Eligible: %s\n", 
				       i, events[i].type, events[i].function,
				       events[i].leader_eligible == 1 ? "YES" : 
				       events[i].leader_eligible == 0 ? "NO" : "DEFAULT");
			} else {
				printf("Button %2d: Type: %d | Function: (not set) | Leader Eligible: %s\n", 
				       i, events[i].type,
				       events[i].leader_eligible == 1 ? "YES" : 
				       events[i].leader_eligible == 0 ? "NO" : "DEFAULT");
			}
		}
		printf("\n=== Wheel Configuration ===\n");
		for (int i = 0; i < totalWheels; i++) {
			printf("Wheel %d: Right: %s | Left: %s\n", i, 
			       wheelEvents[i].right ? wheelEvents[i].right : "(null)",
			       wheelEvents[i].left ? wheelEvents[i].left : "(null)");
		}
		printf("\n=== Leader Configuration ===\n");
		printf("Leader button: %d\n", leader.leader_button);
		printf("Leader function: '%s'\n", leader.leader_function ? leader.leader_function : "(null)");
		printf("Leader timeout: %d ms\n", leader.timeout_ms);
		printf("Leader mode: %s\n", leader_mode_to_string(leader.mode));
		printf("\n");
	}
	
	// Check module state
	int uclogic_loaded = is_module_loaded("hid_uclogic");
	
	if (debug) {
		printf("Module status: hid_uclogic=%s\n",
		       uclogic_loaded ? "loaded" : "not loaded");
		printf("Config: enable_uclogic=%s\n", enable_uclogic ? "true" : "false");
	}
	
	if (uclogic_loaded && !enable_uclogic) {
		print_compatibility_warning();
		printf("hid_uclogic is loaded but enable_uclogic is false.\n");
		printf("Attempting alternative access methods...\n");
	}
	
	free(data);
	int devI = 0;
	int use_hidraw_fallback = 0;
	
	while (err == 0 || err == LIBUSB_ERROR_NO_DEVICE){
		libusb_device **devs;
		libusb_device *dev;
		struct libusb_config_descriptor *desc;
		libusb_device_handle *handle = NULL;
		int hidraw_fd = -1;

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
			if (leader.leader_function != NULL) {
				free(leader.leader_function);
			}
			return;
		}

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
								if (leader.leader_function != NULL) {
									free(leader.leader_function);
								}
								return;
							}
						}
						if (debug > 0){
							printf("\nUsing: %04x:%04x (Bus: %03d Device: %03d)\n", vid, pid, libusb_get_bus_number(dev), libusb_get_device_address(dev));
						}
						break;
					}else{
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
					if (leader.leader_function != NULL) {
						free(leader.leader_function);
					}
					return;
				}
			}
		}else if (found > 0){
			printf("Error: Found device does not appear to be the keydial\n");
			printf("Try running without the -a flag\n");
			libusb_free_device_list(devs, 1);
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
	if (leader.leader_function != NULL) {
		free(leader.leader_function);
	}
	return;
		}

		int interfaces=0;
		if (handle == NULL && hidraw_fd < 0){
			printf("\rWaiting for a device %c", indi[c]);
			fflush(stdout);
			usleep(250000);
			c++;
			if (c == 4){
				c=0;
			}
			err = LIBUSB_ERROR_NO_DEVICE;
		}else{
			if (debug == 0){
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
				if (leader.leader_function != NULL) {
					free(leader.leader_function);
				}
				return;
			} else {
				interfaces = 0;
				printf("Starting driver via libusb...\n");

				dev = libusb_get_device(handle);
				libusb_get_config_descriptor(dev, 0, &desc);
				interfaces = desc->bNumInterfaces;
				libusb_free_config_descriptor(desc);
				
				if (enable_uclogic) {
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

				for (int x = 0; x < interfaces; x++){
					int err = libusb_claim_interface(handle, x);
					if (err != LIBUSB_SUCCESS && debug == 1)
						printf("Failed to claim interface %d: %s\n", x, libusb_error_name(err));
				}

				printf("Driver is running!\n");
				printf("Enhanced Leader Key System v1.4.9\n");
				printf("Mode: %s | Timeout: %d ms\n", 
				       leader_mode_to_string(leader.mode), leader.timeout_ms);
				printf("Press leader button first, then eligible buttons for combinations.\n");

				err = 0;
				prevEvent.function = "";
				prevEvent.type = 0;
				
				while (err >=0){
					unsigned char data[40];
					int keycode = 0;
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

					if (debug == 1 && keycode != 0){
						printf("Keycode: %d\n", keycode);
					}
					
					// Handle wheel events
					if (keycode == 641){
						if (wheelFunction >= 0 && wheelFunction < totalWheels && 
							wheelEvents[wheelFunction].right != NULL) {
							Handler(wheelEvents[wheelFunction].right, -1, debug);
						}
					}else if (keycode == 642){
						if (wheelFunction >= 0 && wheelFunction < totalWheels && 
							wheelEvents[wheelFunction].left != NULL) {
							Handler(wheelEvents[wheelFunction].left, -1, debug);
						}
					}else{
						int button_index = find_button_index(keycode);
						
						if (button_index != -1){
							// Process button press with leader system
							process_leader_combination(&leader, events, button_index, debug);
							
							// Also handle legacy single-button events for compatibility
							if (events[button_index].function != NULL) {
								if (strcmp(events[button_index].function, "NULL") == 0){
									if (prevEvent.type != 0){
										Handler(prevEvent.function, prevEvent.type, debug);
										prevEvent.type = 0;
										prevEvent.function = "";
									}
								}
								else if (strcmp(events[button_index].function, "swap") == 0){
									if (wheelFunction != totalWheels-1){
										wheelFunction++;
									}else
										wheelFunction=0;
									if (debug == 1){
										printf("Function: %s | %s\n", 
											   wheelEvents[wheelFunction].left ? wheelEvents[wheelFunction].left : "(null)",
											   wheelEvents[wheelFunction].right ? wheelEvents[wheelFunction].right : "(null)");
									}
								}else if (strcmp(events[button_index].function, "mouse1") == 0 || strcmp(events[button_index].function, "mouse2") == 0 || strcmp(events[button_index].function, "mouse3") == 0 || strcmp(events[button_index].function, "mouse4") == 0 || strcmp(events[button_index].function, "mouse5") == 0){
									if (strcmp(events[button_index].function, prevEvent.function)){
										if (prevEvent.type != 0){
											Handler(prevEvent.function, prevEvent.type, debug);
										}
										prevEvent.function = events[button_index].function;
										prevEvent.type=3;
									}
									Handler(events[button_index].function, 2, debug);
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
						
						if (leader.leader_active) {
							struct timeval now;
							gettimeofday(&now, NULL);
							long elapsed = time_diff_ms(leader.leader_press_time, now);
							printf("Leader active: YES (%ld ms elapsed, mode: %s)\n", 
							       elapsed, leader_mode_to_string(leader.mode));
						} else {
							printf("Leader active: NO\n");
						}
					}
					
					// Small delay to prevent CPU hogging
					usleep(10000);
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
				sleep(1);
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
	
	if (leader.leader_function != NULL) {
		free(leader.leader_function);
	}
}

void Handler(char* key, int type, int debug){
	if (key == NULL) {
		if (debug == 1) printf("Handler called with NULL key\n");
		return;
	}
	
	if (strcmp(key, "NULL") == 0) {
		return;
	}
	
	char* cmd = NULL;
	int is_mouse = 0;
	char mouse_char = '1';
	
	switch(type) {
		case -1:
			cmd = "xdotool key ";
			break;
		case 0:
			cmd = "xdotool keydown ";
			break;
		case 1:
			cmd = "xdotool keyup ";
			break;
		case 2:
			is_mouse = 1;
			cmd = "xdotool mousedown ";
			break;
		case 3:
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
	
	if (is_mouse) {
		if (strlen(key) >= 6 && strncmp(key, "mouse", 5) == 0) {
			mouse_char = key[5];
			if (mouse_char < '1' || mouse_char > '5') {
				mouse_char = '1';
			}
		}
		
		char temp[strlen(cmd) + 3];
		snprintf(temp, sizeof(temp), "%s %c", cmd, mouse_char);
		if (debug == 1) printf("Executing: %s\n", temp);
		system(temp);
	} else {
		char temp[strlen(cmd) + strlen(key) + 1];
		snprintf(temp, sizeof(temp), "%s%s", cmd, key);
		if (debug == 1) printf("Executing: %s\n", temp);
		system(temp);
	}
}

char* Substring(const char* in, int start, int end) {
    if (in == NULL) {
        char* out = malloc(1);
        if (out) out[0] = '\0';
        return out;
    }
    
    int len = strlen(in);
    
    if (start < 0) start = 0;
    if (start >= len) {
        char* out = malloc(1);
        if (out) out[0] = '\0';
        return out;
    }
    
    if (end <= 0) {
        char* out = malloc(1);
        if (out) out[0] = '\0';
        return out;
    }
    
    if (start + end > len) {
        end = len - start;
    }
    
    char* out = malloc(end + 1);
    if (out == NULL) {
        return NULL;
    }
    
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
			printf("\nNew in v1.4.9 - ENHANCED LEADER KEY SYSTEM:\n");
			printf("\t• Configurable leader modes:\n");
			printf("\t  - one_shot: Leader + 1 key = combination, then reset (default)\n");
			printf("\t  - sticky: Leader stays active for multiple keys until timeout\n");
			printf("\t  - toggle: Leader toggles on/off (press to enable, press again to disable)\n");
			printf("\t• Per-button eligibility: Control which buttons can be modified by leader\n");
			printf("\t• Example config in default.cfg:\n");
			printf("\t  leader_button: 16\n");
			printf("\t  leader_function: shift\n");
			printf("\t  leader_timeout: 1000\n");
			printf("\t  leader_mode: sticky\n");
			printf("\t  button 0\n");
			printf("\t  type: 0\n");
			printf("\t  function: b\n");
			printf("\t  leader_eligible: true\n");
			printf("\t  button 18\n");
			printf("\t  type: 1\n");
			printf("\t  function: swap\n");
			printf("\t  leader_eligible: false\n");
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
	
	if (enable_uclogic) {
		printf("Mode: hid_uclogic compatibility enabled\n");
	} else {
		printf("Mode: OpenTabletDriver compatible (hid_uclogic disabled)\n");
		if (is_module_loaded("hid_uclogic")) {
			print_compatibility_warning();
		}
	}
	
	printf("\nKD100 Driver v1.4.9 - Enhanced Leader Key System\n");
	printf("Features: Configurable modes | Per-button eligibility | Fixed timing\n\n");
	
	GetDevice(ctx, debug, accept, dry);
	libusb_exit(ctx);
	return 0;
}

