#pragma once

#include "esp_err.h"

esp_err_t water_level_init(void);
int water_level_read_voltage_mv(void);
int water_level_read_step(void);
int water_level_read_percent(void);
void water_level_monitor_task(void *arg);