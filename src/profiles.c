#include "profiles.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pwd.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <errno.h>

// ============================================================================
// Helper functions
// ============================================================================

// Helper: free profile contents
static void free_profile(profile_t* profile) {
    if (profile->name) { free(profile->name); profile->name = NULL; }
    if (profile->window_pattern) { free(profile->window_pattern); profile->window_pattern = NULL; }
    if (profile->source_file) { free(profile->source_file); profile->source_file = NULL; }

    // Free config if we own it
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
        if (profile->leader_descriptions[i]) {
            free(profile->leader_descriptions[i]);
            profile->leader_descriptions[i] = NULL;
        }
    }
    for (int i = 0; i < 32; i++) {
        if (profile->wheel_descriptions[i]) {
            free(profile->wheel_descriptions[i]);
            profile->wheel_descriptions[i] = NULL;
        }
    }
}

// Helper: sanitize a description string (max 64 chars, printable ASCII)
static char* sanitize_desc(const char* input) {
    if (input == NULL) return NULL;
    while (*input == ' ' || *input == '\t') input++;
    size_t len = strlen(input);
    if (len > MAX_DESCRIPTION_LEN) len = MAX_DESCRIPTION_LEN;
    char* result = malloc(len + 1);
    if (!result) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < len && input[i]; i++) {
        if (input[i] >= 0x20 && input[i] <= 0x7E)
            result[j++] = input[i];
    }
    result[j] = '\0';
    while (j > 0 && (result[j-1] == ' ' || result[j-1] == '\t'))
        result[--j] = '\0';
    if (j == 0) { free(result); return NULL; }
    return result;
}

// Helper: strip inline comments and trailing whitespace
static void strip_comment(char* str) {
    if (!str) return;
    char* c = strstr(str, "//");
    if (c) *c = '\0';
    size_t len = strlen(str);
    while (len > 0 && (str[len-1] == ' ' || str[len-1] == '\t' || str[len-1] == '\n' || str[len-1] == '\r'))
        str[--len] = '\0';
}

// Helper: build a merged config (default overlaid with profile-specific overrides)
// Only overlays: button events, wheel events, key/leader/wheel descriptions
// Does NOT overlay: leader config, OSD, wheel_mode, enable_uclogic, profile settings
static config_t* config_merge(const config_t* base, const config_t* overlay) {
    if (base == NULL) return NULL;

    config_t* merged = config_create();
    if (merged == NULL) return NULL;

    // Copy base settings that profiles should NOT change
    merged->enable_uclogic = base->enable_uclogic;
    merged->wheel_click_timeout_ms = base->wheel_click_timeout_ms;
    merged->wheel_mode = base->wheel_mode;
    merged->osd = base->osd;
    merged->profile = base->profile;
    // Null out the pointers we copied so they don't get double-freed
    merged->profile.profiles_file = base->profile.profiles_file ? strdup(base->profile.profiles_file) : NULL;
    merged->profile.profiles_dir = base->profile.profiles_dir ? strdup(base->profile.profiles_dir) : NULL;

    // Copy leader config from base (leader is NOT per-profile)
    merged->leader.leader_button = base->leader.leader_button;
    merged->leader.leader_active = base->leader.leader_active;
    merged->leader.last_button = base->leader.last_button;
    merged->leader.leader_press_time = base->leader.leader_press_time;
    merged->leader.leader_function = base->leader.leader_function ? strdup(base->leader.leader_function) : NULL;
    merged->leader.timeout_ms = base->leader.timeout_ms;
    merged->leader.mode = base->leader.mode;
    merged->leader.toggle_state = base->leader.toggle_state;

    // Copy button events from base
    if (base->totalButtons > 0) {
        event* ev = realloc(merged->events, base->totalButtons * sizeof(event));
        if (ev) {
            merged->events = ev;
            merged->totalButtons = base->totalButtons;
            for (int i = 0; i < base->totalButtons; i++) {
                merged->events[i].type = base->events[i].type;
                merged->events[i].function = base->events[i].function ? strdup(base->events[i].function) : NULL;
                merged->events[i].leader_eligible = base->events[i].leader_eligible;
            }
        }
    }

    // Copy wheel events from base
    if (base->totalWheels > 0) {
        wheel* wh = realloc(merged->wheelEvents, base->totalWheels * sizeof(wheel));
        if (wh) {
            merged->wheelEvents = wh;
            merged->totalWheels = base->totalWheels;
            for (int i = 0; i < base->totalWheels; i++) {
                merged->wheelEvents[i].right = base->wheelEvents[i].right ? strdup(base->wheelEvents[i].right) : NULL;
                merged->wheelEvents[i].left = base->wheelEvents[i].left ? strdup(base->wheelEvents[i].left) : NULL;
                merged->wheelEvents[i].description = base->wheelEvents[i].description ? strdup(base->wheelEvents[i].description) : NULL;
            }
        }
    }

    // Copy descriptions from base
    for (int i = 0; i < 19; i++) {
        merged->key_descriptions[i] = base->key_descriptions[i] ? strdup(base->key_descriptions[i]) : NULL;
        merged->leader_descriptions[i] = base->leader_descriptions[i] ? strdup(base->leader_descriptions[i]) : NULL;
    }

    // Now overlay profile-specific values (if overlay is provided)
    if (overlay == NULL) return merged;

    // Overlay button events (only buttons that the overlay defines)
    for (int i = 0; i < overlay->totalButtons; i++) {
        if (overlay->events[i].function != NULL) {
            // Ensure merged has enough button slots
            if (i >= merged->totalButtons) {
                event* ev = realloc(merged->events, (i + 1) * sizeof(event));
                if (ev) {
                    merged->events = ev;
                    for (int j = merged->totalButtons; j <= i; j++) {
                        merged->events[j].function = NULL;
                        merged->events[j].type = 0;
                        merged->events[j].leader_eligible = -1;
                    }
                    merged->totalButtons = i + 1;
                }
            }
            if (i < merged->totalButtons) {
                if (merged->events[i].function) free(merged->events[i].function);
                merged->events[i].function = strdup(overlay->events[i].function);
                merged->events[i].type = overlay->events[i].type;
                if (overlay->events[i].leader_eligible != -1) {
                    merged->events[i].leader_eligible = overlay->events[i].leader_eligible;
                }
            }
        }
    }

    // Overlay wheel events
    for (int i = 0; i < overlay->totalWheels; i++) {
        if (overlay->wheelEvents[i].right || overlay->wheelEvents[i].left) {
            if (i >= merged->totalWheels) {
                wheel* wh = realloc(merged->wheelEvents, (i + 1) * sizeof(wheel));
                if (wh) {
                    merged->wheelEvents = wh;
                    for (int j = merged->totalWheels; j <= i; j++) {
                        merged->wheelEvents[j].right = NULL;
                        merged->wheelEvents[j].left = NULL;
                        merged->wheelEvents[j].description = NULL;
                    }
                    merged->totalWheels = i + 1;
                }
            }
            if (i < merged->totalWheels) {
                if (overlay->wheelEvents[i].right) {
                    if (merged->wheelEvents[i].right) free(merged->wheelEvents[i].right);
                    merged->wheelEvents[i].right = strdup(overlay->wheelEvents[i].right);
                }
                if (overlay->wheelEvents[i].left) {
                    if (merged->wheelEvents[i].left) free(merged->wheelEvents[i].left);
                    merged->wheelEvents[i].left = strdup(overlay->wheelEvents[i].left);
                }
                if (overlay->wheelEvents[i].description) {
                    if (merged->wheelEvents[i].description) free(merged->wheelEvents[i].description);
                    merged->wheelEvents[i].description = strdup(overlay->wheelEvents[i].description);
                }
            }
        }
    }

    // Overlay descriptions
    for (int i = 0; i < 19; i++) {
        if (overlay->key_descriptions[i]) {
            if (merged->key_descriptions[i]) free(merged->key_descriptions[i]);
            merged->key_descriptions[i] = strdup(overlay->key_descriptions[i]);
        }
        if (overlay->leader_descriptions[i]) {
            if (merged->leader_descriptions[i]) free(merged->leader_descriptions[i]);
            merged->leader_descriptions[i] = strdup(overlay->leader_descriptions[i]);
        }
    }

    return merged;
}

// Helper: apply profile switch to OSD (update descriptions and show notification)
static void apply_profile_to_osd(profile_manager_t* manager, profile_t* profile) {
    if (manager->osd == NULL) return;

    osd_clear_descriptions(manager->osd);

    // Get the merged config descriptions, or fall back to profile descriptions
    config_t* active_cfg = profile_manager_get_config(manager);

    // Set key descriptions: prefer merged config, then profile, then default
    for (int i = 0; i < 19; i++) {
        const char* desc = NULL;
        if (active_cfg && active_cfg->key_descriptions[i]) {
            desc = active_cfg->key_descriptions[i];
        } else if (profile && profile->key_descriptions[i]) {
            desc = profile->key_descriptions[i];
        } else if (manager->default_config && manager->default_config->key_descriptions[i]) {
            desc = manager->default_config->key_descriptions[i];
        }
        if (desc) {
            osd_set_key_description(manager->osd, i, desc);
        }
    }

    // Set leader descriptions
    for (int i = 0; i < 19; i++) {
        const char* desc = NULL;
        if (active_cfg && active_cfg->leader_descriptions[i]) {
            desc = active_cfg->leader_descriptions[i];
        } else if (profile && profile->leader_descriptions[i]) {
            desc = profile->leader_descriptions[i];
        } else if (manager->default_config && manager->default_config->leader_descriptions[i]) {
            desc = manager->default_config->leader_descriptions[i];
        }
        if (desc) {
            osd_set_leader_description(manager->osd, i, desc);
        }
    }

    // Set wheel descriptions
    for (int i = 0; i < 32; i++) {
        const char* desc = NULL;
        if (profile && profile->wheel_descriptions[i]) {
            desc = profile->wheel_descriptions[i];
        } else if (active_cfg && i < active_cfg->totalWheels &&
                   active_cfg->wheelEvents[i].description) {
            desc = active_cfg->wheelEvents[i].description;
        }
        if (desc) {
            osd_set_wheel_description(manager->osd, i, desc);
        }
    }

    // Visual feedback: show "Profile: <name>" in OSD
    if (profile && profile->name) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Profile: %s", profile->name);
        osd_record_action(manager->osd, -1, msg);
    }
}

// ============================================================================
// Consistency checking
// ============================================================================

// Validate a profile's data for consistency
static int validate_profile(const char* filename, const char* name,
                            const char* pattern, int priority,
                            profile_manager_t* manager) {
    int errors = 0;

    if (name == NULL || strlen(name) == 0) {
        fprintf(stderr, "Error: %s: profile has no name\n", filename);
        errors++;
    }

    if (pattern == NULL || strlen(pattern) == 0) {
        fprintf(stderr, "Error: %s: profile '%s' has no pattern\n",
                filename, name ? name : "(unnamed)");
        errors++;
    }

    // Check for duplicate names
    if (name && manager) {
        for (int i = 0; i < manager->profile_count; i++) {
            if (manager->profiles[i].name && strcmp(manager->profiles[i].name, name) == 0) {
                fprintf(stderr, "Error: %s: duplicate profile name '%s' (already defined in %s)\n",
                        filename, name,
                        manager->profiles[i].source_file ? manager->profiles[i].source_file : "unknown");
                errors++;
            }
        }
    }

    // Check for duplicate patterns at same priority
    if (pattern && manager) {
        for (int i = 0; i < manager->profile_count; i++) {
            if (manager->profiles[i].window_pattern &&
                strcasecmp(manager->profiles[i].window_pattern, pattern) == 0 &&
                manager->profiles[i].priority == priority) {
                fprintf(stderr, "Warning: %s: profile '%s' has same pattern '%s' at priority %d as '%s'\n",
                        filename, name ? name : "(unnamed)", pattern, priority,
                        manager->profiles[i].name);
            }
        }
    }

    return errors;
}

// Validate a config loaded from a profile file
static void validate_config(const char* filename, const config_t* cfg) {
    if (cfg == NULL) return;

    for (int i = 0; i < cfg->totalButtons; i++) {
        if (i > 18) {
            fprintf(stderr, "Error: %s: Button %d is out of range (valid: 0-18)\n", filename, i);
            continue;
        }
        if (cfg->events[i].type < 0 || cfg->events[i].type > 2) {
            fprintf(stderr, "Error: %s: Button %d has invalid type %d (valid: 0, 1, 2)\n",
                    filename, i, cfg->events[i].type);
        }
        if (cfg->events[i].type >= 0 && cfg->events[i].function == NULL) {
            // Only warn if the button was explicitly defined (has non-default type)
            if (cfg->events[i].type != 0) {
                fprintf(stderr, "Warning: %s: Button %d has type %d but no function defined\n",
                        filename, i, cfg->events[i].type);
            }
        }
    }
}

// ============================================================================
// Profile manager lifecycle
// ============================================================================

profile_manager_t* profile_manager_create(config_t* default_config) {
    profile_manager_t* manager = calloc(1, sizeof(profile_manager_t));
    if (manager == NULL) return NULL;

    manager->default_config = default_config;
    manager->merged_config = NULL;
    manager->profile_count = 0;
    manager->active_profile_index = -1;
    manager->debug = 0;
    manager->window_tracker = NULL;
    manager->osd = NULL;
    manager->inotify_fd = -1;
    manager->inotify_wd = -1;
    manager->profiles_dir[0] = '\0';

    for (int i = 0; i < MAX_PROFILES; i++) {
        memset(&manager->profiles[i], 0, sizeof(profile_t));
    }

    return manager;
}

void profile_manager_destroy(profile_manager_t* manager) {
    if (manager == NULL) return;

    for (int i = 0; i < manager->profile_count; i++) {
        free_profile(&manager->profiles[i]);
    }

    if (manager->merged_config) {
        config_destroy(manager->merged_config);
    }

    profile_manager_watch_stop(manager);

    if (manager->window_tracker) {
        window_tracker_destroy(manager->window_tracker);
    }

    free(manager);
}

int profile_manager_init(profile_manager_t* manager, void* display, osd_state_t* osd) {
    if (manager == NULL) return -1;

    manager->osd = osd;

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

// ============================================================================
// Profile management
// ============================================================================

int profile_add(profile_manager_t* manager, const char* name, const char* window_pattern,
                const char* source_file, int priority) {
    if (manager == NULL || name == NULL || window_pattern == NULL) return -1;
    if (manager->profile_count >= MAX_PROFILES) return -1;

    // Check for duplicate name
    for (int i = 0; i < manager->profile_count; i++) {
        if (manager->profiles[i].name && strcmp(manager->profiles[i].name, name) == 0) {
            return -1;
        }
    }

    profile_t* profile = &manager->profiles[manager->profile_count];

    profile->name = strdup(name);
    profile->window_pattern = strdup(window_pattern);
    profile->source_file = source_file ? strdup(source_file) : NULL;
    profile->priority = priority;
    profile->config = NULL;
    profile->is_default = 0;

    for (int i = 0; i < 19; i++) {
        profile->key_descriptions[i] = NULL;
        profile->leader_descriptions[i] = NULL;
    }
    for (int i = 0; i < 32; i++) {
        profile->wheel_descriptions[i] = NULL;
    }

    manager->profile_count++;

    if (manager->debug) {
        printf("Profile added: '%s' (pattern: '%s', priority: %d, source: %s)\n",
               name, window_pattern, priority,
               source_file ? source_file : "(inline)");
    }

    return 0;
}

int profile_remove(profile_manager_t* manager, const char* name) {
    if (manager == NULL || name == NULL) return -1;

    for (int i = 0; i < manager->profile_count; i++) {
        if (manager->profiles[i].name && strcmp(manager->profiles[i].name, name) == 0) {
            free_profile(&manager->profiles[i]);

            for (int j = i; j < manager->profile_count - 1; j++) {
                manager->profiles[j] = manager->profiles[j + 1];
            }

            memset(&manager->profiles[manager->profile_count - 1], 0, sizeof(profile_t));
            manager->profile_count--;

            if (manager->active_profile_index == i) {
                manager->active_profile_index = -1;
            } else if (manager->active_profile_index > i) {
                manager->active_profile_index--;
            }

            return 0;
        }
    }

    return -1;
}

profile_t* profile_get(profile_manager_t* manager, const char* name) {
    if (manager == NULL || name == NULL) return NULL;
    for (int i = 0; i < manager->profile_count; i++) {
        if (manager->profiles[i].name && strcmp(manager->profiles[i].name, name) == 0) {
            return &manager->profiles[i];
        }
    }
    return NULL;
}

profile_t* profile_get_by_index(profile_manager_t* manager, int index) {
    if (manager == NULL || index < 0 || index >= manager->profile_count) return NULL;
    return &manager->profiles[index];
}

int profile_set_description(profile_manager_t* manager, const char* profile_name,
                            int button_index, const char* description) {
    if (manager == NULL || profile_name == NULL) return -1;
    if (button_index < 0 || button_index > 18) return -1;

    profile_t* profile = profile_get(manager, profile_name);
    if (profile == NULL) return -1;

    if (profile->key_descriptions[button_index])
        free(profile->key_descriptions[button_index]);
    profile->key_descriptions[button_index] = description ? strdup(description) : NULL;

    return 0;
}

const char* profile_get_description(profile_manager_t* manager, const char* profile_name,
                                     int button_index) {
    if (manager == NULL || profile_name == NULL) return NULL;
    if (button_index < 0 || button_index > 18) return NULL;

    profile_t* profile = profile_get(manager, profile_name);
    if (profile == NULL) return NULL;

    return profile->key_descriptions[button_index];
}

int profile_set_default(profile_manager_t* manager, const char* name) {
    if (manager == NULL || name == NULL) return -1;

    for (int i = 0; i < manager->profile_count; i++) {
        manager->profiles[i].is_default = 0;
    }

    profile_t* profile = profile_get(manager, name);
    if (profile) {
        profile->is_default = 1;
        return 0;
    }

    return -1;
}

// ============================================================================
// Profile switching
// ============================================================================

// Update profile manager - check active window, check inotify, switch if needed
int profile_manager_update(profile_manager_t* manager) {
    if (manager == NULL || manager->window_tracker == NULL) return -1;

    // Check for hot reload events (non-blocking)
    if (manager->inotify_fd >= 0) {
        profile_manager_check_reload(manager);
    }

    // Update window tracker
    int window_changed = window_tracker_update(manager->window_tracker);

    if (window_changed <= 0 && manager->active_profile_index >= 0) {
        return 0;
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
    // profile active (sticky behavior)
    if (best_index < 0) {
        best_index = default_index;
    }
    if (best_index < 0) {
        return 0;
    }

    if (best_index == manager->active_profile_index) {
        return 0;
    }

    // Switch to new profile
    int old_index = manager->active_profile_index;
    manager->active_profile_index = best_index;

    // Build merged config (default overlaid with profile overrides)
    if (manager->merged_config) {
        config_destroy(manager->merged_config);
        manager->merged_config = NULL;
    }

    profile_t* profile = &manager->profiles[best_index];
    manager->merged_config = config_merge(manager->default_config, profile->config);

    if (manager->debug) {
        printf("Profile switched: '%s'", profile->name);
        if (old_index >= 0) {
            printf(" (was '%s')", manager->profiles[old_index].name);
        }
        printf("\n");
    } else {
        // Always print profile switch (not just in debug mode)
        printf("Profile: %s\n", profile->name);
    }

    // Update OSD with new profile's descriptions and show notification
    apply_profile_to_osd(manager, profile);

    return 1;
}

profile_t* profile_manager_get_active(profile_manager_t* manager) {
    if (manager == NULL || manager->active_profile_index < 0) return NULL;
    return &manager->profiles[manager->active_profile_index];
}

config_t* profile_manager_get_config(profile_manager_t* manager) {
    if (manager == NULL) return NULL;

    // Return merged config if available
    if (manager->merged_config) {
        return manager->merged_config;
    }

    return manager->default_config;
}

int profile_manager_switch(profile_manager_t* manager, const char* name) {
    if (manager == NULL || name == NULL) return -1;

    for (int i = 0; i < manager->profile_count; i++) {
        if (manager->profiles[i].name && strcmp(manager->profiles[i].name, name) == 0) {
            return profile_manager_switch_by_index(manager, i);
        }
    }

    return -1;
}

int profile_manager_switch_by_index(profile_manager_t* manager, int index) {
    if (manager == NULL || index < 0 || index >= manager->profile_count) return -1;

    manager->active_profile_index = index;

    // Rebuild merged config
    if (manager->merged_config) {
        config_destroy(manager->merged_config);
        manager->merged_config = NULL;
    }

    profile_t* profile = &manager->profiles[index];
    manager->merged_config = config_merge(manager->default_config, profile->config);

    // Update OSD
    apply_profile_to_osd(manager, profile);

    return 0;
}

// ============================================================================
// Debug
// ============================================================================

void profile_manager_set_debug(profile_manager_t* manager, int level) {
    if (manager) manager->debug = level;
}

void profile_manager_print(const profile_manager_t* manager) {
    if (manager == NULL) return;

    printf("\n=== Profile Configuration ===\n");
    printf("Total profiles: %d\n", manager->profile_count);
    printf("Active profile: %d", manager->active_profile_index);
    if (manager->active_profile_index >= 0) {
        printf(" ('%s')", manager->profiles[manager->active_profile_index].name);
    }
    printf("\n");
    printf("Hot reload: %s\n", manager->inotify_fd >= 0 ? "active" : "inactive");
    if (manager->profiles_dir[0]) {
        printf("Profiles dir: %s\n", manager->profiles_dir);
    }
    printf("\n");

    for (int i = 0; i < manager->profile_count; i++) {
        const profile_t* p = &manager->profiles[i];
        printf("Profile %d: '%s'%s\n", i, p->name, p->is_default ? " [DEFAULT]" : "");
        printf("  Pattern:  '%s'\n", p->window_pattern);
        printf("  Priority: %d\n", p->priority);
        printf("  Source:   %s\n", p->source_file ? p->source_file : "(inline)");
        printf("  Config:   %s\n", p->config ? "overlay loaded" : "(default only)");

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

// ============================================================================
// Load from monolithic profiles.cfg (backward compatible)
// ============================================================================

int profile_manager_load(profile_manager_t* manager, const char* filename) {
    if (manager == NULL || filename == NULL) return -1;

    FILE* f = fopen(filename, "r");
    if (f == NULL) {
        char* home = getpwuid(getuid())->pw_dir;
        char temp[MAX_PROFILE_PATH];
        snprintf(temp, sizeof(temp), "%s/.config/KD100/%s", home, filename);
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
        line[strcspn(line, "\n")] = 0;

        if (strstr(line, "//") == line || strlen(line) == 0) continue;

        char* ptr = line;
        while (*ptr == ' ' || *ptr == '\t') ptr++;

        if (strncasecmp(ptr, "profile:", 8) == 0) {
            // Save previous profile
            if (current_profile && current_pattern) {
                if (validate_profile(filename, current_profile, current_pattern,
                                     current_priority, manager) == 0) {
                    if (profile_add(manager, current_profile, current_pattern,
                                   current_config, current_priority) == 0) {
                        if (current_is_default) {
                            profile_set_default(manager, current_profile);
                        }
                        // Load overlay config if specified
                        if (current_config) {
                            profile_t* p = profile_get(manager, current_profile);
                            if (p) {
                                p->config = config_create();
                                if (p->config) {
                                    if (config_load(p->config, current_config, manager->debug) < 0) {
                                        if (manager->debug) {
                                            printf("Profile '%s': Failed to load config '%s'\n",
                                                   current_profile, current_config);
                                        }
                                        config_destroy(p->config);
                                        p->config = NULL;
                                    } else {
                                        validate_config(current_config, p->config);
                                    }
                                }
                            }
                        }
                        for (int i = 0; i < 19; i++) {
                            if (current_descriptions[i]) {
                                profile_set_description(manager, current_profile, i, current_descriptions[i]);
                                free(current_descriptions[i]);
                                current_descriptions[i] = NULL;
                            }
                        }
                    }
                } else {
                    for (int i = 0; i < 19; i++) {
                        if (current_descriptions[i]) { free(current_descriptions[i]); current_descriptions[i] = NULL; }
                    }
                }
            }

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

        if (strncasecmp(ptr, "pattern:", 8) == 0 && current_profile) {
            char* value = ptr + 8;
            while (*value == ' ') value++;
            if (current_pattern) free(current_pattern);
            current_pattern = strdup(value);
            continue;
        }

        if (strncasecmp(ptr, "config:", 7) == 0 && current_profile) {
            char* value = ptr + 7;
            while (*value == ' ') value++;
            if (current_config) free(current_config);
            current_config = strdup(value);
            continue;
        }

        if (strncasecmp(ptr, "priority:", 9) == 0 && current_profile) {
            char* value = ptr + 9;
            while (*value == ' ') value++;
            current_priority = atoi(value);
            continue;
        }

        if (strncasecmp(ptr, "default:", 8) == 0 && current_profile) {
            char* value = ptr + 8;
            while (*value == ' ') value++;
            current_is_default = (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0);
            continue;
        }

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
                } else {
                    fprintf(stderr, "Error: %s: description_%d is out of range (valid: 0-18)\n",
                            filename, btn);
                }
            }
            continue;
        }
    }

    // Save last profile
    if (current_profile && current_pattern) {
        if (validate_profile(filename, current_profile, current_pattern,
                             current_priority, manager) == 0) {
            if (profile_add(manager, current_profile, current_pattern,
                           current_config, current_priority) == 0) {
                if (current_is_default) {
                    profile_set_default(manager, current_profile);
                }
                if (current_config) {
                    profile_t* p = profile_get(manager, current_profile);
                    if (p) {
                        p->config = config_create();
                        if (p->config) {
                            if (config_load(p->config, current_config, manager->debug) < 0) {
                                config_destroy(p->config);
                                p->config = NULL;
                            } else {
                                validate_config(current_config, p->config);
                            }
                        }
                    }
                }
                for (int i = 0; i < 19; i++) {
                    if (current_descriptions[i]) {
                        profile_set_description(manager, current_profile, i, current_descriptions[i]);
                        free(current_descriptions[i]);
                    }
                }
            }
        } else {
            for (int i = 0; i < 19; i++) {
                if (current_descriptions[i]) free(current_descriptions[i]);
            }
        }
    }

    if (current_profile) free(current_profile);
    if (current_pattern) free(current_pattern);
    if (current_config) free(current_config);

    fclose(f);
    return 0;
}

// ============================================================================
// Load from apps.profiles.d/ directory
// ============================================================================

// Load a single profile from a .cfg file in apps.profiles.d/
// File format:
//   name: Krita
//   pattern: krita*
//   priority: 10
//   default: false
//   Button 0
//   type: 0
//   function: b
//   description_0: Brush (B)
//   leader_description_0: Shift+Brush (B)
//   wheel_description_0: Brush Size
//   Wheel
//   function: bracketright
//   ...
static int load_profile_file(profile_manager_t* manager, const char* filepath) {
    FILE* f = fopen(filepath, "r");
    if (f == NULL) {
        fprintf(stderr, "Error: Cannot open profile file: %s\n", filepath);
        return -1;
    }

    // Extract filename for logging
    const char* basename = strrchr(filepath, '/');
    basename = basename ? basename + 1 : filepath;

    char line[512];
    char* name = NULL;
    char* pattern = NULL;
    int priority = 0;
    int is_default = 0;
    char* descriptions[19] = {NULL};
    char* leader_descs[19] = {NULL};
    char* wheel_descs[32] = {NULL};

    // First pass: extract profile metadata (name, pattern, priority, descriptions)
    // and also load the config (buttons, wheel, etc.) using config_load
    while (fgets(line, sizeof(line), f) != NULL) {
        line[strcspn(line, "\n")] = 0;
        if (strstr(line, "//") == line || strlen(line) == 0) continue;

        char* ptr = line;
        while (*ptr == ' ' || *ptr == '\t') ptr++;

        if (strncasecmp(ptr, "name:", 5) == 0) {
            char* value = ptr + 5;
            while (*value == ' ') value++;
            strip_comment(value);
            if (name) free(name);
            name = strdup(value);
            continue;
        }

        if (strncasecmp(ptr, "pattern:", 8) == 0) {
            char* value = ptr + 8;
            while (*value == ' ') value++;
            strip_comment(value);
            if (pattern) free(pattern);
            pattern = strdup(value);
            continue;
        }

        if (strncasecmp(ptr, "priority:", 9) == 0) {
            char* value = ptr + 9;
            while (*value == ' ') value++;
            priority = atoi(value);
            continue;
        }

        if (strncasecmp(ptr, "default:", 8) == 0) {
            char* value = ptr + 8;
            while (*value == ' ') value++;
            is_default = (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0);
            continue;
        }

        if (strncasecmp(ptr, "description_", 12) == 0) {
            char* num_str = ptr + 12;
            char* colon = strchr(num_str, ':');
            if (colon) {
                *colon = '\0';
                int btn = atoi(num_str);
                if (btn >= 0 && btn <= 18) {
                    char* value = colon + 1;
                    while (*value == ' ') value++;
                    if (descriptions[btn]) free(descriptions[btn]);
                    descriptions[btn] = sanitize_desc(value);
                } else {
                    fprintf(stderr, "Error: %s: description_%d is out of range (valid: 0-18)\n",
                            basename, btn);
                }
            }
            continue;
        }

        if (strncasecmp(ptr, "leader_description_", 19) == 0) {
            char* num_str = ptr + 19;
            char* colon = strchr(num_str, ':');
            if (colon) {
                *colon = '\0';
                int btn = atoi(num_str);
                if (btn >= 0 && btn <= 18) {
                    char* value = colon + 1;
                    while (*value == ' ') value++;
                    if (leader_descs[btn]) free(leader_descs[btn]);
                    leader_descs[btn] = sanitize_desc(value);
                }
            }
            continue;
        }

        if (strncasecmp(ptr, "wheel_description_", 18) == 0) {
            char* num_str = ptr + 18;
            char* colon = strchr(num_str, ':');
            if (colon) {
                *colon = '\0';
                int idx = atoi(num_str);
                if (idx >= 0 && idx < 32) {
                    char* value = colon + 1;
                    while (*value == ' ') value++;
                    if (wheel_descs[idx]) free(wheel_descs[idx]);
                    wheel_descs[idx] = sanitize_desc(value);
                }
            }
            continue;
        }
    }

    fclose(f);

    // Validate
    if (validate_profile(basename, name, pattern, priority, manager) != 0) {
        if (name) free(name);
        if (pattern) free(pattern);
        for (int i = 0; i < 19; i++) { if (descriptions[i]) free(descriptions[i]); if (leader_descs[i]) free(leader_descs[i]); }
        for (int i = 0; i < 32; i++) { if (wheel_descs[i]) free(wheel_descs[i]); }
        return -1;
    }

    // Add profile
    if (profile_add(manager, name, pattern, filepath, priority) != 0) {
        fprintf(stderr, "Error: %s: failed to add profile '%s'\n", basename, name);
        if (name) free(name);
        if (pattern) free(pattern);
        for (int i = 0; i < 19; i++) { if (descriptions[i]) free(descriptions[i]); if (leader_descs[i]) free(leader_descs[i]); }
        for (int i = 0; i < 32; i++) { if (wheel_descs[i]) free(wheel_descs[i]); }
        return -1;
    }

    if (is_default) {
        profile_set_default(manager, name);
    }

    profile_t* p = profile_get(manager, name);
    if (p == NULL) {
        if (name) free(name);
        if (pattern) free(pattern);
        for (int i = 0; i < 19; i++) { if (descriptions[i]) free(descriptions[i]); if (leader_descs[i]) free(leader_descs[i]); }
        for (int i = 0; i < 32; i++) { if (wheel_descs[i]) free(wheel_descs[i]); }
        return -1;
    }

    // Load overlay config from the same file (reuses config_load which parses
    // Button/Wheel/function lines)
    p->config = config_create();
    if (p->config) {
        if (config_load(p->config, filepath, manager->debug) < 0) {
            // No button/wheel overrides in this file - that's fine
            config_destroy(p->config);
            p->config = NULL;
        } else {
            validate_config(basename, p->config);
        }
    }

    // Copy descriptions to profile
    for (int i = 0; i < 19; i++) {
        p->key_descriptions[i] = descriptions[i];   // Transfer ownership
        p->leader_descriptions[i] = leader_descs[i];
    }
    for (int i = 0; i < 32; i++) {
        p->wheel_descriptions[i] = wheel_descs[i];
    }

    if (name) free(name);
    if (pattern) free(pattern);

    return 0;
}

int profile_manager_load_dir(profile_manager_t* manager, const char* dirpath) {
    if (manager == NULL || dirpath == NULL) return -1;

    // Resolve the directory path
    char resolved[MAX_PROFILE_PATH];
    DIR* dir = opendir(dirpath);

    if (dir == NULL) {
        // Try relative to ~/.config/KD100/
        char* home = getpwuid(getuid())->pw_dir;
        snprintf(resolved, sizeof(resolved), "%s/.config/KD100/%s", home, dirpath);
        dir = opendir(resolved);
        if (dir == NULL) {
            fprintf(stderr, "Profiles: Cannot open directory: %s\n", dirpath);
            return -1;
        }
    } else {
        strncpy(resolved, dirpath, sizeof(resolved) - 1);
        resolved[sizeof(resolved) - 1] = '\0';
    }

    // Store resolved path for inotify
    strncpy(manager->profiles_dir, resolved, MAX_PROFILE_PATH - 1);
    manager->profiles_dir[MAX_PROFILE_PATH - 1] = '\0';

    int loaded = 0;
    int failed = 0;
    struct dirent* entry;

    while ((entry = readdir(dir)) != NULL) {
        // Only process .cfg files
        size_t namelen = strlen(entry->d_name);
        if (namelen < 5) continue;
        if (strcasecmp(entry->d_name + namelen - 4, ".cfg") != 0) continue;

        // Build full path (dir + "/" + name, both bounded)
        char filepath[MAX_PROFILE_PATH + 256 + 2];
        snprintf(filepath, sizeof(filepath), "%s/%s", resolved, entry->d_name);

        // Check it's a regular file
        struct stat st;
        if (stat(filepath, &st) != 0 || !S_ISREG(st.st_mode)) continue;

        if (load_profile_file(manager, filepath) == 0) {
            loaded++;
        } else {
            failed++;
        }
    }

    closedir(dir);

    printf("Profiles: Loaded %d profile(s) from %s", loaded, resolved);
    if (failed > 0) {
        printf(" (%d failed)", failed);
    }
    printf("\n");

    return loaded > 0 ? 0 : -1;
}

// ============================================================================
// Hot reload via inotify
// ============================================================================

int profile_manager_watch_start(profile_manager_t* manager, const char* dirpath) {
    if (manager == NULL || dirpath == NULL) return -1;

    // Use stored resolved path if available
    const char* watch_path = manager->profiles_dir[0] ? manager->profiles_dir : dirpath;

    manager->inotify_fd = inotify_init1(IN_NONBLOCK);
    if (manager->inotify_fd < 0) {
        fprintf(stderr, "Profiles: Failed to initialize inotify: %s\n", strerror(errno));
        return -1;
    }

    manager->inotify_wd = inotify_add_watch(manager->inotify_fd, watch_path,
                                              IN_CLOSE_WRITE | IN_CREATE | IN_DELETE |
                                              IN_MOVED_TO | IN_MOVED_FROM);
    if (manager->inotify_wd < 0) {
        fprintf(stderr, "Profiles: Failed to watch directory %s: %s\n",
                watch_path, strerror(errno));
        close(manager->inotify_fd);
        manager->inotify_fd = -1;
        return -1;
    }

    printf("Profiles: Hot reload active on %s\n", watch_path);
    return 0;
}

void profile_manager_watch_stop(profile_manager_t* manager) {
    if (manager == NULL) return;

    if (manager->inotify_wd >= 0 && manager->inotify_fd >= 0) {
        inotify_rm_watch(manager->inotify_fd, manager->inotify_wd);
        manager->inotify_wd = -1;
    }
    if (manager->inotify_fd >= 0) {
        close(manager->inotify_fd);
        manager->inotify_fd = -1;
    }
}

int profile_manager_check_reload(profile_manager_t* manager) {
    if (manager == NULL || manager->inotify_fd < 0) return -1;

    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    int reloaded = 0;

    ssize_t len = read(manager->inotify_fd, buf, sizeof(buf));
    if (len <= 0) return 0;  // No events (EAGAIN for non-blocking)

    char* ptr = buf;
    while (ptr < buf + len) {
        struct inotify_event* ev = (struct inotify_event*)ptr;

        if (ev->len > 0 && ev->name[0] != '.') {
            // Only process .cfg files
            size_t namelen = strlen(ev->name);
            if (namelen >= 5 && strcasecmp(ev->name + namelen - 4, ".cfg") == 0) {
                char filepath[MAX_PROFILE_PATH + 256 + 2];
                snprintf(filepath, sizeof(filepath), "%s/%s", manager->profiles_dir, ev->name);

                if (ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE)) {
                    // File created or modified: reload
                    printf("Refreshing configuration for profile %s\n", ev->name);

                    // Find existing profile from this file
                    int found = -1;
                    for (int i = 0; i < manager->profile_count; i++) {
                        if (manager->profiles[i].source_file &&
                            strcmp(manager->profiles[i].source_file, filepath) == 0) {
                            found = i;
                            break;
                        }
                    }

                    if (found >= 0) {
                        // Remove old profile
                        int was_active = (found == manager->active_profile_index);
                        char* old_name = manager->profiles[found].name ?
                                         strdup(manager->profiles[found].name) : NULL;
                        profile_remove(manager, manager->profiles[found].name);

                        // Reload from file
                        if (load_profile_file(manager, filepath) == 0) {
                            reloaded++;
                            if (was_active) {
                                // Re-activate the profile by name if it still exists
                                if (old_name) {
                                    profile_manager_switch(manager, old_name);
                                }
                            }
                        } else {
                            fprintf(stderr, "Error: Failed to reload %s (profile removed)\n", ev->name);
                        }
                        if (old_name) free(old_name);
                    } else {
                        // New file: load as new profile
                        printf("Loading new profile from %s\n", ev->name);
                        if (load_profile_file(manager, filepath) == 0) {
                            reloaded++;
                        }
                    }
                } else if (ev->mask & (IN_DELETE | IN_MOVED_FROM)) {
                    // File deleted: remove profile
                    printf("Removing profile from deleted file %s\n", ev->name);
                    for (int i = 0; i < manager->profile_count; i++) {
                        if (manager->profiles[i].source_file &&
                            strcmp(manager->profiles[i].source_file, filepath) == 0) {
                            profile_remove(manager, manager->profiles[i].name);
                            reloaded++;
                            break;
                        }
                    }
                }
            }
        }

        ptr += sizeof(struct inotify_event) + ev->len;
    }

    return reloaded;
}
