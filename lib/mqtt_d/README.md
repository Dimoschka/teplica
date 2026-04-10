# mqtt_d

Библиотека для работы с MQTT-клиентом на ESP32.

## Основные функции
- `mqttd_init(const mqttd_config_t* cfg)` — инициализация клиента
- `mqttd_add_rx_topic(const char* topic, mqttd_receive_cb_t cb)` — регистрация входящего топика и callback
- `mqttd_add_tx_topic(const char* topic)` — регистрация исходящего топика
- `mqttd_publish_int(const char* topic, int val)` — публикация числа
- `mqttd_publish_str(const char* topic, const char* str)` — публикация строки
- `mqttd_start()` — запуск клиента
- `mqttd_stop()` — остановка клиента

## Пример использования
```c
mqttd_config_t cfg = { .server = "192.168.1.1", .port = 1883, .user = "user", .pass = "pass", .client_id = "dev1" };
mqttd_init(&cfg);
mqttd_add_rx_topic("topic/in", my_rx_cb);
mqttd_add_tx_topic("topic/out");
mqttd_start();
mqttd_publish_str("topic/out", "Hello!");
```
