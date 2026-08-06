#include "delay.h"
#include "i2c.h"
#include "soft_i2c.h"
#include "ssd1306.h"
#include "mpu6050.h"
#include "ui.h"

/* ============================================================
   MPU6050 (accel/gyro) + SSD1306 OLED demo.
   No HAL. Two INDEPENDENT I2C buses, deliberately not shared:
     - OLED:    hardware I2C1, PB6(SCL)/PB7(SDA), AF1 (i2c.c)
     - MPU6050: bit-banged software I2C, PA9(SCL)/PA10(SDA) (soft_i2c.c)
   This isolation is a diagnostic step - if the MPU6050 still doesn't
   show up on its own dedicated bus/pins, it rules out any interaction
   with the OLED's bus entirely.

   Wiring:
     OLED    VCC->3.3V  GND->GND  SCL->PB6  SDA->PB7
     MPU6050 VCC->3.3V  GND->GND  SCL->PA9  SDA->PA10  AD0->GND (addr 0x68)

   LED on PB3: toggles once per rendered frame = running normally,
   fast even blink = MPU6050 not detected on the bus.
   ============================================================ */
#define LED_ON()   (GPIOB->BSRR = (1u << 3))
#define LED_OFF()  (GPIOB->BSRR = (1u << (3 + 16)))

void SysTick_Handler(void) {
    delay_tick();
}

static void LED_Init(void)
{
    RCC->IOPENR |= RCC_IOPENR_GPIOBEN;
    GPIOB->MODER &= ~(3u << (3 * 2));
    GPIOB->MODER |=  (1u << (3 * 2));
    GPIOB->OTYPER &= ~(1u << 3);
    LED_OFF();
}

static void hex2(uint8_t v, char *out)
{
    static const char digits[] = "0123456789ABCDEF";
    out[0] = digits[(v >> 4) & 0xF];
    out[1] = digits[v & 0xF];
    out[2] = '\0';
}

/* Runs the DC pin test on the MPU6050's bus and shows the result. This runs
   before any I2C traffic, so it still reports something useful when the scan
   comes back completely silent. */
static void ShowLineTest(void)
{
    uint8_t t = SoftI2C_LineTest();

    SSD1306_Clear();
    SSD1306_SetCursor(0, 0);
    SSD1306_WriteString("MPU PIN TEST");
    SSD1306_SetCursor(0, 1);
    SSD1306_WriteString("PA9=SCL PA10=SDA");

    SSD1306_SetCursor(0, 3);
    SSD1306_WriteString("IDLE HI  C:");
    SSD1306_WriteString((t & SOFTI2C_SCL_IDLE_HIGH)  ? "Y" : "N");
    SSD1306_WriteString(" D:");
    SSD1306_WriteString((t & SOFTI2C_SDA_IDLE_HIGH)  ? "Y" : "N");

    SSD1306_SetCursor(0, 4);
    SSD1306_WriteString("DRIVE LO C:");
    SSD1306_WriteString((t & SOFTI2C_SCL_DRIVES_LOW) ? "Y" : "N");
    SSD1306_WriteString(" D:");
    SSD1306_WriteString((t & SOFTI2C_SDA_DRIVES_LOW) ? "Y" : "N");

    SSD1306_SetCursor(0, 5);
    SSD1306_WriteString("EXT PU   C:");
    SSD1306_WriteString((t & SOFTI2C_SCL_EXT_PULLUP) ? "Y" : "N");
    SSD1306_WriteString(" D:");
    SSD1306_WriteString((t & SOFTI2C_SDA_EXT_PULLUP) ? "Y" : "N");

    SSD1306_SetCursor(0, 7);
    if ((t & (SOFTI2C_SCL_DRIVES_LOW | SOFTI2C_SDA_DRIVES_LOW))
            != (SOFTI2C_SCL_DRIVES_LOW | SOFTI2C_SDA_DRIVES_LOW))
        SSD1306_WriteString("MCU PIN FAULT");
    else if ((t & (SOFTI2C_SCL_IDLE_HIGH | SOFTI2C_SDA_IDLE_HIGH))
            != (SOFTI2C_SCL_IDLE_HIGH | SOFTI2C_SDA_IDLE_HIGH))
        SSD1306_WriteString("LINE STUCK LOW");
    else if ((t & (SOFTI2C_SCL_EXT_PULLUP | SOFTI2C_SDA_EXT_PULLUP)) == 0)
        SSD1306_WriteString("NOTHING ATTACHED");
    else if ((t & (SOFTI2C_SCL_EXT_PULLUP | SOFTI2C_SDA_EXT_PULLUP))
            != (SOFTI2C_SCL_EXT_PULLUP | SOFTI2C_SDA_EXT_PULLUP))
        SSD1306_WriteString("ONE WIRE DEAD");
    else
        SSD1306_WriteString("PINS LOOK OK");

    SSD1306_UpdateScreen();
    delay(6000);
}

/* Sweeps every valid 7-bit I2C address on whichever bus `probe` belongs
   to and prints whichever ones ACK. Independent of the MPU6050/SSD1306
   driver logic entirely - this only tells you what's physically
   answering on that bus right now.
   Returns the first address that ACKed, or 0 if the bus was silent. */
static uint8_t I2C_ScanAndShow(char *title, uint8_t (*probe)(uint8_t))
{
    uint8_t addr;
    uint8_t row = 1;
    uint8_t first = 0;
    char hex[3];

    SSD1306_Clear();
    SSD1306_SetCursor(0, 0);
    SSD1306_WriteString(title);

    for (addr = 0x08; addr <= 0x77; addr++)
    {
        if (probe(addr))
        {
            if (!first) first = addr;
            hex2(addr, hex);
            SSD1306_SetCursor(0, row);
            SSD1306_WriteString("FOUND 0X");
            SSD1306_WriteString(hex);
            row++;
            if (row > 7) break;
        }
    }

    if (!first)
    {
        SSD1306_SetCursor(0, row);
        SSD1306_WriteString("NONE FOUND");
    }

    SSD1306_UpdateScreen();
    delay(4000);
    return first;
}

int main(void)
{
    MPU6050_Data mpu;
    char buf[12];
    uint8_t mpuOk;
    uint8_t mpuAddr;

    SystemInit();
    SysTick_Config(SystemCoreClock / 1000);

    LED_Init();
    I2C1_Init();
    SoftI2C_Init();
    delay(100);

    SSD1306_Init();
    delay(50);

    /* OLED self-test, independent of the MPU6050: full-on flash, then a
       text line. If neither shows up, it's an OLED/I2C/wiring problem,
       nothing to do with the sensor. */
    SSD1306_AllOn();
    delay(500);
    UI_DrawSplash();
    delay(1200);

    ShowLineTest();

    I2C_ScanAndShow("BUS1 (OLED):", I2C1_ProbeAddress);
    mpuAddr = I2C_ScanAndShow("BUS2 (MPU):", SoftI2C_ProbeAddress);

    /* Trust the scan over the compile-time guess: AD0 high puts the part at
       0x69, and hardcoding 0x68 made the driver miss a sensor the scan had
       just listed. Fall back to the default only if nothing ACKed at all. */
    if (mpuAddr == 0) mpuAddr = MPU6050_ADDR;
    MPU6050_SetAddress(mpuAddr);

    mpuOk = MPU6050_Init();

    if (!mpuOk)
    {
        /* Show why it failed, not just that it failed:
             ACK N        -> nothing answered: wiring, power, or pull-ups
             ACK Y WHO xx -> device is on the bus, the ID byte is the story */
        SSD1306_Clear();
        SSD1306_SetCursor(0, 0);
        SSD1306_WriteString("MPU INIT FAIL");

        SSD1306_SetCursor(0, 2);
        SSD1306_WriteString("ADDR 0X");
        hex2(mpuAddr, buf);
        SSD1306_WriteString(buf);

        SSD1306_SetCursor(0, 3);
        SSD1306_WriteString("ACK  ");
        SSD1306_WriteString(MPU6050_LastAck() ? "YES" : "NO");

        SSD1306_SetCursor(0, 4);
        SSD1306_WriteString("WHO  0X");
        hex2(MPU6050_LastWhoAmI(), buf);
        SSD1306_WriteString(buf);

        SSD1306_SetCursor(0, 6);
        SSD1306_WriteString(MPU6050_LastAck() ? "BAD ID" : "CHECK WIRING");
        SSD1306_UpdateScreen();

        while (1)
        {
            LED_ON();  delay(100);
            LED_OFF(); delay(100);
        }
    }

    /* Steady state: sample, render the dashboard, push one frame. The LED is
       toggled once per frame rather than being blinked with its own delays -
       the old 2 x 500ms pair capped the UI at 1 fps regardless of how fast
       the panel could actually be driven. */
    while (1)
    {
        MPU6050_ReadAll(&mpu);

        UI_DrawDashboard(&mpu);
        SSD1306_UpdateScreen();

        GPIOB->ODR ^= (1u << 3);   /* heartbeat, one toggle per frame */
    }
}
