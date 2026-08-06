#include "ssd1306.h"
#include "i2c.h"
#include <string.h>

/* ============================================================
   Framebuffer SSD1306 driver.

   The previous version streamed bytes straight at the panel, which meant
   every drawing op had to be page-aligned - fine for rows of text, useless
   for a tilted horizon line. Everything now renders into a 1KB RAM buffer
   and is pushed out in one go by SSD1306_UpdateScreen().

   1024 bytes of the L031K6's 8KB SRAM. Layout matches the panel's own page
   addressing: byte index = x + (y/8)*128, bit = y%8 (LSB = topmost row).
   ============================================================ */

static uint8_t fb[SSD1306_WIDTH * SSD1306_HEIGHT / 8];

// Small 5x7 font (ASCII 32..122), plus four symbol glyphs at 0x7B..0x7E.
static const uint8_t Font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x00,0x00,0x5F,0x00,0x00}, // !
    {0x00,0x07,0x00,0x07,0x00}, // "
    {0x14,0x7F,0x14,0x7F,0x14}, // #
    {0x24,0x2A,0x7F,0x2A,0x12}, // $
    {0x23,0x13,0x08,0x64,0x62}, // %
    {0x36,0x49,0x55,0x22,0x50}, // &
    {0x00,0x05,0x03,0x00,0x00}, // '
    {0x00,0x1C,0x22,0x41,0x00}, // (
    {0x00,0x41,0x22,0x1C,0x00}, // )
    {0x14,0x08,0x3E,0x08,0x14}, // *
    {0x08,0x08,0x3E,0x08,0x08}, // +
    {0x00,0x50,0x30,0x00,0x00}, // ,
    {0x08,0x08,0x08,0x08,0x08}, // -
    {0x00,0x60,0x60,0x00,0x00}, // .
    {0x20,0x10,0x08,0x04,0x02}, // /

    {0x3E,0x51,0x49,0x45,0x3E}, // 0
    {0x00,0x42,0x7F,0x40,0x00}, // 1
    {0x42,0x61,0x51,0x49,0x46}, // 2
    {0x21,0x41,0x45,0x4B,0x31}, // 3
    {0x18,0x14,0x12,0x7F,0x10}, // 4
    {0x27,0x45,0x45,0x45,0x39}, // 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 6
    {0x01,0x71,0x09,0x05,0x03}, // 7
    {0x36,0x49,0x49,0x49,0x36}, // 8
    {0x06,0x49,0x49,0x29,0x1E}, // 9

    {0x00,0x36,0x36,0x00,0x00}, // :
    {0x00,0x56,0x36,0x00,0x00}, // ;
    {0x08,0x14,0x22,0x41,0x00}, // <
    {0x14,0x14,0x14,0x14,0x14}, // =
    {0x00,0x41,0x22,0x14,0x08}, // >
    {0x02,0x01,0x51,0x09,0x06}, // ?
    {0x32,0x49,0x79,0x41,0x3E}, // @

    {0x7E,0x11,0x11,0x11,0x7E}, // A
    {0x7F,0x49,0x49,0x49,0x36}, // B
    {0x3E,0x41,0x41,0x41,0x22}, // C
    {0x7F,0x41,0x41,0x22,0x1C}, // D
    {0x7F,0x49,0x49,0x49,0x41}, // E
    {0x7F,0x09,0x09,0x09,0x01}, // F
    {0x3E,0x41,0x49,0x49,0x7A}, // G
    {0x7F,0x08,0x08,0x08,0x7F}, // H
    {0x00,0x41,0x7F,0x41,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01}, // J
    {0x7F,0x08,0x14,0x22,0x41}, // K
    {0x7F,0x40,0x40,0x40,0x40}, // L
    {0x7F,0x02,0x0C,0x02,0x7F}, // M
    {0x7F,0x04,0x08,0x10,0x7F}, // N
    {0x3E,0x41,0x41,0x41,0x3E}, // O
    {0x7F,0x09,0x09,0x09,0x06}, // P
    {0x3E,0x41,0x51,0x21,0x5E}, // Q
    {0x7F,0x09,0x19,0x29,0x46}, // R
    {0x46,0x49,0x49,0x49,0x31}, // S
    {0x01,0x01,0x7F,0x01,0x01}, // T
    {0x3F,0x40,0x40,0x40,0x3F}, // U
    {0x1F,0x20,0x40,0x20,0x1F}, // V
    {0x3F,0x40,0x38,0x40,0x3F}, // W
    {0x63,0x14,0x08,0x14,0x63}, // X
    {0x07,0x08,0x70,0x08,0x07}, // Y
    {0x61,0x51,0x49,0x45,0x43}, // Z

    {0x00,0x7F,0x41,0x41,0x00}, // [
    {0x02,0x04,0x08,0x10,0x20}, // backslash
    {0x00,0x41,0x41,0x7F,0x00}, // ]
    {0x04,0x02,0x01,0x02,0x04}, // ^
    {0x80,0x80,0x80,0x80,0x80}, // _
    {0x00,0x01,0x02,0x04,0x00}, // `
    {0x20,0x54,0x54,0x54,0x78}, // a
    {0x7F,0x48,0x44,0x44,0x38}, // b
    {0x38,0x44,0x44,0x44,0x20}, // c
    {0x38,0x44,0x44,0x48,0x7F}, // d
    {0x38,0x54,0x54,0x54,0x18}, // e
    {0x08,0x7E,0x09,0x01,0x02}, // f
    {0x08,0x54,0x54,0x54,0x3C}, // g
    {0x7F,0x08,0x04,0x04,0x78}, // h
    {0x00,0x44,0x7D,0x40,0x00}, // i
    {0x20,0x40,0x44,0x3D,0x00}, // j
    {0x7F,0x10,0x28,0x44,0x00}, // k
    {0x00,0x41,0x7F,0x40,0x00}, // l
    {0x7C,0x04,0x18,0x04,0x78}, // m
    {0x7C,0x08,0x04,0x04,0x78}, // n
    {0x38,0x44,0x44,0x44,0x38}, // o
    {0x7C,0x14,0x14,0x14,0x08}, // p
    {0x08,0x14,0x14,0x18,0x7C}, // q
    {0x7C,0x08,0x04,0x04,0x08}, // r
    {0x48,0x54,0x54,0x54,0x20}, // s
    {0x04,0x3F,0x44,0x40,0x20}, // t
    {0x3C,0x40,0x40,0x20,0x7C}, // u
    {0x1C,0x20,0x40,0x20,0x1C}, // v
    {0x3C,0x40,0x30,0x40,0x3C}, // w
    {0x44,0x28,0x10,0x28,0x44}, // x
    {0x0C,0x50,0x50,0x50,0x3C}, // y
    {0x44,0x64,0x54,0x4C,0x44}, // z

    {0x00,0x02,0x05,0x02,0x00}, // 0x7B degree
    {0x04,0x02,0x7F,0x02,0x04}, // 0x7C up arrow
    {0x10,0x20,0x7F,0x20,0x10}, // 0x7D down arrow
    {0x00,0x18,0x18,0x00,0x00}  // 0x7E bullet
};

#define FONT_FIRST 32
#define FONT_LAST  0x7E

/* ------------------------------------------------------------------
   Panel transport
   ------------------------------------------------------------------ */

static void SSD1306_SendCommand(uint8_t cmd) {
    uint8_t d[2] = {0x00, cmd};
    I2C1_WriteBuffer(SSD1306_I2C_ADDR, d, 2);
}

void SSD1306_UpdateScreen(void) {
    uint8_t page;
    uint8_t packet[SSD1306_WIDTH + 1];

    packet[0] = 0x40; /* Co=0, D/C=1 -> the rest of this write is display RAM */

    for (page = 0; page < SSD1306_HEIGHT / 8; page++) {
        SSD1306_SendCommand((uint8_t)(0xB0 + page)); /* page address     */
        SSD1306_SendCommand(0x00);                   /* column low nibble  */
        SSD1306_SendCommand(0x10);                   /* column high nibble */

        memcpy(&packet[1], &fb[page * SSD1306_WIDTH], SSD1306_WIDTH);
        I2C1_WriteBuffer(SSD1306_I2C_ADDR, packet, SSD1306_WIDTH + 1);
    }
}

void SSD1306_Init(void) {
    for (volatile int i = 0; i < 100000; i++);

    SSD1306_SendCommand(0xAE);
    SSD1306_SendCommand(0xD5);
    SSD1306_SendCommand(0x80);
    SSD1306_SendCommand(0xA8);
    SSD1306_SendCommand(0x3F);
    SSD1306_SendCommand(0xD3);
    SSD1306_SendCommand(0x00);
    SSD1306_SendCommand(0x40);
    SSD1306_SendCommand(0x8D);
    SSD1306_SendCommand(0x14);
    SSD1306_SendCommand(0x20);
    SSD1306_SendCommand(0x00);
    SSD1306_SendCommand(0xA1);
    SSD1306_SendCommand(0xC8);
    SSD1306_SendCommand(0xDA);
    SSD1306_SendCommand(0x12);
    SSD1306_SendCommand(0x81);
    SSD1306_SendCommand(0x7F);
    SSD1306_SendCommand(0xD9);
    SSD1306_SendCommand(0xF1);
    SSD1306_SendCommand(0xDB);
    SSD1306_SendCommand(0x40);
    SSD1306_SendCommand(0xA4);
    SSD1306_SendCommand(0xA6);
    SSD1306_SendCommand(0xAF);

    SSD1306_Clear();
    SSD1306_UpdateScreen();
}

void SSD1306_DisplayOn(void)  { SSD1306_SendCommand(0xAF); }
void SSD1306_DisplayOff(void) { SSD1306_SendCommand(0xAE); }

/* ------------------------------------------------------------------
   Primitives
   ------------------------------------------------------------------ */

void SSD1306_Clear(void) {
    memset(fb, 0x00, sizeof(fb));
}

void SSD1306_AllOn(void) {
    memset(fb, 0xFF, sizeof(fb));
    SSD1306_UpdateScreen();
}

void SSD1306_DrawPixel(int16_t x, int16_t y, uint8_t color) {
    uint16_t idx;
    uint8_t  mask;

    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT) return;

    idx  = (uint16_t)x + ((uint16_t)(y >> 3) * SSD1306_WIDTH);
    mask = (uint8_t)(1u << (y & 7));

    if (color == SSD1306_WHITE)       fb[idx] |=  mask;
    else if (color == SSD1306_BLACK)  fb[idx] &= (uint8_t)~mask;
    else                              fb[idx] ^=  mask;
}

void SSD1306_DrawHLine(int16_t x, int16_t y, int16_t w, uint8_t color) {
    int16_t i;
    for (i = 0; i < w; i++) SSD1306_DrawPixel((int16_t)(x + i), y, color);
}

void SSD1306_DrawVLine(int16_t x, int16_t y, int16_t h, uint8_t color) {
    int16_t i;
    for (i = 0; i < h; i++) SSD1306_DrawPixel(x, (int16_t)(y + i), color);
}

void SSD1306_DrawDashHLine(int16_t x, int16_t y, int16_t w, uint8_t on, uint8_t off, uint8_t color) {
    int16_t i;
    uint8_t period = (uint8_t)(on + off);
    for (i = 0; i < w; i++)
        if ((i % period) < on) SSD1306_DrawPixel((int16_t)(x + i), y, color);
}

void SSD1306_DrawDashVLine(int16_t x, int16_t y, int16_t h, uint8_t on, uint8_t off, uint8_t color) {
    int16_t i;
    uint8_t period = (uint8_t)(on + off);
    for (i = 0; i < h; i++)
        if ((i % period) < on) SSD1306_DrawPixel(x, (int16_t)(y + i), color);
}

/* Bresenham */
void SSD1306_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color) {
    int16_t dx = (int16_t)(x1 - x0); int16_t sx = 1;
    int16_t dy = (int16_t)(y1 - y0); int16_t sy = 1;
    int16_t err, e2;

    if (dx < 0) { dx = (int16_t)-dx; sx = -1; }
    if (dy < 0) { dy = (int16_t)-dy; sy = -1; }
    dy = (int16_t)-dy;
    err = (int16_t)(dx + dy);

    for (;;) {
        SSD1306_DrawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = (int16_t)(2 * err);
        if (e2 >= dy) { err = (int16_t)(err + dy); x0 = (int16_t)(x0 + sx); }
        if (e2 <= dx) { err = (int16_t)(err + dx); y0 = (int16_t)(y0 + sy); }
    }
}

void SSD1306_DrawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color) {
    SSD1306_DrawHLine(x, y, w, color);
    SSD1306_DrawHLine(x, (int16_t)(y + h - 1), w, color);
    SSD1306_DrawVLine(x, y, h, color);
    SSD1306_DrawVLine((int16_t)(x + w - 1), y, h, color);
}

void SSD1306_FillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color) {
    int16_t j;
    for (j = 0; j < h; j++) SSD1306_DrawHLine(x, (int16_t)(y + j), w, color);
}

/* Midpoint circle */
void SSD1306_DrawCircle(int16_t cx, int16_t cy, int16_t r, uint8_t color) {
    int16_t x = r, y = 0;
    int16_t err = (int16_t)(1 - r);

    while (x >= y) {
        SSD1306_DrawPixel((int16_t)(cx + x), (int16_t)(cy + y), color);
        SSD1306_DrawPixel((int16_t)(cx + y), (int16_t)(cy + x), color);
        SSD1306_DrawPixel((int16_t)(cx - y), (int16_t)(cy + x), color);
        SSD1306_DrawPixel((int16_t)(cx - x), (int16_t)(cy + y), color);
        SSD1306_DrawPixel((int16_t)(cx - x), (int16_t)(cy - y), color);
        SSD1306_DrawPixel((int16_t)(cx - y), (int16_t)(cy - x), color);
        SSD1306_DrawPixel((int16_t)(cx + y), (int16_t)(cy - x), color);
        SSD1306_DrawPixel((int16_t)(cx + x), (int16_t)(cy - y), color);

        y++;
        if (err < 0) {
            err = (int16_t)(err + 2 * y + 1);
        } else {
            x--;
            err = (int16_t)(err + 2 * (y - x) + 1);
        }
    }
}

/* ------------------------------------------------------------------
   Text
   ------------------------------------------------------------------ */

static void draw_glyph(int16_t x, int16_t y, char c, uint8_t color, uint8_t scale) {
    const uint8_t *bmp;
    uint8_t col, row, sx, sy;

    if ((uint8_t)c < FONT_FIRST || (uint8_t)c > FONT_LAST) c = '?';
    bmp = Font5x7[(uint8_t)c - FONT_FIRST];

    for (col = 0; col < 5; col++) {
        uint8_t bits = bmp[col];
        for (row = 0; row < 8; row++) {
            if (!(bits & (1u << row))) continue;
            if (scale == 1) {
                SSD1306_DrawPixel((int16_t)(x + col), (int16_t)(y + row), color);
            } else {
                for (sy = 0; sy < scale; sy++)
                    for (sx = 0; sx < scale; sx++)
                        SSD1306_DrawPixel((int16_t)(x + col * scale + sx),
                                          (int16_t)(y + row * scale + sy), color);
            }
        }
    }
}

uint8_t SSD1306_TextWidth(const char *s, uint8_t scale) {
    uint8_t n = 0;
    while (*s++) n++;
    if (n == 0) return 0;
    /* 5px glyph + 1px gap, minus the trailing gap */
    return (uint8_t)(n * 6 * scale - scale);
}

void SSD1306_DrawString(int16_t x, int16_t y, const char *s, uint8_t color, uint8_t scale) {
    while (*s) {
        draw_glyph(x, y, *s++, color, scale);
        x = (int16_t)(x + 6 * scale);
    }
}

void SSD1306_DrawStringRight(int16_t xr, int16_t y, const char *s, uint8_t color, uint8_t scale) {
    SSD1306_DrawString((int16_t)(xr - SSD1306_TextWidth(s, scale)), y, s, color, scale);
}

/* Legacy row-oriented API used by the boot diagnostic screens. */
static int16_t cur_x = 0, cur_y = 0;

void SSD1306_SetCursor(uint8_t x, uint8_t y) {
    cur_x = (int16_t)x;
    cur_y = (int16_t)(y * 8);
}

void SSD1306_WriteString(char *str) {
    SSD1306_DrawString(cur_x, cur_y, str, SSD1306_WHITE, 1);
    cur_x = (int16_t)(cur_x + SSD1306_TextWidth(str, 1) + 1);
}

/* ------------------------------------------------------------------
   Icons
   ------------------------------------------------------------------ */

void SSD1306_DrawIcon8(int16_t x, int16_t y, const uint8_t *icon, uint8_t color) {
    uint8_t row, col;
    for (row = 0; row < 8; row++) {
        uint8_t bits = icon[row];
        for (col = 0; col < 8; col++)
            if (bits & (0x80u >> col))
                SSD1306_DrawPixel((int16_t)(x + col), (int16_t)(y + row), color);
    }
}
