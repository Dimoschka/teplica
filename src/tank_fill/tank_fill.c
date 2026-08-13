#include "tank_fill.h"

#include "../../include/config.h"
#include "../app_state.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "TANK_FILL";

esp_err_t tank_fill_init(void)
{
    gpio_reset_pin(TANK_FILL_GPIO);

    esp_err_t err = gpio_set_direction(
        TANK_FILL_GPIO,
        GPIO_MODE_OUTPUT
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка настройки GPIO: %s",
                 esp_err_to_name(err));
        return err;
    }

    gpio_set_level(TANK_FILL_GPIO, 0);
    g_tank_fill_state = TANK_FILL_IDLE;

    return ESP_OK;
}

void tank_fill_start(void)
{
    if (g_tank_fill_state == TANK_FILL_IN_PROGRESS) {
        ESP_LOGW(TAG, "Наполнение уже выполняется");
        return;
    }

    gpio_set_level(TANK_FILL_GPIO, 1);
    g_tank_fill_state = TANK_FILL_IN_PROGRESS;

    ESP_LOGI(TAG, "Наполнение бака запущено");
}

void tank_fill_stop(void)
{
    gpio_set_level(TANK_FILL_GPIO, 0);

    if (g_tank_fill_state == TANK_FILL_IN_PROGRESS) {
        g_tank_fill_state = TANK_FILL_IDLE;
    }

    ESP_LOGI(TAG, "Наполнение бака остановлено");
}

tank_fill_state_t tank_fill_get_state(void)
{
    return g_tank_fill_state;
}

bool tank_fill_is_in_progress(void)
{
    return g_tank_fill_state == TANK_FILL_IN_PROGRESS;
}