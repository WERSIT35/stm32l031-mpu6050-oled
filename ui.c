#include "ui.h"
#include "ssd1306.h"
#include <math.h>

/* ============================================================
   128x64 dashboard.

     y  0..10   status bar   - inverted, device name / temp / state icons
     y 11       gutter
     y 12..63   content
                x  2..50     attitude indicator (the hero element)
                x 57         dashed divider
                x 62..125    readout column: ROLL (large), PITCH, ACC bar

   Every position below is a named constant so the layout can be nudged
   in one place instead of hunting magic numbers through the draw calls.
   ============================================================ */

#define MARGIN_L      2
#define MARGIN_R      125          /* right-align target, 2px from the edge */

#define HDR_H         11
#define CONTENT_Y     12

#define ADI_CX        29
#define ADI_CY        38
#define ADI_R         21
#define ADI_FACE      (ADI_R - 1)  /* ground fill stays inside the bezel */

#define DIV_X         57

#define COL_X         62
#define ROLL_LBL_Y    13
#define ROLL_VAL_Y    22           /* scale 2 -> 14px tall, ends y35 */
#define SEP_Y         38
#define PITCH_Y       41
#define BAR_Y         50
#define BAR_X         82
#define BAR_W         44
#define BAR_H         7

#define PITCH_PX_X10  6            /* 0.6 screen px per degree of pitch */

/* 8x8 status icons, one byte per row, bit 7 leftmost. */
static const uint8_t ico_level[8] = {
    0x00, 0x02, 0x06, 0x0C, 0x98, 0xD8, 0x70, 0x20   /* check mark */
};
static const uint8_t ico_motion[8] = {
    0x00, 0x24, 0x42, 0x99, 0x99, 0x42, 0x24, 0x00   /* radiating waves */
};

/* Roll scale ticks around the top of the bezel, precomputed for ADI_R=21 at
   -60/-30/0/+30/+60 degrees: {outer x, outer y, inner x, inner y}. */
static const int8_t roll_ticks[5][4] = {
    { 11, 27, 13, 29 },
    { 18, 20, 20, 22 },
    { 29, 17, 29, 21 },   /* 0 deg reads as the long one */
    { 40, 20, 38, 22 },
    { 47, 27, 45, 29 }
};

/* ------------------------------------------------------------------
   Small helpers
   ------------------------------------------------------------------ */

static uint32_t isqrt32(uint32_t n)
{
    uint32_t op = n, res = 0, one = 1uL << 30;

    while (one > op) one >>= 2;
    while (one != 0) {
        if (op >= res + one) { op -= res + one; res = res + 2 * one; }
        res >>= 1;
        one >>= 2;
    }
    return res;
}

static uint8_t u32str(uint32_t v, char *out)
{
    char rev[12];
    uint8_t n = 0, i = 0;

    if (v == 0) { out[0] = '0'; out[1] = '\0'; return 1; }
    while (v) { rev[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n) out[i++] = rev[--n];
    out[i] = '\0';
    return i;
}

/* "-12" + degree glyph */
static void deg_str(int32_t deg, char *out)
{
    uint8_t i = 0;

    if (deg < 0) { out[i++] = '-'; deg = -deg; }
    i = (uint8_t)(i + u32str((uint32_t)deg, &out[i]));
    out[i++] = 0x7B;
    out[i]   = '\0';
}

/* tenths -> "24.5" */
static void fixed1_str(int32_t v_x10, char *out)
{
    uint8_t i = 0;

    if (v_x10 < 0) { out[i++] = '-'; v_x10 = -v_x10; }
    i = (uint8_t)(i + u32str((uint32_t)(v_x10 / 10), &out[i]));
    out[i++] = '.';
    out[i++] = (char)('0' + (v_x10 % 10));
    out[i]   = '\0';
}

/* ------------------------------------------------------------------
   Widgets
   ------------------------------------------------------------------ */

/* Segmented gauge. Empty segments keep a 1px base line so the track stays
   readable at a glance - a bar that vanishes when the value is 0 just looks
   like a rendering bug. */
static void draw_bar(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t pct)
{
    const int16_t seg = 3, gap = 1;
    int16_t count = (w + gap) / (seg + gap);
    int16_t filled, i;

    if (pct > 100) pct = 100;
    filled = (int16_t)(((int32_t)pct * count + 50) / 100);

    for (i = 0; i < count; i++) {
        int16_t sx = (int16_t)(x + i * (seg + gap));
        if (i < filled) SSD1306_FillRect(sx, y, seg, h, SSD1306_WHITE);
        else            SSD1306_DrawHLine(sx, (int16_t)(y + h - 1), seg, SSD1306_WHITE);
    }
}

/* Bresenham clipped to the instrument face, so the horizon and its pitch
   ladder stop at the bezel instead of running across the whole screen. */
static void line_in_face(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    int16_t dx = (int16_t)(x1 - x0), sx = 1;
    int16_t dy = (int16_t)(y1 - y0), sy = 1;
    int16_t err, e2, ddx, ddy;

    if (dx < 0) { dx = (int16_t)-dx; sx = -1; }
    if (dy < 0) { dy = (int16_t)-dy; sy = -1; }
    dy  = (int16_t)-dy;
    err = (int16_t)(dx + dy);

    for (;;) {
        ddx = (int16_t)(x0 - ADI_CX);
        ddy = (int16_t)(y0 - ADI_CY);
        if (ddx * ddx + ddy * ddy <= ADI_FACE * ADI_FACE)
            SSD1306_DrawPixel(x0, y0, SSD1306_WHITE);

        if (x0 == x1 && y0 == y1) break;
        e2 = (int16_t)(2 * err);
        if (e2 >= dy) { err = (int16_t)(err + dy); x0 = (int16_t)(x0 + sx); }
        if (e2 <= dx) { err = (int16_t)(err + dx); y0 = (int16_t)(y0 + sy); }
    }
}

static void draw_adi(int32_t roll_x10, int32_t pitch_x10)
{
    float   rollRad = (float)(UI_ROLL_SIGN * roll_x10) * (3.14159265f / 1800.0f);
    float   sf = sinf(rollRad), cf = cosf(rollRad);
    int32_t si = (int32_t)(sf * 256.0f);
    int32_t ci = (int32_t)(cf * 256.0f);
    int32_t pitchPx;
    int16_t h0y, x, y, dy, w, i;
    int16_t ux, uy, nx, ny, px, py;

    /* Bezel */
    SSD1306_DrawCircle(ADI_CX, ADI_CY, ADI_R, SSD1306_WHITE);

    /* Roll scale + live roll pointer */
    for (i = 0; i < 5; i++)
        SSD1306_DrawLine(roll_ticks[i][0], roll_ticks[i][1],
                         roll_ticks[i][2], roll_ticks[i][3], SSD1306_WHITE);

    /* Pointer sits on the bezel at the current roll angle. Screen x is
       +sin, screen y is -cos, measured from 12 o'clock. */
    px = (int16_t)(ADI_CX + (int16_t)(sf * (ADI_R - 4)));
    py = (int16_t)(ADI_CY - (int16_t)(cf * (ADI_R - 4)));
    SSD1306_DrawLine((int16_t)(px - 2), (int16_t)(py - 2), (int16_t)(px + 2), (int16_t)(py - 2), SSD1306_WHITE);
    SSD1306_DrawLine((int16_t)(px - 2), (int16_t)(py - 2), px, py, SSD1306_WHITE);
    SSD1306_DrawLine((int16_t)(px + 2), (int16_t)(py - 2), px, py, SSD1306_WHITE);

    /* Horizon reference point: pitch slides it up/down the face. */
    pitchPx = (pitch_x10 * PITCH_PX_X10) / 100;
    if (pitchPx >  (ADI_R - 4)) pitchPx =  (ADI_R - 4);
    if (pitchPx < -(ADI_R - 4)) pitchPx = -(ADI_R - 4);
    h0y = (int16_t)(ADI_CY + pitchPx);

    /* Ground: 50% checkerboard rather than a solid block. A filled white
       half-disc on an OLED glares and swallows the aircraft symbol; the
       dither reads as "shaded" and keeps the overlay legible. */
    for (y = (int16_t)(ADI_CY - ADI_FACE); y <= (int16_t)(ADI_CY + ADI_FACE); y++) {
        dy = (int16_t)(y - ADI_CY);
        w  = (int16_t)isqrt32((uint32_t)(ADI_FACE * ADI_FACE - dy * dy));
        for (x = (int16_t)(ADI_CX - w); x <= (int16_t)(ADI_CX + w); x++) {
            int32_t d;
            if (((x + y) & 1) == 0) continue;
            d = -(int32_t)(x - ADI_CX) * si + (int32_t)(y - h0y) * ci;
            if (d > 0) SSD1306_DrawPixel(x, y, SSD1306_WHITE);
        }
    }

    /* Horizon line, and a pitch ladder rung 10 degrees either side of it.
       u runs along the horizon, n is the unit normal pointing groundward. */
    ux = (int16_t)(cf * (ADI_R + 4));
    uy = (int16_t)(sf * (ADI_R + 4));
    line_in_face((int16_t)(ADI_CX - ux), (int16_t)(h0y - uy),
                 (int16_t)(ADI_CX + ux), (int16_t)(h0y + uy));

    nx = (int16_t)(-sf * 6.0f);
    ny = (int16_t)( cf * 6.0f);
    for (i = -1; i <= 1; i += 2) {
        int16_t mx = (int16_t)(ADI_CX + i * nx);
        int16_t my = (int16_t)(h0y   + i * ny);
        int16_t rx = (int16_t)(cf * 4.0f);
        int16_t ry = (int16_t)(sf * 4.0f);
        line_in_face((int16_t)(mx - rx), (int16_t)(my - ry),
                     (int16_t)(mx + rx), (int16_t)(my + ry));
    }

    /* Fixed aircraft symbol. Punched out in black first so it stays visible
       wherever the dithered ground happens to sit behind it. */
    SSD1306_FillRect((int16_t)(ADI_CX - 11), (int16_t)(ADI_CY - 2), 8, 5, SSD1306_BLACK);
    SSD1306_FillRect((int16_t)(ADI_CX + 4),  (int16_t)(ADI_CY - 2), 8, 5, SSD1306_BLACK);
    SSD1306_FillRect((int16_t)(ADI_CX - 2),  (int16_t)(ADI_CY - 2), 5, 5, SSD1306_BLACK);
    SSD1306_DrawHLine((int16_t)(ADI_CX - 10), ADI_CY, 6, SSD1306_WHITE);
    SSD1306_DrawHLine((int16_t)(ADI_CX + 5),  ADI_CY, 6, SSD1306_WHITE);
    SSD1306_FillRect((int16_t)(ADI_CX - 1), (int16_t)(ADI_CY - 1), 3, 3, SSD1306_WHITE);
}

static void draw_header(const MPU6050_Data *d, uint8_t level_ok, uint8_t moving)
{
    char buf[10];

    SSD1306_FillRect(0, 0, SSD1306_WIDTH, HDR_H, SSD1306_WHITE);
    SSD1306_DrawString(MARGIN_L + 1, 2, "MPU6050", SSD1306_BLACK, 1);

    fixed1_str(d->temp_cx10, buf);
    SSD1306_DrawStringRight(104, 2, buf, SSD1306_BLACK, 1);

    if (level_ok) SSD1306_DrawIcon8(107, 2, ico_level,  SSD1306_BLACK);
    if (moving)   SSD1306_DrawIcon8(117, 2, ico_motion, SSD1306_BLACK);
}

/* ------------------------------------------------------------------
   Public
   ------------------------------------------------------------------ */

void UI_DrawDashboard(const MPU6050_Data *d)
{
    char     buf[10];
    int32_t  ax = d->accel_mg[0], ay = d->accel_mg[1], az = d->accel_mg[2];
    int32_t  gx = d->gyro_dpsx10[0], gy = d->gyro_dpsx10[1], gz = d->gyro_dpsx10[2];
    uint32_t amag, gmag, dyn;
    uint8_t  level_ok, moving, pct;

    amag = isqrt32((uint32_t)(ax * ax + ay * ay + az * az));
    gmag = isqrt32((uint32_t)(gx * gx + gy * gy + gz * gz));

    /* Deviation from 1g, so the gauge rests at empty and responds to real
       movement instead of permanently sitting at half scale. */
    dyn = (amag > 1000u) ? (amag - 1000u) : (1000u - amag);
    pct = (uint8_t)((dyn > 1000u) ? 100u : (dyn / 10u));

    level_ok = (uint8_t)((d->roll_x10  > -20 && d->roll_x10  < 20) &&
                         (d->pitch_x10 > -20 && d->pitch_x10 < 20));
    moving   = (uint8_t)(gmag > 150u);   /* > 15 deg/s on any axis */

    SSD1306_Clear();

    draw_header(d, level_ok, moving);
    draw_adi(d->roll_x10, d->pitch_x10);

    /* Zone separators */
    SSD1306_DrawDashVLine(DIV_X, CONTENT_Y + 1, 50, 2, 2, SSD1306_WHITE);
    SSD1306_DrawDashHLine(COL_X, SEP_Y, (int16_t)(MARGIN_R - COL_X + 1), 2, 2, SSD1306_WHITE);

    /* Primary readout: roll, the only value at scale 2 */
    SSD1306_DrawString(COL_X, ROLL_LBL_Y, "ROLL", SSD1306_WHITE, 1);
    deg_str(d->roll_x10 / 10, buf);
    SSD1306_DrawStringRight(MARGIN_R, ROLL_VAL_Y, buf, SSD1306_WHITE, 2);

    /* Secondary readout: pitch, label and value on one line */
    SSD1306_DrawString(COL_X, PITCH_Y, "PITCH", SSD1306_WHITE, 1);
    deg_str(d->pitch_x10 / 10, buf);
    SSD1306_DrawStringRight(MARGIN_R, PITCH_Y, buf, SSD1306_WHITE, 1);

    /* Tertiary: acceleration as a gauge, no raw number */
    SSD1306_DrawString(COL_X, BAR_Y, "ACC", SSD1306_WHITE, 1);
    draw_bar(BAR_X, BAR_Y, BAR_W, BAR_H, pct);
}

void UI_DrawSplash(void)
{
    SSD1306_Clear();

    SSD1306_DrawRect(6, 12, 116, 40, SSD1306_WHITE);
    SSD1306_DrawString(20, 20, "MPU6050", SSD1306_WHITE, 2);
    SSD1306_DrawString(20, 40, "ATTITUDE MONITOR", SSD1306_WHITE, 1);

    SSD1306_UpdateScreen();
}
