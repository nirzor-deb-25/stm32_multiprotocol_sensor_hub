#include <stdbool.h>
#include <stdint.h>

#include "i2c_probe.h"
#include "timing.h"
#include "stm32_regs.h"

#define PROBE_GPIO_MODER   REG32(0x40020400UL)
#define PROBE_GPIO_OTYPER  REG32(0x40020404UL)
#define PROBE_GPIO_SPEED   REG32(0x40020408UL)
#define PROBE_GPIO_PUPDR   REG32(0x4002040CUL)
#define PROBE_GPIO_IDR     REG32(0x40020410UL)
#define PROBE_GPIO_BSRR    REG32(0x40020418UL)

#define SCL_MASK (1UL << 8)
#define SDA_MASK (1UL << 9)

static void delay_half_bit(void)
{
    uint32_t start = timing_cycles();

    while ((uint32_t)(timing_cycles() - start) < 160U)
    {
        __asm volatile ("nop");
    }
}

static void scl_low(void)
{
    PROBE_GPIO_BSRR = SCL_MASK << 16;
}

static void sda_low(void)
{
    PROBE_GPIO_BSRR = SDA_MASK << 16;
}

static void sda_release(void)
{
    PROBE_GPIO_BSRR = SDA_MASK;
}

static void release_bus(void)
{
    PROBE_GPIO_BSRR = SCL_MASK | SDA_MASK;
}

static bool scl_release_and_wait(void)
{
    PROBE_GPIO_BSRR = SCL_MASK;

    uint32_t start = timing_cycles();

    while ((PROBE_GPIO_IDR & SCL_MASK) == 0U)
    {
        if (timing_has_elapsed(start, 2U))
        {
            return false;
        }
    }

    return true;
}

static bool send_stop(void)
{
    scl_low();
    sda_low();
    delay_half_bit();

    if (!scl_release_and_wait())
    {
        release_bus();
        return false;
    }

    delay_half_bit();
    sda_release();
    delay_half_bit();

    return true;
}

static i2c_probe_result_t register_start(void)
{
    sda_release();
    delay_half_bit();

    if (!scl_release_and_wait())
    {
        release_bus();
        return I2C_PROBE_SCL_LOW;
    }

    delay_half_bit();

    if ((PROBE_GPIO_IDR & SDA_MASK) == 0U)
    {
        release_bus();
        return I2C_PROBE_SDA_LOW;
    }

    sda_low();
    delay_half_bit();
    scl_low();

    return I2C_PROBE_ACK;
}

static i2c_probe_result_t register_write_byte(uint8_t byte)
{
    for (uint32_t bit = 0U; bit < 8U; bit++)
    {
        if ((byte & 0x80U) != 0U)
        {
            sda_release();
        }
        else
        {
            sda_low();
        }

        delay_half_bit();

        if (!scl_release_and_wait())
        {
            release_bus();
            return I2C_PROBE_SCL_LOW;
        }

        delay_half_bit();
        scl_low();

        byte = (uint8_t)(byte << 1);
    }

    sda_release();
    delay_half_bit();

    if (!scl_release_and_wait())
    {
        release_bus();
        return I2C_PROBE_SCL_LOW;
    }

    delay_half_bit();

    bool acknowledged =
        (PROBE_GPIO_IDR & SDA_MASK) == 0U;

    scl_low();
    delay_half_bit();

    return acknowledged ? I2C_PROBE_ACK : I2C_PROBE_NACK;
}

void i2c_probe_init(void)
{
    RCC_AHB1ENR |= (1UL << 1);
    (void)RCC_AHB1ENR;

    /* Open-drain: a high output releases the signal. */
    PROBE_GPIO_OTYPER |= SCL_MASK | SDA_MASK;
    release_bus();

    PROBE_GPIO_SPEED &=
        ~((3UL << 16) | (3UL << 18));

    PROBE_GPIO_PUPDR =
        (PROBE_GPIO_PUPDR &
         ~((3UL << 16) | (3UL << 18))) |
        (1UL << 16) |
        (1UL << 18);

    /* PB8: SCL. PB9: SDA. */
    PROBE_GPIO_MODER =
        (PROBE_GPIO_MODER &
         ~((3UL << 16) | (3UL << 18))) |
        (1UL << 16) |
        (1UL << 18);

    delay_half_bit();
}

uint32_t i2c_probe_lines(void)
{
    uint32_t pins = PROBE_GPIO_IDR;

    return
        (((pins & SCL_MASK) != 0U) ? 1U : 0U) |
        (((pins & SDA_MASK) != 0U) ? 2U : 0U);
}

i2c_probe_result_t i2c_probe_address(uint8_t address)
{
    if ((address < 0x08U) || (address > 0x77U))
    {
        return I2C_PROBE_INVALID_ADDRESS;
    }

    i2c_probe_result_t result = register_start();

    if (result != I2C_PROBE_ACK)
    {
        return result;
    }

    result = register_write_byte(
        (uint8_t)(address << 1)
    );

    if (!send_stop())
    {
        return I2C_PROBE_SCL_LOW;
    }

    return result;
}

i2c_probe_result_t i2c_probe_read_register(
    uint8_t address,
    uint8_t register_address,
    uint8_t *value)
{
    if (value == 0)
    {
        return I2C_PROBE_INVALID_ARGUMENT;
    }

    if ((address < 0x08U) || (address > 0x77U))
    {
        return I2C_PROBE_INVALID_ADDRESS;
    }

    uint8_t received = 0U;

    i2c_probe_result_t result = register_start();

    if (result != I2C_PROBE_ACK)
    {
        return result;
    }

    result = register_write_byte(
        (uint8_t)(address << 1)
    );

    if (result != I2C_PROBE_ACK)
    {
        goto finish;
    }

    result = register_write_byte(register_address);

    if (result != I2C_PROBE_ACK)
    {
        goto finish;
    }

    result = register_start();

    if (result != I2C_PROBE_ACK)
    {
        goto finish;
    }

    result = register_write_byte(
        (uint8_t)((address << 1) | 1U)
    );

    if (result != I2C_PROBE_ACK)
    {
        goto finish;
    }

    sda_release();

    for (uint32_t bit = 0U; bit < 8U; bit++)
    {
        delay_half_bit();

        if (!scl_release_and_wait())
        {
            result = I2C_PROBE_SCL_LOW;
            goto finish;
        }

        delay_half_bit();
        received = (uint8_t)(received << 1);

        if ((PROBE_GPIO_IDR & SDA_MASK) != 0U)
        {
            received |= 1U;
        }

        scl_low();
    }

    /* NACK the final byte before sending STOP. */
    sda_release();
    delay_half_bit();

    if (!scl_release_and_wait())
    {
        result = I2C_PROBE_SCL_LOW;
        goto finish;
    }

    delay_half_bit();
    scl_low();

finish:
    if (!send_stop())
    {
        result = I2C_PROBE_SCL_LOW;
    }

    if (result == I2C_PROBE_ACK)
    {
        *value = received;
    }

    return result;
}

i2c_probe_result_t i2c_probe_write_register(
    uint8_t address,
    uint8_t register_address,
    uint8_t value)
{
    if ((address < 0x08U) || (address > 0x77U))
    {
        return I2C_PROBE_INVALID_ADDRESS;
    }

    i2c_probe_result_t result = register_start();

    if (result != I2C_PROBE_ACK)
    {
        return result;
    }

    result = register_write_byte(
        (uint8_t)(address << 1)
    );

    if (result == I2C_PROBE_ACK)
    {
        result = register_write_byte(register_address);
    }

    if (result == I2C_PROBE_ACK)
    {
        result = register_write_byte(value);
    }

    if (!send_stop())
    {
        return I2C_PROBE_SCL_LOW;
    }

    return result;
}