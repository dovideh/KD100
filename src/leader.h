#ifndef LEADER_H
#define LEADER_H

#include <sys/time.h>
#include "utils.h"

// Forward declaration
typedef struct event event;

// Leader state structure
typedef struct {
    int leader_button;            // Which button is the leader (e.g., Button 16 for shift)
    int leader_active;            // Is leader mode active?
    int last_button;              // Last button pressed (for timing)
    struct timeval leader_press_time; // When was leader pressed?
    char* leader_function;        // What function does the leader have?
    int timeout_ms;               // Leader timeout in milliseconds
    leader_mode_t mode;           // Leader mode (one_shot, sticky, toggle)
    int toggle_state;             // For toggle mode: 0 = off, 1 = on
} leader_state;

// Event structure (button configuration)
struct event {
    int type;
    char* function;
    int leader_eligible;  // 0 = not eligible, 1 = eligible, -1 = not set (default eligible)
};

// Leader key functions
void reset_leader_state(leader_state* state);
void send_leader_combination(leader_state* state, char* combination, int debug);
void process_leader_combination(leader_state* state, event* events, int button_index, int debug);

#endif // LEADER_H
