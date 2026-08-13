#include "water_level.h"
#include "../app_state.h"
#include "../../include/config.h"

#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


/**
 * @brief Инициализация ADC для уровня воды
 */
esp_err_t water_level_init(void) {
    if (!g_adc1_handle) {
        ESP_LOGE(MAIN_TAG, "ADC1 handle не инициирован");
        return ESP_FAIL;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT
    };
    if (adc_oneshot_config_channel(g_adc1_handle, WATER_LEVEL_ADC_CHAN, &chan_cfg) != ESP_OK) {
        ESP_LOGE(MAIN_TAG, "Ошибка конфигурации канала ADC");
        return ESP_FAIL;
    }

    // Калибровка ADC
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_line_fitting(&cali_config, &g_cali_handle) != ESP_OK) {
        ESP_LOGE(MAIN_TAG, "Ошибка создания схемы калибровки ADC");
        return ESP_FAIL;
    }

    return ESP_OK;
}
/**
 * @brief Считывает мгновенное напряжение с датчика уровня воды (ADC) в мВ
 * @return Напряжение в мВ, -1 при ошибке
 */
int water_level_read_voltage_mv(void) {
    if (!g_adc1_handle) {
        ESP_LOGE(MAIN_TAG, "ADC1 handle не инициирован");
        return -1;
    }

    int raw = 0;
    if (adc_oneshot_read(g_adc1_handle, WATER_LEVEL_ADC_CHAN, &raw) != ESP_OK) {
        ESP_LOGE(MAIN_TAG, "Ошибка чтения ADC для уровня воды");
        return -1;
    }

    // Преобразуем raw → мВ через калибровку
    int voltage_mv = 0;
    if (adc_cali_raw_to_voltage(g_cali_handle, raw, &voltage_mv) != ESP_OK) {
        ESP_LOGE(MAIN_TAG, "Ошибка перевода raw в мВ");
        return -1;
    }

    return voltage_mv;
}


/**
 * @brief Определяет шаг уровня воды (0-9, где 0=пусто, 9=полно)
 * @return Шаг (0-9), -1 при отключенном датчике или ошибке
 */
int water_level_read_step(void) {
    int voltage_mv = g_water_level_median_mv;
    
    
    if (voltage_mv < 0) {
        return -1;  // Ошибка чтения ADC
    }

    if (voltage_mv <= WATER_LEVEL_SENSOR_DISCONNECTED_MV) {
        ESP_LOGW(MAIN_TAG, "Датчик уровня воды отключен (напряжение %d мВ ≤ %d мВ)",
                 voltage_mv, WATER_LEVEL_SENSOR_DISCONNECTED_MV);
        return -1;  // Датчик отключен
    }

    // Проверка мертвой зоны (магнит между герконами)
    if (voltage_mv == WATER_LEVEL_DEADZONE_MV) {
        ESP_LOGD(MAIN_TAG, "Датчик в мертвой зоне (810 мВ), используем предыдущее значение");
        if (g_previous_water_level_percent >= 0) {
            return g_previous_water_level_percent / 10;  // Вернуть предыдущий шаг
        }
        // Если предыдущего значения нет, считаем это полным баком
        g_previous_water_level_percent = 100;
        return 9;  // Шаг 9 соответствует 100%
    }

    // Массив напряжений для каждого шага (0..9)
    static const int step_voltages[WATER_LEVEL_STEPS] = {
        WATER_LEVEL_STEP_0_MV,
        WATER_LEVEL_STEP_1_MV,
        WATER_LEVEL_STEP_2_MV,
        WATER_LEVEL_STEP_3_MV,
        WATER_LEVEL_STEP_4_MV,
        WATER_LEVEL_STEP_5_MV,
        WATER_LEVEL_STEP_6_MV,
        WATER_LEVEL_STEP_7_MV,
        WATER_LEVEL_STEP_8_MV,
        WATER_LEVEL_STEP_9_MV
    };

    // Определяем ближайший шаг по абсолютной разнице (простое округление)
    int best_step = 0;
    int best_diff = abs(voltage_mv - step_voltages[0]);

    for (int step = 1; step < WATER_LEVEL_STEPS; step++) {
        int diff = abs(voltage_mv - step_voltages[step]);
        if (diff < best_diff) {
            best_diff = diff;
            best_step = step;
        }
    }

    // Ограничим шаг диапазоном [0, 9]
    if (best_step < 0) best_step = 0;
    if (best_step > 9) best_step = 9;

    ESP_LOGD(MAIN_TAG, "Напряжение: %d мВ → Шаг %d (%d%%)", 
             voltage_mv, best_step, best_step * 10);
    g_previous_water_level_percent = best_step * 10;
    return best_step;
}

/**
 * @brief Считывает уровень воды в процентах (0–100%, кратно 10%)
 * @return Уровень в процентах (0, 10, 20... 100), -1 при отключенном датчике или ошибке
 */
int water_level_read_percent(void) {
    int step = water_level_read_step();
    
    if (step < 0) {
        return -1;  // Ошибка чтения
    }

    // Преобразуем шаг в проценты (0-9 → 0-90% + 100% для шага 9)
    int percent = (step < WATER_LEVEL_STEPS - 1) ? (step * 10) : 100;
    
    return percent;
}

/**
 * @brief Задача мониторинга уровня воды с фильтрацией шумов
 */
void water_level_monitor_task(void *arg) {
    while (1) {
        const int N_SAMPLES = 5;
        int samples[N_SAMPLES];

        // Считываем несколько значений
        for (int i = 0; i < N_SAMPLES; i++) {
            samples[i] = water_level_read_voltage_mv();  // ВАЖНО: это оригинальная функция (читает ADC напрямую)
            if (samples[i] < 0) {
                ESP_LOGW(MAIN_TAG, "Ошибка чтения ADC для мониторинга уровня");
                g_water_level_median_mv = -1; // Ошибка — сбросим глобальную переменную
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(50)); // Небольшая задержка между замерами
        }

        if (samples[0] < 0) {
            // Пропускаем обработку, если ошибка на первом замере
        } else {
            // Сортировка массива (пузырьковая)
            for (int i = 0; i < N_SAMPLES - 1; i++) {
                for (int j = 0; j < N_SAMPLES - i - 1; j++) {
                    if (samples[j] > samples[j + 1]) {
                        int tmp = samples[j];
                        samples[j] = samples[j + 1];
                        samples[j + 1] = tmp;
                    }
                }
            }

            // Вычисляем медиану
            int median = samples[N_SAMPLES / 2];

            // Записываем в глобальную переменную
            g_water_level_median_mv = median;

            // Логируем редко (каждые 30 секунд)
            static int counter = 0;
            if (++counter >= 6) { // 6 * 5 сек = ~30 сек
                char log_buf[128];
                int len = snprintf(log_buf, sizeof(log_buf), "Уровень воды: [");
                for (int i = 0; i < N_SAMPLES; i++) {
                    len += snprintf(log_buf + len, sizeof(log_buf) - len, "%d ", samples[i]);
                }
                snprintf(log_buf + len - 1, sizeof(log_buf) - len + 1, "] → медиана=%d мВ", median);
                ESP_LOGD(MAIN_TAG, "%s", log_buf);
                counter = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5000)); // Обновление каждые 5 секунд
    }
    vTaskDelete(NULL);
}