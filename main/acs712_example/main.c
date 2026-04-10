#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "ACS712.h"

void app_main(void)
{
    acs712_t sensor;
    acs712_init(&sensor, ADC_CHANNEL_6, 185.0f, ADC_BITWIDTH_12, ADC_ATTEN_DB_12);
    acs712_begin(&sensor);
    acs712_calibrate_zero(&sensor, 200);

    while (1) {
        float current = acs712_get_current_dc(&sensor, 100);
        ESP_LOGI("acs_example_c", "Current: %.3f A", current);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
