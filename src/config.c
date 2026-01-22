#include "config.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <unistd.h>

// Create a new configuration structure
config_t* config_create(void) {
    config_t* config = malloc(sizeof(config_t));
    if (config == NULL) {
        return NULL;
    }

    // Initialize events
    config->events = malloc(1 * sizeof(event));
    if (config->events == NULL) {
        free(config);
        return NULL;
    }

    // Initialize wheel events
    config->wheelEvents = malloc(1 * sizeof(wheel));
    if (config->wheelEvents == NULL) {
        free(config->events);
        free(config);
        return NULL;
    }

    config->totalButtons = 0;
    config->totalWheels = 0;
    config->enable_uclogic = 0;

    // Initialize leader state
    config->leader.leader_button = -1;
    config->leader.leader_active = 0;
    config->leader.last_button = -1;
    config->leader.leader_press_time.tv_sec = 0;
    config->leader.leader_press_time.tv_usec = 0;
    config->leader.leader_function = NULL;
    config->leader.timeout_ms = 1000;  // 1 second default timeout
    config->leader.mode = LEADER_MODE_ONE_SHOT;  // Default mode
    config->leader.toggle_state = 0;

    config->wheelEvents[0].right = NULL;
    config->wheelEvents[0].left = NULL;

    return config;
}

// Destroy configuration and free memory
void config_destroy(config_t* config) {
    if (config == NULL) return;

    // Free button events
    for (int i = 0; i < config->totalButtons; i++) {
        if (config->events[i].function != NULL) {
            free(config->events[i].function);
        }
    }
    free(config->events);

    // Free wheel events
    for (int i = 0; i < config->totalWheels; i++) {
        if (config->wheelEvents[i].right != NULL) {
            free(config->wheelEvents[i].right);
        }
        if (config->wheelEvents[i].left != NULL) {
            free(config->wheelEvents[i].left);
        }
    }
    free(config->wheelEvents);

    // Free leader function
    if (config->leader.leader_function != NULL) {
        free(config->leader.leader_function);
    }

    free(config);
}

// Load configuration from file
int config_load(config_t* config, const char* filename, int debug) {
    if (config == NULL || filename == NULL) {
        return -1;
    }

    FILE* f = NULL;
    int button = -1;
    int wheelType = 0;
    int leftWheels = 0;
    int rightWheels = 0;
    char data[512];

    // Try to open config file
    f = fopen(filename, "r");
    if (f == NULL) {
        // Try home directory
        char* home = getpwuid(getuid())->pw_dir;
        char* config_dir = "/.config/KD100/";
        char temp[strlen(home) + strlen(config_dir) + strlen(filename) + 1];

        strcpy(temp, home);
        strcat(temp, config_dir);
        strcat(temp, filename);

        f = fopen(temp, "r");
        if (f == NULL) {
            printf("CONFIG FILE NOT FOUND: %s\n", filename);
            return -1;
        }
    }

    // Parse config file
    while (fgets(data, sizeof(data), f) != NULL) {
        data[strcspn(data, "\n")] = 0;

        // Skip comments and empty lines
        if (strstr(data, "//") == data || strlen(data) == 0) {
            continue;
        }

        // Skip leading whitespace
        char* line = data;
        while (*line == ' ' || *line == '\t') line++;

        // Parse enable_uclogic
        if (strncasecmp(line, "enable_uclogic:", 15) == 0) {
            char* value = line + 15;
            while (*value == ' ') value++;
            if (strncasecmp(value, "true", 4) == 0) {
                config->enable_uclogic = 1;
                if (debug) printf("Config: enable_uclogic = true\n");
            } else if (strncasecmp(value, "false", 5) == 0) {
                config->enable_uclogic = 0;
                if (debug) printf("Config: enable_uclogic = false\n");
            }
            continue;
        }

        // Parse leader_button
        if (strncasecmp(line, "leader_button:", 14) == 0) {
            char* value = line + 14;
            while (*value == ' ') value++;
            config->leader.leader_button = atoi(value);
            if (debug) printf("Config: leader_button = %d\n", config->leader.leader_button);
            continue;
        }

        // Parse leader_function
        if (strncasecmp(line, "leader_function:", 16) == 0) {
            char* value = line + 16;
            while (*value == ' ') value++;
            config->leader.leader_function = strdup(value);
            if (config->leader.leader_function != NULL) {
                trim_trailing_spaces(config->leader.leader_function);
            }
            if (debug) printf("Config: leader_function = '%s'\n", config->leader.leader_function);
            continue;
        }

        // Parse leader_timeout
        if (strncasecmp(line, "leader_timeout:", 15) == 0) {
            char* value = line + 15;
            while (*value == ' ') value++;
            config->leader.timeout_ms = atoi(value);
            if (debug) printf("Config: leader_timeout = %d ms\n", config->leader.timeout_ms);
            continue;
        }

        // Parse leader_mode
        if (strncasecmp(line, "leader_mode:", 12) == 0) {
            char* value = line + 12;
            while (*value == ' ') value++;
            config->leader.mode = parse_leader_mode(value);
            if (debug) printf("Config: leader_mode = %s\n", leader_mode_to_string(config->leader.mode));
            continue;
        }

        // Parse button
        if (strncasecmp(line, "button ", 7) == 0) {
            char* num_str = line + 7;
            while (*num_str == ' ') num_str++;
            button = atoi(num_str);

            if (button >= config->totalButtons) {
                event* temp = realloc(config->events, (button + 1) * sizeof(*config->events));
                if (temp == NULL) {
                    printf("Memory allocation failed!\n");
                    fclose(f);
                    return -1;
                }
                config->events = temp;
                for (int j = config->totalButtons; j <= button; j++) {
                    config->events[j].function = NULL;
                    config->events[j].type = 0;
                    config->events[j].leader_eligible = -1;  // Default: not set
                }
                config->totalButtons = button + 1;
            }
            continue;
        }

        // Parse type
        if (strncasecmp(line, "type:", 5) == 0 && button != -1) {
            char* type_str = line + 5;
            while (*type_str == ' ') type_str++;
            config->events[button].type = atoi(type_str);
            continue;
        }

        // Parse leader_eligible
        if (strncasecmp(line, "leader_eligible:", 16) == 0 && button != -1) {
            char* value = line + 16;
            while (*value == ' ') value++;
            if (strcasecmp(value, "false") == 0) {
                config->events[button].leader_eligible = 0;
            } else if (strcasecmp(value, "true") == 0) {
                config->events[button].leader_eligible = 1;
            } else {
                config->events[button].leader_eligible = atoi(value);
            }
            if (debug) printf("Config: button %d leader_eligible = %d\n", button, config->events[button].leader_eligible);
            continue;
        }

        // Parse function
        if (strncasecmp(line, "function:", 9) == 0) {
            char* func_str = line + 9;
            while (*func_str == ' ') func_str++;

            char* func_copy = strdup(func_str);
            if (func_copy == NULL) {
                printf("Memory allocation failed!\n");
                fclose(f);
                return -1;
            }

            if (!wheelType) {
                if (button >= 0 && button < config->totalButtons) {
                    if (config->events[button].function != NULL) {
                        free(config->events[button].function);
                    }
                    config->events[button].function = func_copy;
                } else {
                    free(func_copy);
                    if (debug) printf("Warning: function without valid button definition\n");
                }
            } else if (wheelType == 1) {
                if (rightWheels != 0) {
                    wheel* temp = realloc(config->wheelEvents, (rightWheels + 1) * sizeof(*config->wheelEvents));
                    if (temp == NULL) {
                        printf("Memory allocation failed!\n");
                        free(func_copy);
                        fclose(f);
                        return -1;
                    }
                    config->wheelEvents = temp;
                    config->wheelEvents[rightWheels].right = func_copy;
                    config->wheelEvents[rightWheels].left = NULL;
                } else {
                    config->wheelEvents[0].right = func_copy;
                    config->wheelEvents[0].left = NULL;
                }
                rightWheels++;
            } else {
                if (leftWheels < rightWheels) {
                    if (config->wheelEvents[leftWheels].left != NULL) {
                        free(config->wheelEvents[leftWheels].left);
                    }
                    config->wheelEvents[leftWheels].left = func_copy;
                } else {
                    wheel* temp = realloc(config->wheelEvents, (leftWheels + 1) * sizeof(*config->wheelEvents));
                    if (temp == NULL) {
                        printf("Memory allocation failed!\n");
                        free(func_copy);
                        fclose(f);
                        return -1;
                    }
                    config->wheelEvents = temp;
                    config->wheelEvents[leftWheels].left = func_copy;
                    config->wheelEvents[leftWheels].right = NULL;
                }
                leftWheels++;
            }
            continue;
        }

        // Parse wheel
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
    for (int i = 0; i < config->totalButtons; i++) {
        if (config->events[i].leader_eligible == -1) {
            // Default: eligible for all buttons except wheel button (18)
            config->events[i].leader_eligible = (i == 18) ? 0 : 1;
        }
    }

    // Calculate total wheels
    if (rightWheels > leftWheels)
        config->totalWheels = rightWheels;
    else
        config->totalWheels = leftWheels;

    return 0;
}

// Print configuration (for debugging)
void config_print(const config_t* config, int debug) {
    if (config == NULL || debug == 0) return;

    printf("\n=== Button Configuration ===\n");
    for (int i = 0; i < config->totalButtons; i++) {
        if (config->events[i].function != NULL) {
            printf("Button %2d: Type: %d | Function: '%s' | Leader Eligible: %s\n",
                   i, config->events[i].type, config->events[i].function,
                   config->events[i].leader_eligible == 1 ? "YES" :
                   config->events[i].leader_eligible == 0 ? "NO" : "DEFAULT");
        } else {
            printf("Button %2d: Type: %d | Function: (not set) | Leader Eligible: %s\n",
                   i, config->events[i].type,
                   config->events[i].leader_eligible == 1 ? "YES" :
                   config->events[i].leader_eligible == 0 ? "NO" : "DEFAULT");
        }
    }

    printf("\n=== Wheel Configuration ===\n");
    for (int i = 0; i < config->totalWheels; i++) {
        printf("Wheel %d: Right: %s | Left: %s\n", i,
               config->wheelEvents[i].right ? config->wheelEvents[i].right : "(null)",
               config->wheelEvents[i].left ? config->wheelEvents[i].left : "(null)");
    }

    printf("\n=== Leader Configuration ===\n");
    printf("Leader button: %d\n", config->leader.leader_button);
    printf("Leader function: '%s'\n", config->leader.leader_function ? config->leader.leader_function : "(null)");
    printf("Leader timeout: %d ms\n", config->leader.timeout_ms);
    printf("Leader mode: %s\n", leader_mode_to_string(config->leader.mode));
    printf("\n");
}
