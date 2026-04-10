#include "ACS712.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *ACS712_TAG = "ACS712_C";

// fallback value if calibration fails to provide vref
static const int DEFAULT_VREF = 1100; // mV

// ACS712 current sensor driver for ESP32 using ADC1
void acs712_init(acs712_t *dev, adc_channel_t channel, float sensitivity_mV_per_amp, adc_bitwidth_t width, adc_atten_t atten)
{
    if (!dev) return;
    memset(dev, 0, sizeof(*dev));
    dev->channel = channel;
    dev->width = width;
    dev->atten = atten;
    dev->sensitivity_mV_per_amp = sensitivity_mV_per_amp;
    dev->zero_offset = 0;
}

void acs712_begin(acs712_t *dev)
{
    if (!dev) return;

    esp_err_t err = ESP_OK;

    // create oneshot ADC unit if not already provided
    if (!dev->adc_handle) {
        adc_oneshot_unit_init_cfg_t unit_cfg = {
            .unit_id = ADC_UNIT_1,
            .clk_src = 0, /* use default clock source */
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        err = adc_oneshot_new_unit(&unit_cfg, &dev->adc_handle);
        if (err != ESP_OK) {
            ESP_LOGE(ACS712_TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(err));
            return;
        }
    }

    // configure channel attenuation/bitwidth
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = dev->atten,
        .bitwidth = dev->width,
    };
    adc_oneshot_config_channel(dev->adc_handle, dev->channel, &chan_cfg);

    // create calibration handle (line‑fitting scheme)
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = dev->atten,
        .bitwidth = dev->width,
#if CONFIG_IDF_TARGET_ESP32
        .default_vref = DEFAULT_VREF,
#endif
    };
    err = adc_cali_create_scheme_line_fitting(&cali_cfg, &dev->cali_handle);
    if (err != ESP_OK) {
        ESP_LOGW(ACS712_TAG, "adc_cali_create_scheme_line_fitting failed (%s), voltage conversions will use default vref", esp_err_to_name(err));
        dev->cali_handle = NULL;
    }
}

int acs712_read_raw(acs712_t *dev)
{
    if (!dev || !dev->adc_handle) return 0;
    int raw;
    esp_err_t err = adc_oneshot_read(dev->adc_handle, dev->channel, &raw);
    if (err != ESP_OK) {
        ESP_LOGE(ACS712_TAG, "adc_oneshot_read failed: %s", esp_err_to_name(err));
        return 0;
    }
    return raw;
}

void acs712_calibrate_zero(acs712_t *dev, int samples)
{
    if (!dev || samples <= 0) return;
    long sum = 0;
    for (int i = 0; i < samples; ++i) {
        sum += acs712_read_raw(dev);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    dev->zero_offset = (int)(sum / samples);
    ESP_LOGI(ACS712_TAG, "Calibrated zero offset: %d", dev->zero_offset);
}

void acs712_set_zero_offset(acs712_t *dev, int offset)
{
    if (!dev) return;
    dev->zero_offset = offset;
}

int acs712_get_zero_offset(acs712_t *dev)
{
    if (!dev) return 0;
    return dev->zero_offset;
}

float acs712_get_current_dc(acs712_t *dev, int samples)
{
    if (!dev || samples <= 0) return 0.0f;
    long sum = 0;
    for (int i = 0; i < samples; ++i) {
        sum += acs712_read_raw(dev);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    int avg = (int)(sum / samples);

    int voltage_mV = 0;
    if (dev->cali_handle) {
        esp_err_t err = adc_cali_raw_to_voltage(dev->cali_handle, avg, &voltage_mV);
        if (err != ESP_OK) {
            ESP_LOGW(ACS712_TAG, "cali_raw_to_voltage failed: %s", esp_err_to_name(err));
            // fall back below
            voltage_mV = (avg * DEFAULT_VREF) / ((1 << dev->width) - 1);
        }
    } else {
        // Предполагаем VDD_A = 3.3 В
        int full_scale_mv = 3300;
        voltage_mV = (avg * full_scale_mv) / ((1 << dev->width) - 1);

        // no calibration handle, compute using default vref
        // voltage_mV = (avg * DEFAULT_VREF) / ((1 << dev->width) - 1);
    }

    int vref_mV = dev->cali_handle ? DEFAULT_VREF /* doesn't matter */ : DEFAULT_VREF;
    float v_mid = vref_mV / 2.0f;
    float current = (voltage_mV - v_mid) / dev->sensitivity_mV_per_amp;
    return current;
}
