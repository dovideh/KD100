#include "osd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/shape.h>

// Button layout for the KD100 (matches the physical layout)
// Row 0: Button 18 (wheel toggle) spans the top
// Rows 1-4: Main button grid
static const int BUTTON_LAYOUT[5][4] = {
    {18, -1, -1, -1},   // Top row: wheel button (spans width)
    { 0,  1,  2,  3},   // Row 1
    { 4,  5,  6,  7},   // Row 2
    { 8,  9, 10, 11},   // Row 3
    {12, 13, 14, 15}    // Row 4 (16 and 17 are special - bottom left)
};

// Button names for display
static const char* BUTTON_NAMES[] = {
    "B0", "B1", "B2", "B3",
    "B4", "B5", "B6", "B7",
    "B8", "B9", "B10", "B11",
    "B12", "B13", "B14", "B15",
    "B16", "B17", "WHEEL"
};

// Helper: get current time in milliseconds (osd-local version)
static long osd_get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// Helper: find ARGB visual for transparency
static Visual* find_argb_visual(Display* display, int screen, int* depth) {
    XVisualInfo vinfo_template;
    XVisualInfo* vinfo_list;
    int num_visuals;

    vinfo_template.screen = screen;
    vinfo_template.depth = 32;
    vinfo_template.class = TrueColor;

    vinfo_list = XGetVisualInfo(display, VisualScreenMask | VisualDepthMask | VisualClassMask,
                                 &vinfo_template, &num_visuals);

    if (vinfo_list != NULL && num_visuals > 0) {
        for (int i = 0; i < num_visuals; i++) {
            XRenderPictFormat* format = XRenderFindVisualFormat(display, vinfo_list[i].visual);
            if (format && format->type == PictTypeDirect && format->direct.alphaMask) {
                Visual* visual = vinfo_list[i].visual;
                *depth = 32;
                XFree(vinfo_list);
                return visual;
            }
        }
        XFree(vinfo_list);
    }

    // Fallback to default visual
    *depth = DefaultDepth(display, screen);
    return DefaultVisual(display, screen);
}

// Create OSD state
osd_state_t* osd_create(config_t* config) {
    osd_state_t* osd = calloc(1, sizeof(osd_state_t));
    if (osd == NULL) return NULL;

    osd->mode = OSD_MODE_HIDDEN;
    osd->enabled = 0;
    osd->pos_x = 50;
    osd->pos_y = 50;
    osd->min_width = 200;
    osd->min_height = 100;
    osd->expanded_width = 375;
    osd->expanded_height = 380;
    osd->width = osd->min_width;
    osd->height = osd->min_height;
    osd->opacity = 0.67f;  // 33% transparent = 67% opaque
    osd->display_duration_ms = 3000;  // Show actions for 3 seconds
    osd->font_size = 13;  // Default font size
    osd->auto_show = 1;  // Auto-show on key press by default
    osd->last_action_time_ms = 0;
    osd->recent_count = 0;
    osd->recent_head = 0;
    osd->config = config;
    osd->dragging = 0;
    osd->cursor_inside = 0;
    osd->font = NULL;

    // Initialize key descriptions to NULL
    for (int i = 0; i < 19; i++) {
        osd->key_descriptions[i] = NULL;
    }

    // Initialize active button state
    osd->active_button = -1;
    osd->active_button_time_ms = 0;

    // Initialize leader state
    osd->leader_active = 0;
    osd->leader_button = -1;

    // Initialize wheel state
    osd->wheel.current_set = 0;
    osd->wheel.position_in_set = 0;
    osd->wheel.wheel_function = 0;
    osd->wheel.wheel_mode = 0;
    osd->wheel.total_wheels = 0;
    osd->wheel.last_wheel_action = NULL;
    osd->wheel.last_wheel_time_ms = 0;
    osd->wheel.wheel_action_count = 0;
    for (int i = 0; i < 32; i++) {
        osd->wheel.descriptions[i] = NULL;
    }

    return osd;
}

// Destroy OSD state
void osd_destroy(osd_state_t* osd) {
    if (osd == NULL) return;

    // Free recent actions
    for (int i = 0; i < 10; i++) {
        if (osd->recent_actions[i].key_name) free(osd->recent_actions[i].key_name);
        if (osd->recent_actions[i].action) free(osd->recent_actions[i].action);
    }

    // Free key descriptions
    for (int i = 0; i < 19; i++) {
        if (osd->key_descriptions[i]) free(osd->key_descriptions[i]);
    }

    // Free wheel descriptions
    for (int i = 0; i < 32; i++) {
        if (osd->wheel.descriptions[i]) free(osd->wheel.descriptions[i]);
    }
    if (osd->wheel.last_wheel_action) free(osd->wheel.last_wheel_action);

    // Close X11 resources
    if (osd->display) {
        Display* dpy = (Display*)osd->display;
        if (osd->font) {
            XFreeFont(dpy, (XFontStruct*)osd->font);
        }
        if (osd->window) {
            XDestroyWindow(dpy, (Window)osd->window);
        }
        if (osd->gc) {
            XFreeGC(dpy, (GC)osd->gc);
        }
        XCloseDisplay(dpy);
    }

    free(osd);
}

// Initialize X11 display
int osd_init_display(osd_state_t* osd) {
    if (osd == NULL) return -1;

    Display* dpy = XOpenDisplay(NULL);
    if (dpy == NULL) {
        fprintf(stderr, "OSD: Cannot open X display\n");
        return -1;
    }

    osd->display = dpy;
    osd->screen = DefaultScreen(dpy);

    // Find ARGB visual for transparency
    int depth;
    Visual* visual = find_argb_visual(dpy, osd->screen, &depth);
    osd->visual = visual;

    // Create colormap for the visual
    Colormap colormap = XCreateColormap(dpy, RootWindow(dpy, osd->screen), visual, AllocNone);
    osd->colormap = colormap;

    // Window attributes for transparency
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;  // No window manager decorations
    attrs.colormap = colormap;
    attrs.background_pixel = 0;  // Transparent background
    attrs.border_pixel = 0;
    attrs.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
                       PointerMotionMask | ButtonMotionMask | StructureNotifyMask |
                       EnterWindowMask | LeaveWindowMask;

    // Create the window
    Window win = XCreateWindow(dpy, RootWindow(dpy, osd->screen),
                                osd->pos_x, osd->pos_y,
                                osd->width, osd->height,
                                0, depth, InputOutput, visual,
                                CWOverrideRedirect | CWColormap | CWBackPixel |
                                CWBorderPixel | CWEventMask,
                                &attrs);

    osd->window = (void*)win;

    // Set window type to overlay/utility
    Atom wm_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    Atom wm_type_utility = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    XChangeProperty(dpy, win, wm_type, XA_ATOM, 32, PropModeReplace,
                    (unsigned char*)&wm_type_utility, 1);

    // Set window to stay on top
    Atom wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom wm_state_above = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);
    Atom wm_state_sticky = XInternAtom(dpy, "_NET_WM_STATE_STICKY", False);
    Atom states[2] = {wm_state_above, wm_state_sticky};
    XChangeProperty(dpy, win, wm_state, XA_ATOM, 32, PropModeReplace,
                    (unsigned char*)states, 2);

    // Make window click-through for most of the area (will update on redraw)
    // Skip input for the main area, but allow dragging from edges

    // Create graphics context
    GC gc = XCreateGC(dpy, win, 0, NULL);
    osd->gc = gc;

    // Load font based on font_size
    char font_pattern[128];
    snprintf(font_pattern, sizeof(font_pattern),
             "-misc-fixed-medium-r-*-*-%d-*-*-*-*-*-*-*", osd->font_size);
    XFontStruct* font = XLoadQueryFont(dpy, font_pattern);
    if (font == NULL) {
        // Try alternative pattern
        snprintf(font_pattern, sizeof(font_pattern),
                 "-*-fixed-medium-r-*-*-%d-*-*-*-*-*-*-*", osd->font_size);
        font = XLoadQueryFont(dpy, font_pattern);
    }
    if (font == NULL) {
        // Fallback to default fixed font
        font = XLoadQueryFont(dpy, "fixed");
    }
    if (font != NULL) {
        osd->font = font;
        XSetFont(dpy, gc, font->fid);
    }

    // Set window name
    XStoreName(dpy, win, "KD100 OSD");

    printf("OSD: Display initialized (ARGB visual: %s, font size: %d)\n",
           depth == 32 ? "yes" : "no", osd->font_size);
    osd->enabled = 1;

    return 0;
}

// Show OSD window
void osd_show(osd_state_t* osd) {
    if (osd == NULL || osd->display == NULL) return;

    Display* dpy = (Display*)osd->display;
    Window win = (Window)osd->window;

    XMapWindow(dpy, win);
    XRaiseWindow(dpy, win);
    XFlush(dpy);

    if (osd->mode == OSD_MODE_HIDDEN) {
        osd->mode = OSD_MODE_MINIMAL;
    }

    osd_redraw(osd);
}

// Hide OSD window
void osd_hide(osd_state_t* osd) {
    if (osd == NULL || osd->display == NULL) return;

    Display* dpy = (Display*)osd->display;
    Window win = (Window)osd->window;

    XUnmapWindow(dpy, win);
    XFlush(dpy);

    osd->mode = OSD_MODE_HIDDEN;
}

// Toggle between minimal and expanded modes
void osd_toggle_mode(osd_state_t* osd) {
    if (osd == NULL) return;

    switch (osd->mode) {
        case OSD_MODE_HIDDEN:
            osd_set_mode(osd, OSD_MODE_MINIMAL);
            break;
        case OSD_MODE_MINIMAL:
            osd_set_mode(osd, OSD_MODE_EXPANDED);
            break;
        case OSD_MODE_EXPANDED:
            osd_set_mode(osd, OSD_MODE_MINIMAL);
            break;
    }
}

// Set specific mode
void osd_set_mode(osd_state_t* osd, osd_mode_t mode) {
    if (osd == NULL || osd->display == NULL) return;

    Display* dpy = (Display*)osd->display;
    Window win = (Window)osd->window;

    osd->mode = mode;

    if (mode == OSD_MODE_HIDDEN) {
        osd_hide(osd);
        return;
    }

    // Calculate scale factor based on font size (13 is the default/base)
    float scale = osd->font_size / 13.0f;
    int padding = (int)(10 * scale);
    int line_height = osd->font_size + (int)(3 * scale);
    int title_height = osd->font_size + (int)(12 * scale);

    // Update window size based on mode (dynamically calculated from font size)
    if (mode == OSD_MODE_MINIMAL) {
        // Minimal: title + mode/set line + (optional leader line) + function pair line
        //        + "Recent Actions:" header + 3 actions + padding
        osd->width = (int)(260 * scale);
        osd->height = title_height + padding
                     + line_height + (int)(3 * scale)   // mode + set indicator line
                     + line_height + (int)(3 * scale)   // leader line (always reserve space)
                     + line_height + (int)(3 * scale)   // function pair line
                     + line_height + (int)(5 * scale)   // "Recent Actions:" header
                     + (3 * line_height)                 // 3 action lines
                     + padding;
    } else {
        // Expanded: calculate based on grid dimensions
        int key_width = (int)(85 * scale);
        int key_height = (int)(45 * scale);
        int grid_padding = (int)(5 * scale);
        int grid_width = 4 * key_width + 3 * grid_padding;
        // Height: title + mode/set line + leader line + function pair + grid + history + padding
        int wheel_height = key_height - (int)(10 * scale);
        int grid_height = wheel_height + 5 * (key_height + grid_padding) + grid_padding;
        int history_height = line_height + (int)(5 * scale) + 3 * line_height;  // header + 3 actions
        osd->width = grid_width + 2 * padding;
        osd->height = title_height + padding
                     + line_height + (int)(3 * scale)   // mode + set indicator line
                     + line_height + (int)(3 * scale)   // leader line (always reserve space)
                     + line_height + (int)(8 * scale)   // function pair line
                     + grid_height
                     + history_height + padding;
    }

    XResizeWindow(dpy, win, osd->width, osd->height);
    XMapWindow(dpy, win);
    XRaiseWindow(dpy, win);
    XFlush(dpy);

    osd_redraw(osd);
}

// Record a key action
void osd_record_action(osd_state_t* osd, int button_index, const char* action) {
    if (osd == NULL || button_index < 0 || button_index > 18) return;

    // Get next slot in circular buffer
    int slot = osd->recent_head;
    osd->recent_head = (osd->recent_head + 1) % 10;
    if (osd->recent_count < 10) osd->recent_count++;

    // Free old data
    if (osd->recent_actions[slot].key_name) free(osd->recent_actions[slot].key_name);
    if (osd->recent_actions[slot].action) free(osd->recent_actions[slot].action);

    // Store new action
    long now = osd_get_time_ms();
    osd->recent_actions[slot].button_index = button_index;
    osd->recent_actions[slot].key_name = strdup(BUTTON_NAMES[button_index]);
    osd->recent_actions[slot].action = action ? strdup(action) : strdup("(unknown)");
    osd->recent_actions[slot].timestamp_ms = now;
    osd->last_action_time_ms = now;

    // Track active button for highlighting
    osd->active_button = button_index;
    osd->active_button_time_ms = now;

    // Auto-show OSD if hidden and auto_show is enabled
    if (osd->mode == OSD_MODE_HIDDEN && osd->auto_show && osd->display != NULL) {
        osd_set_mode(osd, OSD_MODE_MINIMAL);
    } else if (osd->mode != OSD_MODE_HIDDEN) {
        osd_redraw(osd);
    }
}

// Helper: draw a single button key in the grid with optional highlighting
static void draw_button_key(Display* dpy, Window win, GC gc, osd_state_t* osd,
                             int btn, int x, int y, int w, int h, float scale) {
    int text_offset_x = (int)(5 * scale);
    int text_offset_y1 = (int)(15 * scale);
    int text_offset_y2 = (int)(35 * scale);
    unsigned long fg_color = ((unsigned long)255 << 24) | 0xFFFFFF;
    unsigned long accent_color = ((unsigned long)255 << 24) | 0x4488FF;
    unsigned long highlight_color = ((unsigned long)200 << 24) | 0x444444;
    (void)accent_color; // used below conditionally

    long now = osd_get_time_ms();
    int is_active = (osd->active_button == btn && (now - osd->active_button_time_ms) < 500);
    int is_leader = (osd->leader_active && btn == osd->leader_button);
    int is_leader_modified = (osd->leader_active && btn != osd->leader_button &&
                              osd->config && btn < osd->config->totalButtons &&
                              osd->config->events[btn].leader_eligible == 1);

    // Choose background color based on state
    unsigned long bg;
    if (is_active) {
        bg = ((unsigned long)255 << 24) | 0x44AA44;  // Green for active press
    } else if (is_leader) {
        bg = ((unsigned long)255 << 24) | 0xAA6622;  // Orange for active leader
    } else if (is_leader_modified) {
        bg = ((unsigned long)220 << 24) | 0x3A3A5A;  // Subtle purple for leader-eligible
    } else {
        bg = highlight_color;
    }

    XSetForeground(dpy, gc, bg);
    XFillRectangle(dpy, win, gc, x, y, w, h);

    // Draw border
    XSetForeground(dpy, gc, is_active ? (((unsigned long)255 << 24) | 0x88FF88) : fg_color);
    XDrawRectangle(dpy, win, gc, x, y, w - 1, h - 1);

    // Draw button number
    XSetForeground(dpy, gc, fg_color);
    char num[16];
    snprintf(num, sizeof(num), "%d", btn);
    XDrawString(dpy, win, gc, x + text_offset_x, y + text_offset_y1, num, strlen(num));

    // Draw description or function
    const char* desc = NULL;
    if (osd->key_descriptions[btn]) {
        desc = osd->key_descriptions[btn];
    } else if (osd->config && btn < osd->config->totalButtons &&
               osd->config->events[btn].function) {
        desc = osd->config->events[btn].function;
    }

    if (desc && h >= (int)(40 * scale)) {
        // Truncate if too long for the key width
        int max_chars = (w - 2 * text_offset_x) / (osd->font_size / 2);
        if (max_chars < 3) max_chars = 3;
        char truncated[32];
        if ((int)strlen(desc) > max_chars) {
            int copy_len = max_chars - 2;
            if (copy_len < 1) copy_len = 1;
            strncpy(truncated, desc, copy_len);
            truncated[copy_len] = '.';
            truncated[copy_len + 1] = '.';
            truncated[copy_len + 2] = '\0';
            desc = truncated;
        }
        XSetForeground(dpy, gc, accent_color);
        XDrawString(dpy, win, gc, x + text_offset_x, y + text_offset_y2, desc, strlen(desc));
    }
}

// Helper: draw wheel set indicator boxes
static void draw_wheel_set_indicator(Display* dpy, Window win, GC gc, osd_state_t* osd,
                                      int x, int y, float scale) {
    int box_w = (int)(30 * scale);
    int box_h = (int)(18 * scale);
    int box_gap = (int)(8 * scale);
    unsigned long fg_color = ((unsigned long)255 << 24) | 0xFFFFFF;
    unsigned long active_color = ((unsigned long)255 << 24) | 0x44AA44;  // Green
    unsigned long inactive_color = ((unsigned long)150 << 24) | 0x555555;

    int num_sets = (osd->wheel.total_wheels + 1) / 2;
    if (num_sets < 1) num_sets = 1;
    if (num_sets > 3) num_sets = 3;

    for (int i = 0; i < num_sets; i++) {
        int bx = x + i * (box_w + box_gap);
        int is_active = (i == osd->wheel.current_set);

        XSetForeground(dpy, gc, is_active ? active_color : inactive_color);
        XFillRectangle(dpy, win, gc, bx, y, box_w, box_h);
        XSetForeground(dpy, gc, fg_color);
        XDrawRectangle(dpy, win, gc, bx, y, box_w - 1, box_h - 1);

        char label[16];
        snprintf(label, sizeof(label), "%d", i + 1);
        XDrawString(dpy, win, gc, bx + (int)(11 * scale), y + (int)(13 * scale), label, strlen(label));
    }
}

// Helper: draw recent actions list (used in both minimal and expanded modes)
static int draw_recent_actions(Display* dpy, Window win, GC gc, osd_state_t* osd,
                                int x, int y, int max_items, float scale) {
    int line_height = osd->font_size + (int)(3 * scale);
    unsigned long highlight_color = ((unsigned long)200 << 24) | 0x444444;
    long now = osd_get_time_ms();
    int shown = 0;
    int y_off = y;

    for (int i = 0; i < osd->recent_count && shown < max_items; i++) {
        int idx = (osd->recent_head - 1 - i + 10) % 10;
        recent_action_t* action = &osd->recent_actions[idx];

        long age = now - action->timestamp_ms;
        if (age > osd->display_duration_ms && osd->display_duration_ms > 0) continue;

        char line[128];
        const char* desc = osd->key_descriptions[action->button_index];
        if (desc && strlen(desc) > 0) {
            snprintf(line, sizeof(line), "%s - %s (%s)", action->key_name, desc, action->action);
        } else {
            snprintf(line, sizeof(line), "%s - (%s)", action->key_name, action->action);
        }

        // Fade out effect
        if (age < osd->display_duration_ms) {
            float fade = 1.0f - ((float)age / osd->display_duration_ms * 0.5f);
            unsigned char text_alpha = (unsigned char)(255 * fade);
            unsigned long text_color = ((unsigned long)text_alpha << 24) | 0xFFFFFF;
            XSetForeground(dpy, gc, text_color);
        }

        XDrawString(dpy, win, gc, x, y_off, line, strlen(line));
        y_off += line_height;
        shown++;
    }

    if (shown == 0) {
        XSetForeground(dpy, gc, highlight_color);
        const char* empty_msg = "(no recent actions)";
        XDrawString(dpy, win, gc, x, y_off, empty_msg, strlen(empty_msg));
        y_off += line_height;
    }

    return y_off;
}

// Redraw the OSD
void osd_redraw(osd_state_t* osd) {
    if (osd == NULL || osd->display == NULL || osd->mode == OSD_MODE_HIDDEN) return;

    Display* dpy = (Display*)osd->display;
    Window win = (Window)osd->window;
    GC gc = (GC)osd->gc;

    // Calculate alpha value (0-255)
    unsigned char alpha = (unsigned char)(osd->opacity * 255);

    // Scale factor based on font size (13 is the default/base)
    float scale = osd->font_size / 13.0f;
    int padding = (int)(10 * scale);
    int line_height = osd->font_size + (int)(3 * scale);
    int title_height = osd->font_size + (int)(12 * scale);

    // Create colors with alpha
    unsigned long bg_color = ((unsigned long)alpha << 24) | 0x202020;
    unsigned long fg_color = ((unsigned long)255 << 24) | 0xFFFFFF;
    unsigned long accent_color = ((unsigned long)255 << 24) | 0x4488FF;
    unsigned long highlight_color = ((unsigned long)200 << 24) | 0x444444;
    unsigned long dim_color = ((unsigned long)180 << 24) | 0xAAAAAA;

    // Clear active button highlight after timeout
    long now = osd_get_time_ms();
    if (osd->active_button >= 0 && (now - osd->active_button_time_ms) > 500) {
        osd->active_button = -1;
    }

    // Clear window
    XSetForeground(dpy, gc, bg_color);
    XFillRectangle(dpy, win, gc, 0, 0, osd->width, osd->height);

    // Draw border
    XSetForeground(dpy, gc, accent_color);
    XDrawRectangle(dpy, win, gc, 0, 0, osd->width - 1, osd->height - 1);

    // Use stored font
    XFontStruct* font = (XFontStruct*)osd->font;
    if (font) {
        XSetFont(dpy, gc, font->fid);
    }

    // Draw title bar
    XSetForeground(dpy, gc, accent_color);
    XFillRectangle(dpy, win, gc, 0, 0, osd->width, title_height);

    XSetForeground(dpy, gc, fg_color);
    const char* title = (osd->mode == OSD_MODE_MINIMAL) ? "KD100 [+] click to expand" : "KD100 [-] click to collapse";
    XDrawString(dpy, win, gc, padding, title_height - (int)(5 * scale), title, strlen(title));

    int y_offset = title_height + padding;

    // ========== COMMON: Mode line + wheel set indicator + leader + function pair ==========
    // This block is shared between minimal and expanded to keep wheel info at top

    // -- Line 1: Mode + wheel set boxes (no leader text here) --
    {
        char mode_text[64];
        const char* mode_str = osd->wheel.wheel_mode ? "Sets" : "Sequential";
        snprintf(mode_text, sizeof(mode_text), "Mode: %s", mode_str);
        XSetForeground(dpy, gc, dim_color);
        XDrawString(dpy, win, gc, padding, y_offset, mode_text, strlen(mode_text));

        // Draw wheel set boxes inline after the mode text
        int text_w = (int)(strlen(mode_text) * osd->font_size * 0.6f) + (int)(10 * scale);
        if (osd->wheel.wheel_mode == 1) {
            draw_wheel_set_indicator(dpy, win, gc, osd, padding + text_w, y_offset - (int)(12 * scale), scale);
        } else {
            // Sequential mode: draw grayed-out set boxes
            int box_w = (int)(30 * scale);
            int box_h = (int)(18 * scale);
            int box_gap = (int)(8 * scale);
            unsigned long grayed = ((unsigned long)100 << 24) | 0x444444;
            int num_sets = (osd->wheel.total_wheels + 1) / 2;
            if (num_sets < 1) num_sets = 1;
            if (num_sets > 3) num_sets = 3;
            for (int i = 0; i < num_sets; i++) {
                int bx = padding + text_w + i * (box_w + box_gap);
                int by = y_offset - (int)(12 * scale);
                XSetForeground(dpy, gc, grayed);
                XFillRectangle(dpy, win, gc, bx, by, box_w, box_h);
                XSetForeground(dpy, gc, ((unsigned long)120 << 24) | 0x666666);
                XDrawRectangle(dpy, win, gc, bx, by, box_w - 1, box_h - 1);
                char label[16];
                snprintf(label, sizeof(label), "%d", i + 1);
                XSetForeground(dpy, gc, ((unsigned long)120 << 24) | 0x888888);
                XDrawString(dpy, win, gc, bx + (int)(11 * scale), by + (int)(13 * scale), label, strlen(label));
            }
        }
        y_offset += line_height + (int)(3 * scale);
    }

    // -- Line 2: Leader state (always occupies a row for stable layout) --
    {
        if (osd->leader_active) {
            unsigned long leader_on_color = ((unsigned long)255 << 24) | 0xEEAA33;
            XSetForeground(dpy, gc, leader_on_color);
            const char* leader_text = "Leader: ON";
            XDrawString(dpy, win, gc, padding, y_offset, leader_text, strlen(leader_text));
        } else {
            XSetForeground(dpy, gc, ((unsigned long)120 << 24) | 0x666666);
            const char* leader_text = "Leader: OFF";
            XDrawString(dpy, win, gc, padding, y_offset, leader_text, strlen(leader_text));
        }
        y_offset += line_height + (int)(3 * scale);
    }

    // -- Function pair line: show which function is active in current set --
    {
        int pair_idx = osd->wheel.current_set * 2;
        int pos = osd->wheel.position_in_set;

        if (osd->wheel.wheel_mode == 1) {
            // Sets mode: show both functions in the pair with > on active
            const char* desc_a = (pair_idx < 32) ? osd->wheel.descriptions[pair_idx] : NULL;
            const char* desc_b = (pair_idx + 1 < 32) ? osd->wheel.descriptions[pair_idx + 1] : NULL;
            char fn_a[48], fn_b[48];
            snprintf(fn_a, sizeof(fn_a), "%s", desc_a ? desc_a : "Fn 0");
            snprintf(fn_b, sizeof(fn_b), "%s", desc_b ? desc_b : "Fn 1");

            char pair_line[128];
            snprintf(pair_line, sizeof(pair_line), "Set %d:  %s%s  |  %s%s",
                     osd->wheel.current_set + 1,
                     pos == 0 ? "> " : "  ", fn_a,
                     pos == 1 ? "> " : "  ", fn_b);
            XSetForeground(dpy, gc, accent_color);
            XDrawString(dpy, win, gc, padding, y_offset, pair_line, strlen(pair_line));
        } else {
            // Sequential mode: show current wheel function
            int func_idx = osd->wheel.wheel_function;
            const char* func_desc = (func_idx >= 0 && func_idx < 32) ?
                                     osd->wheel.descriptions[func_idx] : NULL;
            char seq_line[128];
            if (func_desc) {
                snprintf(seq_line, sizeof(seq_line), "Wheel: %s (Fn %d/%d)",
                         func_desc, func_idx + 1, osd->wheel.total_wheels);
            } else {
                snprintf(seq_line, sizeof(seq_line), "Wheel: Fn %d/%d",
                         func_idx + 1, osd->wheel.total_wheels);
            }
            XSetForeground(dpy, gc, accent_color);
            XDrawString(dpy, win, gc, padding, y_offset, seq_line, strlen(seq_line));
        }
        y_offset += line_height + (int)(3 * scale);
    }

    if (osd->mode == OSD_MODE_MINIMAL) {
        // ========== MINIMAL MODE ==========

        // -- Recent Actions --
        XSetForeground(dpy, gc, fg_color);
        XDrawString(dpy, win, gc, padding, y_offset, "Recent Actions:", 15);
        y_offset += line_height + (int)(5 * scale);

        draw_recent_actions(dpy, win, gc, osd, padding, y_offset, 3, scale);

    } else {
        // ========== EXPANDED MODE ==========

        // -- Keyboard grid --
        int key_width = (int)(85 * scale);
        int key_height = (int)(45 * scale);
        int grid_padding = (int)(5 * scale);
        int start_x = padding;
        int start_y = y_offset + (int)(5 * scale);
        int grid_width = 4 * key_width + 3 * grid_padding;
        int wheel_height = key_height - (int)(10 * scale);

        // Draw wheel button (top)
        int wheel_y = start_y;
        int wheel_active = (osd->active_button == 18 && (now - osd->active_button_time_ms) < 500);
        unsigned long wheel_bg = wheel_active ?
            (((unsigned long)255 << 24) | 0x44AA44) : highlight_color;
        XSetForeground(dpy, gc, wheel_bg);
        XFillRectangle(dpy, win, gc, start_x, wheel_y, grid_width, wheel_height);
        XSetForeground(dpy, gc, wheel_active ? (((unsigned long)255 << 24) | 0x88FF88) : fg_color);
        XDrawRectangle(dpy, win, gc, start_x, wheel_y, grid_width - 1, wheel_height - 1);

        const char* wheel_desc = osd->key_descriptions[18] ? osd->key_descriptions[18] : "Wheel Toggle";
        XSetForeground(dpy, gc, fg_color);
        XDrawString(dpy, win, gc, start_x + (int)(5 * scale), wheel_y + (int)(22 * scale),
                    wheel_desc, strlen(wheel_desc));

        start_y += key_height;

        // Draw main button grid (rows 1-4, skipping button 15)
        for (int row = 1; row < 5; row++) {
            for (int col = 0; col < 4; col++) {
                int btn = BUTTON_LAYOUT[row][col];
                if (btn < 0 || btn == 15) continue;

                int x = start_x + col * (key_width + grid_padding);
                int y = start_y + (row - 1) * (key_height + grid_padding);

                draw_button_key(dpy, win, gc, osd, btn, x, y, key_width, key_height, scale);
            }
        }

        // Button 15 (tall)
        int btn15_x = start_x + 3 * (key_width + grid_padding);
        int btn15_y = start_y + 3 * (key_height + grid_padding);
        int btn15_height = 2 * key_height + grid_padding;
        draw_button_key(dpy, win, gc, osd, 15, btn15_x, btn15_y, key_width, btn15_height, scale);

        // Button 16 (wide)
        int bottom_y = start_y + 4 * (key_height + grid_padding);
        draw_button_key(dpy, win, gc, osd, 16, start_x, bottom_y,
                        key_width * 2 + grid_padding, key_height, scale);

        // Button 17
        int x17 = start_x + 2 * (key_width + grid_padding);
        draw_button_key(dpy, win, gc, osd, 17, x17, bottom_y, key_width, key_height, scale);

        y_offset = bottom_y + key_height + (int)(10 * scale);

        // -- Recent Actions (3 items in expanded view too) --
        XSetForeground(dpy, gc, fg_color);
        XDrawString(dpy, win, gc, padding, y_offset, "Recent Actions:", 15);
        y_offset += line_height + (int)(3 * scale);

        draw_recent_actions(dpy, win, gc, osd, padding, y_offset, 3, scale);
    }

    XFlush(dpy);
}

// Process X11 events
void osd_update(osd_state_t* osd) {
    if (osd == NULL || osd->display == NULL) return;

    Display* dpy = (Display*)osd->display;
    Window win = (Window)osd->window;
    long now = osd_get_time_ms();

    // Process X11 events first (to update cursor_inside before auto-hide check)
    if (osd->mode != OSD_MODE_HIDDEN) {
        while (XPending(dpy)) {
            XEvent event;
            XNextEvent(dpy, &event);

            switch (event.type) {
                case Expose:
                    if (event.xexpose.count == 0) {
                        osd_redraw(osd);
                    }
                    break;

                case ButtonPress:
                    if (event.xbutton.button == Button1) {
                        // Start dragging from anywhere on the window
                        osd->dragging = 1;
                        osd->drag_start_x = event.xbutton.x_root - osd->pos_x;
                        osd->drag_start_y = event.xbutton.y_root - osd->pos_y;
                    }
                    break;

                case ButtonRelease:
                    if (event.xbutton.button == Button1) {
                        if (osd->dragging) {
                            // If barely moved and clicked in title bar, toggle mode
                            int dx = event.xbutton.x_root - osd->pos_x - osd->drag_start_x;
                            int dy = event.xbutton.y_root - osd->pos_y - osd->drag_start_y;
                            float scale = osd->font_size / 13.0f;
                            int title_height = osd->font_size + (int)(12 * scale);
                            if (abs(dx) < 5 && abs(dy) < 5 && event.xbutton.y < title_height) {
                                osd_toggle_mode(osd);
                            }
                            osd->dragging = 0;
                        }
                    }
                    break;

                case MotionNotify:
                    if (osd->dragging) {
                        // Consume all pending motion events (coalesce)
                        while (XCheckTypedWindowEvent(dpy, win, MotionNotify, &event));

                        osd->pos_x = event.xmotion.x_root - osd->drag_start_x;
                        osd->pos_y = event.xmotion.y_root - osd->drag_start_y;
                        XMoveWindow(dpy, win, osd->pos_x, osd->pos_y);
                        XFlush(dpy);
                    }
                    break;

                case ConfigureNotify:
                    // Window was moved or resized
                    break;

                case EnterNotify:
                    // Cursor entered the window - don't auto-hide while hovering
                    osd->cursor_inside = 1;
                    break;

                case LeaveNotify:
                    // Cursor left the window - restart auto-hide timer
                    osd->cursor_inside = 0;
                    osd->last_action_time_ms = osd_get_time_ms();
                    break;
            }
        }
    }

    // Auto-hide check: AFTER processing events so cursor_inside is up-to-date
    if (osd->auto_show && osd->mode != OSD_MODE_HIDDEN && !osd->cursor_inside && osd->last_action_time_ms > 0) {
        long time_since_action = now - osd->last_action_time_ms;
        if (time_since_action > osd->display_duration_ms) {
            osd_hide(osd);
            return;
        }
    }

    // Check if we need to clear old actions and redraw
    static long last_cleanup = 0;
    if (now - last_cleanup > 500) {  // Check every 500ms
        last_cleanup = now;

        // Check if any visible actions have expired
        int need_redraw = 0;
        for (int i = 0; i < osd->recent_count; i++) {
            int idx = (osd->recent_head - 1 - i + 10) % 10;
            long age = now - osd->recent_actions[idx].timestamp_ms;
            if (age > osd->display_duration_ms && age < osd->display_duration_ms + 1000) {
                need_redraw = 1;
                break;
            }
        }
        if (need_redraw) {
            osd_redraw(osd);
        }
    }
}

// Set OSD position
void osd_set_position(osd_state_t* osd, int x, int y) {
    if (osd == NULL) return;

    osd->pos_x = x;
    osd->pos_y = y;

    if (osd->display && osd->window) {
        Display* dpy = (Display*)osd->display;
        Window win = (Window)osd->window;
        XMoveWindow(dpy, win, x, y);
        XFlush(dpy);
    }
}

// Move OSD by delta
void osd_move(osd_state_t* osd, int dx, int dy) {
    osd_set_position(osd, osd->pos_x + dx, osd->pos_y + dy);
}

// Set key description
void osd_set_key_description(osd_state_t* osd, int button_index, const char* description) {
    if (osd == NULL || button_index < 0 || button_index > 18) return;

    if (osd->key_descriptions[button_index]) {
        free(osd->key_descriptions[button_index]);
    }
    osd->key_descriptions[button_index] = description ? strdup(description) : NULL;
}

// Get key description
const char* osd_get_key_description(osd_state_t* osd, int button_index) {
    if (osd == NULL || button_index < 0 || button_index > 18) return NULL;
    return osd->key_descriptions[button_index];
}

// Clear all descriptions
void osd_clear_descriptions(osd_state_t* osd) {
    if (osd == NULL) return;

    for (int i = 0; i < 19; i++) {
        if (osd->key_descriptions[i]) {
            free(osd->key_descriptions[i]);
            osd->key_descriptions[i] = NULL;
        }
    }
}

// Set opacity
void osd_set_opacity(osd_state_t* osd, float opacity) {
    if (osd == NULL) return;

    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;

    osd->opacity = opacity;

    if (osd->mode != OSD_MODE_HIDDEN) {
        osd_redraw(osd);
    }
}

// Set display duration for actions
void osd_set_display_duration(osd_state_t* osd, int duration_ms) {
    if (osd == NULL) return;
    osd->display_duration_ms = duration_ms;
}

// Record a wheel action with aggregation (don't repeat same action)
void osd_record_wheel_action(osd_state_t* osd, const char* direction, const char* description) {
    if (osd == NULL) return;

    long now = osd_get_time_ms();
    char action_str[128];

    if (description && strlen(description) > 0) {
        snprintf(action_str, sizeof(action_str), "%s %s", description, direction ? direction : "");
    } else {
        snprintf(action_str, sizeof(action_str), "Wheel %s", direction ? direction : "turn");
    }

    // Aggregate: if same action within 500ms, just update timestamp
    if (osd->wheel.last_wheel_action &&
        strcmp(osd->wheel.last_wheel_action, action_str) == 0 &&
        (now - osd->wheel.last_wheel_time_ms) < 500) {
        osd->wheel.last_wheel_time_ms = now;
        osd->wheel.wheel_action_count++;
    } else {
        if (osd->wheel.last_wheel_action) free(osd->wheel.last_wheel_action);
        osd->wheel.last_wheel_action = strdup(action_str);
        osd->wheel.last_wheel_time_ms = now;
        osd->wheel.wheel_action_count = 1;
    }

    osd->last_action_time_ms = now;

    // Auto-show if hidden
    if (osd->mode == OSD_MODE_HIDDEN && osd->auto_show && osd->display != NULL) {
        osd_set_mode(osd, OSD_MODE_MINIMAL);
    } else if (osd->mode != OSD_MODE_HIDDEN) {
        osd_redraw(osd);
    }
}

// Set wheel state for OSD display
void osd_set_wheel_state(osd_state_t* osd, int current_set, int position_in_set,
                          int wheel_function, int wheel_mode, int total_wheels) {
    if (osd == NULL) return;
    osd->wheel.current_set = current_set;
    osd->wheel.position_in_set = position_in_set;
    osd->wheel.wheel_function = wheel_function;
    osd->wheel.wheel_mode = wheel_mode;
    osd->wheel.total_wheels = total_wheels;

    if (osd->mode != OSD_MODE_HIDDEN) {
        osd_redraw(osd);
    }
}

// Set a wheel function description
void osd_set_wheel_description(osd_state_t* osd, int index, const char* description) {
    if (osd == NULL || index < 0 || index >= 32) return;
    if (osd->wheel.descriptions[index]) free(osd->wheel.descriptions[index]);
    osd->wheel.descriptions[index] = description ? strdup(description) : NULL;
}

// Set active button for highlighting
void osd_set_active_button(osd_state_t* osd, int button_index) {
    if (osd == NULL) return;
    osd->active_button = button_index;
    osd->active_button_time_ms = osd_get_time_ms();

    if (osd->mode != OSD_MODE_HIDDEN) {
        osd_redraw(osd);
    }
}

// Set leader state for visual feedback
void osd_set_leader_state(osd_state_t* osd, int active, int leader_button) {
    if (osd == NULL) return;
    osd->leader_active = active;
    osd->leader_button = leader_button;

    if (osd->mode != OSD_MODE_HIDDEN) {
        osd_redraw(osd);
    }
}
