#pragma once

#include <stdint.h>
#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handle / configuration structure for an SHT30 temperature/humidity sensor
 */
typedef struct {
    i2c_port_t port;        /*!< I2C port number (I2C_NUM_0 or I2C_NUM_1) */
    gpio_num_t sda_pin;     /*!< SDA GPIO pin */
    gpio_num_t scl_pin;     /*!< SCL GPIO pin */
    uint8_t address;        /*!< 7‑bit I2C address (normally 0x44 or 0x45) */
} sht30_t;

#define SHT30_ADDR_DEFAULT 0x44

/**
 * @brief Initialize the SHT30 instance and configure the I2C driver
 *
 * @param dev       Pointer to an uninitialized sht30_t structure
 * @param port      I2C port to use (I2C_NUM_0 or I2C_NUM_1)
 * @param sda       SDA pin number
 * @param scl       SCL pin number
 * @param address   7‑bit device address (use SHT30_ADDR_DEFAULT most of the time)
 *
 * @return ESP_OK on success, an esp_err_t error code otherwise.
 */
esp_err_t sht30_init(sht30_t *dev, i2c_port_t port, gpio_num_t sda, gpio_num_t scl, uint8_t address);

/**
 * @brief Read temperature (°C) and relative humidity (%) from the sensor.
 *
 * The function sends a single-shot high‑repeatability measurement command,
 * waits the required 15 ms and then reads 6 bytes. CRC bytes are checked; if
 * they fail the function returns ESP_ERR_INVALID_CRC.
 *
 * @param dev             Pointer to initialized sht30_t instance
 * @param temperature_c   Pointer to float that will receive temperature
 * @param humidity_pct    Pointer to float that will receive humidity
 *
 * @return ESP_OK on success or an esp_err_t code in case of failure.
 */
esp_err_t sht30_read_temperature_humidity(sht30_t *dev, float *temperature_c, float *humidity_pct);

#ifdef __cplusplus
}
#endif
