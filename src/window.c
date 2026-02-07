#include "window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

// Helper: lowercase string copy
static char* str_tolower(const char* str) {
    if (str == NULL) return NULL;

    char* lower = strdup(str);
    if (lower == NULL) return NULL;

    for (char* p = lower; *p; p++) {
        *p = tolower((unsigned char)*p);
    }
    return lower;
}

// Helper: free window info contents
static void free_window_info(window_info_t* info) {
    if (info->title) { free(info->title); info->title = NULL; }
    if (info->class_name) { free(info->class_name); info->class_name = NULL; }
    if (info->instance_name) { free(info->instance_name); info->instance_name = NULL; }
    info->window_id = 0;
}

// Create window tracker
window_tracker_t* window_tracker_create(void) {
    window_tracker_t* tracker = calloc(1, sizeof(window_tracker_t));
    if (tracker == NULL) return NULL;

    tracker->initialized = 0;
    tracker->current.title = NULL;
    tracker->current.class_name = NULL;
    tracker->current.instance_name = NULL;
    tracker->current.window_id = 0;

    return tracker;
}

// Destroy window tracker
void window_tracker_destroy(window_tracker_t* tracker) {
    if (tracker == NULL) return;

    free_window_info(&tracker->current);

    // Only close display if we created it (display != NULL and initialized by us)
    // We'll track this by not closing if it was passed in externally
    // For safety, we won't close it - the caller should manage the display

    free(tracker);
}

// Initialize tracker
int window_tracker_init(window_tracker_t* tracker, void* existing_display) {
    if (tracker == NULL) return -1;

    if (existing_display) {
        tracker->display = existing_display;
    } else {
        tracker->display = XOpenDisplay(NULL);
        if (tracker->display == NULL) {
            fprintf(stderr, "Window tracker: Cannot open X display\n");
            return -1;
        }
    }

    tracker->initialized = 1;
    return 0;
}

// Get active window
static Window get_active_window(Display* dpy) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char* data = NULL;
    Window active_window = None;

    Atom net_active_window = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);

    if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), net_active_window,
                           0, 1, False, XA_WINDOW,
                           &actual_type, &actual_format, &nitems, &bytes_after, &data) == Success) {
        if (data && nitems > 0) {
            active_window = *(Window*)data;
            XFree(data);
        }
    }

    return active_window;
}

// Get window title
static char* get_window_title(Display* dpy, Window win) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char* data = NULL;
    char* title = NULL;

    // Try _NET_WM_NAME first (UTF-8)
    Atom net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", False);
    Atom utf8_string = XInternAtom(dpy, "UTF8_STRING", False);

    if (XGetWindowProperty(dpy, win, net_wm_name, 0, 1024, False, utf8_string,
                           &actual_type, &actual_format, &nitems, &bytes_after, &data) == Success) {
        if (data && nitems > 0) {
            title = strdup((char*)data);
            XFree(data);
            return title;
        }
        if (data) XFree(data);
    }

    // Fall back to WM_NAME
    XTextProperty text_prop;
    if (XGetWMName(dpy, win, &text_prop) && text_prop.value) {
        title = strdup((char*)text_prop.value);
        XFree(text_prop.value);
    }

    return title;
}

// Get window class
static void get_window_class(Display* dpy, Window win, char** class_name, char** instance_name) {
    XClassHint class_hint;

    *class_name = NULL;
    *instance_name = NULL;

    if (XGetClassHint(dpy, win, &class_hint)) {
        if (class_hint.res_class) {
            *class_name = strdup(class_hint.res_class);
            XFree(class_hint.res_class);
        }
        if (class_hint.res_name) {
            *instance_name = strdup(class_hint.res_name);
            XFree(class_hint.res_name);
        }
    }
}

// Update window tracker
int window_tracker_update(window_tracker_t* tracker) {
    if (tracker == NULL || !tracker->initialized || tracker->display == NULL) return -1;

    Display* dpy = (Display*)tracker->display;
    Window active = get_active_window(dpy);

    if (active == None) {
        // No active window
        if (tracker->current.window_id != 0) {
            free_window_info(&tracker->current);
            return 1;  // Changed (to nothing)
        }
        return 0;  // Same (still nothing)
    }

    // Check if window changed
    if (active == tracker->current.window_id) {
        return 0;  // Same window
    }

    // Window changed - get new info
    free_window_info(&tracker->current);

    tracker->current.window_id = active;
    tracker->current.title = get_window_title(dpy, active);
    get_window_class(dpy, active, &tracker->current.class_name, &tracker->current.instance_name);

    return 1;  // Changed
}

// Get current window info
const window_info_t* window_tracker_get_current(const window_tracker_t* tracker) {
    if (tracker == NULL) return NULL;
    return &tracker->current;
}

// Wildcard pattern matching (case-insensitive)
// Supports * (any sequence) and ? (single char)
int window_match_pattern(const char* pattern, const char* text) {
    if (pattern == NULL || text == NULL) return 0;

    // Convert both to lowercase for case-insensitive matching
    char* pattern_lower = str_tolower(pattern);
    char* text_lower = str_tolower(text);

    if (pattern_lower == NULL || text_lower == NULL) {
        if (pattern_lower) free(pattern_lower);
        if (text_lower) free(text_lower);
        return 0;
    }

    const char* p = pattern_lower;
    const char* t = text_lower;
    const char* star_p = NULL;
    const char* star_t = NULL;

    while (*t) {
        if (*p == '?' || *p == *t) {
            // Match single character
            p++;
            t++;
        } else if (*p == '*') {
            // Remember position for backtracking
            star_p = p++;
            star_t = t;
        } else if (star_p) {
            // Backtrack: try matching one more character with *
            p = star_p + 1;
            t = ++star_t;
        } else {
            // No match
            free(pattern_lower);
            free(text_lower);
            return 0;
        }
    }

    // Skip trailing *
    while (*p == '*') p++;

    int result = (*p == '\0');

    free(pattern_lower);
    free(text_lower);

    return result;
}

// Match window against pattern
// Tries to match against title, class_name, and instance_name
int window_matches(const window_info_t* window, const char* pattern) {
    if (window == NULL || pattern == NULL) return 0;

    // Try matching title
    if (window->title && window_match_pattern(pattern, window->title)) {
        return 1;
    }

    // Try matching class name
    if (window->class_name && window_match_pattern(pattern, window->class_name)) {
        return 1;
    }

    // Try matching instance name
    if (window->instance_name && window_match_pattern(pattern, window->instance_name)) {
        return 1;
    }

    return 0;
}
