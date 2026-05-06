# CoreInk Sensor Template

A reusable starting point for low-power sensor projects on the
**M5Stack CoreInk** (200x200 e-ink, ESP32, PCF8563 RTC, no AXP IC).
Drop this into a new PlatformIO project and fill in the four TODO
zones to get a working, deep-sleeping sensor display with battery
percentage, RTC clock, stale-data fallback, and Orbitron font
rendering.

## Files

| File | Purpose |
|---|---|
| `main.cpp` | The template itself. Filled-in skeleton with TODO markers. |
| `platformio.ini` | Build config for `m5stack-coreink`. |
| `Orbitron_Bold_70.h` | Big primary readout font. |
| `Orbitron_Bold_32.h` | Medium font (good for units next to the primary). |
| `Orbitron_Medium_20.h` | Small font for title bar, footer, secondary values. |

## CoreInk hardware notes (important — these shape the firmware)

- **No AXP IC.** Runtime USB detection is not reliable. USB presence
  is only knowable at boot via voltage. Don't try to branch firmware
  behavior on "am I on USB?" at runtime.
- **`M5.shutdown()` enters deep sleep even when on USB**, but on USB
  it's effectively a no-op — the device stays awake. The template
  handles this by always calling `attemptShutdown()` at the end of
  `setup()` and leaving `loop()` empty. On battery it sleeps; on USB
  it sits idle until unplugged.
- **The side reset button is the master "recover" mechanism.** It is
  the only guaranteed way out of a stuck state.
- **Wake source is the on-board PCF8563 RTC alarm.** `scheduleWakeup()`
  sets it before shutdown.
- **Display is 200x200, 1-bit.** Drawing happens into a single
  `Ink_Sprite` and is pushed in one go via `canvas.pushSprite()` to
  avoid partial-refresh ghosting.
- **Persistent state across deep sleep** uses `RTC_DATA_ATTR`. This
  survives deep sleep but is lost on hard reset / power cycle.

## Where to plug things in

Search `main.cpp` for these tags:

| Tag | What goes there |
|---|---|
| `TODO: SENSOR` | Pin defines, peripheral setup, and the `readSensor()` driver function. |
| `TODO: DATA` | Your `RTC_DATA_ATTR` last-known cache variables and the locals passed into `takeMeasurement()` / `renderScreen()`. |
| `TODO: LAYOUT` | Title text and the body of `renderScreen()`. The function has zone comments showing the y-coordinate budget. |
| `TODO: ALARM` | Threshold constants and `alarmBeep()` calls inside `takeMeasurement()`. |

## Pinout cheat sheet (CoreInk)

| Function | Pin |
|---|---|
| Battery sense (ADC1) | GPIO 35 |
| Buzzer | GPIO 2 |
| External LED (top) | GPIO 10 |
| Grove port | GPIO 32 (RX), GPIO 33 (TX) — also usable as I2C |
| HY2.0-4P side port | GPIO 25 (SDA), GPIO 26 (SCL) |

## Screen layout zones

`renderScreen()` is divided into named zones with hRules between them.
You can use any subset:

```
0 ─────────────────────────── 200
│   TITLE BAR     [glyph]    │  y =  0 ..  24   (Orbitron_Medium_20)
│   ─── hRule ───            │  y = 25
│                            │
│     PRIMARY READOUT        │  y = 26 .. 124   (Orbitron_Bold_70 + Bold_32 unit)
│                            │
│   ─── hRule ───            │  y = 125
│   secondary L  secondary R │  y = 125 .. 149  (Orbitron_Medium_20)
│   ─── hRule ───            │  y = 150
│   [optional bar/extra]     │  y = 151 .. 174
│   ─── hRule ───            │  y = 175
│   HH:MM          NN%       │  y = 176 .. 199  (Orbitron_Medium_20, footer auto-drawn)
```

## Built-in helpers you don't need to rewrite

- `drawGFXString(str, font, y[, centred=true, x=0, rightEdge=-1])`
- `hRule(y, thickness=1)`, `drawRect`, `fillRect`
- `drawWarningIcon(cx, y, size)` — triangle + exclamation
- `drawStaleMarker(x, y)` — small X for stale data
- `drawSleepIndicator(x, y)` / `drawMoonIndicator(x, y)` — corner glyphs
- `getBatteryVoltage()` / `batteryPercent()`
- `alarmBeep()` — three short buzzer pulses
- `scheduleWakeup(minutesFromNow)` / `attemptShutdown()`

## Stale-data behavior (already wired up)

When `readSensor()` fails, `takeMeasurement()` should fall back to the
cached `last*` values and increment `consecutiveReadFailures`. After
`STALE_DATA_FAIL_THRESHOLD` (default 3) consecutive failures,
`renderScreen()` automatically draws a small X marker in the top-left
of the title bar so the user knows the displayed numbers are stale.
A successful read resets the counter to 0.

## How to bootstrap a new project from this template

1. Copy `main.cpp`, `platformio.ini`, and the three Orbitron `.h`
   files into a new PlatformIO project (`src/` and project root).
2. Fill in `TODO: SENSOR` — pin defines and `readSensor()`.
3. Fill in `TODO: DATA` — `RTC_DATA_ATTR last*` variables and the
   parameter list of `takeMeasurement()` and `renderScreen()`.
4. Fill in `TODO: LAYOUT` — title and body of `renderScreen()`.
5. (Optional) Fill in `TODO: ALARM` — thresholds and `alarmBeep()`.
6. Adjust `SLEEP_MIN` if 30 minutes isn't right for your use case.
