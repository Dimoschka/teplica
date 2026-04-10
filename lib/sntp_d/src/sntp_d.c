#include "freertos/FreeRTOS.h"

#define SNTP_SYNC_INTERVAL_MS   (24 * 60 * 60 * 1000) // 1 раз в сутки
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_netif.h"
#include <time.h>
#include <string.h>
#include "sntp_d.h"

static const char *TAG = "sntp_d";

// === Настройки по умолчанию ===
#define SNTP_SERVER_MAX_LEN 64
static char SNTP_SERVER1[SNTP_SERVER_MAX_LEN] = "pool.ntp.org";
static char SNTP_SERVER2[SNTP_SERVER_MAX_LEN] = "time.nist.gov";
static const char *TIMEZONE = "MSK-5";

// === Callback функция ===
static sntp_sync_callback_t user_sync_callback = NULL;

// === Forward declaration ===
static void sntp_sync_notification_cb(struct timeval *tv);

/**
 * @brief Callback функция от SNTP при синхронизации времени
 */
static void sntp_sync_notification_cb(struct timeval *tv)
{
    struct tm timeinfo;
    localtime_r(&tv->tv_sec, &timeinfo);
    
    if (timeinfo.tm_year < 110) { // После 2000 года
        ESP_LOGE(TAG, "Синхронизация времени не удалась (год < 2000)");
    } else {
        ESP_LOGI(TAG, "✓ Время синхронизировано: %s", asctime(&timeinfo));
        
        // Вызов callback функции пользователя
        if (user_sync_callback != NULL) {
            user_sync_callback(tv);
        }
    }
}

/**
 * @brief Инициализация и запуск синхронизации времени
 */
void sntpStart(void)
{
    ESP_LOGI(TAG, "Инициализация синхронизации времени SNTP...");
    // ...existing code...
    // Остановка предыдущей синхронизации (если была)
    if (esp_sntp_enabled()) {
        esp_sntp_stop();
        ESP_LOGD(TAG, "Предыдущая синхронизация остановлена");
    }
    
    // Установка режима работы
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);

    // Установка интервала синхронизации
    esp_sntp_set_sync_interval(SNTP_SYNC_INTERVAL_MS);

    // Установка серверов NTP
    esp_sntp_setservername(0, SNTP_SERVER1);
    esp_sntp_setservername(1, SNTP_SERVER2);
    ESP_LOGI(TAG, "NTP серверы: %s, %s", SNTP_SERVER1, SNTP_SERVER2);

    // Установка callback функции
    sntp_set_time_sync_notification_cb(sntp_sync_notification_cb);

    // Установка часового пояса
    setenv("TZ", TIMEZONE, 1);
    tzset();
    ESP_LOGI(TAG, "Часовой пояс установлен: %s", TIMEZONE);

    // Запуск синхронизации
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP синхронизация запущена...");
}

/**
 * @brief Остановка синхронизации времени
 */
void sntpStop(void)
{
    if (esp_sntp_enabled()) {
        esp_sntp_stop();
        ESP_LOGI(TAG, "SNTP синхронизация остановлена");
    }
}

/**
 * @brief Регистрация callback функции для событий синхронизации
 */
void sntpRegisterCallback(sntp_sync_callback_t callback)
{
    user_sync_callback = callback;
    if (callback != NULL) {
        ESP_LOGI(TAG, "Callback функция синхронизации времени зарегистрирована");
    } else {
        ESP_LOGI(TAG, "Callback функция синхронизации времени отключена");
    }
}

/**
 * @brief Установка часового пояса
 */
void sntpSetTimezone(const char *timezone)
{
    if (timezone == NULL) {
        ESP_LOGW(TAG, "Попытка установить NULL часовой пояс");
        return;
    }
    
    setenv("TZ", timezone, 1);
    tzset();
    ESP_LOGI(TAG, "Часовой пояс установлен: %s", timezone);
}

/**
 * @brief Установка NTP серверов
 */
void sntpSetServers(const char *server1, const char *server2)
{
    if (server1 != NULL) {
        strncpy(SNTP_SERVER1, server1, SNTP_SERVER_MAX_LEN - 1);
        SNTP_SERVER1[SNTP_SERVER_MAX_LEN - 1] = '\0';
        ESP_LOGI(TAG, "NTP сервер 1 установлен: %s", SNTP_SERVER1);
    }
    
    if (server2 != NULL) {
        strncpy(SNTP_SERVER2, server2, SNTP_SERVER_MAX_LEN - 1);
        SNTP_SERVER2[SNTP_SERVER_MAX_LEN - 1] = '\0';
        ESP_LOGI(TAG, "NTP сервер 2 установлен: %s", SNTP_SERVER2);
    }
}

/**
 * @brief Проверка, синхронизировано ли время
 */
bool sntpIsTimeSet(void)
{
    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    // Если год меньше 2000, то время не синхронизировано
    return (timeinfo.tm_year >= 100); // 100 = 2000 год в tm_year
}
