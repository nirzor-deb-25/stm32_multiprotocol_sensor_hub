#ifndef I2C_PROBE_H
#define I2C_PROBE_H

#include <stdint.h>

typedef enum
{
    I2C_PROBE_ACK = 0,
    I2C_PROBE_NACK = 1,
    I2C_PROBE_SCL_LOW = 2,
    I2C_PROBE_SDA_LOW = 3,
    I2C_PROBE_INVALID_ADDRESS = 4,
    I2C_PROBE_INVALID_ARGUMENT = 5
} i2c_probe_result_t;

/* Call timing_init() before using this module. */
void i2c_probe_init(void);

/* Bit 0: SCL level. Bit 1: SDA level. */
uint32_t i2c_probe_lines(void);

i2c_probe_result_t i2c_probe_address(uint8_t address);

i2c_probe_result_t i2c_probe_read_register(
    uint8_t address,
    uint8_t register_address,
    uint8_t *value
);

#endif