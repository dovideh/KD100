#include "config.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <unistd.h>

// Helper function to convert wheel mode enum to string
static const char* wheel_mode_to_string(wheel_mode_t mode) {
    switch (mode) {
        case WHEEL_MODE_SEQUENTIAL: return "sequential";
        case WHEEL_MODE_SETS: return "sets";
        default: return "unknown";
    }
}

// Helper function to parse wheel mode string to enum
static wheel_mode_t parse_wheel_mode(const char* mode_str) {
    if (strcasecmp(mode_str, "sequential") == 0) {
        return WHEEL_MODE_SEQUENTIAL;
    } else if (strcasecmp(mode_str, "sets") == 0) {
        return WHEEL_MODE_SETS;
    }
    // Default to sequential for backward compatibility
    return WHEEL_MODE_SEQUENTIAL;
}

// Helper function to strip inline comments (// ...) and trailing whitespace
static char* strip_inline_comment(char* str) {
    if (str == NULL) return NULL;

    // Find // and terminate string there
    char* comment = strstr(str, "//");
    if (comment != NULL) {
        *comment = '\0';
    }

    // Strip trailing whitespace
    size_t len = strlen(str);
    while (len > 0 && (str[len-1] == ' ' || str[len-1] == '\t' || str[len-1] == '\n' || str[len-1] == '\r')) {
        str[--len] = '\0';
    }

    return str;
}

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
    config->wheel_click_timeout_ms = 300;  // 300ms default timeout
    config->wheel_mode = WHEEL_MODE_SEQUENTIAL;  // Default to sequential (legacy behavior)

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

    // Initialize OSD settings
    config->osd.enabled = 0;
    config->osd.start_visible = 0;
    config->osd.pos_x = 50;
    config->osd.pos_y = 50;
    config->osd.opacity = 0.67f;  // 33% transparent = 67% opaque
    config->osd.display_duration_ms = 3000;
    config->osd.min_width = 200;
    config->osd.min_height = 100;
    config->osd.expanded_width = 400;
    config->osd.expanded_height = 350;
    config->osd.osd_toggle_button = -1;  // Disabled by default
    config->osd.font_size = 13;  // Default font size

    // Initialize profile settings
    config->profile.profiles_file = NULL;
    config->profile.auto_switch = 1;  // Enabled by default
    config->profile.check_interval_ms = 500;

    // Initialize key descriptions
    for (int i = 0; i < 19; i++) {
        config->key_descriptions[i] = NULL;
    }

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

    // Free profile settings
    if (config->profile.profiles_file != NULL) {
        free(config->profile.profiles_file);
    }

    // Free key descriptions
    for (int i = 0; i < 19; i++) {
        if (config->key_descriptions[i] != NULL) {
            free(config->key_descriptions[i]);
        }
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

        // Parse wheel_click_timeout
        if (strncasecmp(line, "wheel_click_timeout:", 20) == 0) {
            char* value = line + 20;
            while (*value == ' ') value++;
            int timeout = atoi(value);
            // Enforce hard limits: 20-990ms
            if (timeout < 20) timeout = 20;
            if (timeout > 990) timeout = 990;
            config->wheel_click_timeout_ms = timeout;
            if (debug) printf("Config: wheel_click_timeout = %d ms\n", config->wheel_click_timeout_ms);
            continue;
        }

        // Parse wheel_mode
        if (strncasecmp(line, "wheel_mode:", 11) == 0) {
            char* value = line + 11;
            while (*value == ' ') value++;
            config->wheel_mode = parse_wheel_mode(value);
            if (debug) printf("Config: wheel_mode = %s\n", wheel_mode_to_string(config->wheel_mode));
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

        // Parse OSD settings
        if (strncasecmp(line, "osd_enabled:", 12) == 0) {
            char* value = line + 12;
            while (*value == ' ') value++;
            config->osd.enabled = (strncasecmp(value, "true", 4) == 0 || strcmp(value, "1") == 0);
            if (debug) printf("Config: osd_enabled = %s\n", config->osd.enabled ? "true" : "false");
            continue;
        }

        if (strncasecmp(line, "osd_start_visible:", 18) == 0) {
            char* value = line + 18;
            while (*value == ' ') value++;
            config->osd.start_visible = (strncasecmp(value, "true", 4) == 0 || strcmp(value, "1") == 0);
            if (debug) printf("Config: osd_start_visible = %s\n", config->osd.start_visible ? "true" : "false");
            continue;
        }

        if (strncasecmp(line, "osd_position:", 13) == 0) {
            char* value = line + 13;
            while (*value == ' ') value++;
            if (sscanf(value, "%d,%d", &config->osd.pos_x, &config->osd.pos_y) == 2) {
                if (debug) printf("Config: osd_position = %d,%d\n", config->osd.pos_x, config->osd.pos_y);
            }
            continue;
        }

        if (strncasecmp(line, "osd_opacity:", 12) == 0) {
            char* value = line + 12;
            while (*value == ' ') value++;
            float opacity = atof(value);
            if (opacity < 0.0f) opacity = 0.0f;
            if (opacity > 1.0f) opacity = 1.0f;
            config->osd.opacity = opacity;
            if (debug) printf("Config: osd_opacity = %.2f\n", config->osd.opacity);
            continue;
        }

        if (strncasecmp(line, "osd_display_duration:", 21) == 0) {
            char* value = line + 21;
            while (*value == ' ') value++;
            config->osd.display_duration_ms = atoi(value);
            if (debug) printf("Config: osd_display_duration = %d ms\n", config->osd.display_duration_ms);
            continue;
        }

        if (strncasecmp(line, "osd_min_size:", 13) == 0) {
            char* value = line + 13;
            while (*value == ' ') value++;
            if (sscanf(value, "%d,%d", &config->osd.min_width, &config->osd.min_height) == 2) {
                if (debug) printf("Config: osd_min_size = %dx%d\n", config->osd.min_width, config->osd.min_height);
            }
            continue;
        }

        if (strncasecmp(line, "osd_expanded_size:", 18) == 0) {
            char* value = line + 18;
            while (*value == ' ') value++;
            if (sscanf(value, "%d,%d", &config->osd.expanded_width, &config->osd.expanded_height) == 2) {
                if (debug) printf("Config: osd_expanded_size = %dx%d\n", config->osd.expanded_width, config->osd.expanded_height);
            }
            continue;
        }

        if (strncasecmp(line, "osd_toggle_button:", 18) == 0) {
            char* value = line + 18;
            while (*value == ' ') value++;
            config->osd.osd_toggle_button = atoi(value);
            if (debug) printf("Config: osd_toggle_button = %d\n", config->osd.osd_toggle_button);
            continue;
        }

        if (strncasecmp(line, "osd_font_size:", 14) == 0) {
            char* value = line + 14;
            while (*value == ' ') value++;
            config->osd.font_size = atoi(value);
            if (config->osd.font_size < 8) config->osd.font_size = 8;
            if (config->osd.font_size > 32) config->osd.font_size = 32;
            if (debug) printf("Config: osd_font_size = %d\n", config->osd.font_size);
            continue;
        }

        // Parse profile settings
        if (strncasecmp(line, "profiles_file:", 14) == 0) {
            char* value = line + 14;
            while (*value == ' ') value++;
            if (config->profile.profiles_file) free(config->profile.profiles_file);
            config->profile.profiles_file = strdup(value);
            if (debug) printf("Config: profiles_file = %s\n", config->profile.profiles_file);
            continue;
        }

        if (strncasecmp(line, "profile_auto_switch:", 20) == 0) {
            char* value = line + 20;
            while (*value == ' ') value++;
            config->profile.auto_switch = (strncasecmp(value, "true", 4) == 0 || strcmp(value, "1") == 0);
            if (debug) printf("Config: profile_auto_switch = %s\n", config->profile.auto_switch ? "true" : "false");
            continue;
        }

        if (strncasecmp(line, "profile_check_interval:", 23) == 0) {
            char* value = line + 23;
            while (*value == ' ') value++;
            config->profile.check_interval_ms = atoi(value);
            if (debug) printf("Config: profile_check_interval = %d ms\n", config->profile.check_interval_ms);
            continue;
        }

        // Parse key descriptions (description_0, description_1, etc.)
        if (strncasecmp(line, "description_", 12) == 0) {
            char* num_str = line + 12;
            char* colon = strchr(num_str, ':');
            if (colon) {
                *colon = '\0';
                int btn = atoi(num_str);
                if (btn >= 0 && btn <= 18) {
                    char* value = colon + 1;
                    while (*value == ' ') value++;
                    if (config->key_descriptions[btn]) free(config->key_descriptions[btn]);
                    config->key_descriptions[btn] = strdup(value);
                    if (debug) printf("Config: description_%d = %s\n", btn, config->key_descriptions[btn]);
                }
            }
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

            // Strip inline comments and trailing whitespace
            char* func_copy = strdup(func_str);
            if (func_copy != NULL) {
                strip_inline_comment(func_copy);
            }
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

    printf("\n=== Wheel Click Configuration ===\n");
    printf("Multi-click timeout: %d ms\n", config->wheel_click_timeout_ms);
    printf("Wheel mode: %s\n", wheel_mode_to_string(config->wheel_mode));

    printf("\n=== OSD Configuration ===\n");
    printf("OSD enabled: %s\n", config->osd.enabled ? "yes" : "no");
    printf("Start visible: %s\n", config->osd.start_visible ? "yes" : "no");
    printf("Position: %d, %d\n", config->osd.pos_x, config->osd.pos_y);
    printf("Opacity: %.2f\n", config->osd.opacity);
    printf("Display duration: %d ms\n", config->osd.display_duration_ms);
    printf("Min size: %dx%d\n", config->osd.min_width, config->osd.min_height);
    printf("Expanded size: %dx%d\n", config->osd.expanded_width, config->osd.expanded_height);
    printf("Toggle button: %d\n", config->osd.osd_toggle_button);
    printf("Font size: %d\n", config->osd.font_size);

    printf("\n=== Profile Configuration ===\n");
    printf("Profiles file: %s\n", config->profile.profiles_file ? config->profile.profiles_file : "(none)");
    printf("Auto switch: %s\n", config->profile.auto_switch ? "yes" : "no");
    printf("Check interval: %d ms\n", config->profile.check_interval_ms);

    // Print key descriptions if any are set
    int has_descriptions = 0;
    for (int i = 0; i < 19; i++) {
        if (config->key_descriptions[i]) {
            if (!has_descriptions) {
                printf("\n=== Key Descriptions ===\n");
                has_descriptions = 1;
            }
            printf("Button %d: %s\n", i, config->key_descriptions[i]);
        }
    }

    printf("\n");
}
