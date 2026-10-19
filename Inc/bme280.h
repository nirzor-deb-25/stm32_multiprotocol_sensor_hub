#ifndef BME280_H
#define BME280_H

#include <stdint.h>

typedef enum
{
    BME280_OK = 0,
    BME280_INVALID_ARGUMENT,
    BME280_I2C_ERROR,
    BME280_WRONG_ID,
    BME280_INVALID_CALIBRATION,
    BME280_TIMEOUT,
    BME280_NO_DATA,
    BME280_NOT_INITIALIZED
} bme280_result_t;

typedef struct
{
    uint32_t raw_temperature;
    int32_t temperature_centi_c;
} bme280_sample_t;

/* Most recent underlying I2C result, for diagnostics. */
extern volatile uint32_t bme280_last_i2c_result;

/* Call timing_init() and i2c_probe_init() first. */
bme280_result_t bme280_init(uint8_t address);

/* Blocking temperature measurement with bounded status polling. */
bme280_result_t bme280_read_temperature(bme280_sample_t *sample);

#endif