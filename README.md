# Teplica MQTT configuration

## MQTT topics for irrigation control

- `greenhouse/control/irrigation`
  - Manual irrigation commands
  - Supported payloads:
    - `irrigate` — запустить ручной полив
    - `fill_tank` — начать наполнения бака

## MQTT topics for irrigation configuration

- `greenhouse/config/irrigation_hour`
  - JSON number 0..23
  - Устанавливает час запуска автоматического полива

- `greenhouse/config/irrigation_duration`
  - JSON number 0..3600
  - Устанавливает длительность автоматического и ручного полива в секундах

- `greenhouse/config/irrigation_speed`
  - JSON number 1..100
  - Устанавливает процент мощности PWM для насоса полива
  - Применяется и для автоматического, и для ручного полива

## Пример

Отправить скорость 80%:

```json
80
```

или просто строку `80` в MQTT-платформе.
