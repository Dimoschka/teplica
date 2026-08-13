#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t irrigation_pump_init(void);

void irrigation_apply_mask(uint8_t mask);
void irrigation_mask_to_string(uint8_t mask, char *buf, size_t size);
uint8_t irrigation_parse_command(const char *cmd);