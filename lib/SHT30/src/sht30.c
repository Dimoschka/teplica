#include "sht30.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

static const char *SHT30_TAG = "SHT30";

// polynomial for CRC: x^8 + x^5 + x^4 + 1 (0x31)
static uint8_t sht30_crc8(const uint8_t *data, int len)
{
    uint8_t crc = 0xFF;
    for (int i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

esp_err_t sht30_init(sht30_t *dev, i2c_port_t port, gpio_num_t sda, gpio_num_t scl, uint8_t address)
{
    if (!dev) {
        return ESP_ERR_INVALID_ARG;
    }
    dev->port = port;
    dev->sda_pin = sda;
    dev->scl_pin = scl;
    dev->address = address;

    // Reset GPIO pins
    gpio_reset_pin(dev->sda_pin);
    gpio_reset_pin(dev->scl_pin);

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = dev->sda_pin,
        .scl_io_num = dev->scl_pin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {
            .clk_speed = 100000,
        },
    };

    esp_err_t err = i2c_param_config(dev->port, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(SHT30_TAG, "i2c_param_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_driver_install(dev->port, conf.mode, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(SHT30_TAG, "i2c_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(SHT30_TAG, "SHT30 initialized on port %d SDA %d SCL %d addr 0x%02X",
             dev->port, dev->sda_pin, dev->scl_pin, dev->address);
    return ESP_OK;
}

esp_err_t sht30_read_temperature_humidity(sht30_t *dev, float *temperature_c, float *humidity_pct)
{
    if (!dev || !temperature_c || !humidity_pct) {
        return ESP_ERR_INVALID_ARG;
    }

    // measurement command high repeatability, clock stretching enabled
    const uint8_t cmd[2] = {0x2C, 0x10};

    esp_err_t err = i2c_master_write_to_device(dev->port, dev->address, cmd, sizeof(cmd), pdMS_TO_TICKS(2000));
    if (err != ESP_OK) {
        ESP_LOGE(SHT30_TAG, "write cmd failed: %s", esp_err_to_name(err));
        return err;
    }

    // maximum conversion time ~15ms according to datasheet
    vTaskDelay(pdMS_TO_TICKS(15));

    uint8_t data[6];
    err = i2c_master_read_from_device(dev->port, dev->address, data, sizeof(data), pdMS_TO_TICKS(2000));
    if (err != ESP_OK) {
        ESP_LOGE(SHT30_TAG, "read data failed: %s", esp_err_to_name(err));
        return err;
    }

    // validate CRCs
    if (sht30_crc8(data, 2) != data[2] || sht30_crc8(data + 3, 2) != data[5]) {
        ESP_LOGW(SHT30_TAG, "CRC mismatch: data[2]=%02X calc=%02X, data[5]=%02X calc=%02X", data[2], sht30_crc8(data, 2), data[5], sht30_crc8(data + 3, 2));
        return ESP_ERR_INVALID_CRC;
    }

    uint16_t rawT = (data[0] << 8) | data[1];
    uint16_t rawH = (data[3] << 8) | data[4];

    *temperature_c = -45.0f + 175.0f * (float)rawT / 65535.0f;
    *humidity_pct = 100.0f * (float)rawH / 65535.0f;

    return ESP_OK;
}
