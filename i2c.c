#include "i2c.h"

/* ============================================================
   I2C1 on PB6 (SCL) / PB7 (SDA), AF1.
   Kernel clock routed to HSI16 (16 MHz) via RCC->CCIPR, independent
   of whatever the system clock (SYSCLK) is currently running at -
   avoids re-touching Flash wait states / system clock speed entirely.
   ============================================================ */

void I2C1_Init(void)
{
    /* 1. Make sure HSI16 is running (just the oscillator, not switching SYSCLK to it) */
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY)) { }

    /* 2. Route I2C1 kernel clock to HSI16: RCC_CCIPR I2C1SEL[1:0] = 10 */
    RCC->CCIPR &= ~(3u << 12);
    RCC->CCIPR |=  (2u << 12);

    /* 3. Enable clocks: GPIOB + I2C1 */
    RCC->IOPENR  |= RCC_IOPENR_GPIOBEN;
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;

    /* 4. PB6/PB7 -> AF1 (I2C1_SCL/SDA), open-drain, pull-up enabled */
    GPIOB->MODER &= ~((3u << (6 * 2)) | (3u << (7 * 2)));
    GPIOB->MODER |=  ((2u << (6 * 2)) | (2u << (7 * 2))); /* 10 = AF mode */

    GPIOB->OTYPER |= (1u << 6) | (1u << 7);   /* open-drain, required for I2C */
    GPIOB->PUPDR  &= ~((3u << (6 * 2)) | (3u << (7 * 2)));
    GPIOB->PUPDR  |=  ((1u << (6 * 2)) | (1u << (7 * 2))); /* internal pull-up (module usually has its own too) */

    GPIOB->OSPEEDR |= (3u << (6 * 2)) | (3u << (7 * 2));

    /* AFR[0] covers pins 0-7, 4 bits each. Pin6 -> bits 24-27, Pin7 -> bits 28-31 */
    GPIOB->AFR[0] &= ~((0xFu << 24) | (0xFu << 28));
    GPIOB->AFR[0] |=  ((1u << 24) | (1u << 28)); /* AF1 = I2C1 on PB6/PB7 */

    /* 5. I2C1 timing: standard-mode 100kHz @ 16MHz kernel clock.
       0x00303D5B is ST's published reference value for exactly this
       clock/speed combination (from AN4235 / STM32L0 examples). */
    I2C1->CR1 &= ~I2C_CR1_PE;     /* disable before configuring */
    I2C1->TIMINGR = 0x00303D5B;
    I2C1->CR1 |= I2C_CR1_PE;      /* enable */
}

/* Blocking write: addr7 = 7-bit device address (e.g. 0x3C for SSD1306).
   len must be <= 255 per call (STM32 I2Cv2 NBYTES field is 8-bit) -
   caller should chunk larger transfers into multiple calls. Returns 1 on
   success, 0 on error (NACK / bus problem / timeout). */
uint8_t I2C1_WriteBuffer(uint8_t addr7, const uint8_t *data, uint16_t len)
{
    uint16_t i;
    uint32_t busyTimeout = 100000;

    /* NOTE: this wait now has a timeout - previously this loop could spin
       forever if the bus got stuck (e.g. OLED not wired/powered correctly),
       which is what was silently hanging the whole program before any
       delay() call ever ran. */
    while (I2C1->ISR & I2C_ISR_BUSY)
    {
        if (--busyTimeout == 0) return 0;
    }

    I2C1->CR2 = 0;
    I2C1->CR2 |= ((uint32_t)addr7 << 1) & I2C_CR2_SADD_Msk;
    I2C1->CR2 |= ((uint32_t)len << I2C_CR2_NBYTES_Pos) & I2C_CR2_NBYTES_Msk;
    I2C1->CR2 |= I2C_CR2_AUTOEND;
    I2C1->CR2 |= I2C_CR2_START;    /* RD_WRN left at 0 = write */

    for (i = 0; i < len; i++)
    {
        uint32_t timeout = 100000;
        while (!(I2C1->ISR & I2C_ISR_TXIS))
        {
            if (I2C1->ISR & I2C_ISR_NACKF)
            {
                I2C1->ICR |= I2C_ICR_NACKCF;
                return 0;
            }
            if (--timeout == 0) return 0;
        }
        I2C1->TXDR = data[i];
    }

    {
        uint32_t timeout = 100000;
        while (!(I2C1->ISR & I2C_ISR_STOPF))
        {
            if (--timeout == 0) return 0;
        }
        I2C1->ICR |= I2C_ICR_STOPCF;
    }

    return 1;
}

/* Sends a zero-length write to test whether a device ACKs at this address.
   Used for scanning the bus to find out what's actually connected. */
uint8_t I2C1_ProbeAddress(uint8_t addr7)
{
    uint32_t busyTimeout = 100000;
    uint32_t timeout = 10000;

    while (I2C1->ISR & I2C_ISR_BUSY)
    {
        if (--busyTimeout == 0) return 0;
    }

    I2C1->CR2 = 0;
    I2C1->CR2 |= ((uint32_t)addr7 << 1) & I2C_CR2_SADD_Msk;
    I2C1->CR2 |= (0u << I2C_CR2_NBYTES_Pos); /* 0 bytes - address-only probe */
    I2C1->CR2 |= I2C_CR2_AUTOEND;
    I2C1->CR2 |= I2C_CR2_START;

    while (!(I2C1->ISR & (I2C_ISR_NACKF | I2C_ISR_STOPF)))
    {
        if (--timeout == 0) return 0;
    }

    if (I2C1->ISR & I2C_ISR_NACKF)
    {
        I2C1->ICR |= I2C_ICR_NACKCF;
        while (!(I2C1->ISR & I2C_ISR_STOPF)) { if (--timeout == 0) break; }
        I2C1->ICR |= I2C_ICR_STOPCF;
        return 0; /* no device at this address */
    }

    I2C1->ICR |= I2C_ICR_STOPCF;
    return 1; /* device ACKed */
}

/* Two-phase transaction: write the register address with no STOP, then a
   repeated START to read `len` bytes back. This is the standard way to
   read a register out of an I2C sensor (MPU6050, etc) - the write phase
   has AUTOEND cleared so the bus stays owned between the two phases. */
uint8_t I2C1_ReadRegister(uint8_t addr7, uint8_t reg, uint8_t *data, uint16_t len)
{
    uint16_t i;
    uint32_t busyTimeout = 100000;
    uint32_t timeout;

    while (I2C1->ISR & I2C_ISR_BUSY)
    {
        if (--busyTimeout == 0) return 0;
    }

    /* Phase 1: write register address, AUTOEND=0 so no STOP is issued */
    I2C1->CR2 = 0;
    I2C1->CR2 |= ((uint32_t)addr7 << 1) & I2C_CR2_SADD_Msk;
    I2C1->CR2 |= (1u << I2C_CR2_NBYTES_Pos) & I2C_CR2_NBYTES_Msk;
    I2C1->CR2 |= I2C_CR2_START;    /* RD_WRN left at 0 = write */

    timeout = 100000;
    while (!(I2C1->ISR & I2C_ISR_TXIS))
    {
        if (I2C1->ISR & I2C_ISR_NACKF)
        {
            I2C1->ICR |= I2C_ICR_NACKCF;
            return 0;
        }
        if (--timeout == 0) return 0;
    }
    I2C1->TXDR = reg;

    timeout = 100000;
    while (!(I2C1->ISR & I2C_ISR_TC))
    {
        if (I2C1->ISR & I2C_ISR_NACKF)
        {
            I2C1->ICR |= I2C_ICR_NACKCF;
            return 0;
        }
        if (--timeout == 0) return 0;
    }

    /* Phase 2: repeated START, read `len` bytes, AUTOEND issues STOP after */
    I2C1->CR2 = 0;
    I2C1->CR2 |= ((uint32_t)addr7 << 1) & I2C_CR2_SADD_Msk;
    I2C1->CR2 |= ((uint32_t)len << I2C_CR2_NBYTES_Pos) & I2C_CR2_NBYTES_Msk;
    I2C1->CR2 |= I2C_CR2_RD_WRN;
    I2C1->CR2 |= I2C_CR2_AUTOEND;
    I2C1->CR2 |= I2C_CR2_START;

    for (i = 0; i < len; i++)
    {
        timeout = 100000;
        while (!(I2C1->ISR & I2C_ISR_RXNE))
        {
            if (I2C1->ISR & I2C_ISR_NACKF)
            {
                I2C1->ICR |= I2C_ICR_NACKCF;
                return 0;
            }
            if (--timeout == 0) return 0;
        }
        data[i] = (uint8_t)I2C1->RXDR;
    }

    {
        uint32_t stopTimeout = 100000;
        while (!(I2C1->ISR & I2C_ISR_STOPF))
        {
            if (--stopTimeout == 0) return 0;
        }
        I2C1->ICR |= I2C_ICR_STOPCF;
    }

    return 1;
}