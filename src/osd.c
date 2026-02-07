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
    osd->expanded_width = 400;
    osd->expanded_height = 350;
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

    // Update window size based on mode
    if (mode == OSD_MODE_MINIMAL) {
        osd->width = osd->min_width;
        osd->height = osd->min_height;
    } else {
        osd->width = osd->expanded_width;
        osd->height = osd->expanded_height;
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
    osd->recent_actions[slot].key_name = strdup(BUTTON_NAMES[button_index]);
    osd->recent_actions[slot].action = action ? strdup(action) : strdup("(unknown)");
    osd->recent_actions[slot].timestamp_ms = now;
    osd->last_action_time_ms = now;

    // Auto-show OSD if hidden and auto_show is enabled
    if (osd->mode == OSD_MODE_HIDDEN && osd->auto_show && osd->display != NULL) {
        osd_set_mode(osd, OSD_MODE_MINIMAL);
    } else if (osd->mode != OSD_MODE_HIDDEN) {
        osd_redraw(osd);
    }
}

// Redraw the OSD
void osd_redraw(osd_state_t* osd) {
    if (osd == NULL || osd->display == NULL || osd->mode == OSD_MODE_HIDDEN) return;

    Display* dpy = (Display*)osd->display;
    Window win = (Window)osd->window;
    GC gc = (GC)osd->gc;

    // Calculate alpha value (0-255)
    unsigned char alpha = (unsigned char)(osd->opacity * 255);

    // Create colors with alpha
    unsigned long bg_color = ((unsigned long)alpha << 24) | 0x202020;  // Dark gray with alpha
    unsigned long fg_color = ((unsigned long)255 << 24) | 0xFFFFFF;   // White, full alpha
    unsigned long accent_color = ((unsigned long)255 << 24) | 0x4488FF; // Blue accent
    unsigned long highlight_color = ((unsigned long)200 << 24) | 0x444444; // Highlight

    // Clear window
    XSetForeground(dpy, gc, bg_color);
    XFillRectangle(dpy, win, gc, 0, 0, osd->width, osd->height);

    // Draw border
    XSetForeground(dpy, gc, accent_color);
    XDrawRectangle(dpy, win, gc, 0, 0, osd->width - 1, osd->height - 1);

    // Use stored font (set in init)
    XFontStruct* font = (XFontStruct*)osd->font;
    if (font) {
        XSetFont(dpy, gc, font->fid);
    }

    int line_height = osd->font_size + 3;
    int padding = 10;
    int title_height = osd->font_size + 12;
    int y_offset = padding + line_height;

    // Draw title bar with mode indicator
    XSetForeground(dpy, gc, accent_color);
    XFillRectangle(dpy, win, gc, 0, 0, osd->width, title_height);

    XSetForeground(dpy, gc, fg_color);
    const char* title = (osd->mode == OSD_MODE_MINIMAL) ? "KD100 [+] click to expand" : "KD100 [-] click to collapse";
    XDrawString(dpy, win, gc, padding, title_height - 5, title, strlen(title));

    y_offset = title_height + padding;

    if (osd->mode == OSD_MODE_MINIMAL) {
        // Minimal mode: show recent actions
        XSetForeground(dpy, gc, fg_color);
        XDrawString(dpy, win, gc, padding, y_offset, "Recent Actions:", 15);
        y_offset += line_height + 5;

        long now = osd_get_time_ms();
        int shown = 0;

        // Show up to 4 recent actions
        for (int i = 0; i < osd->recent_count && shown < 4; i++) {
            int idx = (osd->recent_head - 1 - i + 10) % 10;
            recent_action_t* action = &osd->recent_actions[idx];

            // Check if action is still within display duration
            long age = now - action->timestamp_ms;
            if (age > osd->display_duration_ms && osd->display_duration_ms > 0) continue;

            // Format: "B0: ctrl+z"
            char line[128];
            snprintf(line, sizeof(line), "%s: %s", action->key_name, action->action);

            // Fade out effect based on age
            if (age < osd->display_duration_ms) {
                float fade = 1.0f - ((float)age / osd->display_duration_ms * 0.5f);
                unsigned char text_alpha = (unsigned char)(255 * fade);
                unsigned long text_color = ((unsigned long)text_alpha << 24) | 0xFFFFFF;
                XSetForeground(dpy, gc, text_color);
            }

            XDrawString(dpy, win, gc, padding, y_offset, line, strlen(line));
            y_offset += line_height;
            shown++;
        }

        if (shown == 0) {
            XSetForeground(dpy, gc, highlight_color);
            XDrawString(dpy, win, gc, padding, y_offset, "(no recent actions)", 19);
        }

    } else {
        // Expanded mode: show full keyboard layout
        XSetForeground(dpy, gc, fg_color);
        XDrawString(dpy, win, gc, padding, y_offset, "Keyboard Layout:", 16);
        y_offset += line_height + 10;

        // Draw keyboard grid
        int key_width = 85;
        int key_height = 45;
        int grid_padding = 5;
        int start_x = padding;
        int start_y = y_offset;

        // Draw wheel button (top, spanning)
        int wheel_y = start_y;
        XSetForeground(dpy, gc, highlight_color);
        XFillRectangle(dpy, win, gc, start_x, wheel_y, osd->width - 2 * padding, key_height - 10);
        XSetForeground(dpy, gc, fg_color);
        XDrawRectangle(dpy, win, gc, start_x, wheel_y, osd->width - 2 * padding - 1, key_height - 11);

        // Wheel button label
        const char* wheel_desc = osd->key_descriptions[18] ? osd->key_descriptions[18] : "Wheel Toggle";
        XDrawString(dpy, win, gc, start_x + 5, wheel_y + 22, wheel_desc, strlen(wheel_desc));

        start_y += key_height;

        // Draw main button grid (4 rows x 4 columns)
        for (int row = 1; row < 5; row++) {
            for (int col = 0; col < 4; col++) {
                int btn = BUTTON_LAYOUT[row][col];
                if (btn < 0) continue;

                int x = start_x + col * (key_width + grid_padding);
                int y = start_y + (row - 1) * (key_height + grid_padding);

                // Draw key background
                XSetForeground(dpy, gc, highlight_color);
                XFillRectangle(dpy, win, gc, x, y, key_width, key_height);

                // Draw key border
                XSetForeground(dpy, gc, fg_color);
                XDrawRectangle(dpy, win, gc, x, y, key_width - 1, key_height - 1);

                // Draw button number
                char num[16];
                snprintf(num, sizeof(num), "%d", btn);
                XDrawString(dpy, win, gc, x + 5, y + 15, num, strlen(num));

                // Draw description or function
                const char* desc = NULL;
                if (osd->key_descriptions[btn]) {
                    desc = osd->key_descriptions[btn];
                } else if (osd->config && btn < osd->config->totalButtons &&
                           osd->config->events[btn].function) {
                    desc = osd->config->events[btn].function;
                }

                if (desc) {
                    // Truncate if too long
                    char truncated[12];
                    if (strlen(desc) > 10) {
                        strncpy(truncated, desc, 9);
                        truncated[9] = '.';
                        truncated[10] = '.';
                        truncated[11] = '\0';
                        desc = truncated;
                    }
                    XSetForeground(dpy, gc, accent_color);
                    XDrawString(dpy, win, gc, x + 5, y + 35, desc, strlen(desc));
                }
            }
        }

        // Draw bottom row (buttons 16 and 17 have special layout)
        int bottom_y = start_y + 4 * (key_height + grid_padding);

        // Button 16 (wide)
        XSetForeground(dpy, gc, highlight_color);
        XFillRectangle(dpy, win, gc, start_x, bottom_y, key_width * 2 + grid_padding, key_height);
        XSetForeground(dpy, gc, fg_color);
        XDrawRectangle(dpy, win, gc, start_x, bottom_y, key_width * 2 + grid_padding - 1, key_height - 1);
        XDrawString(dpy, win, gc, start_x + 5, bottom_y + 15, "16", 2);
        if (osd->key_descriptions[16]) {
            XSetForeground(dpy, gc, accent_color);
            XDrawString(dpy, win, gc, start_x + 5, bottom_y + 35, osd->key_descriptions[16],
                        strlen(osd->key_descriptions[16]) > 15 ? 15 : strlen(osd->key_descriptions[16]));
        }

        // Button 17
        int x17 = start_x + 2 * (key_width + grid_padding);
        XSetForeground(dpy, gc, highlight_color);
        XFillRectangle(dpy, win, gc, x17, bottom_y, key_width, key_height);
        XSetForeground(dpy, gc, fg_color);
        XDrawRectangle(dpy, win, gc, x17, bottom_y, key_width - 1, key_height - 1);
        XDrawString(dpy, win, gc, x17 + 5, bottom_y + 15, "17", 2);
        if (osd->key_descriptions[17]) {
            XSetForeground(dpy, gc, accent_color);
            XDrawString(dpy, win, gc, x17 + 5, bottom_y + 35, osd->key_descriptions[17],
                        strlen(osd->key_descriptions[17]) > 10 ? 10 : strlen(osd->key_descriptions[17]));
        }
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
                            int title_height = osd->font_size + 12;
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
