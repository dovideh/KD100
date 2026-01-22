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
} config_t;

// Configuration functions
config_t* config_create(void);
void config_destroy(config_t* config);
int config_load(config_t* config, const char* filename, int debug);
void config_print(const config_t* config, int debug);

#endif // CONFIG_H
