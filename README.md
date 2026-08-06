# MPU-6050 Attitude Monitor — STM32L031K6

A bare-metal attitude display for the **NUCLEO-L031K6**: an MPU-6050 accelerometer/gyro
rendered on a 128×64 SSD1306 OLED as an aircraft-style artificial horizon, with a
self-diagnosing boot sequence that pinpoints wiring faults instead of just saying
"not found".

No HAL, no CubeMX — direct register access throughout.

```
┌──────────────────────────────────────────┐
│ MPU6050            24.5      [✓] [≈]     │  status bar
├──────────────────────────────────────────┤
│        ▲                ┊                │
│    ╱───┼───╲            ┊ ROLL           │
│   │  ▒▒▒▒▒  │           ┊                │
│   │▪▪──●──▪▪│           ┊  ─12°          │
│    ╲▒▒▒▒▒▒╱             ┊ ─ ─ ─ ─ ─ ─ ─  │
│        ▒                ┊ PITCH     +4°  │
│                         ┊ ACC ▐▐▐▐▐▁▁▁▁  │
└──────────────────────────────────────────┘
```

---

## Hardware

| Item | Detail |
|---|---|
| MCU | STM32L031K6T6, Cortex-M0+, 32 KB flash / 8 KB SRAM |
| Board | NUCLEO-L031K6 (Nucleo-32, MB1180) |
| Display | SSD1306 128×64 OLED, I²C, address `0x3C` |
| Sensor | MPU-6050 on a GY-521 breakout, address `0x68` |
| Clock | MSI ≈ 2.097 MHz (reset default — `SystemInit()` does not touch the PLL) |

### Wiring

Two **independent** I²C buses. This is deliberate: the sensor gets its own pins and its
own bit-banged controller so a fault on one bus can never be mistaken for a fault on the
other.

**OLED — hardware I²C1**

| OLED | STM32 | Nucleo pin |
|---|---|---|
| VCC | — | `3V3` |
| GND | — | `GND` |
| SCL | PB6 | `D5` |
| SDA | PB7 | `D4` |

**MPU-6050 — bit-banged software I²C**

| GY-521 | STM32 | Nucleo pin |
|---|---|---|
| VCC | — | `3V3` |
| GND | — | `GND` |
| SCL | PA9 | `D1` |
| SDA | PA10 | `D0` |
| AD0 | — | `GND` (→ address `0x68`; leave high for `0x69`) |
| XDA / XCL / INT | — | unconnected |

`D0` and `D1` are the first two pins of CN3, nearest the USB jack. The ST-LINK virtual COM
port lives on PA2/PA15 on this board, so PA9/PA10 are genuinely free — no solder-bridge
conflict.

> **All grounds must be one net.** If you power the sensor from a bench supply, that
> supply's ground has to run back to a Nucleo `GND` pin. I²C levels are referenced to a
> shared ground; two grounds that never meet is the same as no ground.

The on-board LED (PB3 / `D13`) toggles once per rendered frame in normal operation, and
blinks fast and evenly if the sensor was never detected.

---

## Building

Open `ssd1306_mpu6050.uvprojx` in Keil µVision 5 and build. Requires the
**Keil::STM32L0xx_DFP** device pack (tested with 2.3.0).

Build output (`Objects/`, `Listings/`, `DebugConfig/`) is not tracked and is regenerated
on first build. `RTE/` **is** tracked, since it carries the startup and system files the
project links against.

---

## Boot sequence

The firmware runs a diagnostic ladder before it ever tries to talk to the sensor. Each
screen holds for a few seconds. This exists because "MPU6050 NOT FOUND" on its own tells
you nothing — every one of these stages fails differently.

### 1. OLED self-test

All pixels on, then a splash. If you see neither, the problem is the OLED, its bus, or
power — nothing to do with the sensor.

### 2. `MPU PIN TEST`

A pure DC test of PA9/PA10. **No I²C protocol is involved**, so it still reports something
useful when the bus is completely silent.

```
MPU PIN TEST
PA9=SCL PA10=SDA

IDLE HI  C:Y D:Y
DRIVE LO C:Y D:Y
EXT PU   C:Y D:Y
PINS LOOK OK
```

| Row | What it does | `N` means |
|---|---|---|
| `IDLE HI` | Releases both lines with internal pull-ups | Line shorted low, or a wire in the wrong hole |
| `DRIVE LO` | MCU pulls each line down and reads the low back | GPIO config is wrong — MCU-side, passes with nothing connected |
| `EXT PU` | Pins become inputs with the **internal pull-down**; sees if anything outside still holds them high | Nothing powered on the end of that wire |

`EXT PU` is the decisive one. The GY-521 carries 10 kΩ pull-ups to its own 3V3 rail, which
beat the ~40 kΩ internal pull-down. A `Y` therefore proves, in one reading, that the wire is
intact *and* the module is powered.

Verdicts: `MCU PIN FAULT` → `LINE STUCK LOW` → `NOTHING ATTACHED` → `ONE WIRE DEAD` →
`PINS LOOK OK`, in order of how early they abort.

> Caveat: the internal pull-down is spec'd 25–55 kΩ, so at the strong end of that range the
> divider sits only just above the input threshold. A `Y` is trustworthy; a lone `N` on an
> otherwise healthy board is worth confirming by reseating before you conclude the wire is
> broken.

### 3. Bus scans

Sweeps `0x08`–`0x77` on each bus and lists every address that ACKs. Expect
`BUS1 (OLED): FOUND 0X3C` and `BUS2 (MPU): FOUND 0X68`.

The scan result **feeds the driver** — if your AD0 is strapped high the sensor appears at
`0x69` and is used at `0x69`, rather than the code stubbornly probing a hardcoded `0x68`.

### 4. Init, or a failure screen that says why

```
MPU INIT FAIL
ADDR 0X68
ACK  NO
WHO  0XFF
CHECK WIRING
```

| Screen | Meaning |
|---|---|
| `ACK NO`, `WHO 0XFF` | Nothing on the bus — wiring, power, or SCL/SDA swapped |
| `ACK YES`, `WHO 0Xxx` | Sensor is alive and talking; the ID byte is the story |

---

## The UI

Rendered into a 1 KB framebuffer and pushed as one frame.

**Status bar** (y 0–10, inverted): device name, die temperature, and two 8×8 state icons —
a check mark when level (±2° on both axes) and a radiating-waves glyph when the gyro
exceeds 15 °/s.

**Attitude indicator** (x 2–50): bezel circle with a roll tick scale at −60/−30/0/+30/+60
and a live pointer, a horizon line that rotates with roll and slides 0.6 px per degree of
pitch, a ±10° pitch ladder, and a fixed aircraft symbol.

Two deliberate rendering choices: the ground is a **50% checkerboard rather than a solid
fill**, because a white half-disc on an OLED glares and swallows the overlay; and the
aircraft symbol is punched out in black before being drawn white, so it stays legible
wherever the horizon happens to sit behind it.

**Readout column** (x 62–125): roll at 10×14 as the primary value, pitch at 5×7 as
secondary, and acceleration as a segmented gauge with no raw number. The gauge shows
*deviation from 1 g*, so it rests empty and responds to real movement instead of sitting
permanently at half scale. Empty segments keep a 1 px base line — a gauge that vanishes at
zero reads as a rendering bug.

Every coordinate is a named `#define` at the top of `ui.c`. Retune the layout there rather
than hunting magic numbers through the draw calls.

### Yaw

Not shown, and not available. An MPU-6050 has no magnetometer, and gyro-integrated yaw
drifts to nonsense within a minute with nothing to correct it. Roll and pitch are derived
from gravity via `atan2`, which is absolute and drift-free. The gyro drives the motion icon
instead.

### If the horizon tilts the wrong way

Which way roll reads depends on how the breakout is physically mounted. Flip
`UI_ROLL_SIGN` in `ui.h` from `+1` to `-1`.

---

## Source layout

| File | Role |
|---|---|
| `main.c` | Boot diagnostics, init ladder, render loop |
| `ui.c` / `.h` | Dashboard composition — all layout constants live here |
| `ssd1306.c` / `.h` | Framebuffer driver: primitives, scalable 5×7 text, 8×8 icons |
| `mpu6050.c` / `.h` | Sensor driver, runtime address, init diagnostics |
| `soft_i2c.c` / `.h` | Bit-banged I²C on PA9/PA10 + the DC pin test |
| `i2c.c` / `.h` | Hardware I²C1 on PB6/PB7 for the OLED |
| `delay.c` / `.h` | SysTick millisecond delay |

### Notes on the drivers

**`mpu6050.c` does not whitelist WHO_AM_I.** Genuine InvenSense parts answer `0x68`, but
the dies fitted to GY-521 clones report `0x70`, `0x71`, `0x72`, `0x73`, `0x75` and `0x98`
while remaining register-compatible. Rejecting those reported a perfectly working sensor as
missing. A NACK is the only reliable "nothing there" signal; `0x00`/`0xFF` just means the
bus read garbage.

**`delay_time` is `volatile`.** It is decremented in `SysTick_Handler` and polled in
`delay()`. Without `volatile` the compiler may hoist the load out of the loop and spin
forever once optimisation is enabled.

---

## Known constraints

**Frame rate is roughly 5–7 fps.** A full frame is 1032 bytes at 100 kHz ≈ 95 ms, and
SYSCLK is MSI at 2.1 MHz. Two independent one-line fixes if that bothers you:

- `i2c.c` — `I2C1->TIMINGR = 0x00100413` for 400 kHz
- switch SYSCLK to HSI16 (16 MHz), remembering the flash wait state and that
  `SysTick_Config(SystemCoreClock / 1000)` picks up the new value

Both are left alone by default rather than risk a working display for a cosmetic gain.

**RAM.** The framebuffer is 1024 bytes of the 8 KB SRAM, plus a 129-byte transfer buffer on
the stack during `SSD1306_UpdateScreen()`.

**Bit-banged bus speed.** At 2.1 MHz SYSCLK the soft I²C runs near 4 kHz. Slow, but I²C has
no minimum clock and the MPU-6050 has no bus timeout, so it is perfectly legal — the full
112-address scan still completes in well under a second.
