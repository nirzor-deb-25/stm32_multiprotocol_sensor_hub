#include <stdbool.h>
#include <stdint.h>

#include "stm32_regs.h"
#include "timing.h"
#include "i2c_probe.h"
#include "bme280.h"
#include "spi.h"
#include "lis3dsh.h"

#define BLUE_LED_MASK (1UL << 15)

/* Nineteen consecutive 32-bit words. */
typedef struct
{
    uint32_t temperature_initialized;
    uint32_t temperature_result;
    uint32_t temperature_i2c_result;
    uint32_t temperature_count;
    uint32_t temperature_errors;
    uint32_t temperature_raw;
    int32_t temperature_centi_c;
    uint32_t temperature_valid;

    uint32_t accelerometer_id;
    uint32_t accelerometer_initialized;
    uint32_t accelerometer_spi_result;
    uint32_t accelerometer_count;
    uint32_t accelerometer_errors;
    uint32_t accelerometer_not_ready;
    int32_t x_mg;
    int32_t y_mg;
    int32_t z_mg;
    uint32_t accelerometer_valid;

    uint32_t loop_count;
} hub_diagnostics_t;

volatile hub_diagnostics_t hub_diagnostics = {0};

static void heartbeat_init(void)
{
    RCC_AHB1ENR |= (1UL << 3);
    (void)RCC_AHB1ENR;

    GPIOD_BSRR = BLUE_LED_MASK << 16;

    GPIOD_OTYPER &= ~BLUE_LED_MASK;
    GPIOD_OSPEEDR &= ~(3UL << 30);
    GPIOD_PUPDR &= ~(3UL << 30);

    GPIOD_MODER =
        (GPIOD_MODER & ~(3UL << 30)) |
        (1UL << 30);
}

static bool spi_succeeded(spi_status_t result)
{
    hub_diagnostics.accelerometer_spi_result =
        (uint32_t)result;

    return result == SPI_OK;
}

static bool accelerometer_initialize(void)
{
    uint8_t id = 0U;

    if (!spi_succeeded(lis3dsh_read_id(&id)))
    {
        return false;
    }

    hub_diagnostics.accelerometer_id = id;

    if (id != LIS3DSH_EXPECTED_ID)
    {
        return false;
    }

    if (!spi_succeeded(lis3dsh_configure()))
    {
        return false;
    }

    /* Verify that configuration writes reached the sensor. */
    const uint8_t registers[3] = {
        LIS3DSH_REG_CTRL4,
        LIS3DSH_REG_CTRL5,
        LIS3DSH_REG_CTRL6
    };

    const uint8_t expected[3] = {
        LIS3DSH_CTRL4_VALUE,
        LIS3DSH_CTRL5_VALUE,
        LIS3DSH_CTRL6_VALUE
    };

    for (uint32_t index = 0U; index < 3U; index++)
    {
        uint8_t actual = 0U;

        if (!spi_succeeded(lis3dsh_read_register(
                registers[index],
                &actual)))
        {
            return false;
        }

        if (actual != expected[index])
        {
            return false;
        }
    }

    return true;
}

static void update_temperature(void)
{
    hub_diagnostics.temperature_valid = 0U;

    if (hub_diagnostics.temperature_initialized == 0U)
    {
        bme280_result_t result = bme280_init(0x77U);

        hub_diagnostics.temperature_result = (uint32_t)result;
        hub_diagnostics.temperature_i2c_result =
            bme280_last_i2c_result;

        if (result != BME280_OK)
        {
            hub_diagnostics.temperature_errors++;
            return;
        }

        hub_diagnostics.temperature_initialized = 1U;
    }

    bme280_sample_t sample;

    bme280_result_t result =
        bme280_read_temperature(&sample);

    hub_diagnostics.temperature_result = (uint32_t)result;
    hub_diagnostics.temperature_i2c_result =
        bme280_last_i2c_result;

    if (result != BME280_OK)
    {
        hub_diagnostics.temperature_errors++;
        hub_diagnostics.temperature_initialized = 0U;
        return;
    }

    hub_diagnostics.temperature_raw =
        sample.raw_temperature;

    hub_diagnostics.temperature_centi_c =
        sample.temperature_centi_c;

    hub_diagnostics.temperature_count++;
    hub_diagnostics.temperature_valid = 1U;
}

static void update_accelerometer(void)
{
    hub_diagnostics.accelerometer_valid = 0U;

    if (hub_diagnostics.accelerometer_initialized == 0U)
    {
        if (!accelerometer_initialize())
        {
            hub_diagnostics.accelerometer_errors++;
            return;
        }

        hub_diagnostics.accelerometer_initialized = 1U;
    }

    uint8_t status = 0U;

    if (!spi_succeeded(lis3dsh_read_register(
            LIS3DSH_REG_STATUS,
            &status)))
    {
        hub_diagnostics.accelerometer_errors++;
        hub_diagnostics.accelerometer_initialized = 0U;
        return;
    }

    if ((status & LIS3DSH_STATUS_DATA_READY) == 0U)
    {
        hub_diagnostics.accelerometer_not_ready++;
        return;
    }

    lis3dsh_raw_t sample;

    if (!spi_succeeded(lis3dsh_read_xyz(&sample)))
    {
        hub_diagnostics.accelerometer_errors++;
        hub_diagnostics.accelerometer_initialized = 0U;
        return;
    }

    hub_diagnostics.x_mg = lis3dsh_raw_to_mg(sample.x);
    hub_diagnostics.y_mg = lis3dsh_raw_to_mg(sample.y);
    hub_diagnostics.z_mg = lis3dsh_raw_to_mg(sample.z);

    hub_diagnostics.accelerometer_count++;
    hub_diagnostics.accelerometer_valid = 1U;
}

int main(void)
{
    timing_init();
    heartbeat_init();

    i2c_probe_init();
    spi_init();

    timing_delay_ms(100U);

    bool blue_on = false;

    for (;;)
    {
        /*
         * Each sensor is handled independently.
         * A failed measurement does not skip the other sensor.
         */
        update_temperature();
        update_accelerometer();

        hub_diagnostics.loop_count++;

        blue_on = !blue_on;

        GPIOD_BSRR = blue_on
            ? BLUE_LED_MASK
            : BLUE_LED_MASK << 16;

        timing_delay_ms(500U);
    }
}