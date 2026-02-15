#include "profiles.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <unistd.h>

// Helper: free profile contents
static void free_profile(profile_t* profile) {
    if (profile->name) { free(profile->name); profile->name = NULL; }
    if (profile->window_pattern) { free(profile->window_pattern); profile->window_pattern = NULL; }
    if (profile->config_file) { free(profile->config_file); profile->config_file = NULL; }

    // Free config if we own it (not the default)
    if (profile->config) {
        config_destroy(profile->config);
        profile->config = NULL;
    }

    // Free descriptions
    for (int i = 0; i < 19; i++) {
        if (profile->key_descriptions[i]) {
            free(profile->key_descriptions[i]);
            profile->key_descriptions[i] = NULL;
        }
    }
}

// Create profile manager
profile_manager_t* profile_manager_create(config_t* default_config) {
    profile_manager_t* manager = calloc(1, sizeof(profile_manager_t));
    if (manager == NULL) return NULL;

    manager->default_config = default_config;
    manager->profile_count = 0;
    manager->active_profile_index = -1;
    manager->debug = 0;
    manager->window_tracker = NULL;
    manager->osd = NULL;

    // Initialize all profiles
    for (int i = 0; i < MAX_PROFILES; i++) {
        memset(&manager->profiles[i], 0, sizeof(profile_t));
    }

    return manager;
}

// Destroy profile manager
void profile_manager_destroy(profile_manager_t* manager) {
    if (manager == NULL) return;

    // Free all profiles
    for (int i = 0; i < manager->profile_count; i++) {
        free_profile(&manager->profiles[i]);
    }

    // Free window tracker
    if (manager->window_tracker) {
        window_tracker_destroy(manager->window_tracker);
    }

    free(manager);
}

// Initialize with X11 display
int profile_manager_init(profile_manager_t* manager, void* display, osd_state_t* osd) {
    if (manager == NULL) return -1;

    manager->osd = osd;

    // Create window tracker
    manager->window_tracker = window_tracker_create();
    if (manager->window_tracker == NULL) {
        fprintf(stderr, "Profile manager: Failed to create window tracker\n");
        return -1;
    }

    if (window_tracker_init(manager->window_tracker, display) < 0) {
        window_tracker_destroy(manager->window_tracker);
        manager->window_tracker = NULL;
        return -1;
    }

    return 0;
}

// Add a profile
int profile_add(profile_manager_t* manager, const char* name, const char* window_pattern,
                const char* config_file, int priority) {
    if (manager == NULL || name == NULL || window_pattern == NULL) return -1;
    if (manager->profile_count >= MAX_PROFILES) return -1;

    // Check for duplicate name
    for (int i = 0; i < manager->profile_count; i++) {
        if (manager->profiles[i].name && strcmp(manager->profiles[i].name, name) == 0) {
            return -1;  // Already exists
        }
    }

    profile_t* profile = &manager->profiles[manager->profile_count];

    profile->name = strdup(name);
    profile->window_pattern = strdup(window_pattern);
    profile->config_file = config_file ? strdup(config_file) : NULL;
    profile->priority = priority;
    profile->config = NULL;
    profile->is_default = 0;

    // Initialize descriptions to NULL
    for (int i = 0; i < 19; i++) {
        profile->key_descriptions[i] = NULL;
    }

    // Load config if specified
    if (config_file) {
        profile->config = config_create();
        if (profile->config) {
            if (config_load(profile->config, config_file, manager->debug) < 0) {
                if (manager->debug) {
                    printf("Profile '%s': Failed to load config '%s', using default\n",
                           name, config_file);
                }
                config_destroy(profile->config);
                profile->config = NULL;
            }
        }
    }

    manager->profile_count++;

    if (manager->debug) {
        printf("Profile added: '%s' (pattern: '%s', priority: %d)\n",
               name, window_pattern, priority);
    }

    return 0;
}

// Remove a profile
int profile_remove(profile_manager_t* manager, const char* name) {
    if (manager == NULL || name == NULL) return -1;

    for (int i = 0; i < manager->profile_count; i++) {
        if (manager->profiles[i].name && strcmp(manager->profiles[i].name, name) == 0) {
            // Free this profile
            free_profile(&manager->profiles[i]);

            // Shift remaining profiles
            for (int j = i; j < manager->profile_count - 1; j++) {
                manager->profiles[j] = manager->profiles[j + 1];
            }

            // Clear last slot
            memset(&manager->profiles[manager->profile_count - 1], 0, sizeof(profile_t));

            manager->profile_count--;

            // Adjust active index if needed
            if (manager->active_profile_index == i) {
                manager->active_profile_index = -1;
            } else if (manager->active_profile_index > i) {
                manager->active_profile_index--;
            }

            return 0;
        }
    }

    return -1;  // Not found
}

// Get profile by name
profile_t* profile_get(profile_manager_t* manager, const char* name) {
    if (manager == NULL || name == NULL) return NULL;

    for (int i = 0; i < manager->profile_count; i++) {
        if (manager->profiles[i].name && strcmp(manager->profiles[i].name, name) == 0) {
            return &manager->profiles[i];
        }
    }

    return NULL;
}

// Get profile by index
profile_t* profile_get_by_index(profile_manager_t* manager, int index) {
    if (manager == NULL || index < 0 || index >= manager->profile_count) return NULL;
    return &manager->profiles[index];
}

// Set key description for a profile
int profile_set_description(profile_manager_t* manager, const char* profile_name,
                            int button_index, const char* description) {
    if (manager == NULL || profile_name == NULL) return -1;
    if (button_index < 0 || button_index > 18) return -1;

    profile_t* profile = profile_get(manager, profile_name);
    if (profile == NULL) return -1;

    if (profile->key_descriptions[button_index]) {
        free(profile->key_descriptions[button_index]);
    }
    profile->key_descriptions[button_index] = description ? strdup(description) : NULL;

    return 0;
}

// Get key description for a profile
const char* profile_get_description(profile_manager_t* manager, const char* profile_name,
                                     int button_index) {
    if (manager == NULL || profile_name == NULL) return NULL;
    if (button_index < 0 || button_index > 18) return NULL;

    profile_t* profile = profile_get(manager, profile_name);
    if (profile == NULL) return NULL;

    return profile->key_descriptions[button_index];
}

// Set default profile
int profile_set_default(profile_manager_t* manager, const char* name) {
    if (manager == NULL || name == NULL) return -1;

    // Clear existing default
    for (int i = 0; i < manager->profile_count; i++) {
        manager->profiles[i].is_default = 0;
    }

    // Set new default
    profile_t* profile = profile_get(manager, name);
    if (profile) {
        profile->is_default = 1;
        return 0;
    }

    return -1;
}

// Update profile manager - check active window and switch if needed
//
// Profile matching behavior:
//   1. If window matches a profile pattern, switch to highest-priority match
//   2. If no pattern matches but a default profile exists, switch to default
//   3. If no pattern matches and no default profile exists, keep current
//      profile active ("sticky" behavior -- see docs/PROFILES_DESIGN.md)
//
// NOTE: Currently no visual feedback is given on profile switch (debug only).
// Planned: OSD notification "Profile: <name>" on switch.
int profile_manager_update(profile_manager_t* manager) {
    if (manager == NULL || manager->window_tracker == NULL) return -1;

    // Update window tracker
    int window_changed = window_tracker_update(manager->window_tracker);

    if (window_changed <= 0 && manager->active_profile_index >= 0) {
        return 0;  // Same window, same profile
    }

    const window_info_t* window = window_tracker_get_current(manager->window_tracker);

    if (manager->debug) {
        printf("Window changed: title='%s' class='%s' instance='%s'\n",
               window->title ? window->title : "(null)",
               window->class_name ? window->class_name : "(null)",
               window->instance_name ? window->instance_name : "(null)");
    }

    // Find matching profile (highest priority)
    int best_index = -1;
    int best_priority = -999999;
    int default_index = -1;

    for (int i = 0; i < manager->profile_count; i++) {
        profile_t* p = &manager->profiles[i];

        if (p->is_default) {
            default_index = i;
        }

        if (p->window_pattern && window_matches(window, p->window_pattern)) {
            if (p->priority > best_priority) {
                best_priority = p->priority;
                best_index = i;
            }
        }
    }

    // Use default profile if no match; if no default either, keep current
    // profile active (sticky behavior -- do not reset to -1)
    if (best_index < 0) {
        best_index = default_index;
    }
    if (best_index < 0) {
        // No match and no default profile: keep current profile (sticky)
        return 0;
    }

    // Check if profile changed
    if (best_index == manager->active_profile_index) {
        return 0;  // Same profile
    }

    // Switch to new profile
    manager->active_profile_index = best_index;

    if (manager->debug) {
        if (best_index >= 0) {
            printf("Profile switched: '%s'\n", manager->profiles[best_index].name);
        } else {
            printf("Profile switched: (default config)\n");
        }
    }

    // Update OSD with new profile's descriptions
    if (manager->osd && best_index >= 0) {
        profile_t* profile = &manager->profiles[best_index];
        osd_clear_descriptions(manager->osd);
        for (int i = 0; i < 19; i++) {
            if (profile->key_descriptions[i]) {
                osd_set_key_description(manager->osd, i, profile->key_descriptions[i]);
            }
        }
    }

    return 1;  // Profile changed
}

// Get active profile
profile_t* profile_manager_get_active(profile_manager_t* manager) {
    if (manager == NULL || manager->active_profile_index < 0) return NULL;
    return &manager->profiles[manager->active_profile_index];
}

// Get active config
config_t* profile_manager_get_config(profile_manager_t* manager) {
    if (manager == NULL) return NULL;

    profile_t* active = profile_manager_get_active(manager);
    if (active && active->config) {
        return active->config;
    }

    return manager->default_config;
}

// Manual profile switch by name
int profile_manager_switch(profile_manager_t* manager, const char* name) {
    if (manager == NULL || name == NULL) return -1;

    for (int i = 0; i < manager->profile_count; i++) {
        if (manager->profiles[i].name && strcmp(manager->profiles[i].name, name) == 0) {
            return profile_manager_switch_by_index(manager, i);
        }
    }

    return -1;
}

// Manual profile switch by index
int profile_manager_switch_by_index(profile_manager_t* manager, int index) {
    if (manager == NULL || index < 0 || index >= manager->profile_count) return -1;

    manager->active_profile_index = index;

    // Update OSD descriptions
    if (manager->osd) {
        profile_t* profile = &manager->profiles[index];
        osd_clear_descriptions(manager->osd);
        for (int i = 0; i < 19; i++) {
            if (profile->key_descriptions[i]) {
                osd_set_key_description(manager->osd, i, profile->key_descriptions[i]);
            }
        }
    }

    return 0;
}

// Set debug level
void profile_manager_set_debug(profile_manager_t* manager, int level) {
    if (manager) manager->debug = level;
}

// Print profile information
void profile_manager_print(const profile_manager_t* manager) {
    if (manager == NULL) return;

    printf("\n=== Profile Configuration ===\n");
    printf("Total profiles: %d\n", manager->profile_count);
    printf("Active profile: %d", manager->active_profile_index);
    if (manager->active_profile_index >= 0) {
        printf(" ('%s')", manager->profiles[manager->active_profile_index].name);
    }
    printf("\n\n");

    for (int i = 0; i < manager->profile_count; i++) {
        const profile_t* p = &manager->profiles[i];
        printf("Profile %d: '%s'%s\n", i, p->name, p->is_default ? " [DEFAULT]" : "");
        printf("  Pattern:  '%s'\n", p->window_pattern);
        printf("  Priority: %d\n", p->priority);
        printf("  Config:   %s\n", p->config_file ? p->config_file : "(default)");

        // Print non-null descriptions
        int has_desc = 0;
        for (int j = 0; j < 19; j++) {
            if (p->key_descriptions[j]) {
                if (!has_desc) {
                    printf("  Descriptions:\n");
                    has_desc = 1;
                }
                printf("    Button %d: %s\n", j, p->key_descriptions[j]);
            }
        }
        printf("\n");
    }
}

// Load profiles from config file
int profile_manager_load(profile_manager_t* manager, const char* filename) {
    if (manager == NULL || filename == NULL) return -1;

    FILE* f = fopen(filename, "r");
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
            if (manager->debug) {
                printf("Profile config not found: %s\n", filename);
            }
            return -1;
        }
    }

    char line[512];
    char* current_profile = NULL;
    char* current_pattern = NULL;
    char* current_config = NULL;
    int current_priority = 0;
    int current_is_default = 0;
    char* current_descriptions[19] = {NULL};

    while (fgets(line, sizeof(line), f) != NULL) {
        // Remove newline
        line[strcspn(line, "\n")] = 0;

        // Skip comments and empty lines
        if (strstr(line, "//") == line || strlen(line) == 0) continue;

        // Skip leading whitespace
        char* ptr = line;
        while (*ptr == ' ' || *ptr == '\t') ptr++;

        // Parse profile name
        if (strncasecmp(ptr, "profile:", 8) == 0) {
            // Save previous profile if exists
            if (current_profile && current_pattern) {
                if (profile_add(manager, current_profile, current_pattern,
                               current_config, current_priority) == 0) {
                    if (current_is_default) {
                        profile_set_default(manager, current_profile);
                    }
                    for (int i = 0; i < 19; i++) {
                        if (current_descriptions[i]) {
                            profile_set_description(manager, current_profile, i, current_descriptions[i]);
                            free(current_descriptions[i]);
                            current_descriptions[i] = NULL;
                        }
                    }
                }
            }

            // Start new profile
            if (current_profile) free(current_profile);
            if (current_pattern) free(current_pattern);
            if (current_config) free(current_config);

            char* value = ptr + 8;
            while (*value == ' ') value++;
            current_profile = strdup(value);
            current_pattern = NULL;
            current_config = NULL;
            current_priority = 0;
            current_is_default = 0;
            continue;
        }

        // Parse pattern
        if (strncasecmp(ptr, "pattern:", 8) == 0 && current_profile) {
            char* value = ptr + 8;
            while (*value == ' ') value++;
            if (current_pattern) free(current_pattern);
            current_pattern = strdup(value);
            continue;
        }

        // Parse config file
        if (strncasecmp(ptr, "config:", 7) == 0 && current_profile) {
            char* value = ptr + 7;
            while (*value == ' ') value++;
            if (current_config) free(current_config);
            current_config = strdup(value);
            continue;
        }

        // Parse priority
        if (strncasecmp(ptr, "priority:", 9) == 0 && current_profile) {
            char* value = ptr + 9;
            while (*value == ' ') value++;
            current_priority = atoi(value);
            continue;
        }

        // Parse default flag
        if (strncasecmp(ptr, "default:", 8) == 0 && current_profile) {
            char* value = ptr + 8;
            while (*value == ' ') value++;
            current_is_default = (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0);
            continue;
        }

        // Parse button descriptions (description_0, description_1, etc.)
        if (strncasecmp(ptr, "description_", 12) == 0 && current_profile) {
            char* num_str = ptr + 12;
            char* colon = strchr(num_str, ':');
            if (colon) {
                *colon = '\0';
                int btn = atoi(num_str);
                if (btn >= 0 && btn <= 18) {
                    char* value = colon + 1;
                    while (*value == ' ') value++;
                    if (current_descriptions[btn]) free(current_descriptions[btn]);
                    current_descriptions[btn] = strdup(value);
                }
            }
            continue;
        }
    }

    // Save last profile
    if (current_profile && current_pattern) {
        if (profile_add(manager, current_profile, current_pattern,
                       current_config, current_priority) == 0) {
            if (current_is_default) {
                profile_set_default(manager, current_profile);
            }
            for (int i = 0; i < 19; i++) {
                if (current_descriptions[i]) {
                    profile_set_description(manager, current_profile, i, current_descriptions[i]);
                    free(current_descriptions[i]);
                }
            }
        }
    }

    if (current_profile) free(current_profile);
    if (current_pattern) free(current_pattern);
    if (current_config) free(current_config);

    fclose(f);
    return 0;
}
