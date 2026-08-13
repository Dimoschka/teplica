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

static const char *TAG = "IRRIG_CTRL";


// Функция сброса дневных флагов полива и наполнения бака
static void reset_daily_flags(const struct tm *timeinfo) {
            if (timeinfo == NULL) {
            return;
        }

        if (timeinfo->tm_hour != 0 ||
            timeinfo->tm_min != 0 ||
            timeinfo->tm_sec >= 10) {
            return;
        }

        g_tank_filled = false;
        g_irrigation_happened_today = false;
        g_last_irrigation_time = 0;

        for (int bed = 0; bed < GARDEN_BEDS_COUNT; ++bed) {
            g_garden_irrigation_done_today[bed] = 0;
        }

        ESP_LOGI(TAG, "Дневные флаги полива сброшены");
    }

// Функция публикации состояния наполнения бака через MQTT
static void publish_tank_state(const char *state)
    {
        if (state == NULL) {
            return;
        }

        if (!g_wifi_connected || !g_mqtt_started) {
            return;
        }

        mqttd_publish_type(
            CONFIG_TOPIC_TANK_FILL_STATE,
            TYPE_CHAR,
            state
        );
    }
static void check_irrigation_connection_timeout(void)
    {
        if (g_irrigation_beds_mask == 0) {
            return;
        }

        time_t now;
        time(&now);

        if ((!g_wifi_connected || !g_mqtt_started) &&
            g_irrigation_disconnect_deadline == 0) {
            g_irrigation_disconnect_deadline = now + 30 * 60;

            ESP_LOGW(
                TAG,
                "Связь потеряна, полив будет отключён через 30 минут"
            );
        }

        if (g_irrigation_disconnect_deadline != 0 &&
            now >= g_irrigation_disconnect_deadline) {
            ESP_LOGW(TAG, "Таймер отключения полива истёк");

            irrigation_apply_mask(0);
            g_irrigation_beds_mask = 0;
            g_irrigation_disconnect_deadline = 0;
        }
    }
// Функция обработки ручного наполнения бака    
static void process_manual_tank_fill(void)
    {
        ESP_LOGI(TAG, "Запуск ручного наполнения бака");

        tank_fill_start();

        bool fill_complete = false;
        bool sensor_error = false;

        for (int i = 0; i < 600; ++i) {
            vTaskDelay(pdMS_TO_TICKS(1000));

            if (!g_tank_fill_manual_requested) {
                ESP_LOGI(TAG, "Ручное наполнение остановлено пользователем");
                tank_fill_stop();
                g_tank_fill_state = TANK_FILL_IDLE;
                publish_tank_state("idle");
                return;
            }

            int current_level = water_level_read_percent();

            if (current_level < 0) {
                ESP_LOGW(TAG, "Ошибка датчика уровня при ручном наполнении");

                tank_fill_stop();
                g_tank_fill_state = TANK_FILL_ERROR;
                publish_tank_state("error_sensor");

                g_tank_fill_manual_requested = false;
                return;
            }

            if (g_water_level_median_mv >= WATER_LEVEL_FILL_MAX_MV) {
                ESP_LOGI(TAG, "Бак заполнен");
                fill_complete = true;
                break;
            }

            if (i % 10 == 0) {
                ESP_LOGD(
                    TAG,
                    "Ручное наполнение: %d%%, прошло %d сек",
                    current_level,
                    i
                );
            }
        }

        tank_fill_stop();
        g_tank_fill_manual_requested = false;

        if (fill_complete) {
            g_tank_fill_state = TANK_FILL_COMPLETE;
            publish_tank_state("complete");
        } else if (!sensor_error) {
            g_tank_fill_state = TANK_FILL_ERROR;
            publish_tank_state("timeout");
        }

        g_tank_fill_state = TANK_FILL_IDLE;
    } 
// Функция обработки планового наполнения бака   
static void process_scheduled_tank_fill(int hour)
        {
            int level = water_level_read_percent();

            if (level < 0) {
                ESP_LOGW(TAG, "Невозможно проверить уровень воды");
                g_tank_fill_state = TANK_FILL_IDLE;
                return;
            }

            if (g_tank_filled ||
                hour < g_fill_tank_start_hour ||
                hour > g_fill_tank_end_hour ||
                level >= 100) {
                return;
            }

            ESP_LOGI(
                TAG,
                "Запуск планового наполнения бака, уровень %d%%",
                level
            );

            tank_fill_start();

            bool fill_complete = false;
            bool sensor_error = false;

            for (int i = 0; i < 600; ++i) {
                vTaskDelay(pdMS_TO_TICKS(1000));

                int current_level = water_level_read_percent();

                if (current_level < 0) {
                    ESP_LOGW(TAG, "Ошибка датчика уровня при наполнении");

                    tank_fill_stop();
                    g_tank_fill_state = TANK_FILL_ERROR;
                    publish_tank_state("error_sensor");

                    sensor_error = true;
                    break;
                }

                if (g_water_level_median_mv >= WATER_LEVEL_FILL_MAX_MV) {
                    fill_complete = true;
                    break;
                }

                if (i % 10 == 0) {
                    ESP_LOGD(
                        TAG,
                        "Наполнение: %d%%, прошло %d сек",
                        current_level,
                        i
                    );
                }
            }

            // Гарантированно выключаем наполнение после цикла
            tank_fill_stop();

            if (fill_complete && !sensor_error) {
                g_tank_filled = true;
                g_tank_fill_state = TANK_FILL_COMPLETE;
                publish_tank_state("complete");
            } else if (sensor_error) {
                g_tank_fill_state = TANK_FILL_ERROR;
                publish_tank_state("error_sensor");
            } else {
                // Таймаут не считаем успешным заполнением
                g_tank_fill_state = TANK_FILL_ERROR;
                publish_tank_state("timeout");
            }

            g_tank_fill_state = TANK_FILL_IDLE;
        }

/* ========================================================================
 * Автоматическое наполнение бака
 * ======================================================================== */

void irrigation_controller_task(void *arg)
{
    (void)arg;

    while (1) {
        time_t now;
        struct tm timeinfo;

        time(&now);
        localtime_r(&now, &timeinfo);

        reset_daily_flags(&timeinfo);

        if (g_tank_fill_manual_requested &&
            !tank_fill_is_in_progress()) {
            process_manual_tank_fill();
        }

        process_scheduled_tank_fill(timeinfo.tm_hour);
        check_irrigation_connection_timeout();

        vTaskDelay(pdMS_TO_TICKS(5000));
    } 
    vTaskDelete(NULL);
}
