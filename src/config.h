#ifndef CONFIG_H
#define CONFIG_H

#include "leader.h"

// Wheel mode enumeration
typedef enum {
    WHEEL_MODE_SEQUENTIAL,  // Single-click cycles through all functions (default/legacy)
    WHEEL_MODE_SETS         // Multi-click for set-based navigation
} wheel_mode_t;

// Wheel event structure
typedef struct {
    char* right;
    char* left;
} wheel;

// OSD configuration
typedef struct {
    int enabled;              // Is OSD enabled
    int start_visible;        // Show OSD on startup
    int pos_x;                // Initial X position
    int pos_y;                // Initial Y position
    float opacity;            // Background opacity (0.0-1.0, default 0.67 = 33% transparent)
    int display_duration_ms;  // How long to show key actions (default 3000)
    int min_width;            // Minimal mode width
    int min_height;           // Minimal mode height
    int expanded_width;       // Expanded mode width
    int expanded_height;      // Expanded mode height
    int osd_toggle_button;    // Button to toggle OSD (-1 = disabled)
} osd_config_t;

// Profile configuration (for referencing profile files)
typedef struct {
    char* profiles_file;      // Path to profiles configuration file
    int auto_switch;          // Enable automatic profile switching
    int check_interval_ms;    // How often to check active window (default 500ms)
} profile_config_t;

// Configuration structure
typedef struct {
    event* events;
    int totalButtons;
    wheel* wheelEvents;
    int totalWheels;
    leader_state leader;
    int enable_uclogic;
    int wheel_click_timeout_ms;  // Multi-click detection timeout (20-990ms)
    wheel_mode_t wheel_mode;     // Wheel toggle mode (sequential or sets)
    osd_config_t osd;            // OSD settings
    profile_config_t profile;    // Profile settings
    char* key_descriptions[19];  // Per-button descriptions (for default profile)
} config_t;

// Configuration functions
config_t* config_create(void);
void config_destroy(config_t* config);
int config_load(config_t* config, const char* filename, int debug);
void config_print(const config_t* config, int debug);

#endif // CONFIG_H
