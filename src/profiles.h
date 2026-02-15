#ifndef PROFILES_H
#define PROFILES_H

#include "config.h"
#include "window.h"
#include "osd.h"

// Maximum profiles supported
#define MAX_PROFILES 32

// ============================================================================
// PROFILE SYSTEM ARCHITECTURE
// ============================================================================
//
// See docs/PROFILES_DESIGN.md for the full design rationale.
//
// Current behavior (v1.7.1):
//   - Profiles loaded from a single profiles.cfg file
//   - Each profile can optionally reference a full config_file, which does a
//     FULL config replacement (keys, wheel, leader, OSD -- everything)
//   - Without a config_file, a profile can only change description labels
//   - No hot reload: configs loaded once at startup
//   - No visual feedback on profile switch (debug log only)
//   - When no profile matches, falls back to default profile (pattern: *)
//     or to default_config if no default profile exists
//
// Planned behavior (see PROFILES_DESIGN.md):
//   - Per-app config files in apps.profiles.d/ directory (one .cfg per app)
//   - Overlay semantics: profile overrides ONLY explicitly set parameters
//     (keys, wheel functions, descriptions) on top of default.cfg
//   - Leader, OSD, wheel_mode, and hardware settings are NOT per-profile
//   - Sticky profiles: switching to a window without a profile keeps the
//     current profile active (unless a default catch-all profile exists)
//   - OSD notification on profile switch ("Profile: Krita")
//   - Hot reload via inotify on the profiles directory
//   - Consistency checking on load (bounds, duplicates, type validation)
//
// ============================================================================

// Profile definition
typedef struct {
    char* name;                    // Profile name (shown in OSD on switch)
    char* window_pattern;          // Window title/class pattern (e.g., "krita*", "*photoshop*")
    int priority;                  // Higher priority matched first (default: 0)
    char* config_file;             // Config file for this profile (NULL = use main config)
                                   // NOTE: currently does full config replacement; planned
                                   // change is overlay-only (keys, wheel, descriptions)
    config_t* config;              // Loaded configuration (or NULL to use default)
    char* key_descriptions[19];    // Per-button descriptions for this profile
    int is_default;                // Is this the default/fallback profile?
                                   // NOTE: if no default profile is defined, switching to
                                   // an unmatched window keeps the current profile active
} profile_t;

// Profile manager state
typedef struct {
    profile_t profiles[MAX_PROFILES];
    int profile_count;
    int active_profile_index;      // Currently active profile (-1 = none/default)
    config_t* default_config;      // Default configuration (fallback when no profile matches
                                   // and no default profile is defined)
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
