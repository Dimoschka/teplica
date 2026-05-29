# Teplica MQTT configuration

## MQTT topics for irrigation control

- `greenhouse/control/irrigation`
  - Manual irrigation commands
  - Supported payloads:
    - `irrigate` — запустить ручной полив
    - `fill_tank` — начать наполнения бака

## MQTT topics for irrigation configuration

- `greenhouse/config/garden1_irrigation_pct`
  - JSON number 1..100
  - Процент объёма бака для полива грядки 1
  - Default: 20

- `greenhouse/config/garden1_irrigation_freq`
  - JSON number 1..4
  - Сколько раз в сутки поливать грядку 1
  - Default: 1

- `greenhouse/config/garden2_irrigation_pct`
  - JSON number 1..100
  - Процент объёма бака для полива грядки 2
  - Default: 20

- `greenhouse/config/garden2_irrigation_freq`
  - JSON number 1..4
  - Сколько раз в сутки поливать грядку 2
  - Default: 1

- `greenhouse/config/garden3_irrigation_pct`
  - JSON number 1..100
  - Процент объёма бака для полива грядки 3
  - Default: 20

- `greenhouse/config/garden3_irrigation_freq`
  - JSON number 1..4
  - Сколько раз в сутки поливать грядку 3
  - Default: 1

- `greenhouse/config/irrigation_duration`
  - JSON number 0..3600
  - Устанавливает длительность ручного полива в секундах

- `greenhouse/config/irrigation_speed`
  - JSON number 1..100
  - Устанавливает процент мощности PWM для насоса полива
  - Применяется и для автоматического и для ручного полива

## Пример

Отправить скорость 80%:

```json
80
```

или просто строку `80` в MQTT-платформе.

## Примеры команд ручного полива

- Топик управления: `greenhouse/control/irrigation`

- Поддерживаемые полезные нагрузки:
  - Простая строка: `irrigate` — устаревшая форма, полит все грядки (оставлена для совместимости).
  - JSON: `{"beds":"all"}` — полит все грядки.
  - JSON: `{"beds":2}` — полит грядку 2.
  - JSON: `{"beds":[1,3]}` — полит грядки 1 и 3.

Примеры payload'ов:

```json
"irrigate"
```

```json
{"beds":"all"}
```

```json
{"beds":[1,3]}
```

При успешном выполнении контроллер публикует статус в топик `greenhouse/status/irrigation` со значением `done`. При ошибке датчика уровня — `error_sensor_disconnected`. Если в команде не указаны грядки — `no_beds_specified`.
