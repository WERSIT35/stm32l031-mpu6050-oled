#ifndef SOFT_I2C_H
#define SOFT_I2C_H

#include <stdint.h>

/* Bit-banged I2C on PA9(SCL)/PA10(SDA) - deliberately different pins from
   the hardware I2C1 bus (PB6/PB7) used for the OLED, so the MPU6050 gets
   its own independent bus/wires for isolation testing. */

/* Bit flags returned by SoftI2C_LineTest() */
#define SOFTI2C_SCL_IDLE_HIGH   0x01
#define SOFTI2C_SDA_IDLE_HIGH   0x02
#define SOFTI2C_SCL_DRIVES_LOW  0x04
#define SOFTI2C_SDA_DRIVES_LOW  0x08
#define SOFTI2C_SCL_EXT_PULLUP  0x10
#define SOFTI2C_SDA_EXT_PULLUP  0x20

/* Pure DC test of the two bus pins - no I2C protocol involved. Separates
   "the MCU pin is misconfigured", "the line is shorted low" and "there is
   nothing attached to this wire" from each other. See soft_i2c.c. */
uint8_t SoftI2C_LineTest(void);

void    SoftI2C_Init(void);
uint8_t SoftI2C_ProbeAddress(uint8_t addr7);
uint8_t SoftI2C_WriteBuffer(uint8_t addr7, const uint8_t *data, uint16_t len);
uint8_t SoftI2C_ReadRegister(uint8_t addr7, uint8_t reg, uint8_t *data, uint16_t len);

#endif
