/**
 * @file config.h
 * @brief Общие настройки устройства
 */

#ifndef CONFIG_H
#define CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__has_include)
#  if __has_include("config_local.h")
#    include "config_local.h"
#  endif
#endif


// ========================================
// === Wi-Fi Настройки ====================
// ========================================

// Основная сеть

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

// Максимальное количество попыток подключения
#define WIFI_MAX_RETRIES    5


// ========================================
// === Время и SNTP =======================
// ========================================

// Часовой пояс (пример: MSK, UTC, Europe/Moscow)
#define CONFIG_TIMEZONE     "MSK-3"

// NTP серверы
#define CONFIG_NTP_SERVER1  "pool.ntp.org"
#define CONFIG_NTP_SERVER2  "time.nist.gov"

// Период ежедневной синхронизации (час)
#define CONFIG_DAILY_SYNC_HOUR  3


// ========================================
// === MQTT Настройки =====================
// ========================================

// Брокер

#ifndef CONFIG_MQTT_BROKER
#define CONFIG_MQTT_BROKER ""
#endif

#ifndef CONFIG_MQTT_PORT
#define CONFIG_MQTT_PORT 1883
#endif

#ifndef CONFIG_MQTT_USER
#define CONFIG_MQTT_USER ""
#endif

#ifndef CONFIG_MQTT_PASS
#define CONFIG_MQTT_PASS ""
#endif

#define CONFIG_MQTT_CLIENT_ID   "greenhouse_controller"

#define MQTTD_MAX_TOPICS 25   // Максимальное количество топиков должно быть меньше 25

// Топики
#define CONFIG_TOPIC_STATUS       "greenhouse/status"
#define CONFIG_TOPIC_TEMP         "greenhouse/temperature"
#define CONFIG_TOPIC_HUMID        "greenhouse/humidity"
#define CONFIG_TOPIC_VENT_STATE   "greenhouse/vent_state"
#define CONFIG_TOPIC_VENT_CONTROL "greenhouse/vent_control"
#define CONFIG_TOPIC_WATER_LEVEL     "greenhouse/sensors/water_level"
#define CONFIG_TOPIC_IRRIG_STATE      "greenhouse/status/irrigation"
#define CONFIG_TOPIC_IRRIG_CONTROL    "greenhouse/control/irrigation"
#define CONFIG_TOPIC_IRRIG_TODAY      "greenhouse/status/irrigation_today"
#define CONFIG_TOPIC_IRRIG_LAST_TIME  "greenhouse/status/irrigation_last_time"
#define CONFIG_TOPIC_TANK_FILL_STATE  "greenhouse/status/tank_fill"
#define CONFIG_TOPIC_TANK_FILL_CONTROL "greenhouse/control/tank_fill"


// ========================================
// === MQTT Топики для конфигурации =======
// ========================================

#define CONFIG_TOPIC_CONFIG_VENT_OPEN_TEMP       "greenhouse/config/vent_open_temp"
#define CONFIG_TOPIC_CONFIG_VENT_CLOSE_TEMP      "greenhouse/config/vent_close_temp"
#define CONFIG_TOPIC_CONFIG_GARDEN1_IRRIGATION_PCT "greenhouse/config/garden1_irrigation_pct"
#define CONFIG_TOPIC_CONFIG_GARDEN1_IRRIGATION_FREQ "greenhouse/config/garden1_irrigation_freq"
#define CONFIG_TOPIC_CONFIG_GARDEN2_IRRIGATION_PCT "greenhouse/config/garden2_irrigation_pct"
#define CONFIG_TOPIC_CONFIG_GARDEN2_IRRIGATION_FREQ "greenhouse/config/garden2_irrigation_freq"
#define CONFIG_TOPIC_CONFIG_GARDEN3_IRRIGATION_PCT "greenhouse/config/garden3_irrigation_pct"
#define CONFIG_TOPIC_CONFIG_GARDEN3_IRRIGATION_FREQ "greenhouse/config/garden3_irrigation_freq" 
#define CONFIG_TOPIC_CONFIG_IRRIGATION_DURATION  "greenhouse/config/irrigation_duration"
#define CONFIG_TOPIC_CONFIG_IRRIGATION_SPEED     "greenhouse/config/irrigation_speed"  // Скорость насоса полива, значение 1..100


// ========================================
// === Дефолтные значения настроек ========
// ========================================

#define DEFAULT_VENT_OPEN_TEMP      28.0f
#define DEFAULT_VENT_CLOSE_TEMP     25.0f
#define DEFAULT_FILL_TANK_START_HOUR 10
#define DEFAULT_FILL_TANK_END_HOUR    16
#define DEFAULT_IRRIGATION_DURATION_S 300
#define GARDEN_BEDS_COUNT             3
#define DEFAULT_GARDEN_IRRIGATION_PCT 20
#define DEFAULT_GARDEN_IRRIGATION_FREQ 1
#define GARDEN_IRRIGATION_FALLBACK_DURATION_S 300
#define GARDEN_IRRIGATION_MANUAL_DURATION_S 600
#define GARDEN_IRRIGATION_MAX_DURATION_S 900
#define GARDEN_IRRIGATION_SCHEDULE_START_HOUR 8
#define GARDEN_IRRIGATION_SCHEDULE_END_HOUR 20


// ========================================
// === Датчик SHT30 =======================
// ========================================

#define SHT30_I2C_PORT      I2C_NUM_1
#define SHT30_SDA_PIN       GPIO_NUM_21
#define SHT30_SCL_PIN       GPIO_NUM_22
#define SHT30_ADDR          SHT30_ADDR_DEFAULT
#define SHT30_MEASUREMENT_PERIOD_MS 30000  // Период измерения температуры и влажности (30 секунд)


// ========================================
// === Управление форточкой ===============
// ========================================

// Пороги температуры (°C)
#define VENT_OPEN_TEMP      28.0f
#define VENT_CLOSE_TEMP     25.0f

// GPIO
#define VENT_RELAY_GPIO     GPIO_NUM_16     // Реле направления
#define VENT_MOTOR_PWM_GPIO GPIO_NUM_25     // PWM двигателя

// Параметры PWM двигателя
#define VENT_MOTOR_FREQ_HZ      20000       // 20 кГц
#define VENT_MOTOR_RESOLUTION   8           // бит
#define VENT_MOTOR_CHANNEL      1
#define VENT_MOTOR_TIMER        1

// Время работы двигателя
#define VENT_OPERATION_TIME_MS  (60 * 1000) // 60 секунд
#define VENT_RAMP_UP_TIME_MS    2000        // Плавный старт
#define VENT_RAMP_DOWN_TIME_MS  2000        // Плавная остановка
#define VENT_RAMP_STEPS         50

// Максимальная мощность двигателя (%)
#define VENT_MOTOR_MAX_DUTY     60


// ========================================
// === Контроль тока (ACS712) =============
// ========================================

#define CURRENT_SENSOR_GPIO     GPIO_NUM_36
#define CURRENT_SENSOR_TYPE     ACS712_TYPE_30A  // 66 мВ/А
#define MAX_ALLOWED_CURRENT     6.0f             // Ампер

/* ========================================================================
 * Полив =================================================================
 * ======================================================================== */

#define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))

#define WATER_PUMP_GPIO       26
#define VALVE_GARDEN1_GPIO    27
#define VALVE_GARDEN2_GPIO    33
#define VALVE_GARDEN3_GPIO    32
#define WATER_LEVEL_ADC_CHAN  ADC_CHANNEL_3  // GPIO39
#define TANK_FILL_GPIO        23

#define LEDC_CHANNEL_1       2
#define LEDC_TIMER_1         2
#define PUMP_PWM_FREQ_HZ      1000
#define PUMP_PWM_RES_BITS     8
#define PUMP_MAX_DUTY         ((1 << PUMP_PWM_RES_BITS) - 1)

// ========================================
// === Датчик уровня воды (10 герконов) ===
// ========================================
// 10 дискретных уровней на основе герконовых переключателей
// Каждый герькон = один шаг (0%, 10%, 20%... 100%)

#define WATER_LEVEL_STEPS 10  // Количество герконов

// Напряжения для каждого шага (в мВ)
#define WATER_LEVEL_STEP_0_MV    860   // 0%
#define WATER_LEVEL_STEP_1_MV    930   // 10%
#define WATER_LEVEL_STEP_2_MV   1010   // 20%
#define WATER_LEVEL_STEP_3_MV   1100   // 30%
#define WATER_LEVEL_STEP_4_MV   1210   // 40%
#define WATER_LEVEL_STEP_5_MV   1350   // 50%
#define WATER_LEVEL_STEP_6_MV   1520   // 60%
#define WATER_LEVEL_STEP_7_MV   1740   // 70%
#define WATER_LEVEL_STEP_8_MV   2030   // 80%
#define WATER_LEVEL_STEP_9_MV   2450   // 100%

#define WATER_LEVEL_DEADZONE_MV      810   // Мертвая зона датчика (между герконами)
#define WATER_LEVEL_NORMAL_FULL_MV  2450   // Максимум при нормальном считывании (100%)
#define WATER_LEVEL_FILL_MAX_MV     3080   // Максимум при наполнении бака (гистерезис)
#define WATER_LEVEL_STEP_TOLERANCE_MV 50   // Допуск ±5% для определения шага (учет погрешности резисторов)
#define WATER_LEVEL_SENSOR_DISCONNECTED_MV 100  // Порог отключения датчика

#define IRRIGATION_HOUR       19    // Полив в 19:00
#define FILL_TANK_START_HOUR  10    // Начало заправки бака
#define FILL_TANK_END_HOUR    16    // Окончание заправки

#define IRRIGATION_DURATION_S 1800  // 30 минут полива
#define DEFAULT_IRRIGATION_PUMP_SPEED 100 // % мощности насоса во время полива

// ========================================
// === Логгирование =======================
// ========================================

#define MAIN_TAG        "MAIN"
#define TAG_VENT        "VENT"
#define TAG_CURRENT     "CURRENT"
#define PWM_TAG         "PWM_LOAD"


#ifdef __cplusplus
}
#endif

#endif /* CONFIG_H */
