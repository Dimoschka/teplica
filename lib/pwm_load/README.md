# PWM Load Controller Library

Библиотека для управления нагрузкой через ШИМ (PWM) с поддержкой плавного пуска и остановки.

## Описание

Библиотека предоставляет удобный интерфейс для управления нагрузкой на ESP32 с использованием ШИМ сигнала. Основные возможности:

- **Плавный пуск** (Ramp Up) - постепенное увеличение мощности от 0% до 100% за заданное время
- **Плавная остановка** (Ramp Down) - постепенное снижение мощности от 100% до 0% за заданное время
- **Плавный переход** - изменение мощности от текущего уровня к целевому за определённый период
- **Прямое управление** - установка мощности на конкретный уровень (0-100%)
- **Мониторинг** - проверка текущего уровня и статуса процесса плавного изменения

## API

### Инициализация

```c
pwm_load_handle_t pwm_load_init(const pwm_load_config_t* config)
```

Инициализирует контроллер PWM с заданной конфигурацией.

**Параметры конфигурации:**
- `gpio_pin` - номер GPIO пина для вывода PWM
- `frequency` - частота ШИМ в Гц (по умолчанию 1000)
- `resolution_bits` - разрешение в битах (8-16)
- `ledc_channel` - канал LEDC (0-7)
- `ledc_timer` - таймер LEDC (0-3)

**Пример:**
```c
pwm_load_config_t config = {
    .gpio_pin = GPIO_NUM_5,
    .frequency = 1000,
    .resolution_bits = 8,
    .ledc_channel = LEDC_CHANNEL_0,
    .ledc_timer = LEDC_TIMER_0,
};

pwm_load_handle_t pwm = pwm_load_init(&config);
if (!pwm) {
    ESP_LOGE(TAG, "Failed to initialize PWM");
    return;
}
```

### Основные функции

#### Прямое управление

```c
int pwm_load_set_duty(pwm_load_handle_t handle, uint8_t duty_percent)
```

Устанавливает мощность на определённый уровень (0-100%) без плавного переходов.

```c
int pwm_load_get_duty(pwm_load_handle_t handle)
```

Получает текущий уровень мощности.

#### Плавное управление

```c
int pwm_load_ramp_up(pwm_load_handle_t handle, uint32_t duration_ms, uint16_t steps)
```

Плавное включение нагрузки с заданной длительностью.
- `duration_ms` - время разгона в миллисекундах
- `steps` - количество шагов для плавного изменения (рекомендуется 100)

**Пример:**
```c
// Плавный разгон за 5 секунд
pwm_load_ramp_up(pwm, 5000, 100);
```

```c
int pwm_load_ramp_down(pwm_load_handle_t handle, uint32_t duration_ms, uint16_t steps)
```

Плавное выключение нагрузки с заданной длительностью.

**Пример:**
```c
// Плавная остановка за 3 секунды
pwm_load_ramp_down(pwm, 3000, 100);
```

```c
int pwm_load_ramp_to(pwm_load_handle_t handle, uint8_t target_duty, uint32_t duration_ms, uint16_t steps)
```

Плавный переход к целевому уровню мощности.

**Пример:**
```c
// Плавный переход на 75% мощности за 2 секунды
pwm_load_ramp_to(pwm, 75, 2000, 100);
```

#### Контроль процесса

```c
bool pwm_load_is_ramping(pwm_load_handle_t handle)
```

Проверяет, идёт ли процесс плавного изменения мощности.

```c
int pwm_load_stop_ramp(pwm_load_handle_t handle)
```

Останавливает текущий процесс плавного изменения и удерживает текущий уровень.

### Завершение работы

```c
int pwm_load_deinit(pwm_load_handle_t handle)
```

Деинициализирует контроллер и освобождает ресурсы.

## Примеры использования

### Простой пример: управление нагрузкой

```c
#include "pwm_load.h"

void app_main() {
    // Конфигурация
    pwm_load_config_t config = {
        .gpio_pin = GPIO_NUM_5,
        .frequency = 1000,
        .resolution_bits = 8,
        .ledc_channel = LEDC_CHANNEL_0,
        .ledc_timer = LEDC_TIMER_0,
    };

    // Инициализация
    pwm_load_handle_t pwm = pwm_load_init(&config);
    if (!pwm) {
        return;
    }

    // Плавный разгон за 5 секунд
    pwm_load_ramp_up(pwm, 5000, 100);

    // Ждём завершения разгона
    vTaskDelay(pdMS_TO_TICKS(5000));

    // Плавная остановка за 3 секунды
    pwm_load_ramp_down(pwm, 3000, 100);

    // Ждём завершения
    vTaskDelay(pdMS_TO_TICKS(3000));

    // Деинициализация
    pwm_load_deinit(pwm);
}
```

### Пример с мониторингом

```c
#include "pwm_load.h"

void pwm_monitor_task(void* arg) {
    pwm_load_handle_t pwm = (pwm_load_handle_t)arg;
    
    while (1) {
        int duty = pwm_load_get_duty(pwm);
        bool ramping = pwm_load_is_ramping(pwm);
        
        printf("Текущая мощность: %d%%, Изменение: %s\n", 
               duty, ramping ? "ДА" : "НЕТ");
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main() {
    pwm_load_config_t config = {
        .gpio_pin = GPIO_NUM_5,
        .frequency = 1000,
        .resolution_bits = 8,
        .ledc_channel = LEDC_CHANNEL_0,
        .ledc_timer = LEDC_TIMER_0,
    };

    pwm_load_handle_t pwm = pwm_load_init(&config);
    
    // Запуск задачи мониторинга
    xTaskCreate(pwm_monitor_task, "PWM Monitor", 2048, pwm, 5, NULL);
    
    // Плавный разгон
    pwm_load_ramp_up(pwm, 5000, 100);
}
```

## Требования

- ESP-IDF >= 4.0
- FreeRTOS
- Driver компоненты для LEDC

## Параметры и рекомендации

### Выбор частоты ШИМ
- **1000 Hz** - подходит для большинства случаев
- **5000 Hz** - для быстрых устройств
- **50-100 Hz** - для устройств с инерцией (нагреватели, моторы)

### Выбор количества шагов
- **100 шагов** - хороший компромисс между гладкостью и точностью
- **200+ шагов** - для критичных приложений (медицина, пищевая промышленность)
- **50 шагов** - если производительность ограничена

### Разрешение ШИМ
- **8 бит** - стандартное значение, достаточно для большинства применений
- **10 бит** - для приложений, требующих точности
- **12+ бит** - для высокоточных систем

## Логирование

Библиотека использует ESP_LOG с тегом "PWM_LOAD" для вывода диагностической информации:
- INFO - начало/завершение операций
- DEBUG - детальная информация о каждом шаге процесса
- ERROR - ошибки инициализации и работы

## Замечания по безопасности

- Убедитесь, что GPIO пин правильно сконфигурирован и может обеспечить необходимый ток
- Используйте промежуточные элементы (MOSFET, реле) для управления мощными нагрузками
- Проверяйте возвращаемые значения всех функций на ошибки
- При плавном изменении убедитесь, что нагрузка может выдержать плавное увеличение мощности

## Лицензия

MIT License
