#ifndef PWM_LOAD_H
#define PWM_LOAD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief PWM Load Controller configuration structure
 */
typedef struct {
    uint8_t gpio_pin;          /*!< GPIO pin for PWM output */
    uint32_t frequency;        /*!< PWM frequency in Hz (default 1000) */
    uint8_t resolution_bits;   /*!< PWM resolution in bits (8-16) */
    uint8_t ledc_channel;      /*!< LEDC channel (0-7) */
    uint8_t ledc_timer;        /*!< LEDC timer (0-3) */
} pwm_load_config_t;

/**
 * @brief PWM Load Controller handle
 */
typedef void* pwm_load_handle_t;

/**
 * @brief Initialize PWM Load Controller
 * @param config Pointer to configuration structure
 * @return Handle to PWM load controller, NULL on error
 */
pwm_load_handle_t pwm_load_init(const pwm_load_config_t* config);

/**
 * @brief Deinitialize PWM Load Controller
 * @param handle PWM load controller handle
 * @return 0 on success, -1 on error
 */
int pwm_load_deinit(pwm_load_handle_t handle);

/**
 * @brief Set PWM duty cycle (0-100%)
 * @param handle PWM load controller handle
 * @param duty_percent Duty cycle in percent (0-100)
 * @return 0 on success, -1 on error
 */
int pwm_load_set_duty(pwm_load_handle_t handle, uint8_t duty_percent);

/**
 * @brief Get current PWM duty cycle
 * @param handle PWM load controller handle
 * @return Current duty cycle in percent (0-100), -1 on error
 */
int pwm_load_get_duty(pwm_load_handle_t handle);

/**
 * @brief Smooth startup - ramp up from 0% to 100%
 * @param handle PWM load controller handle
 * @param duration_ms Duration in milliseconds
 * @param steps Number of steps for smooth ramping (default 100)
 * @return 0 on success, -1 on error
 */
int pwm_load_ramp_up(pwm_load_handle_t handle, uint32_t duration_ms, uint16_t steps);

/**
 * @brief Smooth shutdown - ramp down from current level to 0%
 * @param handle PWM load controller handle
 * @param duration_ms Duration in milliseconds
 * @param steps Number of steps for smooth ramping (default 100)
 * @return 0 on success, -1 on error
 */
int pwm_load_ramp_down(pwm_load_handle_t handle, uint32_t duration_ms, uint16_t steps);

/**
 * @brief Ramp to target duty cycle
 * @param handle PWM load controller handle
 * @param target_duty Target duty cycle in percent (0-100)
 * @param duration_ms Duration in milliseconds
 * @param steps Number of steps for smooth ramping
 * @return 0 on success, -1 on error
 */
int pwm_load_ramp_to(pwm_load_handle_t handle, uint8_t target_duty, uint32_t duration_ms, uint16_t steps);

/**
 * @brief Check if ramping is in progress
 * @param handle PWM load controller handle
 * @return true if ramping, false otherwise
 */
bool pwm_load_is_ramping(pwm_load_handle_t handle);

/**
 * @brief Stop current ramping operation
 * @param handle PWM load controller handle
 * @return 0 on success, -1 on error
 */
int pwm_load_stop_ramp(pwm_load_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* PWM_LOAD_H */
