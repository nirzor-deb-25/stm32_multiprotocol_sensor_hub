#include <stdbool.h>
#include <stdint.h>

#include "stm32_regs.h"
#include "timing.h"
#include "i2c_probe.h"
#include "bme280.h"

#define BLUE_LED_MASK (1UL << 15)

typedef struct
{
    uint32_t initialized;
    uint32_t driver_result;
    uint32_t i2c_result;
    uint32_t sample_count;
    uint32_t error_count;
    uint32_t raw_temperature;
    int32_t temperature_centi_c;
    uint32_t sample_valid;
} hub_temperature_diagnostics_t;

volatile hub_temperature_diagnostics_t hub_temperature = {0};

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

int main(void)
{
    timing_init();
    heartbeat_init();
    i2c_probe_init();

    timing_delay_ms(100U);

    bool initialized = false;
    bool blue_on = false;

    for (;;)
    {
        hub_temperature.sample_valid = 0U;

        if (!initialized)
        {
            bme280_result_t result = bme280_init(0x77U);

            hub_temperature.driver_result = (uint32_t)result;
            hub_temperature.i2c_result = bme280_last_i2c_result;

            initialized = (result == BME280_OK);
            hub_temperature.initialized = initialized ? 1U : 0U;

            if (!initialized)
            {
                hub_temperature.error_count++;
            }
        }

        if (initialized)
        {
            bme280_sample_t sample;

            bme280_result_t result =
                bme280_read_temperature(&sample);

            hub_temperature.driver_result = (uint32_t)result;
            hub_temperature.i2c_result = bme280_last_i2c_result;

            if (result == BME280_OK)
            {
                hub_temperature.raw_temperature =
                    sample.raw_temperature;

                hub_temperature.temperature_centi_c =
                    sample.temperature_centi_c;

                hub_temperature.sample_count++;
                hub_temperature.sample_valid = 1U;
            }
            else
            {
                hub_temperature.error_count++;

                /* Retry initialization on the next iteration. */
                initialized = false;
                hub_temperature.initialized = 0U;
            }
        }

        blue_on = !blue_on;

        GPIOD_BSRR = blue_on
            ? BLUE_LED_MASK
            : BLUE_LED_MASK << 16;

        timing_delay_ms(500U);
    }
}