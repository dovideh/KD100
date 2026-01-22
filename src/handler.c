#include "handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void Handler(char* key, int type, int debug) {
    if (key == NULL) {
        if (debug == 1) printf("Handler called with NULL key\n");
        return;
    }

    if (strcmp(key, "NULL") == 0) {
        return;
    }

    char* cmd = NULL;
    int is_mouse = 0;
    char mouse_char = '1';

    switch(type) {
        case -1:
            cmd = "xdotool key ";
            break;
        case 0:
            cmd = "xdotool keydown ";
            break;
        case 1:
            cmd = "xdotool keyup ";
            break;
        case 2:
            is_mouse = 1;
            cmd = "xdotool mousedown ";
            break;
        case 3:
            is_mouse = 1;
            cmd = "xdotool mouseup ";
            break;
        default:
            if (debug == 1) printf("Handler called with unknown type: %d\n", type);
            return;
    }

    if (cmd == NULL) {
        return;
    }

    if (is_mouse) {
        if (strlen(key) >= 6 && strncmp(key, "mouse", 5) == 0) {
            mouse_char = key[5];
            if (mouse_char < '1' || mouse_char > '5') {
                mouse_char = '1';
            }
        }

        char temp[strlen(cmd) + 3];
        snprintf(temp, sizeof(temp), "%s %c", cmd, mouse_char);
        if (debug == 1) printf("Executing: %s\n", temp);
        system(temp);
    } else {
        char temp[strlen(cmd) + strlen(key) + 1];
        snprintf(temp, sizeof(temp), "%s%s", cmd, key);
        if (debug == 1) printf("Executing: %s\n", temp);
        system(temp);
    }
}
