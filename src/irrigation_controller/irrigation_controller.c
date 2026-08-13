#include "irrigation_controller.h"

#include "../app_state.h"
#include "../irrigation/irrigation.h"
#include "../tank_fill/tank_fill.h"
#include "../water_level/water_level.h"
#include "../../include/config.h"

#include "mqtt_d.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <time.h>

/* ========================================================================
 * Автоматическое наполнение бака
 * ======================================================================== */

void irrigation_controller_task(void *arg) {
    time_t now;
    struct tm timeinfo;

    while (1) {
        time(&now);
        localtime_r(&now, &timeinfo);

        int hour = timeinfo.tm_hour;
        int day_changed = (timeinfo.tm_min == 0 && timeinfo.tm_sec < 10);

        // Сброс флагов в начале дня
        if (day_changed && hour == 0) {
            g_tank_filled = false;
            g_irrigation_happened_today = false;
            g_last_irrigation_time = 0;
            for (int bed = 0; bed < GARDEN_BEDS_COUNT; ++bed) {
                g_garden_irrigation_done_today[bed] = 0;
            }
            ESP_LOGI("IRRIG", "Сброс флагов полива на новый день");
        }

        // 🔹 Обработка ручного запроса наполнения бака
        if (g_tank_fill_manual_requested && g_tank_fill_state != TANK_FILL_IN_PROGRESS) {
            ESP_LOGI("IRRIG", "Запуск ручного наполнения бака");
           tank_fill_start();

            // Мониторинг ручного наполнения
            bool fill_complete = false;
            bool sensor_error = false;
            
            for (int i = 0; i < 600; i++) {  // Максимум 10 минут
                vTaskDelay(pdMS_TO_TICKS(1000));
                
                // Проверяем флаг отмены (если пришла команда stop)
                if (!g_tank_fill_manual_requested) {
                    ESP_LOGI("IRRIG", "Ручное наполнение остановлено пользователем");
                    tank_fill_stop();
                    g_tank_fill_state = TANK_FILL_IDLE;
                    ESP_LOGI(MAIN_TAG, "💧 [БАКА] Наполнение отменено (вручную)");
                    if (g_wifi_connected && g_mqtt_started) mqttd_publish_type(CONFIG_TOPIC_TANK_FILL_STATE, TYPE_CHAR, "idle");
                    break;
                }
                
                int current_level = water_level_read_percent();
                if (current_level < 0) {
                    ESP_LOGW("IRRIG", "Датчик уровня отключен во время ручного наполнения");
                    sensor_error = true;
                    g_tank_fill_state = TANK_FILL_ERROR;
                    if (g_wifi_connected && g_mqtt_started) mqttd_publish_type(CONFIG_TOPIC_TANK_FILL_STATE, TYPE_CHAR, "error_sensor");
                    break;
                }
                
                // Проверяем достижение максимума
                if (g_water_level_median_mv >= WATER_LEVEL_FILL_MAX_MV) {
                    ESP_LOGI("IRRIG", "Ручное наполнение завершено: максимальный уровень достигнут");
                    fill_complete = true;
                    break;
                }
                
                // Логируем прогресс каждые 10 секунд
                if (i % 10 == 0) {
                    ESP_LOGD("IRRIG", "Ручное наполнение: уровень %d%%, прошло %d сек", current_level, i);
                }
            }

            if (fill_complete || !sensor_error) {
                tank_fill_stop();
                g_tank_fill_state = TANK_FILL_COMPLETE;
                if (g_wifi_connected && g_mqtt_started) mqttd_publish_type(CONFIG_TOPIC_TANK_FILL_STATE, TYPE_CHAR, "complete");
                ESP_LOGI("IRRIG", "Ручное наполнение завершено");
            }
            
            g_tank_fill_manual_requested = false;
            g_tank_fill_state = TANK_FILL_IDLE;
        }

        // 🔹 Наполнение бака днём (если ещё не заполняли и уровень низкий)
        int level = water_level_read_percent();
        if (!g_tank_filled &&  // Бак ещё не заполняли сегодня
            hour >= g_fill_tank_start_hour && hour <= g_fill_tank_end_hour &&
            level >= 0 && level < 100) {
            ESP_LOGI("IRRIG", "Запуск наполнения бака (уровень %d%%)", level);
            tank_fill_start();

            // Мониторинг наполнения с контролем напряжения датчика
            bool fill_complete = false;
            bool sensor_error = false;
            
            for (int i = 0; i < 600; i++) {  // Максимум 10 минут (600 сек)
                vTaskDelay(pdMS_TO_TICKS(1000));
                
                int current_level = water_level_read_percent();
                if (current_level < 0) {
                    ESP_LOGW("IRRIG", "Датчик уровня отключен во время наполнения бака");
                    sensor_error = true;
                    g_tank_fill_state = TANK_FILL_ERROR;
                    if (g_wifi_connected && g_mqtt_started) mqttd_publish_type(CONFIG_TOPIC_TANK_FILL_STATE, TYPE_CHAR, "error_sensor");
                    break;
                }
                
                // Проверяем достижение максимума при наполнении (2500 мВ)
                if (g_water_level_median_mv >= WATER_LEVEL_FILL_MAX_MV) {
                    ESP_LOGI("IRRIG", "Бак заполнен до максимума (2500 мВ), текущий уровень %d%%", current_level);
                    fill_complete = true;
                    break;
                }
                
                // Логируем прогресс каждые 10 секунд
                if (i % 10 == 0) {
                    ESP_LOGD("IRRIG", "Наполнение в процессе: уровень %d%%, прошло %d сек", current_level, i);
                }
            }

            tank_fill_stop();
            if (fill_complete && !sensor_error) {
                g_tank_filled = true;
                g_tank_fill_state = TANK_FILL_COMPLETE;
                if (g_wifi_connected && g_mqtt_started) mqttd_publish_type(CONFIG_TOPIC_TANK_FILL_STATE, TYPE_CHAR, "complete");
                ESP_LOGI("IRRIG", "Бак наполнен успешно");
            } else if (sensor_error) {
                ESP_LOGW("IRRIG", "Наполнение бака прервано: датчик отключен");
            } else {
                g_tank_filled = true;
                g_tank_fill_state = TANK_FILL_COMPLETE;
                if (g_wifi_connected && g_mqtt_started) mqttd_publish_type(CONFIG_TOPIC_TANK_FILL_STATE, TYPE_CHAR, "complete");
                ESP_LOGI("IRRIG", "Бак наполнен (таймаут или неполный)");
            }
            g_tank_fill_state = TANK_FILL_IDLE;
        } else if (level < 0) {
            ESP_LOGW("IRRIG", "Невозможно проверить уровень воды: датчик отключен");
            g_tank_fill_state = TANK_FILL_IDLE;
        } else {
            g_tank_fill_state = TANK_FILL_IDLE;
        }

        // 🔹 Режим ручного/командного полива
        // Автополив по расписанию отключен: управление только по MQTT-командам (on_1, on_1-3, on_1,3, off)

        // Мониторинг состояния связи и таймера автоматического отключения полива
        if (g_irrigation_beds_mask != 0) {
            time_t now_check;
            time(&now_check);

            // Если связь потеряна и таймер ещё не установлен — установить на 30 минут
            if ((!g_wifi_connected || !g_mqtt_started) && g_irrigation_disconnect_deadline == 0) {
                g_irrigation_disconnect_deadline = now_check + (30 * 60);
                ESP_LOGW("IRRIG", "Связь потеряна — установлен таймер отключения полива через 30 минут (until %ld)", g_irrigation_disconnect_deadline);
            }

            // Если таймер установлен и истёк — форсированно выключаем полив
            if (g_irrigation_disconnect_deadline != 0 && now_check >= g_irrigation_disconnect_deadline) {
                ESP_LOGW("IRRIG", "Таймер отключения полива истёк — выключаем полив");
                irrigation_apply_mask(0);
                g_irrigation_beds_mask = 0;
                g_irrigation_disconnect_deadline = 0;
                if (g_wifi_connected && g_mqtt_started) {
                    mqttd_publish_type(CONFIG_TOPIC_IRRIG_STATE, TYPE_CHAR, "off_timeout");
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5000)); // Проверка каждые 5 секунд
    }
    vTaskDelete(NULL);
}
