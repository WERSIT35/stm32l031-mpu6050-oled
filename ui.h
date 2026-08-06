#ifndef UI_H
#define UI_H

#include "mpu6050.h"

/* The accelerometer's roll sign depends on which way round the breakout is
   physically mounted, so the horizon can come out mirrored. If tilting the
   board right makes the horizon tilt the wrong way, flip this to -1. */
#define UI_ROLL_SIGN  (+1)

/* Renders the whole dashboard into the SSD1306 framebuffer.
   The caller pushes it to the panel with SSD1306_UpdateScreen(). */
void UI_DrawDashboard(const MPU6050_Data *d);

/* Boot splash, drawn and pushed immediately. */
void UI_DrawSplash(void);

#endif
