#include "app_state.h"

#include "../include/config.h"

bool g_tank_filled = false;
int g_previous_water_level_percent = -1;
tank_fill_state_t g_tank_fill_state = TANK_FILL_IDLE;
bool g_tank_fill_manual_requested = false;

float g_temperature = 0.0f;
float g_humidity = 0.0f;
bool g_vent_open = false;

int g_water_level_median_mv = -1;

bool g_wifi_connected = false;
bool g_mqtt_started = false;

pwm_load_handle_t g_motor_pwm = NULL;
pwm_load_handle_t g_pump_pwm = NULL;
QueueHandle_t g_climate_queue = NULL;
TaskHandle_t g_ventilation_task_handle = NULL;

acs712_t current_sensor;
adc_oneshot_unit_handle_t g_adc1_handle = NULL;
adc_cali_handle_t g_cali_handle = NULL;
sht30_t sht30_dev;

vent_state_t g_vent_state = VENT_CLOSED;
bool g_vent_position = false;

float g_vent_open_temp = DEFAULT_VENT_OPEN_TEMP;
float g_vent_close_temp = DEFAULT_VENT_CLOSE_TEMP;
int g_fill_tank_start_hour = DEFAULT_FILL_TANK_START_HOUR;
int g_fill_tank_end_hour = DEFAULT_FILL_TANK_END_HOUR;
int g_irrigation_duration_s = DEFAULT_IRRIGATION_DURATION_S;
int g_irrigation_pump_speed = DEFAULT_IRRIGATION_PUMP_SPEED;

int g_garden_irrigation_pct[GARDEN_BEDS_COUNT] = {
    DEFAULT_GARDEN_IRRIGATION_PCT,
    DEFAULT_GARDEN_IRRIGATION_PCT,
    DEFAULT_GARDEN_IRRIGATION_PCT
};

int g_garden_irrigation_freq[GARDEN_BEDS_COUNT] = {
    DEFAULT_GARDEN_IRRIGATION_FREQ,
    DEFAULT_GARDEN_IRRIGATION_FREQ,
    DEFAULT_GARDEN_IRRIGATION_FREQ
};

int g_garden_irrigation_done_today[GARDEN_BEDS_COUNT] = {0};

bool g_irrigation_happened_today = false;
time_t g_last_irrigation_time = 0;
uint8_t g_irrigation_beds_mask = 0;
time_t g_irrigation_disconnect_deadline = 0;