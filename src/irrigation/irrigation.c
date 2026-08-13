#include "irrigation.h"

#include "../../include/config.h"
#include "../app_state.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "pwm_load.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

const char *TAG = "IRRIG";

/**
 * @brief Инициализация насоса (PWM)
 */
esp_err_t irrigation_pump_init(void) {
    pwm_load_config_t pump_cfg = {
        .gpio_pin = WATER_PUMP_GPIO,
        .ledc_channel = LEDC_CHANNEL_1,
        .ledc_timer = LEDC_TIMER_1,
        .frequency = PUMP_PWM_FREQ_HZ,
        .resolution_bits = PUMP_PWM_RES_BITS
    };
    g_pump_pwm = pwm_load_init(&pump_cfg);
    if (!g_pump_pwm) {
        ESP_LOGE(MAIN_TAG, "Не удалось инициализировать PWM насоса");
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * @brief Включает насос с заданной мощностью
 */
static bool irrigation_start_pump(int duty_percent) {
    if (g_pump_pwm == NULL) {
        ESP_LOGE(TAG, "PWM насоса не инициализирован");
        return false;
        }
    int duty = CLAMP(duty_percent, 0, 100);
    pwm_load_set_duty(g_pump_pwm, duty);
    ESP_LOGI("IRRIG", "Насос включён: %d%%", duty);
    return true;
}

/**
 * @brief Выключает насос
 */
static void irrigation_stop_pump(void) {
    pwm_load_set_duty(g_pump_pwm, 0);
    ESP_LOGI("IRRIG", "Насос выключен");
}


/**
 * @brief Открывает клапан грядки
 */
static void irrigation_open_valve(int gpio) {
    gpio_set_level(gpio, 1);
    ESP_LOGI("IRRIG", "Клапан на GPIO%d открыт", gpio);
}

/**
 * @brief Закрывает клапан
 */
static void irrigation_close_valve(int gpio) {
    gpio_set_level(gpio, 0);
    ESP_LOGI("IRRIG", "Клапан на GPIO%d закрыт", gpio);
}

// Получение GPIO для клапана грядки по индексу
static int irrigation_get_valve_gpio(int bed_index) {
    switch (bed_index) {
        case 0: return VALVE_GARDEN1_GPIO;
        case 1: return VALVE_GARDEN2_GPIO;
        case 2: return VALVE_GARDEN3_GPIO;
        default: return VALVE_GARDEN1_GPIO;
    }
}


/**
 * @brief Преобразует маску в строку вида "1,3" для публикации статуса
 */
void irrigation_mask_to_string(uint8_t mask, char* buf, size_t size) {
    if (mask == 0) {
        snprintf(buf, size, "off");
        return;
    }
    
    int pos = 0;
    for (int bed = 0; bed < GARDEN_BEDS_COUNT; ++bed) {
        if (mask & (1 << bed)) {
            if (pos > 0) pos += snprintf(buf + pos, size - pos, ",");
            pos += snprintf(buf + pos, size - pos, "%d", bed + 1);
        }
    }
    if (pos == 0) {
        snprintf(buf, size, "off");
    }
}

/**
 * @brief Парсит строку команды полива (on_1, on_1-3, on_1,3, off) и возвращает маску грядок
 * @return Маска грядок (0 если команда невалидна или это off)
 */
uint8_t irrigation_parse_command(const char* cmd) {
    uint8_t mask = 0;
    
    // Команда выключения
    if (strcmp(cmd, "off") == 0) {
        return 0;
    }
    
    // Проверяем префикс "on_"
    if (strncmp(cmd, "on_", 3) != 0) {
        ESP_LOGW("IRRIG", "Неизвестная команда: %s", cmd);
        return 0;
    }
    
    const char* beds_str = cmd + 3; // Пропускаем "on_"
    char temp[64];
    strncpy(temp, beds_str, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = 0;
    
    char* saveptr = NULL;
    char* token = strtok_r(temp, ",", &saveptr);
    
    while (token != NULL) {
        // Проверяем формат "X-Y" (диапазон)
        char* dash = strchr(token, '-');
        if (dash != NULL) {
            int start = atoi(token);
            int end = atoi(dash + 1);
            if (start >= 1 && end >= 1 && start <= GARDEN_BEDS_COUNT && end <= GARDEN_BEDS_COUNT && start <= end) {
                for (int i = start; i <= end; ++i) {
                    mask |= (1 << (i - 1));
                }
                ESP_LOGD("IRRIG", "Добавлен диапазон грядок %d-%d", start, end);
            } else {
                ESP_LOGW("IRRIG", "Некорректный диапазон: %s", token);
            }
        } else {
            // Одна грядка
            int bed = atoi(token);
            if (bed >= 1 && bed <= GARDEN_BEDS_COUNT) {
                mask |= (1 << (bed - 1));
                ESP_LOGD("IRRIG", "Добавлена грядка %d", bed);
            } else {
                ESP_LOGW("IRRIG", "Некорректный номер грядки: %d", bed);
            }
        }
        token = strtok_r(NULL, ",", &saveptr);
    }
    
    return mask;
}

/**
 * @brief Применяет маску к грядкам: открывает/закрывает клапаны согласно маске.
 * Сначала включает насос, ждёт набора давления, затем управляет клапанами.
 */
void irrigation_apply_mask(uint8_t mask) {
    char beds_str[16];
    irrigation_mask_to_string(mask, beds_str, sizeof(beds_str));
    ESP_LOGI("IRRIG", "Применение маски полива: грядки %s", beds_str);

    // === Сначала включаем насос ===
    if (mask != 0) {
        irrigation_start_pump(g_irrigation_pump_speed);

        // === Ждём набора давления (важно для гидравлического удара) ===
       // vTaskDelay(pdMS_TO_TICKS(CONFIG_IRRIG_PRESSURE_SETTLE_TIME_MS));  // или IRRIG_PRESSURE_SETTLE_TIME_MS из config.h

        // === Теперь открываем/закрываем клапаны ===
        for (int bed = 0; bed < GARDEN_BEDS_COUNT; ++bed) {
            int gpio = irrigation_get_valve_gpio(bed);
            if (mask & (1 << bed)) {
                irrigation_open_valve(gpio);
            } else {
                irrigation_close_valve(gpio);
            }
        }
    } else {
        // === Выключение: сначала клапаны, потом насос ===
        for (int bed = 0; bed < GARDEN_BEDS_COUNT; ++bed) {
            int gpio = irrigation_get_valve_gpio(bed);
            irrigation_close_valve(gpio);
        }
        irrigation_stop_pump();
    }
}