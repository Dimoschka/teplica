#pragma once

#include <stdint.h>
/* new ADC APIs replace legacy driver/calibration headers */
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

typedef struct {
    adc_channel_t channel;               /*!< ADC channel (ADC1/ADC2) */
    adc_bitwidth_t width;                /*!< bit width for oneshot conversion */
    adc_atten_t atten;                   /*!< attenuation setting */
    float sensitivity_mV_per_amp;        /*!< sensor sensitivity */
    int zero_offset;                     /*!< calibrated zero offset */

    /* handles for new ADC driver */
    adc_oneshot_unit_handle_t adc_handle;
    adc_cali_handle_t cali_handle;
} acs712_t;

// Initialize instance structure. Must be called before begin.
// Инициализируйте структуру экземпляра. Должен быть вызван перед begin
// channel: ADC1_CHANNEL_0..ADC1_CHANNEL_9
// sensitivity_mV_per_amp: e.g. 185 for ACS712-05B, 100 for ACS712-20A, 66 for ACS712-30A
// width: ADC_WIDTH_BIT_9..ADC_WIDTH_BIT_12
// atten: ADC_ATTEN_DB_0..ADC_ATTEN_DB_12
void acs712_init(acs712_t *dev, adc_channel_t channel, float sensitivity_mV_per_amp, adc_bitwidth_t width, adc_atten_t atten);

// Configure ADC hardware and characterize calibration.
void acs712_begin(acs712_t *dev);

// Read raw ADC sample (unadjusted)
int acs712_read_raw(acs712_t *dev);

// Calibrate zero offset by averaging samples
void acs712_calibrate_zero(acs712_t *dev, int samples);

// Set/get zero offset manually
void acs712_set_zero_offset(acs712_t *dev, int offset);
int acs712_get_zero_offset(acs712_t *dev);

// Get DC current in amperes (averaged over samples)
float acs712_get_current_dc(acs712_t *dev, int samples);
