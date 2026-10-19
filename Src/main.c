#include <stdbool.h>
#include <stdint.h>

#include "stm32_regs.h"
#include "timing.h"
#include "i2c_probe.h"

#define BLUE_LED_MASK (1UL << 15)

typedef struct
{
    uint32_t scan_count;
    uint32_t idle_lines;
    uint32_t result_76;
    uint32_t result_77;
    uint32_t final_lines;
    uint32_t id_read_result;
    uint32_t chip_id;
    uint32_t id_success_count;
    uint32_t id_error_count;
} hub_probe_diagnostics_t;

volatile hub_probe_diagnostics_t hub_probe = {0};

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

    bool blue_on = false;

    for (;;)
    {
        hub_probe.idle_lines = i2c_probe_lines();

        hub_probe.result_76 =
            (uint32_t)i2c_probe_address(0x76U);

        timing_delay_ms(2U);

        hub_probe.result_77 =
            (uint32_t)i2c_probe_address(0x77U);

        timing_delay_ms(2U);

        uint8_t chip_id = 0U;

        i2c_probe_result_t result = i2c_probe_read_register(
            0x77U,
            0xD0U,
            &chip_id
        );

        hub_probe.id_read_result = (uint32_t)result;

        if (result == I2C_PROBE_ACK)
        {
            hub_probe.chip_id = chip_id;

            if (chip_id == 0x60U)
            {
                hub_probe.id_success_count++;
            }
            else
            {
                hub_probe.id_error_count++;
            }
        }
        else
        {
            /* Mark this scan's ID as unavailable. */
            hub_probe.chip_id = 0xFFFFFFFFUL;
            hub_probe.id_error_count++;
        }

        hub_probe.final_lines = i2c_probe_lines();
        hub_probe.scan_count++;

        blue_on = !blue_on;

        GPIOD_BSRR = blue_on
            ? BLUE_LED_MASK
            : BLUE_LED_MASK << 16;

        timing_delay_ms(500U);
    }
}