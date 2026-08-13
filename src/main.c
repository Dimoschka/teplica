/**
 *  Версия 1.1
 * @file main.c
 * @brief Управление форточкой теплицы по температуре
 *
 * Основные функции:
 * - Чтение температуры и влажности (SHT30)
 * - Автоматическое открытие/закрытие форточки
 * - Контроль тока двигателя (ACS712)
 * - Подключение к Wi-Fi, NTP, MQTT
 * - Публикация данных и приём команд
 * - Полив грядок через команды с mqtt брокера
 * - 
 */
#include "app_state.h" // Типы и глобальные переменные
#include "water_level/water_level.h" // Функции работы с датчиком уровня воды
#include "irrigation/irrigation.h" // Функциии полива
#include "tank_fill/tank_fill.h" // Функиции наполения бака
#include "../include/config.h"
#include "pwm_load.h"
#include "sht30.h"
#include "acs712.h"
#include "wi_fi_d.h"
#include "sntp_d.h"
#include "mqtt_d.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>




/* ========================================================================
 * Вспомогательные функции
 * ======================================================================== */

/**
 * @brief Загружает настройки из NVS, если нет — использует дефолты
 */
static void load_settings_from_nvs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("settings", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(MAIN_TAG, "NVS не открыт для чтения, используем дефолты: %s", esp_err_to_name(err));
        return;
    }

    // Загрузка температур (хранятся как int * 100)
    int32_t temp;
    if (nvs_get_i32(nvs_handle, "vent_open_temp", &temp) == ESP_OK) {
        g_vent_open_temp = temp / 100.0f;
    }
    if (nvs_get_i32(nvs_handle, "vent_close_temp", &temp) == ESP_OK) {
        g_vent_close_temp = temp / 100.0f;
    }

    // Загрузка длительности (секунды)
    if (nvs_get_i32(nvs_handle, "irrigation_dur", &temp) == ESP_OK) {
        g_irrigation_duration_s = temp;
    }
    if (nvs_get_i32(nvs_handle, "irrigation_speed", &temp) == ESP_OK) {
        if (temp >= 1 && temp <= 100) {
            g_irrigation_pump_speed = temp;
        } else {
            ESP_LOGW(MAIN_TAG, "Некорректная сохранённая скорость насоса: %d", temp);
        }
    }

    for (int i = 0; i < GARDEN_BEDS_COUNT; ++i) {
        char key[32];
        snprintf(key, sizeof(key), "garden%d_irrigation_pct", i + 1);
        if (nvs_get_i32(nvs_handle, key, &temp) == ESP_OK && temp >= 1 && temp <= 100) {
            g_garden_irrigation_pct[i] = temp;
        }

        snprintf(key, sizeof(key), "garden%d_irrigation_freq", i + 1);
        if (nvs_get_i32(nvs_handle, key, &temp) == ESP_OK && temp >= 1 && temp <= 4) {
            g_garden_irrigation_freq[i] = temp;
        }
    }

    nvs_close(nvs_handle);
    ESP_LOGI(MAIN_TAG, "Настройки загружены из NVS");
}

/**
 * @brief Сохраняет настройку в NVS
 */
static void save_setting_to_nvs(const char* key, int32_t value) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("settings", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(MAIN_TAG, "Не удалось открыть NVS для записи: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_i32(nvs_handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGE(MAIN_TAG, "Не удалось сохранить настройку %s: %s", key, esp_err_to_name(err));
    } else {
        nvs_commit(nvs_handle);
        ESP_LOGI(MAIN_TAG, "Настройка %s сохранена: %ld", key, value);
    }

    nvs_close(nvs_handle);
}

/**
 * @brief Проверяет ток двигателя. Если превышает допустимый — возвращает true
 * @param sensor Указатель на датчик ACS712
 * @return true если ток > MAX_ALLOWED_CURRENT
 */
bool is_overcurrent(acs712_t *sensor) {
    float current = acs712_get_current_dc(sensor, 5);
    ESP_LOGD(TAG_CURRENT, "Ток двигателя: %.2f А", current);

    if (current > MAX_ALLOWED_CURRENT) {
        ESP_LOGE(TAG_CURRENT, "❌ ПРЕВЫШЕНИЕ ТОКА: %.2f А > %.2f А", current, MAX_ALLOWED_CURRENT);
        return true;
    }
    return false;
}


/* ========================================================================
 * Задачи FreeRTOS
 * ======================================================================== */
/* ========================================================================
 * Автоматическое наполнение бака
 * ======================================================================== */

static void irrigation_task(void *arg) {
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


/**
 * @brief Управление форточкой: открывает/закрывает по температуре
 */
static void ventilation_task(void *arg) {
    while (1) {
        climate_data_t data;
        if (xQueueReceive(g_climate_queue, &data, portMAX_DELAY) != pdPASS) continue;

        bool should_be_open = (data.temperature >= g_vent_open_temp);
        bool should_be_closed = (data.temperature <= g_vent_close_temp);

        ESP_LOGI(TAG_VENT, "T=%.2f°C | Форточка: %s | Цель: %s",
                 data.temperature,
                 g_vent_position ? "открыта" : "закрыта",
                 should_be_open ? "открыть" : (should_be_closed ? "закрыть" : "ничего"));

        // Уже в нужном состоянии
        if ((g_vent_position && should_be_open) || (!g_vent_position && should_be_closed)) {
            ESP_LOGD(TAG_VENT, "Форточка уже в нужном положении");
            continue;
        }

        // Проверка: не в процессе ли операции?
        if (g_vent_state == VENT_OPENING || g_vent_state == VENT_CLOSING) {
            ESP_LOGW(TAG_VENT, "Операция уже выполняется — пропускаем");
            continue;
        }

        // === Открытие форточки ===
        if (should_be_open && !g_vent_position) {
            ESP_LOGI(TAG_VENT, "🔥 Открываем форточку — плавный старт двигателя");

            gpio_set_level(VENT_RELAY_GPIO, 0);
            pwm_load_ramp_to(g_motor_pwm, VENT_MOTOR_MAX_DUTY, VENT_RAMP_UP_TIME_MS, VENT_RAMP_STEPS);
            vTaskDelay(pdMS_TO_TICKS(VENT_RAMP_UP_TIME_MS));

            g_vent_state = VENT_OPENING;
            bool fault = false;
            TickType_t start = xTaskGetTickCount();

            while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(56000)) {
                if (is_overcurrent(&current_sensor)) {
                    ESP_LOGE(TAG_VENT, "🛑 Перегрузка — останавливаем двигатель");
                    fault = true;
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
            }

            pwm_load_ramp_to(g_motor_pwm, 0, VENT_RAMP_DOWN_TIME_MS, VENT_RAMP_STEPS);
            vTaskDelay(pdMS_TO_TICKS(VENT_RAMP_DOWN_TIME_MS));

            if (!fault) {
                g_vent_position = true;
                g_vent_state = VENT_OPEN;
                g_vent_open = true;
                ESP_LOGI(TAG_VENT, "✅ Форточка открыта");
            } else {
                g_vent_state = VENT_CLOSED;
                ESP_LOGW(TAG_VENT, "❗ Форточка не открыта из-за перегрузки");
            }
        }

        // === Закрытие форточки ===
        else if (should_be_closed && g_vent_position) {
            ESP_LOGI(TAG_VENT, "❄️ Закрываем форточку — плавный старт двигателя");

            gpio_set_level(VENT_RELAY_GPIO, 1);
            pwm_load_ramp_to(g_motor_pwm, VENT_MOTOR_MAX_DUTY, VENT_RAMP_UP_TIME_MS, VENT_RAMP_STEPS);
            vTaskDelay(pdMS_TO_TICKS(VENT_RAMP_UP_TIME_MS));

            g_vent_state = VENT_CLOSING;
            bool fault = false;
            TickType_t start = xTaskGetTickCount();

            while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(56000)) {
                if (is_overcurrent(&current_sensor)) {
                    ESP_LOGE(TAG_VENT, "🛑 Перегрузка — останавливаем двигатель");
                    fault = true;
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
            }

            pwm_load_ramp_to(g_motor_pwm, 0, VENT_RAMP_DOWN_TIME_MS, VENT_RAMP_STEPS);
            vTaskDelay(pdMS_TO_TICKS(VENT_RAMP_DOWN_TIME_MS));

            if (!fault) {
                g_vent_position = false;
                g_vent_state = VENT_CLOSED;
                g_vent_open = false;
                ESP_LOGI(TAG_VENT, "✅ Форточка закрыта");
            } else {
                g_vent_state = VENT_OPEN;
                ESP_LOGW(TAG_VENT, "❗ Форточка не закрыта из-за перегрузки");
            }
        }
    }
    vTaskDelete(NULL);
}

/**
 * @brief Читает данные SHT30 и отправляет в очередь управления
 */
static void climate_control_task(void *arg) {
    while (1) {
        if (sht30_read_temperature_humidity(&sht30_dev, &g_temperature, &g_humidity) == ESP_OK) {
            ESP_LOGI(MAIN_TAG, "T=%.2f°C H=%.2f%%", g_temperature, g_humidity);

            climate_data_t data = { .temperature = g_temperature, .humidity = g_humidity };
            if (xQueueSend(g_climate_queue, &data, pdMS_TO_TICKS(100)) != pdPASS) {
                ESP_LOGW(MAIN_TAG, "Не удалось отправить данные в очередь");
            }
        } else {
            ESP_LOGE(MAIN_TAG, "Failed to read SHT30");
        }
        vTaskDelay(pdMS_TO_TICKS(SHT30_MEASUREMENT_PERIOD_MS));
    }
    vTaskDelete(NULL);
}

/**
 * @brief Периодическая публикация данных в MQTT
 */
static void mqtt_publish_task(void *pvParameters) {
    static unsigned int publish_count = 0;
    while (1) {
        if (g_wifi_connected && g_mqtt_started) {
            ESP_LOGI(MAIN_TAG, "📤 MQTT публикация [#%d]", ++publish_count);
        
            
            mqttd_publish_type(CONFIG_TOPIC_STATUS, TYPE_CHAR, "online");
            mqttd_publish_type(CONFIG_TOPIC_TEMP, TYPE_FLOAT, &g_temperature);
            mqttd_publish_type(CONFIG_TOPIC_HUMID, TYPE_FLOAT, &g_humidity);
            const char *vent_state = g_vent_open ? "open" : "closed";
            mqttd_publish_type(CONFIG_TOPIC_VENT_STATE, TYPE_CHAR, vent_state);
            int water_level = water_level_read_percent();
            if (water_level < 0) {
                ESP_LOGW(MAIN_TAG, "Публикация уровня воды пропущена: датчик отключен");
                water_level = -1;
            }
            mqttd_publish_type(CONFIG_TOPIC_WATER_LEVEL, TYPE_INT, &water_level);

            const char *today_state = g_irrigation_happened_today ? "1" : "0";
            mqttd_publish_type(CONFIG_TOPIC_IRRIG_TODAY, TYPE_CHAR, today_state);

            char last_time_str[16] = "none";
            if (g_last_irrigation_time > 0) {
                struct tm timeinfo;
                localtime_r(&g_last_irrigation_time, &timeinfo);
                strftime(last_time_str, sizeof(last_time_str), "%H:%M", &timeinfo);
            }
            mqttd_publish_type(CONFIG_TOPIC_IRRIG_LAST_TIME, TYPE_CHAR, last_time_str);
            // Публикация состояния полива (маска грядок: "1,3" или "off")
            char irrig_status[16];
            irrigation_mask_to_string(g_irrigation_beds_mask, irrig_status, sizeof(irrig_status));
            mqttd_publish_type(CONFIG_TOPIC_IRRIG_STATE, TYPE_CHAR, irrig_status);
            
            // === Публикация статуса наполнения бака ===
            const char *tank_status = "idle";
            if (tank_fill_is_in_progress()) {
                tank_status = "filling";
            } else if (g_tank_fill_state == TANK_FILL_COMPLETE) {
                tank_status = "complete";
            } else if (g_tank_fill_state == TANK_FILL_ERROR) {
                tank_status = "error";
            }
            //ESP_LOGW(MAIN_TAG, "🚰 Публикация состояния бака: %s (код=%d)", tank_status, g_tank_fill_state);
            mqttd_publish_type(CONFIG_TOPIC_TANK_FILL_STATE, TYPE_CHAR, tank_status);
        } else {
            ESP_LOGW(MAIN_TAG, "❌ MQTT недоступен: WiFi=%d, MQTT=%d", g_wifi_connected, g_mqtt_started);
        }
        vTaskDelay(pdMS_TO_TICKS(10000)); // каждые 10 секунд
    }
    vTaskDelete(NULL);
}

/**
 * @brief Ежедневная синхронизация времени
 */
static void daily_sntp_task(void *pvParameters) {
    while (1) {
        if (g_wifi_connected && sntpIsTimeSet()) {
            time_t now;
            time(&now);
            struct tm timeinfo;
            localtime_r(&now, &timeinfo);

            if (timeinfo.tm_hour == CONFIG_DAILY_SYNC_HOUR && timeinfo.tm_min < 2) {
                ESP_LOGI(MAIN_TAG, "🔄 Ежедневная синхронизация времени");
                sntpStop();
                vTaskDelay(pdMS_TO_TICKS(1000));
                sntpStart();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(60000)); // раз в минуту
    }
    vTaskDelete(NULL);
}


/* ========================================================================
 * Callback-функции событий
 * ======================================================================== */

/**
 * @brief Обработчик событий Wi-Fi
 */
void wifi_event_handler(wifi_event_type_t event, const char *data) {
    switch (event) {
        case WIFI_EVENT_IP_OBTAINED:
            ESP_LOGI(MAIN_TAG, "✅ Wi-Fi подключён: %s", data);
            g_wifi_connected = true;

            sntpStart();
            if (!g_mqtt_started) {
                mqttd_config_t mqtt_cfg = {
                    .server = CONFIG_MQTT_BROKER,
                    .port = CONFIG_MQTT_PORT,
                    .user = CONFIG_MQTT_USER,
                    .pass = CONFIG_MQTT_PASS,
                    .client_id = CONFIG_MQTT_CLIENT_ID
                };
                if (mqttd_init(&mqtt_cfg)) {
                    mqttd_start();
                    g_mqtt_started = true;
                }
            }
            break;

        case WIFI_EVENT_CONNECTION_LOST:
            ESP_LOGW(MAIN_TAG, "⚠️ Wi-Fi соединение потеряно");
            g_wifi_connected = false;
            if (g_mqtt_started) {
                mqttd_stop();
                g_mqtt_started = false;
            }
            sntpStop();
            break;

        case WIFI_EVENT_MAX_RETRIES_EXCEEDED:
            ESP_LOGE(MAIN_TAG, "❌ Превышено число попыток подключения к Wi-Fi");
            break;
    }
}

/**
 * @brief Обработчик синхронизации времени
 */
void sntp_sync_handler(struct timeval *tv) {
    struct tm timeinfo;
    localtime_r(&tv->tv_sec, &timeinfo);
    ESP_LOGI(MAIN_TAG, "📅 Время синхронизировано: %s", asctime(&timeinfo));
}

/**
 * @brief Приём команд по MQTT
 */
void mqtt_receive_callback(const char* topic, const char* data, int data_len) {
    char value[data_len + 1];
    memcpy(value, data, data_len);
    value[data_len] = 0;

    ESP_LOGI(MAIN_TAG, "📩 Получено: %s = %s", topic, value);

    // === Управление наполнением бака ===
    if (strcmp(topic, CONFIG_TOPIC_TANK_FILL_CONTROL) == 0) {
        if (strcmp(value, "fill_tank") == 0) {
            ESP_LOGI("IRRIG", "Ручная команда: наполнить бак");
            
            // Проверяем текущее состояние
            if (tank_fill_is_in_progress()) {
                ESP_LOGW("IRRIG", "Наполнение бака уже в процессе, команда проигнорирована");
                mqttd_publish_type(CONFIG_TOPIC_TANK_FILL_STATE, TYPE_CHAR, "error_already_running");
                return;
            }
            
            int level = water_level_read_percent();
            if (level < 0) {
                ESP_LOGE("IRRIG", "❌ Наполнение невозможно: датчик уровня воды отключен");
                g_tank_fill_state = TANK_FILL_ERROR;
                mqttd_publish_type(CONFIG_TOPIC_TANK_FILL_STATE, TYPE_CHAR, "error_sensor");
                return;
            }
            
            g_tank_fill_manual_requested = true;
            mqttd_publish_type(CONFIG_TOPIC_TANK_FILL_STATE, TYPE_CHAR, "filling");
        }
        else if (strcmp(value, "stop_fill") == 0) {
            ESP_LOGI("IRRIG", "Ручная команда: остановить наполнение бака");
            g_tank_fill_manual_requested = false;
            tank_fill_stop();
            g_tank_fill_state = TANK_FILL_IDLE;
            mqttd_publish_type(CONFIG_TOPIC_TANK_FILL_STATE, TYPE_CHAR, "idle");
        }
        return;
    }

    // === Остальные команды ===
    climate_data_t cmd = { .humidity = 50, .force_update = true };

    if (strcmp(value, "open") == 0) {
        ESP_LOGI(MAIN_TAG, "指令: Открыть форточку");
        cmd.temperature = VENT_OPEN_TEMP + 1;
        xQueueSend(g_climate_queue, &cmd, 0);
    }
    else if (strcmp(value, "close") == 0) {
        ESP_LOGI(MAIN_TAG, "指令: Закрыть форточку");
        cmd.temperature = VENT_CLOSE_TEMP - 1;
        xQueueSend(g_climate_queue, &cmd, 0);
    }

    // === Управление поливом по MQTT (on_1, on_1-3, on_1,3, off) ===
    else if (strcmp(topic, CONFIG_TOPIC_IRRIG_CONTROL) == 0) {
        ESP_LOGI("IRRIG", "MQTT команда полива: %s", value);
        
        uint8_t new_mask = irrigation_parse_command(value);
        
        if (strcmp(value, "off") == 0) {
            // Явная команда выключения: очищаем маску и таймер
            g_irrigation_beds_mask = 0;
            g_irrigation_disconnect_deadline = 0;
            irrigation_apply_mask(0);
            ESP_LOGI("IRRIG", "✓ Полив выключен (команда off)");
        } else if (new_mask != 0) {
            // Команда включения: применяем маску и сбрасываем таймер отключения
            g_irrigation_beds_mask = new_mask;
            g_irrigation_disconnect_deadline = 0;
            irrigation_apply_mask(new_mask);
            char beds_status[16];
            irrigation_mask_to_string(new_mask, beds_status, sizeof(beds_status));
            ESP_LOGI("IRRIG", "✓ Полив включен для грядок: %s", beds_status);
            // === Зафиксировать полив сегодня ===
            g_irrigation_happened_today = true;
            g_last_irrigation_time = time(NULL);
        } else if (new_mask == 0 && strcmp(value, "off") != 0) {
            // Неизвестная команда
            ESP_LOGW("IRRIG", "⚠️ Неизвестная команда полива: %s", value);
            mqttd_publish_type(CONFIG_TOPIC_IRRIG_STATE, TYPE_CHAR, "unknown_command");
            return;
        }
        
        // Публикуем новый статус
        if (g_wifi_connected && g_mqtt_started) {
            char irrig_status[16];
            irrigation_mask_to_string(g_irrigation_beds_mask, irrig_status, sizeof(irrig_status));
            mqttd_publish_type(CONFIG_TOPIC_IRRIG_STATE, TYPE_CHAR, irrig_status);
        }
        return;
    }

    // === Конфигурационные команды ===
    else if (strcmp(topic, CONFIG_TOPIC_CONFIG_VENT_OPEN_TEMP) == 0) {
        cJSON *json = cJSON_Parse(value);
        if (json && cJSON_IsNumber(json)) {
            float new_temp = cJSON_GetNumberValue(json);
            if (new_temp >= 0 && new_temp <= 50) {
                g_vent_open_temp = new_temp;
                save_setting_to_nvs("vent_open_temp", (int32_t)(new_temp * 100));
                ESP_LOGI(MAIN_TAG, "Температура открытия форточки установлена: %.1f°C", new_temp);
            } else {
                ESP_LOGW(MAIN_TAG, "Некорректная температура открытия: %.1f°C", new_temp);
            }
        }
        cJSON_Delete(json);
    }
    else if (strcmp(topic, CONFIG_TOPIC_CONFIG_VENT_CLOSE_TEMP) == 0) {
        cJSON *json = cJSON_Parse(value);
        if (json && cJSON_IsNumber(json)) {
            float new_temp = cJSON_GetNumberValue(json);
            if (new_temp >= 0 && new_temp <= 50) {
                g_vent_close_temp = new_temp;
                save_setting_to_nvs("vent_close_temp", (int32_t)(new_temp * 100));
                ESP_LOGI(MAIN_TAG, "Температура закрытия форточки установлена: %.1f°C", new_temp);
            } else {
                ESP_LOGW(MAIN_TAG, "Некорректная температура закрытия: %.1f°C", new_temp);
            }
        }
        cJSON_Delete(json);
    }
    else if (strcmp(topic, CONFIG_TOPIC_CONFIG_GARDEN1_IRRIGATION_PCT) == 0 ||
             strcmp(topic, CONFIG_TOPIC_CONFIG_GARDEN2_IRRIGATION_PCT) == 0 ||
             strcmp(topic, CONFIG_TOPIC_CONFIG_GARDEN3_IRRIGATION_PCT) == 0) {
        cJSON *json = cJSON_Parse(value);
        if (json && cJSON_IsNumber(json)) {
            int new_pct = (int)cJSON_GetNumberValue(json);
            if (new_pct >= 1 && new_pct <= 100) {
                int bed_index = 0;
                if (strcmp(topic, CONFIG_TOPIC_CONFIG_GARDEN2_IRRIGATION_PCT) == 0) {
                    bed_index = 1;
                } else if (strcmp(topic, CONFIG_TOPIC_CONFIG_GARDEN3_IRRIGATION_PCT) == 0) {
                    bed_index = 2;
                }
                g_garden_irrigation_pct[bed_index] = new_pct;
                char key[32];
                snprintf(key, sizeof(key), "garden%d_irrigation_pct", bed_index + 1);
                save_setting_to_nvs(key, new_pct);
                ESP_LOGI(MAIN_TAG, "Процент полива грядки %d установлен: %d%%", bed_index + 1, new_pct);
            } else {
                ESP_LOGW(MAIN_TAG, "Некорректный процент полива: %d", new_pct);
            }
        }
        cJSON_Delete(json);
    }
    else if (strcmp(topic, CONFIG_TOPIC_CONFIG_GARDEN1_IRRIGATION_FREQ) == 0 ||
             strcmp(topic, CONFIG_TOPIC_CONFIG_GARDEN2_IRRIGATION_FREQ) == 0 ||
             strcmp(topic, CONFIG_TOPIC_CONFIG_GARDEN3_IRRIGATION_FREQ) == 0) {
        cJSON *json = cJSON_Parse(value);
        if (json && cJSON_IsNumber(json)) {
            int new_freq = (int)cJSON_GetNumberValue(json);
            if (new_freq >= 1 && new_freq <= 4) {
                int bed_index = 0;
                if (strcmp(topic, CONFIG_TOPIC_CONFIG_GARDEN2_IRRIGATION_FREQ) == 0) {
                    bed_index = 1;
                } else if (strcmp(topic, CONFIG_TOPIC_CONFIG_GARDEN3_IRRIGATION_FREQ) == 0) {
                    bed_index = 2;
                }
                g_garden_irrigation_freq[bed_index] = new_freq;
                g_garden_irrigation_done_today[bed_index] = 0;
                char key[32];
                snprintf(key, sizeof(key), "garden%d_irrigation_freq", bed_index + 1);
                save_setting_to_nvs(key, new_freq);
                ESP_LOGI(MAIN_TAG, "Частота полива грядки %d установлена: %d раз/сутки", bed_index + 1, new_freq);
            } else {
                ESP_LOGW(MAIN_TAG, "Некорректная частота полива: %d", new_freq);
            }
        }
        cJSON_Delete(json);
    }
    else if (strcmp(topic, CONFIG_TOPIC_CONFIG_IRRIGATION_DURATION) == 0) {
        cJSON *json = cJSON_Parse(value);
        if (json && cJSON_IsNumber(json)) {
            int new_duration = (int)cJSON_GetNumberValue(json);
            if (new_duration >= 0 && new_duration <= 3600) {
                g_irrigation_duration_s = new_duration;
                save_setting_to_nvs("irrigation_dur", new_duration);
                ESP_LOGI(MAIN_TAG, "Длительность полива установлена: %d сек", new_duration);
            } else {
                ESP_LOGW(MAIN_TAG, "Некорректная длительность полива: %d сек", new_duration);
            }
        }
        cJSON_Delete(json);
    }
    else if (strcmp(topic, CONFIG_TOPIC_CONFIG_IRRIGATION_SPEED) == 0) {
        cJSON *json = cJSON_Parse(value);
        if (json && cJSON_IsNumber(json)) {
            int new_speed = (int)cJSON_GetNumberValue(json);
            if (new_speed >= 1 && new_speed <= 100) {
                g_irrigation_pump_speed = new_speed;
                save_setting_to_nvs("irrigation_speed", new_speed);
                ESP_LOGI(MAIN_TAG, "Скорость насоса полива установлена: %d%%", new_speed);
            } else {
                ESP_LOGW(MAIN_TAG, "Некорректная скорость насоса полива: %d", new_speed);
            }
        }
        cJSON_Delete(json);
    }
}


/* ========================================================================
 * Инициализация и точка входа
 * ======================================================================== */

/**
 * @brief Проверка наличия критических конфигурационных параметров
 * @return true если все параметры указаны, false если какой-то пуст
 */
static bool validate_credentials(void) {
    bool valid = true;
    
    // Проверка Wi-Fi credentials
    if (strlen(WIFI_SSID) == 0) {
        ESP_LOGE(MAIN_TAG, "❌ WIFI_SSID не установлен в config.h");
        valid = false;
    }
    if (strlen(WIFI_PASS) == 0) {
        ESP_LOGE(MAIN_TAG, "❌ WIFI_PASS не установлен в config.h");
        valid = false;
    }
    
    // Проверка MQTT credentials
    if (strlen(CONFIG_MQTT_BROKER) == 0) {
        ESP_LOGE(MAIN_TAG, "❌ CONFIG_MQTT_BROKER не установлен в config.h");
        valid = false;
    }
    if (strlen(CONFIG_MQTT_PASS) == 0) {
        ESP_LOGE(MAIN_TAG, "❌ CONFIG_MQTT_PASS не установлен в config.h");
        valid = false;
    }
    
    if (!valid) {
        ESP_LOGE(MAIN_TAG, "⛔ ОШИБКА: Не все необходимые параметры конфигурации установлены!");
        ESP_LOGE(MAIN_TAG, "   Отредактируйте include/config.h и установите:");
        ESP_LOGE(MAIN_TAG, "   - WIFI_SSID и WIFI_PASS");
        ESP_LOGE(MAIN_TAG, "   - CONFIG_MQTT_BROKER и CONFIG_MQTT_PASS");
    }
    
    return valid;
}

/**
 * @brief Точка входа приложения
 */
void app_main() {
    // === Проверка обязательных конфигурационных параметров ===
    if (!validate_credentials()) {
        ESP_LOGE(MAIN_TAG, "⛔ Приложение остановлено: недостаточно данных конфигурации");
        vTaskDelay(pdMS_TO_TICKS(2000));
        return;
    }
    
    ESP_LOGI(MAIN_TAG, "✓ Конфигурация проверена успешно");

    // === Загрузка настроек из NVS ===
    load_settings_from_nvs();

    // === Инициализация датчика тока ===
    acs712_init(&current_sensor, ADC_CHANNEL_0, 66.0f, ADC_BITWIDTH_DEFAULT, ADC_ATTEN_DB_12);
    acs712_begin(&current_sensor);
    g_adc1_handle = current_sensor.adc_handle;

    ESP_LOGI(TAG_CURRENT, "Калибровка датчика тока...");
    acs712_calibrate_zero(&current_sensor, 50);
    ESP_LOGI(TAG_CURRENT, "Калибровка завершена. Offset: %d", acs712_get_zero_offset(&current_sensor));

    // === Инициализация PWM двигателя ===
    pwm_load_config_t motor_config = {
        .gpio_pin = VENT_MOTOR_PWM_GPIO,
        .ledc_channel = VENT_MOTOR_CHANNEL,
        .ledc_timer = VENT_MOTOR_TIMER,
        .frequency = VENT_MOTOR_FREQ_HZ,
        .resolution_bits = VENT_MOTOR_RESOLUTION
    };

    g_motor_pwm = pwm_load_init(&motor_config);
    if (!g_motor_pwm) {
        ESP_LOGE(TAG_VENT, "Failed to initialize motor PWM");
        return;
    }
    
    // === Инициализация GPIO для клапанов и наполнения бака ===
    gpio_reset_pin(VALVE_GARDEN1_GPIO);
    gpio_set_direction(VALVE_GARDEN1_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(VALVE_GARDEN1_GPIO, 0);

    gpio_reset_pin(VALVE_GARDEN2_GPIO);
    gpio_set_direction(VALVE_GARDEN2_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(VALVE_GARDEN2_GPIO, 0);

    gpio_reset_pin(VALVE_GARDEN3_GPIO);
    gpio_set_direction(VALVE_GARDEN3_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(VALVE_GARDEN3_GPIO, 0);

    gpio_reset_pin(TANK_FILL_GPIO);
    gpio_set_direction(TANK_FILL_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(TANK_FILL_GPIO, 0);
   
    // === Настройка реле ===
    gpio_reset_pin(VENT_RELAY_GPIO);
    gpio_set_direction(VENT_RELAY_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(VENT_RELAY_GPIO, 1);

    // === Очередь управления ===
    g_climate_queue = xQueueCreate(5, sizeof(climate_data_t));
    if (!g_climate_queue) {
        ESP_LOGE(MAIN_TAG, "Failed to create climate queue");
        return;
    }

     // === Инициализация ADC для уровня воды ===
    if (water_level_init() != ESP_OK) return;

    // === Инициализация насоса ===
    if (irrigation_pump_init() != ESP_OK) return;
    if (sht30_init(&sht30_dev, SHT30_I2C_PORT, SHT30_SDA_PIN, SHT30_SCL_PIN, SHT30_ADDR_DEFAULT) != ESP_OK) {
        ESP_LOGE(MAIN_TAG, "Failed to initialize SHT30");
        return;
    }

    // === Запуск задач ===
    xTaskCreate(ventilation_task, "ventilation", 2048, NULL, 6, &g_ventilation_task_handle);
    xTaskCreate(climate_control_task, "climate_ctrl", 4096, NULL, 5, NULL);

    // === Сетевые сервисы ===
    wifiRegisterEventCallback(wifi_event_handler);
    sntpSetTimezone(CONFIG_TIMEZONE);
    sntpSetServers(CONFIG_NTP_SERVER1, CONFIG_NTP_SERVER2);
    sntpRegisterCallback(sntp_sync_handler);

    mqttd_add_tx_topic_ex(CONFIG_TOPIC_STATUS, TYPE_CHAR);
    mqttd_add_tx_topic_ex(CONFIG_TOPIC_TEMP, TYPE_FLOAT);
    mqttd_add_tx_topic_ex(CONFIG_TOPIC_HUMID, TYPE_FLOAT);
    mqttd_add_tx_topic_ex(CONFIG_TOPIC_VENT_STATE, TYPE_CHAR);
    mqttd_add_rx_topic(CONFIG_TOPIC_VENT_CONTROL, mqtt_receive_callback);

     // === Регистрация  MQTT топиков  для полива ===
    mqttd_add_tx_topic_ex(CONFIG_TOPIC_WATER_LEVEL, TYPE_INT);      // Уровень воды
    mqttd_add_tx_topic_ex(CONFIG_TOPIC_IRRIG_STATE, TYPE_CHAR);     // Состояние полива
    mqttd_add_tx_topic_ex(CONFIG_TOPIC_IRRIG_TODAY, TYPE_CHAR);     // Полив сегодня
    mqttd_add_tx_topic_ex(CONFIG_TOPIC_IRRIG_LAST_TIME, TYPE_CHAR); // Время последнего полива
    mqttd_add_rx_topic(CONFIG_TOPIC_IRRIG_CONTROL, mqtt_receive_callback);  // Команды
    
    // === Регистрация MQTT топиков для наполнения бака ===
    mqttd_add_tx_topic_ex(CONFIG_TOPIC_TANK_FILL_STATE, TYPE_CHAR);    // Статус наполнения
    mqttd_add_rx_topic(CONFIG_TOPIC_TANK_FILL_CONTROL, mqtt_receive_callback);  // Команды управления

    // === Регистрация MQTT топиков для конфигурации ===
    mqttd_add_rx_topic(CONFIG_TOPIC_CONFIG_VENT_OPEN_TEMP, mqtt_receive_callback);
    mqttd_add_rx_topic(CONFIG_TOPIC_CONFIG_VENT_CLOSE_TEMP, mqtt_receive_callback);
    mqttd_add_rx_topic(CONFIG_TOPIC_CONFIG_GARDEN1_IRRIGATION_PCT, mqtt_receive_callback);
    mqttd_add_rx_topic(CONFIG_TOPIC_CONFIG_GARDEN1_IRRIGATION_FREQ, mqtt_receive_callback);
    mqttd_add_rx_topic(CONFIG_TOPIC_CONFIG_GARDEN2_IRRIGATION_PCT, mqtt_receive_callback);
    mqttd_add_rx_topic(CONFIG_TOPIC_CONFIG_GARDEN2_IRRIGATION_FREQ, mqtt_receive_callback);
    mqttd_add_rx_topic(CONFIG_TOPIC_CONFIG_GARDEN3_IRRIGATION_PCT, mqtt_receive_callback);
    mqttd_add_rx_topic(CONFIG_TOPIC_CONFIG_GARDEN3_IRRIGATION_FREQ, mqtt_receive_callback);
    mqttd_add_rx_topic(CONFIG_TOPIC_CONFIG_IRRIGATION_DURATION, mqtt_receive_callback);
    mqttd_add_rx_topic(CONFIG_TOPIC_CONFIG_IRRIGATION_SPEED, mqtt_receive_callback);

    wifiStart();
    // === Запуск задачи мониторинга уровня ===
    xTaskCreate(
        water_level_monitor_task, 
        "water_level_mon", 
        4096, 
        NULL, 
        3, 
        NULL
    );
     // === Запуск задачи полива ===
    xTaskCreate(irrigation_task, "irrigation", 4096, NULL, 4, NULL);
    // === Запуск задачи ежедневной синхронизации времени ===
    xTaskCreate(daily_sntp_task, "daily_sntp", 2048, NULL, 4, NULL);
    // === Запуск задачи публикации в MQTT ===
    xTaskCreate(mqtt_publish_task, "mqtt_pub", 4096, NULL, 5, NULL);
}
