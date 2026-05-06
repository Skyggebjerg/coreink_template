# CoreInk Sensor Template

A reusable starting point for low-power sensor projects on the
**M5Stack CoreInk** (200x200 e-ink, ESP32, PCF8563 RTC, no AXP IC).
Drop this into a new PlatformIO project and fill in the four TODO
zones to get a working, deep-sleeping sensor display with battery
percentage, RTC clock, automatic USB-vs-battery detection, and
Orbitron font rendering.

## Files

| File | Purpose |
|---|---|
| `main.cpp` | The template itself. Filled-in skeleton with TODO markers. |
| `platformio.ini` | Build config for `m5stack-coreink`. |
| `Orbitron_Bold_70.h` | Big primary readout font. |
| `Orbitron_Bold_32.h` | Medium font (good for units next to the primary). |
| `Orbitron_Medium_20.h` | Small font for title bar, footer, secondary values. |

## CoreInk hardware notes (important — these shape the firmware)

- **No AXP IC.** USB cannot be detected at runtime. The template
  works around this by reading the battery sense pin **once at boot**
  and treating any voltage above `USB_VOLTAGE_THRESHOLD` (default
  4.26V) as USB power. A real Li-ion cell tops out around 4.2V, so
  a clearly-higher reading is the USB charge rail showing through.
  Calibrate this threshold against your own unit if needed.
- **Two different sleep paths**, picked automatically based on the
  USB detection above:
  - **Battery**: `M5.shutdown()` physically cuts the board's power
    rail via a latch circuit. The PCF8563 RTC re-engages the latch
    on alarm. True microamp sleep.
  - **USB**: `M5.shutdown()` would hang forever (the latch can't
    cut the USB-supplied rail), so we use plain ESP32 deep sleep
    with `esp_sleep_enable_timer_wakeup()` instead. Not as low
    power, but it works on USB and re-enters `setup()` cleanly.
- **Battery wakes are cold boots.** Because the latch physically
  cuts power, the wake from battery sleep is a full POWERON reset
  — RAM is wiped, and `RTC_DATA_ATTR` variables do **not** survive
  across cycles on this board. If you need state across cycles
  (counters, last-known cache, calibration data), use the
  `Preferences` library to write to flash. The template does not
  include any cross-cycle persistence.
- **The side reset button is the master "recover" mechanism.** It is
  the only guaranteed way out of a stuck state.
- **Display is 200x200, 1-bit.** Drawing happens into a single
  `Ink_Sprite` and is pushed in one go via `canvas.pushSprite()` to
  avoid partial-refresh ghosting.

## Where to plug things in

Search `main.cpp` for these tags:

| Tag | What goes there |
|---|---|
| `TODO: SENSOR` | Pin defines, peripheral setup, and the `readSensor()` driver function. |
| `TODO: DATA` | Locals for the current cycle's readings and the call into `takeMeasurement()` / `renderScreen()`. On a failed read, decide how to represent "no data" (NaN, zero, sentinel). |
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

## Built-in helpers you don't need to rewrite

- `drawGFXString(str, font, y[, centred=true, x=0, rightEdge=-1])`
- `hRule(y, thickness=1)`, `drawRect`, `fillRect`
- `drawWarningIcon(cx, y, size)` — triangle + exclamation
- `drawSleepIndicator(x, y)` / `drawMoonIndicator(x, y)` — corner glyphs
- `getBatteryVoltage()` / `batteryPercent()` (also used for boot-time USB detection)
- `alarmBeep()` — three short buzzer pulses
- `scheduleWakeup(minutesFromNow)` — sets the PCF8563 alarm
- `attemptShutdown(bool onUSB)` — branches between battery latch shutdown and ESP32 deep sleep

## Failed sensor reads

Each measurement cycle is independent. If `readSensor()` fails, your
`takeMeasurement()` wrapper should fill the out-parameters with
something the renderer can recognize as "no data" — `NAN` is a good
default for floats since it's easy to test for with `isnan()` and won't
be confused with a real reading. The renderer can then show a dash
or a placeholder instead of a number for that field. There is no
cached "last known good" value because RAM does not survive sleep on
this board; if you want that behavior, add NVS-backed caching via
the `Preferences` library.

## How to bootstrap a new project from this template

1. Copy `main.cpp`, `platformio.ini`, and the three Orbitron `.h`
   files into a new PlatformIO project (`src/` and project root).
2. Fill in `TODO: SENSOR` — pin defines and `readSensor()`.
3. Fill in `TODO: DATA` — parameter list of `takeMeasurement()` and
   `renderScreen()`, plus your "no data" handling.
4. Fill in `TODO: LAYOUT` — title and body of `renderScreen()`.
5. (Optional) Fill in `TODO: ALARM` — thresholds and `alarmBeep()`.
6. Adjust `SLEEP_MIN` if 30 minutes isn't right for your use case.
7. (Optional) Calibrate `USB_VOLTAGE_THRESHOLD` for your specific
   unit by reading `getBatteryVoltage()` once on USB and once on a
   fully charged battery, and picking a value between them.