#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "pwm_load.h"
#include "acs712.h"
#include "sht30.h"

#include "tank_fill/tank_fill.h"

typedef enum {
    VENT_CLOSED,
    VENT_OPENING,
    VENT_OPEN,
    VENT_CLOSING,
    VENT_FAULT,
    VENT_UNKNOWN
} vent_state_t;



typedef struct {
    float temperature;
    float humidity;
    bool force_update;
} climate_data_t;

extern bool g_tank_filled;
extern int g_previous_water_level_percent;
extern tank_fill_state_t g_tank_fill_state;
extern bool g_tank_fill_manual_requested;

extern float g_temperature;
extern float g_humidity;
extern bool g_vent_open;

extern int g_water_level_median_mv;

extern bool g_wifi_connected;
extern bool g_mqtt_started;

extern pwm_load_handle_t g_motor_pwm;
extern pwm_load_handle_t g_pump_pwm;
extern QueueHandle_t g_climate_queue;
extern TaskHandle_t g_ventilation_task_handle;

extern acs712_t current_sensor;
extern adc_oneshot_unit_handle_t g_adc1_handle;
extern adc_cali_handle_t g_cali_handle;
extern sht30_t sht30_dev;

extern vent_state_t g_vent_state;
extern bool g_vent_position;

extern float g_vent_open_temp;
extern float g_vent_close_temp;
extern int g_fill_tank_start_hour;
extern int g_fill_tank_end_hour;
extern int g_irrigation_duration_s;
extern int g_irrigation_pump_speed;

extern int g_garden_irrigation_pct[];
extern int g_garden_irrigation_freq[];
extern int g_garden_irrigation_done_today[];

extern bool g_irrigation_happened_today;
extern time_t g_last_irrigation_time;
extern uint8_t g_irrigation_beds_mask;
extern time_t g_irrigation_disconnect_deadline;

