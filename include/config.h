/**
 * @file config.h
 * @brief Общие настройки устройства
 */

#ifndef CONFIG_H
#define CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif


#define Change_WiFi_MQTT 0 // 0 - для домашней сети, 1 - для завода

// ========================================
// === Wi-Fi Настройки ====================
// ========================================

// Основная сеть

#if Change_WiFi_MQTT == 1
#define WIFI_SSID           "ZCM2"
#define WIFI_PASS           "ZavodSpecMash17"
#else
#define WIFI_SSID          "Sweet_Home"
#define WIFI_PASS          "F4emz75L"
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

#if Change_WiFi_MQTT == 1
#define CONFIG_MQTT_BROKER      "192.168.126.230"
#define CONFIG_MQTT_PORT        1885
#define CONFIG_MQTT_USER        "mqtt_client"
#define CONFIG_MQTT_PASS        "qwer1234"
#else
#define CONFIG_MQTT_BROKER       "192.168.100.100"
#define CONFIG_MQTT_PORT         1883
#define CONFIG_MQTT_USER         "mqtt"
#define CONFIG_MQTT_PASS         "mqtt"
#endif
#define CONFIG_MQTT_CLIENT_ID   "greenhouse_controller"

// Топики
#define CONFIG_TOPIC_STATUS       "greenhouse/status"
#define CONFIG_TOPIC_TEMP         "greenhouse/temperature"
#define CONFIG_TOPIC_HUMID        "greenhouse/humidity"
#define CONFIG_TOPIC_VENT_STATE   "greenhouse/vent_state"
#define CONFIG_TOPIC_VENT_CONTROL "greenhouse/vent_control"
#define CONFIG_TOPIC_WATER_LEVEL     "greenhouse/sensors/water_level"
#define CONFIG_TOPIC_IRRIG_STATE     "greenhouse/status/irrigation"
#define CONFIG_TOPIC_IRRIG_CONTROL   "greenhouse/control/irrigation"


// ========================================
// === Датчик SHT30 =======================
// ========================================

#define SHT30_I2C_PORT      I2C_NUM_0
#define SHT30_SDA_PIN       GPIO_NUM_21
#define SHT30_SCL_PIN       GPIO_NUM_22
#define SHT30_ADDR          SHT30_ADDR_DEFAULT


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

#define WATER_LEVEL_EMPTY_MV  0  // Значение при пустом баке (настройка под датчик!)
#define WATER_LEVEL_FULL_MV   3000  // При полном баке

#define IRRIGATION_HOUR       19    // Полив в 19:00
#define FILL_TANK_START_HOUR  10    // Начало заправки бака
#define FILL_TANK_END_HOUR    16    // Окончание заправки

#define IRRIGATION_DURATION_S 300  // 5 минут полива


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
