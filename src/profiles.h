#ifndef PROFILES_H
#define PROFILES_H

#include "config.h"
#include "window.h"
#include "osd.h"

// Maximum profiles supported
#define MAX_PROFILES 32

// Profile definition
typedef struct {
    char* name;                    // Profile name
    char* window_pattern;          // Window title/class pattern (e.g., "krita*", "*photoshop*")
    int priority;                  // Higher priority matched first (default: 0)
    char* config_file;             // Config file for this profile (NULL = use main config)
    config_t* config;              // Loaded configuration (or NULL to use default)
    char* key_descriptions[19];    // Per-button descriptions for this profile
    int is_default;                // Is this the default/fallback profile?
} profile_t;

// Profile manager state
typedef struct {
    profile_t profiles[MAX_PROFILES];
    int profile_count;
    int active_profile_index;      // Currently active profile (-1 = none)
    config_t* default_config;      // Default configuration (fallback)
    window_tracker_t* window_tracker;
    osd_state_t* osd;              // Reference to OSD for updating descriptions
    int debug;                     // Debug output level
} profile_manager_t;

// Lifecycle functions
profile_manager_t* profile_manager_create(config_t* default_config);
void profile_manager_destroy(profile_manager_t* manager);

// Initialize with shared X11 display
int profile_manager_init(profile_manager_t* manager, void* display, osd_state_t* osd);

// Profile management
int profile_add(profile_manager_t* manager, const char* name, const char* window_pattern,
                const char* config_file, int priority);
int profile_remove(profile_manager_t* manager, const char* name);
profile_t* profile_get(profile_manager_t* manager, const char* name);
profile_t* profile_get_by_index(profile_manager_t* manager, int index);

// Key descriptions for a profile
int profile_set_description(profile_manager_t* manager, const char* profile_name,
                            int button_index, const char* description);
const char* profile_get_description(profile_manager_t* manager, const char* profile_name,
                                     int button_index);

// Set default profile
int profile_set_default(profile_manager_t* manager, const char* name);

// Update function - check active window and switch profile if needed
// Returns 1 if profile changed, 0 if same, -1 on error
int profile_manager_update(profile_manager_t* manager);

// Get current active profile
profile_t* profile_manager_get_active(profile_manager_t* manager);

// Get current active config (either from active profile or default)
config_t* profile_manager_get_config(profile_manager_t* manager);

// Manual profile switching
int profile_manager_switch(profile_manager_t* manager, const char* name);
int profile_manager_switch_by_index(profile_manager_t* manager, int index);

// Debug
void profile_manager_set_debug(profile_manager_t* manager, int level);
void profile_manager_print(const profile_manager_t* manager);

// Load profiles from config file
// Format in config:
//   profile: <name>
//   pattern: <window_pattern>
//   config: <config_file>  (optional)
//   priority: <number>     (optional, default 0)
//   description_0: <desc>  (optional)
//   description_1: <desc>
//   ...
int profile_manager_load(profile_manager_t* manager, const char* filename);

#endif // PROFILES_H
