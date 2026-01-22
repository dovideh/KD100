#ifndef UTILS_H
#define UTILS_H

#include <sys/time.h>

// Leader mode enumeration
typedef enum {
    LEADER_MODE_ONE_SHOT,    // Leader + 1 key = combination, then reset (default)
    LEADER_MODE_STICKY,      // Leader stays active for multiple keys until timeout
    LEADER_MODE_TOGGLE       // Leader toggles on/off (press to enable, press again to disable)
} leader_mode_t;

// Time utilities
long get_time_ms(void);
long time_diff_ms(struct timeval start, struct timeval end);

// String utilities
void trim_trailing_spaces(char* str);
char* Substring(const char* in, int start, int end);

// Leader mode utilities
leader_mode_t parse_leader_mode(const char* mode_str);
const char* leader_mode_to_string(leader_mode_t mode);

// Button utilities
int find_button_index(int keycode);
int is_modifier_key(const char* key);

#endif // UTILS_H
