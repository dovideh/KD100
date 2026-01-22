#ifndef CONFIG_H
#define CONFIG_H

#include "leader.h"

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
} config_t;

// Configuration functions
config_t* config_create(void);
void config_destroy(config_t* config);
int config_load(config_t* config, const char* filename, int debug);
void config_print(const config_t* config, int debug);

#endif // CONFIG_H
