#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    TANK_FILL_IDLE,
    TANK_FILL_IN_PROGRESS,
    TANK_FILL_COMPLETE,
    TANK_FILL_ERROR
} tank_fill_state_t;

esp_err_t tank_fill_init(void);

void tank_fill_start(void);
void tank_fill_stop(void);

tank_fill_state_t tank_fill_get_state(void);
bool tank_fill_is_in_progress(void);