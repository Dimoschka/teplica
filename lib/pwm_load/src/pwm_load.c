#include "pwm_load.h"
#include <driver/ledc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <esp_log.h>

static const char* TAG = "PWM_LOAD";

/**
 * @brief PWM Load Controller internal structure
 */
typedef struct {
    uint8_t gpio_pin;
    uint8_t ledc_channel;
    uint8_t ledc_timer;
    uint32_t frequency;
    uint8_t resolution_bits;
    uint32_t max_duty;
    uint8_t current_duty;
    
    // Ramping variables
    bool is_ramping;
    uint8_t start_duty;     // Initial duty when ramp started
    uint8_t target_duty;
    uint16_t ramp_steps;
    uint16_t current_step;
    int8_t ramp_direction;  // 1 for up, -1 for down
    
    // Timer for ramping
    TimerHandle_t ramp_timer;
    uint32_t step_duration_ms;
} pwm_load_t;

/**
 * @brief Timer callback for ramping operation
 */
static void pwm_load_ramp_callback(TimerHandle_t xTimer)
{
    pwm_load_t* pwm = (pwm_load_t*)pvTimerGetTimerID(xTimer);
    
    if (!pwm || !pwm->is_ramping) {
        return;
    }
    
    pwm->current_step++;
    
    // Calculate current duty cycle using linear interpolation from start to target
    int16_t duty_range = (int16_t)pwm->target_duty - (int16_t)pwm->start_duty;
    int16_t new_duty = pwm->start_duty + (duty_range * pwm->current_step) / pwm->ramp_steps;
    
    // Check if ramp finished
    if (pwm->current_step >= pwm->ramp_steps) {
        new_duty = pwm->target_duty;
        pwm->is_ramping = false;
        xTimerStop(pwm->ramp_timer, 0);
    }
    
    // Clamp to valid range
    if (new_duty < 0) new_duty = 0;
    if (new_duty > 100) new_duty = 100;
    
    // Update PWM duty
    pwm->current_duty = (uint8_t)new_duty;
    uint32_t duty_value = (uint32_t)(new_duty * pwm->max_duty / 100);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, pwm->ledc_channel, duty_value);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, pwm->ledc_channel);
    
    ESP_LOGD(TAG, "Ramping: step %u/%u, duty: %u%%", 
             pwm->current_step, pwm->ramp_steps, pwm->current_duty);
}

/**
 * @brief Initialize PWM Load Controller
 */
pwm_load_handle_t pwm_load_init(const pwm_load_config_t* config)
{
    if (!config) {
        ESP_LOGE(TAG, "Invalid config pointer");
        return NULL;
    }
    
    // Allocate memory for PWM controller
    pwm_load_t* pwm = (pwm_load_t*)malloc(sizeof(pwm_load_t));
    if (!pwm) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        return NULL;
    }
    
    // Initialize structure
    memset(pwm, 0, sizeof(pwm_load_t));
    pwm->gpio_pin = config->gpio_pin;
    pwm->ledc_channel = config->ledc_channel;
    pwm->ledc_timer = config->ledc_timer;
    pwm->frequency = config->frequency > 0 ? config->frequency : 1000;
    pwm->resolution_bits = config->resolution_bits > 0 ? config->resolution_bits : 8;
    pwm->max_duty = (1 << pwm->resolution_bits) - 1;
    pwm->current_duty = 0;
    pwm->is_ramping = false;
    
    // Configure LEDC timer
    // Using clk_cfg field which exists in IDF <=5, and newer versions will
    // ignore this field or provide compatibility. This avoids version checks.
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = pwm->ledc_timer,
        .freq_hz = pwm->frequency,
        .duty_resolution = pwm->resolution_bits,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    
    if (ledc_timer_config(&timer_conf) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure timer");
        free(pwm);
        return NULL;
    }
    
    // Configure LEDC channel
    ledc_channel_config_t channel_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = pwm->ledc_channel,
        .timer_sel = pwm->ledc_timer,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = pwm->gpio_pin,
        .duty = 0,
        .hpoint = 0,
        .flags = {
            .output_invert = 0
        }
    };
    
    if (ledc_channel_config(&channel_conf) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure channel");
        free(pwm);
        return NULL;
    }
    
    // Create timer for ramping operations
    pwm->ramp_timer = xTimerCreate(
        "PWM_Ramp",
        pdMS_TO_TICKS(100),
        pdTRUE,
        pwm,
        pwm_load_ramp_callback
    );
    
    if (!pwm->ramp_timer) {
        ESP_LOGE(TAG, "Failed to create ramp timer");
        free(pwm);
        return NULL;
    }
    
    ESP_LOGI(TAG, "PWM Load initialized on GPIO %d, freq: %d Hz, resolution: %d bits",
             pwm->gpio_pin, pwm->frequency, pwm->resolution_bits);
    
    return (pwm_load_handle_t)pwm;
}

/**
 * @brief Deinitialize PWM Load Controller
 */
int pwm_load_deinit(pwm_load_handle_t handle)
{
    pwm_load_t* pwm = (pwm_load_t*)handle;
    
    if (!pwm) {
        return -1;
    }
    
    // Stop ramping
    if (pwm->ramp_timer) {
        xTimerStop(pwm->ramp_timer, 0);
        xTimerDelete(pwm->ramp_timer, 0);
    }
    
    // Stop PWM
    ledc_stop(LEDC_LOW_SPEED_MODE, pwm->ledc_channel, 0);
    
    free(pwm);
    ESP_LOGI(TAG, "PWM Load deinitialized");
    
    return 0;
}

/**
 * @brief Set PWM duty cycle (0-100%)
 */
int pwm_load_set_duty(pwm_load_handle_t handle, uint8_t duty_percent)
{
    pwm_load_t* pwm = (pwm_load_t*)handle;
    
    if (!pwm) {
        return -1;
    }
    
    // Clamp duty
    if (duty_percent > 100) {
        duty_percent = 100;
    }
    
    // Stop ramping if in progress
    if (pwm->is_ramping) {
        xTimerStop(pwm->ramp_timer, 0);
        pwm->is_ramping = false;
    }
    
    pwm->current_duty = duty_percent;
    uint32_t duty_value = (uint32_t)(duty_percent * pwm->max_duty / 100);
    
    ledc_set_duty(LEDC_LOW_SPEED_MODE, pwm->ledc_channel, duty_value);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, pwm->ledc_channel);
    
    ESP_LOGD(TAG, "Duty set to %u%%", duty_percent);
    
    return 0;
}

/**
 * @brief Get current PWM duty cycle
 */
int pwm_load_get_duty(pwm_load_handle_t handle)
{
    pwm_load_t* pwm = (pwm_load_t*)handle;
    
    if (!pwm) {
        return -1;
    }
    
    return pwm->current_duty;
}

/**
 * @brief Smooth startup - ramp up from 0% to 100%
 */
int pwm_load_ramp_up(pwm_load_handle_t handle, uint32_t duration_ms, uint16_t steps)
{
    pwm_load_t* pwm = (pwm_load_t*)handle;
    
    if (!pwm || duration_ms == 0 || steps == 0) {
        return -1;
    }
    
    return pwm_load_ramp_to(handle, 100, duration_ms, steps);
}

/**
 * @brief Smooth shutdown - ramp down from current level to 0%
 */
int pwm_load_ramp_down(pwm_load_handle_t handle, uint32_t duration_ms, uint16_t steps)
{
    pwm_load_t* pwm = (pwm_load_t*)handle;
    
    if (!pwm || duration_ms == 0 || steps == 0) {
        return -1;
    }
    
    return pwm_load_ramp_to(handle, 0, duration_ms, steps);
}

/**
 * @brief Ramp to target duty cycle
 */
int pwm_load_ramp_to(pwm_load_handle_t handle, uint8_t target_duty, uint32_t duration_ms, uint16_t steps)
{
    pwm_load_t* pwm = (pwm_load_t*)handle;
    
    if (!pwm || duration_ms == 0 || steps == 0) {
        return -1;
    }
    
    // Clamp target duty
    if (target_duty > 100) {
        target_duty = 100;
    }
    
    // If already at target, just set it
    if (pwm->current_duty == target_duty) {
        return pwm_load_set_duty(handle, target_duty);
    }
    
    // Stop any ongoing ramping
    if (pwm->is_ramping) {
        xTimerStop(pwm->ramp_timer, 0);
    }
    
    // Setup ramping parameters
    pwm->start_duty = pwm->current_duty;  // Save starting point
    pwm->target_duty = target_duty;
    pwm->ramp_steps = steps;
    pwm->current_step = 0;
    pwm->ramp_direction = (target_duty > pwm->current_duty) ? 1 : -1;
    pwm->step_duration_ms = duration_ms / steps;
    
    if (pwm->step_duration_ms < 1) {
        pwm->step_duration_ms = 1;
    }
    
    pwm->is_ramping = true;
    
    ESP_LOGI(TAG, "Starting ramp: %u%% -> %u%% in %u ms (%u steps, %u ms per step)",
             pwm->current_duty, target_duty, duration_ms, steps, pwm->step_duration_ms);
    
    // Start ramping timer
    xTimerChangePeriod(pwm->ramp_timer, pdMS_TO_TICKS(pwm->step_duration_ms), 0);
    xTimerStart(pwm->ramp_timer, 0);
    
    return 0;
}

/**
 * @brief Check if ramping is in progress
 */
bool pwm_load_is_ramping(pwm_load_handle_t handle)
{
    pwm_load_t* pwm = (pwm_load_t*)handle;
    
    if (!pwm) {
        return false;
    }
    
    return pwm->is_ramping;
}

/**
 * @brief Stop current ramping operation
 */
int pwm_load_stop_ramp(pwm_load_handle_t handle)
{
    pwm_load_t* pwm = (pwm_load_t*)handle;
    
    if (!pwm) {
        return -1;
    }
    
    if (pwm->is_ramping) {
        xTimerStop(pwm->ramp_timer, 0);
        pwm->is_ramping = false;
        ESP_LOGI(TAG, "Ramping stopped at %u%%", pwm->current_duty);
    }
    
    return 0;
}
