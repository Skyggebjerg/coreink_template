/* ============================================================
    M5Stack CoreInk — SENSOR TEMPLATE
    ============================================================
    A reusable starting point for low-power sensor projects on the
    M5Stack CoreInk (200x200 e-ink, ESP32, RTC, no AXP IC).

    What this template gives you out of the box:
      - Deep-sleep cycle driven by the on-board PCF8563 RTC alarm
      - Battery voltage measurement + percentage estimate
      - Orbitron font rendering (Bold 70 / Bold 32 / Medium 20)
      - Persistent state across deep sleep via RTC_DATA_ATTR
      - Stale-data marker, sleep glyphs, warning icon helpers
      - Buzzer + external LED pin definitions
      - Time + battery footer rendered automatically

    What you need to fill in for a new project:
      - Sensor wiring + driver code  (search: "TODO: SENSOR")
      - Measurement struct + last-known cache  (search: "TODO: DATA")
      - Title and main-area screen layout    (search: "TODO: LAYOUT")
      - Optional alarm thresholds and beeping (search: "TODO: ALARM")

    Hardware notes (CoreInk specifics — keep these in mind):
      - There is NO AXP power IC, so runtime USB detection is not
        reliable. USB presence is only known at boot via voltage.
      - M5.shutdown() enters deep sleep even when on USB. On USB it
        is effectively a no-op; loop() will run idle until unplugged
        and the side power button is pressed for a real sleep cycle.
      - The side reset button is the master "recover" mechanism if
        firmware gets stuck — it is the only guaranteed way out of
        a bad state.
      - Deep sleep is woken by the RTC alarm we schedule before
        shutdown.
      - Display is 200x200, 1-bit. Drawing happens into Ink_Sprite
        and is pushed in one go to avoid partial refresh ghosting.
   ============================================================ */

#include <M5CoreInk.h>
#include <esp_adc_cal.h>
#include <esp_sleep.h>
#include "Orbitron_Bold_70.h"
#include "Orbitron_Bold_32.h"
#include "Orbitron_Medium_20.h"

// =============================================================
//  TIMING
// =============================================================
const uint32_t SLEEP_MIN = 30; // Deep-sleep interval between measurements (minutes)

// =============================================================
//  PINS
// =============================================================
#define BAT_ADC_PIN 35  // CoreInk battery sense pin (do not change)
#define BUZZER_PIN  2   // CoreInk on-board buzzer  (do not change)
#ifndef LED_EXT_PIN
#define LED_EXT_PIN 10  // CoreInk top LED          (do not change)
#endif

// -- TODO: SENSOR --------------------------------------------------
// Add your sensor's pin definitions here. The CoreInk Grove port is
// on GPIO 32 (RX) and 33 (TX). The HY2.0-4P side port exposes I2C on
// GPIO 25 (SDA) and 26 (SCL). Pick whichever fits your sensor.
//
// Example for a UART sensor on the Grove port:
//   #define SENSOR_RX_PIN 32
//   #define SENSOR_TX_PIN 33
//   #define SENSOR_BAUD   9600
//   HardwareSerial sensorSerial(1);
//
// Example for an I2C sensor on the HY2.0-4P side port:
//   #define SENSOR_SDA 25
//   #define SENSOR_SCL 26
//   #define SENSOR_I2C_ADDR 0x76
// -----------------------------------------------------------------

// =============================================================
//  PERSISTENT STATE (survives deep sleep, lost on hard reset)
// =============================================================
RTC_DATA_ATTR uint32_t bootCount = 0;
RTC_DATA_ATTR uint32_t consecutiveReadFailures = 0;

// After this many consecutive failed sensor reads, the screen shows
// a stale-data marker (small X in the top-left of the title bar).
const uint32_t STALE_DATA_FAIL_THRESHOLD = 3;

// -- TODO: DATA ---------------------------------------------------
// Replace these with the readings your sensor produces. Anything you
// want to keep across deep-sleep (so you can show the last known
// value when a read fails) must be RTC_DATA_ATTR.
//
// Example:
//   RTC_DATA_ATTR float lastTemperature = 0.0f;
//   RTC_DATA_ATTR float lastHumidity    = 0.0f;
//   RTC_DATA_ATTR float lastPressure    = 1013.0f;
// -----------------------------------------------------------------

// =============================================================
//  DISPLAY OBJECTS
// =============================================================
Ink_Sprite  canvas(&M5.M5Ink);
LGFX_Sprite gfx;
enum SleepGlyph { GLYPH_NONE, GLYPH_ZZ, GLYPH_MOON };

// =============================================================
//  CORE DRAWING PRIMITIVES (keep these — they are reusable)
// =============================================================

// Draw a string using a GFX font. Centered by default. Pass
// centred=false with x for left-aligned, or rightEdge for right-
// aligned drawing. Renders into a 1-bit sprite then blits onto the
// 200x200 e-ink canvas.
void drawGFXString(const char* str, const lgfx::GFXfont* font, int y,
                   bool centred = true, int x = 0, int rightEdge = -1) {
    gfx.setFont(font);
    int32_t tw = gfx.textWidth(str);
    int32_t th = gfx.fontHeight();
    if (centred) x = (200 - tw) / 2;
    else if (rightEdge >= 0) x = rightEdge - tw;
    gfx.setColorDepth(1);
    gfx.createSprite(tw + 2, th + 2);
    gfx.fillSprite(1);
    gfx.setTextColor(0);
    gfx.setCursor(1, 1);
    gfx.print(str);
    for (int32_t py = 0; py < th + 2; py++) {
        for (int32_t px = 0; px < tw + 2; px++) {
            if (gfx.readPixel(px, py) == 0) {
                int cx = x + px; int cy = y + py;
                if (cx >= 0 && cx < 200 && cy >= 0 && cy < 200) canvas.drawPix(cx, cy, 0);
            }
        }
    }
    gfx.deleteSprite();
}

// Horizontal rule across the full 200px width.
void hRule(int y, int thickness = 1) {
    for (int t = 0; t < thickness; t++)
        for (int x = 0; x < 200; x++) canvas.drawPix(x, y + t, 0);
}

// Outline-only rectangle.
void drawRect(int x, int y, int w, int h) {
    for (int i = x; i < x + w; i++) { canvas.drawPix(i, y, 0); canvas.drawPix(i, y + h - 1, 0); }
    for (int j = y; j < y + h; j++) { canvas.drawPix(x, j, 0); canvas.drawPix(x + w - 1, j, 0); }
}

// Filled rectangle.
void fillRect(int x, int y, int w, int h) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) canvas.drawPix(i, j, 0);
}

// =============================================================
//  GLYPH HELPERS (status icons in the corners of the screen)
// =============================================================

// "Z" character used to build a sleep indicator.
void drawSleepZ(int x, int y, int size) {
    for (int i = 0; i < size; i++) canvas.drawPix(x + i, y, 0);
    for (int i = 0; i < size; i++) canvas.drawPix(x + i, y + size - 1, 0);
    for (int i = 0; i < size; i++) {
        int dx = size - 1 - i; int dy = i;
        if (dy < size - 1 && dy > 0) canvas.drawPix(x + dx, y + dy, 0);
    }
}

// Two stacked Z's. Use to indicate "going to sleep".
void drawSleepIndicator(int x, int y) {
    drawSleepZ(x, y + 4, 8);
    drawSleepZ(x + 9, y, 5);
}

// Crescent moon. Alternative sleep glyph.
void drawMoonIndicator(int x, int y) {
    const int r1 = 7; const int r2 = 6;
    const int cx1 = x + r1; const int cy1 = y + r1;
    const int cx2 = cx1 + 4; const int cy2 = cy1 - 1;
    for (int dy = -r1; dy <= r1; dy++) {
        for (int dx = -r1; dx <= r1; dx++) {
            int px = cx1 + dx; int py = cy1 + dy;
            if (dx*dx + dy*dy <= r1*r1) {
                int ddx = px - cx2; int ddy = py - cy2;
                if (ddx*ddx + ddy*ddy > r2*r2) canvas.drawPix(px, py, 0);
            }
        }
    }
}

// Small X mark — drawn when sensor reads have been failing for a
// while so the user knows the displayed values are stale.
void drawStaleMarker(int x, int y) {
    const int sz = 10;
    for (int i = 0; i < sz; i++) {
        canvas.drawPix(x + i,          y + i,     0);
        canvas.drawPix(x + i,          y + i + 1, 0);
        canvas.drawPix(x + sz - 1 - i, y + i,     0);
        canvas.drawPix(x + sz - 1 - i, y + i + 1, 0);
    }
}

// Triangle warning sign with exclamation mark. Useful for threshold
// alarms. cx = horizontal center, y = top, size = half-base width.
void drawWarningIcon(int cx, int y, int size) {
    int h = (int)(size * 1.732f);
    for (int row = 0; row <= h; row++) {
        float frac = (float)row / (float)h;
        int hw = (int)(frac * size);
        int lx = cx - hw, rx = cx + hw, ry = y + row;
        if (ry >= 0 && ry < 200) {
            if (lx >= 0 && lx < 200) canvas.drawPix(lx, ry, 0);
            if (rx >= 0 && rx < 200) canvas.drawPix(rx, ry, 0);
        }
    }
    int lx = cx - size, rx = cx + size, ry = y + h;
    for (int i = lx; i <= rx; i++)
        if (i >= 0 && i < 200 && ry >= 0 && ry < 200) canvas.drawPix(i, ry, 0);
    int mx = cx;
    for (int row = y + h/4; row < y + h - h/4 - 3; row++)
        if (mx >= 0 && mx < 200 && row >= 0 && row < 200) canvas.drawPix(mx, row, 0);
    canvas.drawPix(mx, y + h - h/4, 0);
}

// =============================================================
//  BATTERY
// =============================================================

// Returns battery voltage in volts. Uses ADC1 with calibrated
// characterization and the CoreInk's 25.1k / 5.1k divider.
float getBatteryVoltage() {
    static esp_adc_cal_characteristics_t adc_chars;
    static bool characterized = false;
    if (!characterized) {
        esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 3600, &adc_chars);
        characterized = true;
    }
    analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);
    uint32_t adcValue = 0;
    for (int i = 0; i < 8; i++) adcValue += analogRead(BAT_ADC_PIN);
    adcValue /= 8;
    return float(esp_adc_cal_raw_to_voltage(adcValue, &adc_chars)) * 25.1f / 5.1f / 1000.0f;
}

// 0–100% estimate using a simple linear 3.2V–4.2V mapping. Fine for
// a status footer; not a fuel gauge.
int batteryPercent() {
    float v = getBatteryVoltage();
    return constrain((int)(((v - 3.2f) / (4.2f - 3.2f)) * 100.0f), 0, 100);
}

// =============================================================
//  ALARM / FEEDBACK
// =============================================================

// Three short beeps via the on-board buzzer.
void alarmBeep() {
    for (int i = 0; i < 3; i++) {
        digitalWrite(BUZZER_PIN, HIGH); delay(150);
        digitalWrite(BUZZER_PIN, LOW);  delay(100);
    }
}

// =============================================================
//  SENSOR I/O
// =============================================================

// -- TODO: SENSOR --------------------------------------------------
// Implement your sensor read here. Return true on success and fill
// the out-parameters; return false if the read failed (the caller
// will substitute the last cached values and increment the
// failure counter).
//
// Keep the signature shape consistent with takeMeasurement() below.
//
// Example (UART sensor):
//   bool readSensor(float& temperature, float& humidity) {
//       sensorSerial.begin(SENSOR_BAUD, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);
//       delay(100);
//       // ... send command, parse response ...
//       sensorSerial.end();
//       return true;
//   }
//
// Example (I2C sensor):
//   #include <Wire.h>
//   bool readSensor(float& temperature, float& humidity) {
//       Wire.begin(SENSOR_SDA, SENSOR_SCL);
//       // ... transactions ...
//       return true;
//   }
// -----------------------------------------------------------------

// =============================================================
//  RENDER
// =============================================================

// -- TODO: LAYOUT -------------------------------------------------
// Replace the body of this function with your own screen layout.
// The 200x200 area broken into rough zones used by this template:
//
//      0 ─────────────────────────── 200
//      │   TITLE BAR     [glyph]    │  y =  0 .. 24    (Medium 20)
//      │   ─── hRule ───             │  y = 25
//      │                             │
//      │     PRIMARY READOUT         │  y = 26 .. 124   (Bold 70 + Bold 32 unit)
//      │                             │
//      │   ─── hRule ───             │  y = 125
//      │   secondary L  secondary R  │  y = 125 .. 149  (Medium 20)
//      │   ─── hRule ───             │  y = 150
//      │   [optional bar/extra]      │  y = 151 .. 174
//      │   ─── hRule ───             │  y = 175
//      │   HH:MM          NN%        │  y = 176 .. 199  (Medium 20)
//
// Helpful primitives already available:
//   drawGFXString(text, &Orbitron_Bold_70,   y);                       // centered
//   drawGFXString(text, &Orbitron_Medium_20, y, false, 4);             // left-aligned, x=4
//   drawGFXString(text, &Orbitron_Medium_20, y, false, 0, 196);        // right-aligned to x=196
//   hRule(y, thickness);
//   drawRect / fillRect for boxes and bars
//   drawWarningIcon(cx, y, size) when a threshold is exceeded
//   drawStaleMarker(4, 6) when consecutiveReadFailures >= STALE_DATA_FAIL_THRESHOLD
// -----------------------------------------------------------------
void renderScreen(SleepGlyph glyph = GLYPH_NONE /* TODO: add your measurement params */) {
    canvas.clear();

    // ----- Title bar -----
    drawGFXString("CORE INK", &Orbitron_Medium_20, 1);   // TODO: LAYOUT — your title
    hRule(25, 2);

    // Stale-data marker (top-left of title bar) when reads have been
    // failing — values shown are last-known cache.
    if (consecutiveReadFailures >= STALE_DATA_FAIL_THRESHOLD)
        drawStaleMarker(4, 6);

    // ----- Primary readout area (y=26..124) -----
    // TODO: LAYOUT — example big numeric:
    //   char buf[10]; sprintf(buf, "%.1f", value);
    //   drawGFXString(buf, &Orbitron_Bold_70, 20);
    //   drawGFXString("UNIT", &Orbitron_Bold_32, 90);

    hRule(125, 1);

    // ----- Secondary readout area (y=125..149) -----
    // TODO: LAYOUT — left/right aligned smaller values:
    //   char lBuf[12], rBuf[12];
    //   sprintf(lBuf, "%.1fA", a); sprintf(rBuf, "%.1fB", b);
    //   drawGFXString(lBuf, &Orbitron_Medium_20, 125, false, 4);
    //   drawGFXString(rBuf, &Orbitron_Medium_20, 125, false, 0, 196);

    hRule(150, 1);

    // ----- Optional extra zone (y=151..174) -----
    // TODO: LAYOUT — bar graph, status, second row of values, etc.

    hRule(175, 1);

    // ----- Footer: clock + battery (always-on) -----
    RTC_TimeTypeDef rt; M5.rtc.GetTime(&rt);
    char timeBuf[20]; sprintf(timeBuf, "%02d:%02d", rt.Hours, rt.Minutes);
    drawGFXString(timeBuf, &Orbitron_Medium_20, 173, false, 4);

    int bPct = batteryPercent();
    char bBuf[6]; sprintf(bBuf, "%d%%", bPct);
    drawGFXString(bBuf, &Orbitron_Medium_20, 173, false, 0, 196);

    // Sleep glyph in top-right corner.
    if      (glyph == GLYPH_ZZ)   drawSleepIndicator(178, 6);
    else if (glyph == GLYPH_MOON) drawMoonIndicator(180, 4);

    canvas.pushSprite();
}

// =============================================================
//  MEASUREMENT WRAPPER
// =============================================================

// -- TODO: DATA + ALARM -------------------------------------------
// Wrap your readSensor() call so the caller gets either a fresh
// reading or the last known cached values, and so that consecutive
// failures get tracked (this drives the stale-data marker).
//
// Example shape:
//
//   void takeMeasurement(float& temperature, float& humidity) {
//       if (!readSensor(temperature, humidity)) {
//           temperature = lastTemperature;
//           humidity    = lastHumidity;
//           consecutiveReadFailures++;
//       } else {
//           lastTemperature = temperature;
//           lastHumidity    = humidity;
//           consecutiveReadFailures = 0;
//       }
//       // TODO: ALARM — beep on threshold breach, e.g.:
//       //   if (temperature > TEMP_ALARM_C) alarmBeep();
//   }
// -----------------------------------------------------------------

// =============================================================
//  POWER / SLEEP
// =============================================================

// Schedule the PCF8563 RTC to wake us up after `minutesFromNow`
// minutes. Wraps across midnight.
void scheduleWakeup(uint32_t minutesFromNow) {
    RTC_TimeTypeDef rt; M5.rtc.GetTime(&rt);
    int totalMin = rt.Hours * 60 + rt.Minutes + (int)minutesFromNow;
    totalMin %= (24 * 60);
    RTC_TimeTypeDef alarm;
    alarm.Hours   = totalMin / 60;
    alarm.Minutes = totalMin % 60;
    alarm.Seconds = 0;
    M5.rtc.SetAlarmIRQ(alarm);
}

// On battery: cuts power, RTC wakes us at the alarm.
// On USB:    no-op; we'll sit idle in loop() until unplugged and the
//            side power button is pressed for a real sleep cycle.
void attemptShutdown() {
    scheduleWakeup(SLEEP_MIN);
    delay(200);
    M5.shutdown();
}

// =============================================================
//  ARDUINO ENTRY POINTS
// =============================================================
void setup() {
    delay(500); // STABILIZATION DELAY: critical for USB disconnect reliability
    M5.begin();
    bootCount++;

    // External LED off-by-default. Toggle if you want a brief blink
    // to confirm a wake-cycle started.
    pinMode(LED_EXT_PIN, OUTPUT);
    digitalWrite(LED_EXT_PIN, HIGH);

    M5.M5Ink.isInit();
    canvas.creatSprite(0, 0, 200, 200, true);

    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    // -- TODO: DATA ---------------------------------------------
    // Declare locals to hold this cycle's readings, call your
    // takeMeasurement(), then pass them into renderScreen().
    //
    //   float temperature, humidity;
    //   takeMeasurement(temperature, humidity);
    //   renderScreen(GLYPH_ZZ, temperature, humidity);
    // -----------------------------------------------------------
    renderScreen(GLYPH_ZZ);
    delay(500);

    // CoreInk has no AXP IC and no runtime USB detection, so battery-
    // percent tricks for "am I on USB?" are unreliable. We always
    // attempt deep sleep:
    //   - On battery: M5.shutdown() cuts power and the RTC alarm
    //                 wakes us.
    //   - On USB:     M5.shutdown() is a no-op; loop() stays empty
    //                 and the device sits idle until unplugged and
    //                 the side power button is pressed for a proper
    //                 sleep cycle.
    attemptShutdown();
}

void loop() {}