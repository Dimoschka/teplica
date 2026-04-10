## Пример использования
```c
wifiRegisterEventCallback(my_wifi_cb);
wifiStart();
```
# wi_fi_d

Библиотека для управления Wi-Fi на ESP32: сканирование, подключение, обработка событий, переподключение.

## Основные функции
- `void wifiStart(void);` — инициализация и подключение к Wi-Fi
- `void wifiRegisterEventCallback(wifi_event_callback_t callback);` — регистрация callback для событий Wi-Fi
- `wifi_network_t wifiGetCurrentNetwork(void);` — получить текущую сеть