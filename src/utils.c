#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>

// External keycode array (defined in main)
extern int keycodes[];

// Get current time in milliseconds
long get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

// Calculate time difference in milliseconds
long time_diff_ms(struct timeval start, struct timeval end) {
    long sec_diff = end.tv_sec - start.tv_sec;
    long usec_diff = end.tv_usec - start.tv_usec;
    return (sec_diff * 1000) + (usec_diff / 1000);
}

// Trim trailing spaces from a string
void trim_trailing_spaces(char* str) {
    if (str == NULL) return;

    int len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[len - 1] = '\0';
        len--;
    }
}

// Parse leader mode string
leader_mode_t parse_leader_mode(const char* mode_str) {
    if (mode_str == NULL) return LEADER_MODE_ONE_SHOT;

    if (strcasecmp(mode_str, "sticky") == 0) {
        return LEADER_MODE_STICKY;
    } else if (strcasecmp(mode_str, "toggle") == 0) {
        return LEADER_MODE_TOGGLE;
    } else if (strcasecmp(mode_str, "one_shot") == 0 || strcasecmp(mode_str, "oneshot") == 0) {
        return LEADER_MODE_ONE_SHOT;
    }

    return LEADER_MODE_ONE_SHOT; // Default
}

// Convert leader mode to string
const char* leader_mode_to_string(leader_mode_t mode) {
    switch (mode) {
        case LEADER_MODE_ONE_SHOT: return "one_shot";
        case LEADER_MODE_STICKY: return "sticky";
        case LEADER_MODE_TOGGLE: return "toggle";
        default: return "unknown";
    }
}

// Find button index from keycode
int find_button_index(int keycode) {
    for (int k = 0; k < 19; k++) {  // Only 0-17 for buttons, 18 is wheel button
        if (keycodes[k] == keycode) {
            return k;
        }
    }
    return -1;
}

// Check if a key is a modifier
int is_modifier_key(const char* key) {
    if (key == NULL) return 0;

    if (strcmp(key, "ctrl") == 0 || strcmp(key, "control") == 0 ||
        strcmp(key, "shift") == 0 ||
        strcmp(key, "alt") == 0 ||
        strcmp(key, "super") == 0 || strcmp(key, "meta") == 0) {
        return 1;
    }
    return 0;
}

// Substring function
char* Substring(const char* in, int start, int end) {
    if (in == NULL) {
        char* out = malloc(1);
        if (out) out[0] = '\0';
        return out;
    }

    int len = strlen(in);

    if (start < 0) start = 0;
    if (start >= len) {
        char* out = malloc(1);
        if (out) out[0] = '\0';
        return out;
    }

    if (end <= 0) {
        char* out = malloc(1);
        if (out) out[0] = '\0';
        return out;
    }

    if (start + end > len) {
        end = len - start;
    }

    char* out = malloc(end + 1);
    if (out == NULL) {
        return NULL;
    }

    for (int i = 0; i < end; i++) {
        out[i] = in[start + i];
    }
    out[end] = '\0';

    return out;
}
