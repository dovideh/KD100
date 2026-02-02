#ifndef WINDOW_H
#define WINDOW_H

// Active window information
typedef struct {
    char* title;           // Window title (WM_NAME or _NET_WM_NAME)
    char* class_name;      // Window class (WM_CLASS)
    char* instance_name;   // Window instance name
    unsigned long window_id; // X11 window ID
} window_info_t;

// Window tracking state
typedef struct {
    void* display;         // X11 Display*
    window_info_t current; // Current active window
    int initialized;       // Is the tracker initialized
} window_tracker_t;

// Lifecycle functions
window_tracker_t* window_tracker_create(void);
void window_tracker_destroy(window_tracker_t* tracker);

// Initialize with existing display or create new one
int window_tracker_init(window_tracker_t* tracker, void* existing_display);

// Get current active window info (updates internal state)
// Returns 1 if window changed, 0 if same, -1 on error
int window_tracker_update(window_tracker_t* tracker);

// Get current window info (without updating)
const window_info_t* window_tracker_get_current(const window_tracker_t* tracker);

// Wildcard matching function
// Pattern supports:
//   * - matches any sequence of characters
//   ? - matches any single character
// Case-insensitive by default
int window_match_pattern(const char* pattern, const char* text);

// Match against window title or class
int window_matches(const window_info_t* window, const char* pattern);

#endif // WINDOW_H
