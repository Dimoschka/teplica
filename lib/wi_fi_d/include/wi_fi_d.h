#ifndef WI_FI_D_H
#define WI_FI_D_H

// === Типы событий Wi-Fi ===
typedef enum {
    WIFI_EVENT_IP_OBTAINED,         // IP адрес получен
    WIFI_EVENT_CONNECTION_LOST,     // Соединение разорвано
    WIFI_EVENT_MAX_RETRIES_EXCEEDED // Максимум попыток исчерпано
} wifi_event_type_t;

// === Callback функция для событий Wi-Fi ===
typedef void (*wifi_event_callback_t)(wifi_event_type_t event, const char *data);

/**
 * @brief Инициализация и запуск Wi-Fi подключения
 * 
 * Функция выполняет:
 * - Инициализацию Wi-Fi в режиме STA
 * - Подключение к заданной сети
 * - Переподключение при разрыве с максимум 5 попытками
 */

void wifiStart(void);

/**
 * @brief Регистрация callback функции для событий Wi-Fi
 * 
 * @param callback Указатель на функцию-обработчик событий
 */
void wifiRegisterEventCallback(wifi_event_callback_t callback);

#endif /* WI_FI_D_H */
