#ifndef SSD1306_H_
#define SSD1306_H_

#include <stdint.h>

// SSD1306 7-bit I2C address (0x3C is the common default; try 0x3D if the
// module doesn't respond).
#define SSD1306_I2C_ADDR 0x3C

#define SSD1306_WIDTH    128
#define SSD1306_HEIGHT   64

/* Colours. INVERT flips whatever is already in the buffer, which is how the
   header bar draws dark text onto its light background. */
#define SSD1306_BLACK    0
#define SSD1306_WHITE    1
#define SSD1306_INVERT   2

/* Extra glyphs live in the unused ASCII tail (0x7B-0x7E) so they can be
   embedded directly in ordinary string literals:
       SSD1306_DrawString(x, y, "-12.3" DEG, SSD1306_WHITE, 1); */
#define DEG   "\x7B"   /* degree sign      */
#define ARR_U "\x7C"   /* small up arrow   */
#define ARR_D "\x7D"   /* small down arrow */
#define BULT  "\x7E"   /* centre bullet    */

/* ---- panel ---- */
void SSD1306_Init(void);
void SSD1306_DisplayOn(void);
void SSD1306_DisplayOff(void);

/* Pushes the whole framebuffer out over I2C. Nothing drawn since the last
   call is visible until this runs. */
void SSD1306_UpdateScreen(void);

/* ---- framebuffer ---- */
void SSD1306_Clear(void);                 /* buffer only - call UpdateScreen() */
void SSD1306_AllOn(void);                 /* fills and pushes immediately      */

void SSD1306_DrawPixel(int16_t x, int16_t y, uint8_t color);
void SSD1306_DrawHLine(int16_t x, int16_t y, int16_t w, uint8_t color);
void SSD1306_DrawVLine(int16_t x, int16_t y, int16_t h, uint8_t color);
void SSD1306_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color);
void SSD1306_DrawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
void SSD1306_FillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
void SSD1306_DrawCircle(int16_t cx, int16_t cy, int16_t r, uint8_t color);

/* Dashed rules, for dividers that should read as separators rather than as
   another piece of data. `on`/`off` are run lengths in pixels. */
void SSD1306_DrawDashHLine(int16_t x, int16_t y, int16_t w, uint8_t on, uint8_t off, uint8_t color);
void SSD1306_DrawDashVLine(int16_t x, int16_t y, int16_t h, uint8_t on, uint8_t off, uint8_t color);

/* ---- text ---- */
/* scale 1 = 5x7 glyphs on a 6px pitch, scale 2 = 10x14 on a 12px pitch. */
void    SSD1306_DrawString(int16_t x, int16_t y, const char *s, uint8_t color, uint8_t scale);
void    SSD1306_DrawStringRight(int16_t xr, int16_t y, const char *s, uint8_t color, uint8_t scale);
uint8_t SSD1306_TextWidth(const char *s, uint8_t scale);

/* Legacy row-based text API, kept for the boot diagnostic screens.
   y is a text row (0-7), x is a pixel column. */
void SSD1306_SetCursor(uint8_t x, uint8_t y);
void SSD1306_WriteString(char *str);

/* ---- icons ---- */
/* 8x8 bitmap, one byte per row, bit 7 = leftmost pixel. */
void SSD1306_DrawIcon8(int16_t x, int16_t y, const uint8_t *icon, uint8_t color);

#endif
