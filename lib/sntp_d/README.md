## Пример использования
```c
sntpRegisterCallback(my_time_cb);
sntpStart();
```
# sntp_d

Библиотека для синхронизации времени по SNTP на ESP32.

## Основные функции
- `void sntpStart(void);` — запуск синхронизации времени
- `void sntpStop(void);` — остановка синхронизации
- `void sntpRegisterCallback(sntp_sync_callback_t callback);` — регистрация callback для события синхронизации