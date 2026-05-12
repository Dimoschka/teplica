/**
 * @file main.c
 * @brief Управление форточкой теплицы по температуре
 *
 * Основные функции:
 * - Чтение температуры и влажности (SHT30)
 * - Автоматическое открытие/закрытие форточки
 * - Контроль тока двигателя (ACS712)
 * - Подключение к Wi-Fi, NTP, MQTT
 * - Публикация данных и приём команд
 */

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
#include <string.h>
#include "cJSON.h"





/* ========================================================================
 * Типы и структуры
 * ======================================================================== */

/**
 * @brief Состояние форточки
 */
typedef enum {
    VENT_CLOSED,
    VENT_OPENING,
    VENT_OPEN,
    VENT_CLOSING
} vent_state_t;

/**
 * @brief Сообщение для задачи управления климатом
 */
typedef struct {
    float temperature;
    float humidity;
    bool force_update;  // Принудительное обновление
} climate_data_t;


/* ========================================================================
 * Глобальные переменные
 * ======================================================================== */

 // Данные датчиков

static bool g_tank_filled = false;  // Флаг: бак заполнен сегодня
static bool g_irrigation_done = false;  // Полив выполнен сегодня


// Данные датчиков
float g_temperature = 0.0f;
float g_humidity = 0.0f;
bool g_vent_open = false;  // положение форточки (для MQTT)

// Флаги состояния сети
bool g_wifi_connected = false;
bool g_mqtt_started = false;

// Устройства
static pwm_load_handle_t g_motor_pwm = NULL;
static QueueHandle_t g_climate_queue = NULL;
static TaskHandle_t g_ventilation_task_handle = NULL;

// Драйверы
static acs712_t current_sensor;
static pwm_load_handle_t g_pump_pwm = NULL;
static adc_oneshot_unit_handle_t g_adc1_handle = NULL;
static adc_cali_handle_t g_cali_handle = NULL;
static sht30_t sht30_dev;

// Состояние форточки
static vent_state_t g_vent_state = VENT_CLOSED;
static bool g_vent_position = false;  // false = закрыто, true = открыто

// Настраиваемые параметры (сохранение в NVS)
static float g_vent_open_temp = DEFAULT_VENT_OPEN_TEMP;
static float g_vent_close_temp = DEFAULT_VENT_CLOSE_TEMP;
static int g_irrigation_hour = DEFAULT_IRRIGATION_HOUR;
static int g_fill_tank_start_hour = DEFAULT_FILL_TANK_START_HOUR;
static int g_fill_tank_end_hour = DEFAULT_FILL_TANK_END_HOUR;
static int g_irrigation_duration_s = DEFAULT_IRRIGATION_DURATION_S;

/* ========================================================================
 * Вспомогательные функции для полива
 * ======================================================================== */

/**
 * @brief Инициализация насоса (PWM)
 */
static esp_err_t init_water_pump(void) {
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
 * @brief Инициализация ADC для уровня воды
 */
static esp_err_t init_water_level_sensor(void) {
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
 * @brief Считывает напряжение датчика уровня воды в мВ
 * @return Напряжение в мВ, -1 при ошибке чтения ADC
 */
static int read_water_level_voltage_mv(void) {
    int raw;
    if (adc_oneshot_read(g_adc1_handle, WATER_LEVEL_ADC_CHAN, &raw) != ESP_OK) {
        ESP_LOGW(MAIN_TAG, "Ошибка чтения ADC уровня воды");
        return -1;
    }

    int voltage_mv;
    if (adc_cali_raw_to_voltage(g_cali_handle, raw, &voltage_mv) != ESP_OK) {
        ESP_LOGW(MAIN_TAG, "Ошибка калибровки ADC");
        return -1;
    }

    return voltage_mv;
}

/**
 * @brief Считывает уровень воды в процентах (0–100%)
 * @return Уровень в процентах, -1 при отключенном датчике или ошибке
 */
static int read_water_level_percent(void) {
    int voltage_mv = read_water_level_voltage_mv();
    if (voltage_mv < 0) {
        return -1;  // Ошибка чтения
    }

    if (voltage_mv == 0) {
        ESP_LOGW(MAIN_TAG, "Датчик уровня воды отключен (0 мВ)");
        return -1;  // Отключенный датчик
    }

    // Градиент погрешности: если напряжение в зоне пустого бака с погрешностью, считаем пустым
    if (voltage_mv <= WATER_LEVEL_EMPTY_MV + WATER_LEVEL_EMPTY_MARGIN_MV) {
        return 0;
    }

    // Если напряжение в зоне полного бака с погрешностью, считаем полным
    if (voltage_mv >= WATER_LEVEL_FULL_MV - WATER_LEVEL_FULL_MARGIN_MV) {
        return 100;
    }

    // Линейная интерполяция между зонами с погрешностью
    int empty_with_margin = WATER_LEVEL_EMPTY_MV + WATER_LEVEL_EMPTY_MARGIN_MV;
    int full_with_margin = WATER_LEVEL_FULL_MV - WATER_LEVEL_FULL_MARGIN_MV;
    int range_mv = full_with_margin - empty_with_margin;

    if (range_mv <= 0) {
        ESP_LOGE(MAIN_TAG, "Некорректные настройки датчика уровня воды");
        return -1;
    }

    int percent = ((voltage_mv - empty_with_margin) * 100) / range_mv;
    return CLAMP(percent, 0, 100);
}

/**
 * @brief Открывает клапан грядки
 */
static void open_valve(int gpio) {
    gpio_set_level(gpio, 1);
    ESP_LOGI("IRRIG", "Клапан на GPIO%d открыт", gpio);
}

/**
 * @brief Закрывает клапан
 */
static void close_valve(int gpio) {
    gpio_set_level(gpio, 0);
    ESP_LOGI("IRRIG", "Клапан на GPIO%d закрыт", gpio);
}

/**
 * @brief Включает насос с заданной мощностью
 */
static void start_pump(uint32_t duty) {
    pwm_load_set_duty(g_pump_pwm, duty);
    ESP_LOGI("IRRIG", "Насос включён: %d%%", (int)(duty * 100 / PUMP_MAX_DUTY));
}

/**
 * @brief Выключает насос
 */
static void stop_pump(void) {
    pwm_load_set_duty(g_pump_pwm, 0);
    ESP_LOGI("IRRIG", "Насос выключен");
}

/**
 * @brief Включает наполнение бака
 */
static void start_tank_fill(void) {
    gpio_set_level(TANK_FILL_GPIO, 1);
    ESP_LOGI("IRRIG", "Запуск наполнения бака");
}

/**
 * @brief Останавливает наполнение бака
 */
static void stop_tank_fill(void) {
    gpio_set_level(TANK_FILL_GPIO, 0);
    ESP_LOGI("IRRIG", "Наполнение бака остановлено");
}
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

    // Загрузка времени (часы)
    if (nvs_get_i32(nvs_handle, "irrigation_hour", &temp) == ESP_OK) {
        g_irrigation_hour = temp;
    }
    if (nvs_get_i32(nvs_handle, "fill_start_hour", &temp) == ESP_OK) {
        g_fill_tank_start_hour = temp;
    }
    if (nvs_get_i32(nvs_handle, "fill_end_hour", &temp) == ESP_OK) {
        g_fill_tank_end_hour = temp;
    }

    // Загрузка длительности (секунды)
    if (nvs_get_i32(nvs_handle, "irrigation_dur", &temp) == ESP_OK) {
        g_irrigation_duration_s = temp;
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
 * Автоматический полив и наполнене
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
            g_irrigation_done = false;
            g_tank_filled = false;
            ESP_LOGI("IRRIG", "Сброс флагов полива на новый день");
        }

        // 🔹 Наполнение бака днём (если ещё не заполняли и уровень низкий)
        int level = read_water_level_percent();
        if (!g_tank_filled &&
            hour >= g_fill_tank_start_hour && hour <= g_fill_tank_end_hour &&
            level >= 0 && level < 20) {
            ESP_LOGI("IRRIG", "Запуск наполнения бака (уровень %d%%)", level);
            start_tank_fill();

            // Мониторинг наполнения с проверкой датчика
            bool fill_aborted = false;
            for (int i = 0; i < 30; i++) {  // 30 секунд
                vTaskDelay(pdMS_TO_TICKS(1000));
                int current_level = read_water_level_percent();
                if (current_level < 0) {
                    ESP_LOGW("IRRIG", "Датчик уровня отключен во время наполнения бака, прерываем");
                    fill_aborted = true;
                    break;
                }
            }

            stop_tank_fill();
            if (!fill_aborted) {
                g_tank_filled = true;
                ESP_LOGI("IRRIG", "Бак наполнен");
            }
        } else if (level < 0) {
            ESP_LOGW("IRRIG", "Невозможно проверить уровень воды: датчик отключен");
        }

        // 🔹 Автоматический полив вечером
        if (!g_irrigation_done &&
            hour == g_irrigation_hour && timeinfo.tm_min == 0 && timeinfo.tm_sec < 5) {
            int level = read_water_level_percent();
            if (level < 0) {
                ESP_LOGW("IRRIG", "❗ Датчик уровня воды отключен, полив пропущен");
            } else if (level < 30) {
                ESP_LOGW("IRRIG", "❗ Уровень воды низкий (%d%%), полив пропущен", level);
            } else {
                ESP_LOGI("IRRIG", "🌿 Запуск автоматического полива (уровень %d%%)", level);

                open_valve(VALVE_GARDEN1_GPIO);
                open_valve(VALVE_GARDEN2_GPIO);
                open_valve(VALVE_GARDEN3_GPIO);
                start_pump(PUMP_MAX_DUTY * 0.8);  // 80%

                // Мониторинг полива с проверкой датчика каждые 30 секунд
                bool irrigation_aborted = false;
                int duration_remaining = g_irrigation_duration_s;
                while (duration_remaining > 0) {
                    int check_interval = (duration_remaining > 30) ? 30 : duration_remaining;
                    vTaskDelay(pdMS_TO_TICKS(check_interval * 1000));
                    duration_remaining -= check_interval;

                    int current_level = read_water_level_percent();
                    if (current_level < 0) {
                        ESP_LOGW("IRRIG", "Датчик уровня отключен во время полива, прерываем");
                        irrigation_aborted = true;
                        break;
                    }
                }

                stop_pump();
                close_valve(VALVE_GARDEN1_GPIO);
                close_valve(VALVE_GARDEN2_GPIO);
                close_valve(VALVE_GARDEN3_GPIO);

                if (!irrigation_aborted) {
                    g_irrigation_done = true;
                    ESP_LOGI("IRRIG", "✅ Полив завершён");
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
    while (1) {
        if (g_wifi_connected && g_mqtt_started) {
            mqttd_publish_type(CONFIG_TOPIC_STATUS, TYPE_CHAR, "online");
            mqttd_publish_type(CONFIG_TOPIC_TEMP, TYPE_FLOAT, &g_temperature);
            mqttd_publish_type(CONFIG_TOPIC_HUMID, TYPE_FLOAT, &g_humidity);
            const char *vent_state = g_vent_open ? "open" : "closed";
            mqttd_publish_type(CONFIG_TOPIC_VENT_STATE, TYPE_CHAR, vent_state);
            int water_level = read_water_level_percent();
            if (water_level < 0) {
                ESP_LOGW(MAIN_TAG, "Публикация уровня воды пропущена: датчик отключен");
                // Можно опубликовать -1 как индикатор ошибки
                water_level = -1;
            }
            mqttd_publish_type(CONFIG_TOPIC_WATER_LEVEL, TYPE_INT, &water_level);
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

      // === Управление поливом ===
    else if (strcmp(value, "irrigate") == 0) {
        ESP_LOGI("IRRIG", "Получена команда на ручной полив");

        int level = read_water_level_percent();
        if (level < 0) {
            ESP_LOGE("IRRIG", "❌ Полив невозможен: датчик уровня воды отключен");
            mqttd_publish_type(CONFIG_TOPIC_IRRIG_STATE, TYPE_CHAR, "error_sensor_disconnected");
        } else {
            open_valve(VALVE_GARDEN1_GPIO);
            open_valve(VALVE_GARDEN2_GPIO);
            open_valve(VALVE_GARDEN3_GPIO);
            start_pump(PUMP_MAX_DUTY * 0.8);

            // Мониторинг ручного полива
            bool irrigation_aborted = false;
            for (int i = 0; i < 60; i++) {  // 60 секунд
                vTaskDelay(pdMS_TO_TICKS(1000));
                int current_level = read_water_level_percent();
                if (current_level < 0) {
                    ESP_LOGW("IRRIG", "Датчик уровня отключен во время ручного полива, прерываем");
                    irrigation_aborted = true;
                    break;
                }
            }

            stop_pump();
            close_valve(VALVE_GARDEN1_GPIO);
            close_valve(VALVE_GARDEN2_GPIO);
            close_valve(VALVE_GARDEN3_GPIO);

            if (!irrigation_aborted) {
                mqttd_publish_type(CONFIG_TOPIC_IRRIG_STATE, TYPE_CHAR, "done");
                ESP_LOGI("IRRIG", "✅ Ручной полив завершён");
            } else {
                mqttd_publish_type(CONFIG_TOPIC_IRRIG_STATE, TYPE_CHAR, "error_sensor_disconnected");
            }
        }
    }
    else if (strcmp(value, "fill_tank") == 0) {
        ESP_LOGI("IRRIG", "Ручная команда: наполнить бак");
        start_tank_fill();
        vTaskDelay(pdMS_TO_TICKS(60000)); // 1 минута
        stop_tank_fill();
        mqttd_publish_type(CONFIG_TOPIC_IRRIG_STATE, TYPE_CHAR, "tank_filled");
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
    else if (strcmp(topic, CONFIG_TOPIC_CONFIG_IRRIGATION_HOUR) == 0) {
        cJSON *json = cJSON_Parse(value);
        if (json && cJSON_IsNumber(json)) {
            int new_hour = (int)cJSON_GetNumberValue(json);
            if (new_hour >= 0 && new_hour <= 23) {
                g_irrigation_hour = new_hour;
                save_setting_to_nvs("irrigation_hour", new_hour);
                ESP_LOGI(MAIN_TAG, "Час полива установлен: %d", new_hour);
            } else {
                ESP_LOGW(MAIN_TAG, "Некорректный час полива: %d", new_hour);
            }
        }
        cJSON_Delete(json);
    }
    else if (strcmp(topic, CONFIG_TOPIC_CONFIG_FILL_TANK_START_HOUR) == 0) {
        cJSON *json = cJSON_Parse(value);
        if (json && cJSON_IsNumber(json)) {
            int new_hour = (int)cJSON_GetNumberValue(json);
            if (new_hour >= 0 && new_hour <= 23) {
                g_fill_tank_start_hour = new_hour;
                save_setting_to_nvs("fill_start_hour", new_hour);
                ESP_LOGI(MAIN_TAG, "Час начала наполнения бака установлен: %d", new_hour);
            } else {
                ESP_LOGW(MAIN_TAG, "Некорректный час начала наполнения: %d", new_hour);
            }
        }
        cJSON_Delete(json);
    }
    else if (strcmp(topic, CONFIG_TOPIC_CONFIG_FILL_TANK_END_HOUR) == 0) {
        cJSON *json = cJSON_Parse(value);
        if (json && cJSON_IsNumber(json)) {
            int new_hour = (int)cJSON_GetNumberValue(json);
            if (new_hour >= 0 && new_hour <= 23) {
                g_fill_tank_end_hour = new_hour;
                save_setting_to_nvs("fill_end_hour", new_hour);
                ESP_LOGI(MAIN_TAG, "Час окончания наполнения бака установлен: %d", new_hour);
            } else {
                ESP_LOGW(MAIN_TAG, "Некорректный час окончания наполнения: %d", new_hour);
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
    if (init_water_level_sensor() != ESP_OK) return;

    // === Инициализация насоса ===
    if (init_water_pump() != ESP_OK) return;
    if (sht30_init(&sht30_dev, SHT30_I2C_PORT, SHT30_SDA_PIN, SHT30_SCL_PIN, SHT30_ADDR_DEFAULT) != ESP_OK) {
        ESP_LOGE(MAIN_TAG, "Failed to initialize SHT30");
        return;
    }

    // === Запуск задач ===
    xTaskCreate(ventilation_task, "ventilation", 2048, NULL, 6, &g_ventilation_task_handle);
    xTaskCreate(climate_control_task, "climate_ctrl", 2048, NULL, 5, NULL);

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
    mqttd_add_rx_topic(CONFIG_TOPIC_IRRIG_CONTROL, mqtt_receive_callback);  // Команды

    // === Регистрация MQTT топиков для конфигурации ===
    mqttd_add_rx_topic(CONFIG_TOPIC_CONFIG_VENT_OPEN_TEMP, mqtt_receive_callback);
    mqttd_add_rx_topic(CONFIG_TOPIC_CONFIG_VENT_CLOSE_TEMP, mqtt_receive_callback);
    mqttd_add_rx_topic(CONFIG_TOPIC_CONFIG_IRRIGATION_HOUR, mqtt_receive_callback);
    mqttd_add_rx_topic(CONFIG_TOPIC_CONFIG_FILL_TANK_START_HOUR, mqtt_receive_callback);
    mqttd_add_rx_topic(CONFIG_TOPIC_CONFIG_FILL_TANK_END_HOUR, mqtt_receive_callback);
    mqttd_add_rx_topic(CONFIG_TOPIC_CONFIG_IRRIGATION_DURATION, mqtt_receive_callback);

    wifiStart();
     // === Запуск задачи полива ===
    xTaskCreate(irrigation_task, "irrigation", 2048, NULL, 4, NULL);
    // === Запуск задачи ежедневной синхронизации времени ===
    xTaskCreate(daily_sntp_task, "daily_sntp", 2048, NULL, 4, NULL);
    // === Запуск задачи публикации в MQTT ===
    xTaskCreate(mqtt_publish_task, "mqtt_pub", 2048, NULL, 5, NULL);
}
