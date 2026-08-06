#ifndef MPU6050_H
#define MPU6050_H

#include <stdint.h>

/* 7-bit I2C address. AD0 pin tied low (default/GND) = 0x68.
   If your breakout ties AD0 high (3.3V), change this to 0x69. */
#define MPU6050_ADDR 0x68

typedef struct {
    int16_t accel_raw[3];
    int16_t gyro_raw[3];
    int16_t temp_raw;

    int32_t accel_mg[3];     /* milli-g, X/Y/Z */
    int32_t gyro_dpsx10[3];  /* tenths of a degree/sec, X/Y/Z */
    int32_t temp_cx10;       /* tenths of a degree C */

    int32_t roll_x10;        /* tenths of a degree, tilt about X axis (from accel) */
    int32_t pitch_x10;       /* tenths of a degree, tilt about Y axis (from accel) */
} MPU6050_Data;

/* Override the address used for all subsequent transfers. Pass whatever the
   bus scan actually found (0x68 or 0x69) instead of assuming AD0 is low. */
void    MPU6050_SetAddress(uint8_t addr7);
uint8_t MPU6050_GetAddress(void);

/* Diagnostics from the last MPU6050_Init() call: whether the device ACKed at
   all, and the raw WHO_AM_I byte it returned. Display these when init fails -
   "no ACK" (wiring/power) and "ACKed but odd ID" (clone chip) are completely
   different problems and look identical otherwise. */
uint8_t MPU6050_LastAck(void);
uint8_t MPU6050_LastWhoAmI(void);

/* Probes the device (WHO_AM_I) and wakes it up with sane default ranges
   (+/-2g accel, +/-250 dps gyro, ~125Hz sample rate). Returns 1 on success,
   0 if the device didn't respond or reported an unexpected ID. */
uint8_t MPU6050_Init(void);

/* Reads accel/gyro/temp in one burst and fills in all derived fields. */
uint8_t MPU6050_ReadAll(MPU6050_Data *out);

#endif
