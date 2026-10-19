#include <stdbool.h>
#include <stdint.h>

#include "bme280.h"
#include "i2c_probe.h"
#include "timing.h"

#define BME_REG_ID        0xD0U
#define BME_REG_RESET     0xE0U
#define BME_REG_CTRL_HUM  0xF2U
#define BME_REG_STATUS    0xF3U
#define BME_REG_CTRL_MEAS 0xF4U
#define BME_REG_CONFIG    0xF5U

typedef struct
{
    uint16_t t1;
    int16_t t2;
    int16_t t3;
} temperature_calibration_t;

static temperature_calibration_t calibration;
static uint8_t sensor_address;
static bool initialized = false;

volatile uint32_t bme280_last_i2c_result = 0U;

static bool read_register(uint8_t reg, uint8_t *value)
{
    i2c_probe_result_t result =
        i2c_probe_read_register(sensor_address, reg, value);

    bme280_last_i2c_result = (uint32_t)result;

    return result == I2C_PROBE_ACK;
}

static bool write_register(uint8_t reg, uint8_t value)
{
    i2c_probe_result_t result =
        i2c_probe_write_register(sensor_address, reg, value);

    bme280_last_i2c_result = (uint32_t)result;

    return result == I2C_PROBE_ACK;
}

static uint16_t decode_unsigned(uint8_t low, uint8_t high)
{
    return (uint16_t)(
        (uint16_t)low |
        ((uint16_t)high << 8)
    );
}

static int16_t decode_signed(uint8_t low, uint8_t high)
{
    int32_t value = (int32_t)decode_unsigned(low, high);

    if (value >= 32768L)
    {
        value -= 65536L;
    }

    return (int16_t)value;
}

static bme280_result_t wait_status_clear(uint8_t mask)
{
    uint32_t start = timing_cycles();

    for (;;)
    {
        uint8_t status;

        if (!read_register(BME_REG_STATUS, &status))
        {
            return BME280_I2C_ERROR;
        }

        if ((status & mask) == 0U)
        {
            return BME280_OK;
        }

        if (timing_has_elapsed(start, 50U))
        {
            return BME280_TIMEOUT;
        }

        timing_delay_ms(1U);
    }
}

static int32_t compensate_temperature(uint32_t raw)
{
    /*
     * Bosch integer temperature formula.
     * Wider intermediates avoid 32-bit multiplication overflow.
     * GNU Arm GCC provides arithmetic signed right shifts.
     */
    int64_t adc = (int64_t)raw;
    int64_t t1 = (int64_t)calibration.t1;

    int64_t first =
        (((adc >> 3) - (t1 * 2)) *
         (int64_t)calibration.t2) >> 11;

    int64_t difference = (adc >> 4) - t1;

    int64_t second =
        (((difference * difference) >> 12) *
         (int64_t)calibration.t3) >> 14;

    int64_t fine = first + second;

    return (int32_t)((fine * 5 + 128) >> 8);
}

bme280_result_t bme280_init(uint8_t address)
{
    initialized = false;

    if ((address != 0x76U) && (address != 0x77U))
    {
        return BME280_INVALID_ARGUMENT;
    }

    sensor_address = address;

    uint8_t id;

    if (!read_register(BME_REG_ID, &id))
    {
        return BME280_I2C_ERROR;
    }

    if (id != 0x60U)
    {
        return BME280_WRONG_ID;
    }

    /* Reset the sensor to a known configuration. */
    if (!write_register(BME_REG_RESET, 0xB6U))
    {
        return BME280_I2C_ERROR;
    }

    timing_delay_ms(3U);

    /* Wait for factory calibration copying to finish. */
    bme280_result_t result = wait_status_clear(1U);

    if (result != BME280_OK)
    {
        return result;
    }

    uint8_t bytes[6];

    for (uint32_t index = 0U; index < 6U; index++)
    {
        if (!read_register(
                (uint8_t)(0x88U + index),
                &bytes[index]))
        {
            return BME280_I2C_ERROR;
        }
    }

    calibration.t1 = decode_unsigned(bytes[0], bytes[1]);
    calibration.t2 = decode_signed(bytes[2], bytes[3]);
    calibration.t3 = decode_signed(bytes[4], bytes[5]);

    if ((calibration.t1 == 0U) ||
        (calibration.t1 == 0xFFFFU))
    {
        return BME280_INVALID_CALIBRATION;
    }

    /* Temperature only; humidity and pressure disabled. */
    if (!write_register(BME_REG_CTRL_HUM, 0U))
    {
        return BME280_I2C_ERROR;
    }

    /* Disable the IIR filter; keep four-wire SPI selection. */
    if (!write_register(BME_REG_CONFIG, 0U))
    {
        return BME280_I2C_ERROR;
    }

    /* Temperature oversampling x1, initially in sleep mode. */
    if (!write_register(BME_REG_CTRL_MEAS, 0x20U))
    {
        return BME280_I2C_ERROR;
    }

    initialized = true;

    return BME280_OK;
}

bme280_result_t bme280_read_temperature(bme280_sample_t *sample)
{
    if (sample == 0)
    {
        return BME280_INVALID_ARGUMENT;
    }

    if (!initialized)
    {
        return BME280_NOT_INITIALIZED;
    }

    /* Temperature x1, pressure skipped, forced mode. */
    if (!write_register(BME_REG_CTRL_MEAS, 0x21U))
    {
        return BME280_I2C_ERROR;
    }

    /*
     * Allow conversion to start and finish before checking status.
     * This exceeds the temperature-only x1 conversion time.
     */
    timing_delay_ms(10U);

    bme280_result_t result = wait_status_clear(1U << 3);

    if (result != BME280_OK)
    {
        return result;
    }

    /*
     * Forced mode returns to sleep after measurement.
     * With no new trigger, these three registers remain stable
     * while they are read in separate transactions.
     */
    uint8_t bytes[3];

    for (uint32_t index = 0U; index < 3U; index++)
    {
        if (!read_register(
                (uint8_t)(0xFAU + index),
                &bytes[index]))
        {
            return BME280_I2C_ERROR;
        }
    }

    uint32_t raw =
        ((uint32_t)bytes[0] << 12) |
        ((uint32_t)bytes[1] << 4) |
        ((uint32_t)bytes[2] >> 4);

    if (raw == 0x80000UL)
    {
        return BME280_NO_DATA;
    }

    bme280_sample_t measured = {
        .raw_temperature = raw,
        .temperature_centi_c = compensate_temperature(raw)
    };

    *sample = measured;

    return BME280_OK;
}