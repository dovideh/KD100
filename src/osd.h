#ifndef OSD_H
#define OSD_H

#include "config.h"

// OSD display modes
typedef enum {
    OSD_MODE_HIDDEN,      // OSD not visible
    OSD_MODE_MINIMAL,     // Small box showing recent commands
    OSD_MODE_EXPANDED     // Full keyboard layout view
} osd_mode_t;

// Recent key action for display
typedef struct {
    int button_index;     // Button index (0-18) for looking up description
    char* key_name;       // Display name of the key
    char* action;         // What action was performed
    long timestamp_ms;    // When it was pressed
} recent_action_t;

// Wheel state information for OSD display
typedef struct {
    int current_set;              // Current active set (0, 1, 2)
    int position_in_set;          // Position within set (0 or 1)
    int wheel_function;           // Current wheel function index
    int wheel_mode;               // 0 = sequential, 1 = sets
    int total_wheels;             // Total number of wheel functions
    char* descriptions[32];       // Descriptions for each wheel function
    char* last_wheel_action;      // Last aggregated wheel action description
    long last_wheel_time_ms;      // Timestamp of last wheel event
    int wheel_action_count;       // Count of repeated wheel actions (for aggregation)
} osd_wheel_state_t;

// OSD state structure
typedef struct {
    osd_mode_t mode;              // Current display mode
    int enabled;                  // Is OSD feature enabled
    int pos_x;                    // Window X position
    int pos_y;                    // Window Y position
    int width;                    // Current width
    int height;                   // Current height
    int min_width;                // Minimal mode width
    int min_height;               // Minimal mode height
    int expanded_width;           // Expanded mode width
    int expanded_height;          // Expanded mode height
    float opacity;                // Background opacity (0.0-1.0)
    int display_duration_ms;      // How long to show key actions
    int font_size;                // Font size in pixels (default 13)
    int auto_show;                // Auto-show OSD on key press
    long last_action_time_ms;     // Time of last action (for auto-hide)

    // Recent actions buffer (circular)
    recent_action_t recent_actions[10];
    int recent_count;
    int recent_head;

    // X11 handles (void* to avoid X11 header in .h)
    void* display;
    void* window;
    void* gc;
    void* visual;
    void* font;                   // Current font
    int screen;
    int colormap;

    // Drag state
    int dragging;
    int drag_start_x;
    int drag_start_y;
    int cursor_inside;            // Is cursor hovering over window

    // Configuration reference for key names
    config_t* config;

    // Key descriptions for the current profile
    char* key_descriptions[19];    // Descriptions for buttons 0-18
    char* leader_descriptions[19]; // Descriptions when leader is active

    // Active button highlighting
    int active_button;            // Currently pressed button (-1 = none)
    long active_button_time_ms;   // When the button was pressed

    // Leader state feedback
    int leader_active;            // Is leader key currently active
    int leader_button;            // Which button is the leader

    // Wheel state
    osd_wheel_state_t wheel;
} osd_state_t;

// OSD lifecycle functions
osd_state_t* osd_create(config_t* config);
void osd_destroy(osd_state_t* osd);

// OSD control functions
int osd_init_display(osd_state_t* osd);
void osd_show(osd_state_t* osd);
void osd_hide(osd_state_t* osd);
void osd_toggle_mode(osd_state_t* osd);  // Toggle between minimal and expanded
void osd_set_mode(osd_state_t* osd, osd_mode_t mode);

// OSD update functions
void osd_record_action(osd_state_t* osd, int button_index, const char* action);
void osd_record_wheel_action(osd_state_t* osd, const char* direction, const char* description);
void osd_update(osd_state_t* osd);       // Process X11 events and redraw if needed
void osd_redraw(osd_state_t* osd);       // Force redraw

// OSD position functions
void osd_set_position(osd_state_t* osd, int x, int y);
void osd_move(osd_state_t* osd, int dx, int dy);

// Key description functions
void osd_set_key_description(osd_state_t* osd, int button_index, const char* description);
void osd_set_leader_description(osd_state_t* osd, int button_index, const char* description);
const char* osd_get_key_description(osd_state_t* osd, int button_index);
void osd_clear_descriptions(osd_state_t* osd);

// Configuration
void osd_set_opacity(osd_state_t* osd, float opacity);
void osd_set_display_duration(osd_state_t* osd, int duration_ms);

// Wheel state functions
void osd_set_wheel_state(osd_state_t* osd, int current_set, int position_in_set,
                          int wheel_function, int wheel_mode, int total_wheels);
void osd_set_wheel_description(osd_state_t* osd, int index, const char* description);

// Active button and leader state
void osd_set_active_button(osd_state_t* osd, int button_index);
void osd_set_leader_state(osd_state_t* osd, int active, int leader_button);

#endif // OSD_H
