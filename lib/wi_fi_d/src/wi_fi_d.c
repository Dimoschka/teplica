#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include <string.h>
#include <stdlib.h>
#include "wi_fi_d.h"
#include "../../../include/config.h"

#define MAX_RETRY_COUNT       WIFI_MAX_RETRIES
extern const char api_telegram_org_pem_start[] asm("_binary_api_telegram_org_pem_start");
// === Логирование ===
static const char *TAG = "wifi_sta";

// === Счётчик попыток подключения ===
static int retry_count = 0;

static wifi_event_callback_t user_callback = NULL;

// === Глобальный дескриптор задачи повторного подключения ===
static TaskHandle_t wifi_reconnect_task_handle = NULL;

// === Forward declaration ===
static void wifi_trigger_event(wifi_event_type_t event, const char *data);
void wifi_reconnect_task(void *pvParameters);

// === Обработчик Wi-Fi и IP-событий ===
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Попытка подключения к Wi-Fi...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // Вызов callback события "соединение разорвано"
        wifi_trigger_event(WIFI_EVENT_CONNECTION_LOST, "Соединение с Wi-Fi потеряно");
        
        if (retry_count < MAX_RETRY_COUNT) {
            retry_count++;
            ESP_LOGI(TAG, "Подключение не удалось, попытка %d/%d...", retry_count, MAX_RETRY_COUNT);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "Не удалось подключиться к Wi-Fi после %d попыток", MAX_RETRY_COUNT);
            wifi_trigger_event(WIFI_EVENT_MAX_RETRIES_EXCEEDED, "Достигнут максимум попыток подключения");
            // Новая логика: запускать задачу только если она ещё не запущена
            if (wifi_reconnect_task_handle == NULL) {
                xTaskCreatePinnedToCore(wifi_reconnect_task, "wifi_reconnect_task", 2048, NULL, 1, &wifi_reconnect_task_handle, 1);
            } else {
                ESP_LOGW(TAG, "wifi_reconnect_task уже запущена, повторный запуск не требуется");
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP получен: " IPSTR, IP2STR(&event->ip_info.ip));
        retry_count = 0; // Сбросить счётчик при успехе
        
        // Вызов callback события "IP получен"
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        wifi_trigger_event(WIFI_EVENT_IP_OBTAINED, ip_str);
    }
}

// === Функция вызова callback события ===
static void wifi_trigger_event(wifi_event_type_t event, const char *data)
{
    if (user_callback != NULL) {
        user_callback(event, data);
    }
}

// === Функция инициализации Wi-Fi ===
static void wifi_init_sta(void)
{
    // Инициализация NVS (нужна для Wi-Fi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NEW_VERSION_FOUND || ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_LOGW (TAG, "NVS требует форматирования, выполняется очистка...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Создание обработчика событий
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Создание интерфейса STA (клиент)
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    // Инициализация Wi-Fi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Регистрация callback-обработчика
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    // Установка режима STA
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi инициализирован.");
}

// === Главная функция библиотеки ===
void wifiStart(void)
{
    ESP_LOGI(TAG, "wifiStart: запуск и инициализация Wi-Fi");
    wifi_init_sta();

    // Настройка подключения к сети
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };

    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';

    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_LOGI(TAG, "Подключение к Wi-Fi сети: %s", WIFI_SSID);
}

// === Функция регистрации callback для событий Wi-Fi ===
void wifiRegisterEventCallback(wifi_event_callback_t callback)
{
    user_callback = callback;
    if (callback != NULL) {
        ESP_LOGI(TAG, "Callback функция для Wi-Fi событий зарегистрирована");
    } else {
        ESP_LOGI(TAG, "Callback функция для Wi-Fi событий отключена");
    }
}

// Задача для повторного подключения к Wi-Fi через длительный интервал
void wifi_reconnect_task(void *pvParameters) {
    ESP_LOGI(TAG, "Ожидание 30 секунд перед повторной попыткой подключения к Wi-Fi...");
    vTaskDelay(30000 / portTICK_PERIOD_MS); // 30 секунд
    retry_count = 0;
    ESP_LOGI(TAG, "Повторная попытка подключения к Wi-Fi после ожидания");
    esp_wifi_connect();
    wifi_reconnect_task_handle = NULL; // Освобождаем дескриптор
    vTaskDelete(NULL);
}