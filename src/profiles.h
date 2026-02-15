#ifndef PROFILES_H
#define PROFILES_H

#include "config.h"
#include "window.h"
#include "osd.h"

// Maximum profiles supported
#define MAX_PROFILES 32

// Maximum path length for profile directory/files
#define MAX_PROFILE_PATH 1024

// ============================================================================
// PROFILE SYSTEM ARCHITECTURE
// ============================================================================
//
// See docs/PROFILES_DESIGN.md for the full design rationale.
//
// Features:
//   - Per-app config files in apps.profiles.d/ directory (one .cfg per app)
//   - Backward-compatible: also supports monolithic profiles.cfg
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
    char* source_file;             // Source .cfg file this profile was loaded from
    config_t* config;              // Overlay configuration (merged with default on switch)
    char* key_descriptions[19];    // Per-button descriptions for this profile
    char* leader_descriptions[19]; // Per-button leader descriptions for this profile
    char* wheel_descriptions[32];  // Per-wheel-function descriptions
    int is_default;                // Is this the default/fallback profile?
                                   // If no default profile is defined, switching to
                                   // an unmatched window keeps the current profile active
} profile_t;

// Profile manager state
typedef struct {
    profile_t profiles[MAX_PROFILES];
    int profile_count;
    int active_profile_index;      // Currently active profile (-1 = none/default)
    config_t* default_config;      // Default configuration (fallback when no profile matches
                                   // and no default profile is defined)
    config_t* merged_config;       // Merged config (default overlaid with active profile)
    window_tracker_t* window_tracker;
    osd_state_t* osd;              // Reference to OSD for updating descriptions
    int debug;                     // Debug output level

    // Hot reload via inotify
    int inotify_fd;                // inotify file descriptor (-1 if not active)
    int inotify_wd;                // inotify watch descriptor
    char profiles_dir[MAX_PROFILE_PATH]; // Watched directory path
} profile_manager_t;

// Lifecycle functions
profile_manager_t* profile_manager_create(config_t* default_config);
void profile_manager_destroy(profile_manager_t* manager);

// Initialize with shared X11 display
int profile_manager_init(profile_manager_t* manager, void* display, osd_state_t* osd);

// Profile management
int profile_add(profile_manager_t* manager, const char* name, const char* window_pattern,
                const char* source_file, int priority);
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
// Also checks inotify for hot reload events
// Returns 1 if profile changed, 0 if same, -1 on error
int profile_manager_update(profile_manager_t* manager);

// Get current active profile
profile_t* profile_manager_get_active(profile_manager_t* manager);

// Get current active config (merged overlay of default + active profile)
config_t* profile_manager_get_config(profile_manager_t* manager);

// Manual profile switching
int profile_manager_switch(profile_manager_t* manager, const char* name);
int profile_manager_switch_by_index(profile_manager_t* manager, int index);

// Debug
void profile_manager_set_debug(profile_manager_t* manager, int level);
void profile_manager_print(const profile_manager_t* manager);

// Load profiles from monolithic config file (backward compatible)
int profile_manager_load(profile_manager_t* manager, const char* filename);

// Load profiles from apps.profiles.d/ directory (one .cfg per app)
// Each file contains: name, pattern, priority, button overrides, descriptions
int profile_manager_load_dir(profile_manager_t* manager, const char* dirpath);

// Start inotify watcher on the profiles directory for hot reload
int profile_manager_watch_start(profile_manager_t* manager, const char* dirpath);

// Stop inotify watcher
void profile_manager_watch_stop(profile_manager_t* manager);

// Check for inotify events and reload changed profiles (non-blocking)
// Returns number of profiles reloaded, 0 if none, -1 on error
int profile_manager_check_reload(profile_manager_t* manager);

#endif // PROFILES_H
