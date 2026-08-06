#include "soft_i2c.h"
#include "stm32l0xx.h"

#define SCL_PIN 9u
#define SDA_PIN 10u

#define SCL_L()  (GPIOA->BSRR = (1u << (SCL_PIN + 16)))
#define SCL_H()  (GPIOA->BSRR = (1u << SCL_PIN))
#define SDA_L()  (GPIOA->BSRR = (1u << (SDA_PIN + 16)))
#define SDA_H()  (GPIOA->BSRR = (1u << SDA_PIN))
#define SDA_READ() ((GPIOA->IDR >> SDA_PIN) & 1u)
#define SCL_READ() ((GPIOA->IDR >> SCL_PIN) & 1u)

static void i2c_delay(void)
{
    volatile uint32_t i;
    for (i = 0; i < 40; i++) { }
}

void SoftI2C_Init(void)
{
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN;

    GPIOA->MODER &= ~((3u << (SCL_PIN * 2)) | (3u << (SDA_PIN * 2)));
    GPIOA->MODER |=  ((1u << (SCL_PIN * 2)) | (1u << (SDA_PIN * 2))); /* general purpose output */

    GPIOA->OTYPER |= (1u << SCL_PIN) | (1u << SDA_PIN); /* open-drain */

    GPIOA->PUPDR &= ~((3u << (SCL_PIN * 2)) | (3u << (SDA_PIN * 2)));
    GPIOA->PUPDR |=  ((1u << (SCL_PIN * 2)) | (1u << (SDA_PIN * 2))); /* pull-up */

    SCL_H();
    SDA_H();
}

/* ------------------------------------------------------------------
   Pin-level diagnostics. "Nothing ACKed on any address" has three very
   different causes that all look identical from the protocol layer, so
   test the wires directly instead of guessing.
   ------------------------------------------------------------------ */

static void settle(void)
{
    volatile uint32_t i;
    for (i = 0; i < 3000; i++) { }
}

static void pin_input_pull(uint32_t pin, uint32_t pupd)
{
    GPIOA->MODER &= ~(3u << (pin * 2));          /* 00 = input */
    GPIOA->PUPDR &= ~(3u << (pin * 2));
    GPIOA->PUPDR |=  (pupd << (pin * 2));
}

uint8_t SoftI2C_LineTest(void)
{
    uint8_t r = 0;

    /* 1. Idle high? With the pins released and pulled up, both lines should
          read 1. A line stuck at 0 means a short to GND, a wire in the wrong
          hole, or a dead device clamping the bus. */
    SoftI2C_Init();
    SCL_H(); SDA_H();
    settle();
    if (SCL_READ()) r |= SOFTI2C_SCL_IDLE_HIGH;
    if (SDA_READ()) r |= SOFTI2C_SDA_IDLE_HIGH;

    /* 2. Can the MCU pull each line down and read the low back? This is
          entirely MCU-side: it proves PA9/PA10 really are the pins being
          driven and that the open-drain config works. It passes with
          nothing at all connected. */
    SCL_L(); settle();
    if (!SCL_READ()) r |= SOFTI2C_SCL_DRIVES_LOW;
    SCL_H(); settle();

    SDA_L(); settle();
    if (!SDA_READ()) r |= SOFTI2C_SDA_DRIVES_LOW;
    SDA_H(); settle();

    /* 3. The decisive one. Turn the pins into inputs with the INTERNAL
          PULL-DOWN and see whether anything outside still holds them high.
          The MPU6050 breakout carries 10k pull-ups to its own 3V3 rail;
          against the ~40k internal pull-down those win and the line reads 1.
          Nothing attached - or attached but unpowered, or a broken wire -
          and the pull-down wins, reading 0. This is a direct "is a powered
          sensor on the end of this wire" answer. */
    pin_input_pull(SCL_PIN, 2u); /* 10 = pull-down */
    pin_input_pull(SDA_PIN, 2u);
    settle();
    if (SCL_READ()) r |= SOFTI2C_SCL_EXT_PULLUP;
    if (SDA_READ()) r |= SOFTI2C_SDA_EXT_PULLUP;

    SoftI2C_Init(); /* back to open-drain + pull-up for normal operation */
    return r;
}

static void i2c_start(void)
{
    SDA_H(); SCL_H(); i2c_delay();
    SDA_L(); i2c_delay();
    SCL_L(); i2c_delay();
}

static void i2c_stop(void)
{
    SDA_L(); i2c_delay();
    SCL_H(); i2c_delay();
    SDA_H(); i2c_delay();
}

/* Returns 1 if the slave ACKed, 0 otherwise (NACK / nothing on the bus). */
static uint8_t i2c_write_byte(uint8_t b)
{
    uint8_t i;
    uint8_t ack;

    for (i = 0; i < 8; i++)
    {
        if (b & 0x80) SDA_H(); else SDA_L();
        i2c_delay();
        SCL_H(); i2c_delay();
        SCL_L(); i2c_delay();
        b = (uint8_t)(b << 1);
    }

    SDA_H(); /* release SDA so the slave can pull it low for ACK */
    i2c_delay();
    SCL_H(); i2c_delay();
    ack = (uint8_t)(SDA_READ() == 0);
    SCL_L(); i2c_delay();

    return ack;
}

static uint8_t i2c_read_byte(uint8_t ack)
{
    uint8_t i;
    uint8_t b = 0;

    SDA_H(); /* release so the slave can drive SDA */
    for (i = 0; i < 8; i++)
    {
        b = (uint8_t)(b << 1);
        SCL_H(); i2c_delay();
        if (SDA_READ()) b |= 1u;
        SCL_L(); i2c_delay();
    }

    if (ack) SDA_L(); else SDA_H();
    i2c_delay();
    SCL_H(); i2c_delay();
    SCL_L(); i2c_delay();
    SDA_H();

    return b;
}

uint8_t SoftI2C_ProbeAddress(uint8_t addr7)
{
    uint8_t ack;
    i2c_start();
    ack = i2c_write_byte((uint8_t)(addr7 << 1));
    i2c_stop();
    return ack;
}

uint8_t SoftI2C_WriteBuffer(uint8_t addr7, const uint8_t *data, uint16_t len)
{
    uint16_t i;

    i2c_start();
    if (!i2c_write_byte((uint8_t)(addr7 << 1)))
    {
        i2c_stop();
        return 0;
    }
    for (i = 0; i < len; i++)
    {
        if (!i2c_write_byte(data[i]))
        {
            i2c_stop();
            return 0;
        }
    }
    i2c_stop();
    return 1;
}

uint8_t SoftI2C_ReadRegister(uint8_t addr7, uint8_t reg, uint8_t *data, uint16_t len)
{
    uint16_t i;

    i2c_start();
    if (!i2c_write_byte((uint8_t)(addr7 << 1)))
    {
        i2c_stop();
        return 0;
    }
    if (!i2c_write_byte(reg))
    {
        i2c_stop();
        return 0;
    }

    i2c_start(); /* repeated start */
    if (!i2c_write_byte((uint8_t)((addr7 << 1) | 1)))
    {
        i2c_stop();
        return 0;
    }

    for (i = 0; i < len; i++)
    {
        data[i] = i2c_read_byte((uint8_t)(i < (len - 1))); /* ACK all but the last byte */
    }

    i2c_stop();
    return 1;
}
