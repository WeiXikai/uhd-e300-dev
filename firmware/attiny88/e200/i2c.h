#ifndef I2C_H
#define I2C_H

#include "io.h"

void i2c_init(io_pin_t sda, io_pin_t scl);
bool i2c_read(io_pin_t sda, io_pin_t scl, uint8_t addr, uint8_t subaddr, uint8_t* value);
bool i2c_write(io_pin_t sda, io_pin_t scl, uint8_t addr, uint8_t subaddr, uint8_t value);

#endif // I2C_H
