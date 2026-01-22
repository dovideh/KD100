#include "leader.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

// Forward declaration of Handler (implemented in handler module)
void Handler(char* key, int type, int debug);

// Reset leader state (but preserve toggle state for toggle mode)
void reset_leader_state(leader_state* state) {
    state->leader_active = 0;
    state->last_button = -1;
    state->leader_press_time.tv_sec = 0;
    state->leader_press_time.tv_usec = 0;
    // Don't reset toggle_state here - it's managed separately
}

// Send leader combination
void send_leader_combination(leader_state* state, char* combination, int debug) {
    if (combination == NULL || strlen(combination) == 0) {
        return;
    }

    if (debug == 1) {
        printf("Sending leader combination: %s\n", combination);
    }

    // Send the combination
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "xdotool key %s", combination);
    system(cmd);

    // Reset leader state after sending (except for sticky and toggle modes)
    if (state->mode == LEADER_MODE_ONE_SHOT) {
        reset_leader_state(state);
        state->toggle_state = 0; // Also reset toggle for one_shot
    } else if (state->mode == LEADER_MODE_STICKY) {
        // Update timer for sticky mode
        gettimeofday(&state->leader_press_time, NULL);
    }
    // For toggle mode, don't reset anything - stay active
}

// Process leader key combinations
void process_leader_combination(leader_state* state, event* events, int button_index, int debug) {
    if (button_index < 0 || button_index >= 19) {
        return;
    }

    // Check if this is the wheel button (button 18) - handle specially
    if (button_index == 18) {
        // Wheel button - handle normally without leader
        if (events[button_index].function != NULL &&
            strcmp(events[button_index].function, "swap") == 0) {
            // Handle swap function
            // (This would need wheel state tracking - implemented elsewhere)
        }
        // Don't reset leader state for wheel button in toggle mode
        if (state->mode != LEADER_MODE_TOGGLE) {
            reset_leader_state(state);
        }
        return;
    }

    if (events[button_index].function == NULL) {
        return;
    }

    char* button_func = events[button_index].function;

    // Check if this button is configured as a leader
    if (strcmp(button_func, "leader") == 0) {
        // This button is the leader itself
        if (state->mode == LEADER_MODE_TOGGLE) {
            // Toggle mode: press to enable, press again to disable
            if (!state->toggle_state) {
                // Enable toggle mode
                state->toggle_state = 1;
                state->leader_active = 1;
                gettimeofday(&state->leader_press_time, NULL);
                state->last_button = button_index;

                if (debug == 1) {
                    printf("Leader toggle mode ENABLED by button %d\n", button_index);
                }
            } else {
                // Disable toggle mode
                state->toggle_state = 0;
                state->leader_active = 0;
                reset_leader_state(state);

                if (debug == 1) {
                    printf("Leader toggle mode DISABLED by button %d\n", button_index);
                }
            }
        } else {
            // One-shot or sticky mode
            if (!state->leader_active) {
                // Activate leader mode
                state->leader_active = 1;
                gettimeofday(&state->leader_press_time, NULL);
                state->last_button = button_index;

                if (debug == 1) {
                    printf("Leader mode activated by button %d\n", button_index);
                }
            } else {
                // Leader pressed again - cancel leader mode
                reset_leader_state(state);
                if (debug == 1) {
                    printf("Leader mode cancelled\n");
                }
            }
        }
        return;
    }

    // Check if we're in leader mode (either active or toggle mode is on)
    int in_leader_mode = state->leader_active;
    if (state->mode == LEADER_MODE_TOGGLE) {
        in_leader_mode = state->toggle_state;
    }

    if (in_leader_mode) {
        // Check eligibility first
        if (events[button_index].leader_eligible == 0) {
            // Button is not eligible for leader modifications
            if (debug == 1) {
                printf("Button %d not eligible for leader - handling normally\n", button_index);
            }

            // For sticky mode, we might want to keep leader active
            if (state->mode != LEADER_MODE_STICKY && state->mode != LEADER_MODE_TOGGLE) {
                reset_leader_state(state);
            }

            // Fall through to normal button handling
        } else {
            // Button is eligible for leader modifications
            // Calculate time since leader was pressed (for timeout)
            struct timeval now;
            gettimeofday(&now, NULL);
            long elapsed = time_diff_ms(state->leader_press_time, now);

            // Check timeout (skip for toggle mode)
            if (elapsed > state->timeout_ms && state->mode != LEADER_MODE_TOGGLE) {
                // Leader timeout - reset (except for toggle mode)
                if (debug == 1) {
                    printf("Leader timeout (%ld ms > %d ms)\n", elapsed, state->timeout_ms);
                }
                reset_leader_state(state);
                state->toggle_state = 0;
                // Fall through to normal button handling
            } else {
                // We have a leader combination!
                // Format: leader_function + button_function
                char combination[256] = "";

                if (state->leader_function != NULL && strlen(state->leader_function) > 0) {
                    strcpy(combination, state->leader_function);
                    strcat(combination, "+");
                }

                strcat(combination, button_func);

                // Send the combination
                send_leader_combination(state, combination, debug);

                // For sticky mode, update the timer to extend timeout
                if (state->mode == LEADER_MODE_STICKY) {
                    gettimeofday(&state->leader_press_time, NULL);
                }

                return; // Don't also handle as normal button
            }
        }
    }

    // Not in leader mode, leader timed out, or button not eligible - handle normal button press
    if (strcmp(button_func, "NULL") != 0 && strcmp(button_func, "swap") != 0 &&
        strcmp(button_func, "mouse1") != 0 && strcmp(button_func, "mouse2") != 0 &&
        strcmp(button_func, "mouse3") != 0 && strcmp(button_func, "mouse4") != 0 &&
        strcmp(button_func, "mouse5") != 0) {

        if (events[button_index].type == 0) {
            // Type 0: Key press
            Handler(button_func, 0, debug);
            usleep(10000); // Small delay
            Handler(button_func, 1, debug);
        } else if (events[button_index].type == 1) {
            // Type 1: Run program/script
            system(button_func);
        }
    }
}
