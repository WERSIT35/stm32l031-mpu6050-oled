#include "mpu6050.h"
#include "soft_i2c.h"
#include <math.h>

#define REG_SMPLRT_DIV    0x19
#define REG_CONFIG        0x1A
#define REG_GYRO_CONFIG   0x1B
#define REG_ACCEL_CONFIG  0x1C
#define REG_PWR_MGMT_1    0x6B
#define REG_WHO_AM_I      0x75
#define REG_ACCEL_XOUT_H  0x3B

static uint8_t s_addr = MPU6050_ADDR;
static uint8_t s_lastAck = 0;
static uint8_t s_lastWho = 0xFF;

void    MPU6050_SetAddress(uint8_t addr7) { s_addr = addr7; }
uint8_t MPU6050_GetAddress(void)          { return s_addr; }
uint8_t MPU6050_LastAck(void)             { return s_lastAck; }
uint8_t MPU6050_LastWhoAmI(void)          { return s_lastWho; }

static uint8_t MPU6050_WriteReg(uint8_t reg, uint8_t val)
{
    uint8_t packet[2];
    packet[0] = reg;
    packet[1] = val;
    return SoftI2C_WriteBuffer(s_addr, packet, 2);
}

uint8_t MPU6050_Init(void)
{
    uint8_t who = 0;

    s_lastWho = 0xFF;
    s_lastAck = SoftI2C_ReadRegister(s_addr, REG_WHO_AM_I, &who, 1);
    if (!s_lastAck) return 0;
    s_lastWho = who;

    /* Do NOT reject on an unfamiliar ID. Genuine InvenSense parts answer 0x68,
       but the dies fitted to GY-521 clones report 0x70, 0x71, 0x72, 0x73, 0x75
       and 0x98 while remaining register-compatible - rejecting them here was
       reporting a perfectly working sensor as "NOT FOUND". A NACK is the only
       reliable "nothing there" signal; 0x00/0xFF just means the bus read
       garbage (stuck line, no pull-up). */
    if (who == 0x00 || who == 0xFF) return 0;

    if (!MPU6050_WriteReg(REG_PWR_MGMT_1, 0x00)) return 0; /* wake from sleep, internal 8MHz osc */

    MPU6050_WriteReg(REG_SMPLRT_DIV, 0x07);   /* sample rate = 1kHz / (1+7) = 125Hz */
    MPU6050_WriteReg(REG_CONFIG, 0x03);       /* DLPF ~44Hz bandwidth */
    MPU6050_WriteReg(REG_GYRO_CONFIG, 0x00);  /* +/-250 deg/s */
    MPU6050_WriteReg(REG_ACCEL_CONFIG, 0x00); /* +/-2g */

    return 1;
}

uint8_t MPU6050_ReadAll(MPU6050_Data *out)
{
    uint8_t raw[14];
    uint8_t i;
    float ax_g, ay_g, az_g, roll, pitch;

    if (!SoftI2C_ReadRegister(s_addr, REG_ACCEL_XOUT_H, raw, 14)) return 0;

    out->accel_raw[0] = (int16_t)((raw[0] << 8) | raw[1]);
    out->accel_raw[1] = (int16_t)((raw[2] << 8) | raw[3]);
    out->accel_raw[2] = (int16_t)((raw[4] << 8) | raw[5]);
    out->temp_raw      = (int16_t)((raw[6] << 8) | raw[7]);
    out->gyro_raw[0]   = (int16_t)((raw[8] << 8) | raw[9]);
    out->gyro_raw[1]   = (int16_t)((raw[10] << 8) | raw[11]);
    out->gyro_raw[2]   = (int16_t)((raw[12] << 8) | raw[13]);

    for (i = 0; i < 3; i++)
    {
        out->accel_mg[i]    = ((int32_t)out->accel_raw[i] * 1000) / 16384; /* +/-2g: 16384 LSB/g */
        out->gyro_dpsx10[i] = ((int32_t)out->gyro_raw[i] * 10) / 131;      /* +/-250dps: 131 LSB/(deg/s) */
    }

    out->temp_cx10 = ((int32_t)out->temp_raw * 10) / 340 + 365; /* datasheet: Temp(C) = raw/340 + 36.53 */

    ax_g = out->accel_mg[0] / 1000.0f;
    ay_g = out->accel_mg[1] / 1000.0f;
    az_g = out->accel_mg[2] / 1000.0f;

    roll  = atan2f(ay_g, az_g) * 57.29578f;
    pitch = atan2f(-ax_g, sqrtf(ay_g * ay_g + az_g * az_g)) * 57.29578f;

    out->roll_x10  = (int32_t)(roll * 10.0f);
    out->pitch_x10 = (int32_t)(pitch * 10.0f);

    return 1;
}
