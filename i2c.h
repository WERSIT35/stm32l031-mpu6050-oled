#ifndef I2C_H
#define I2C_H

#include "stm32l0xx.h"
#include <stdint.h>

void    I2C1_Init(void);
uint8_t I2C1_WriteBuffer(uint8_t addr7, const uint8_t *data, uint16_t len);
uint8_t I2C1_ProbeAddress(uint8_t addr7); /* returns 1 if a device ACKs at this address */

/* Writes `reg` (no STOP), then repeated-START read of `len` bytes into `data`.
   Standard "read starting at register" transaction used by I2C sensors
   (MPU6050, etc). Returns 1 on success, 0 on error (NACK/timeout). */
uint8_t I2C1_ReadRegister(uint8_t addr7, uint8_t reg, uint8_t *data, uint16_t len);

#endif