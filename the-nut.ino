#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include "config.h"
#include "state_machine.h"
#include "secrets.h"

enum class Gesture { NONE, TAP, DOUBLE_TAP, SWIPE_LEFT, SWIPE_RIGHT };

// ── Display ───────────────────────────────────────────────────────────────────
Arduino_DataBus *bus = new Arduino_ESP32SPI(PIN_DC, PIN_CS, PIN_SCLK, PIN_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_GC9A01(bus, PIN_RST, 0, true);

// ── Globals ───────────────────────────────────────────────────────────────────
GameState gGame;
AppState  gApp;

WiFiMulti wifiMulti;
WiFiUDP   ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVER, NTP_OFFSET_SEC, NTP_INTERVAL_MS);

// ── Touch ─────────────────────────────────────────────────────────────────────
#define CST816_ADDR 0x15

void touchInit() {
    pinMode(PIN_TP_RST, OUTPUT);
    digitalWrite(PIN_TP_RST, LOW); delay(50);
    digitalWrite(PIN_TP_RST, HIGH); delay(200);
    Wire.begin(PIN_TP_SDA, PIN_TP_SCL);
}

bool touchRead(int16_t &x, int16_t &y) {
    Wire.beginTransmission(CST816_ADDR);
    Wire.write(0x02);
    Wire.endTransmission(false);
    if (Wire.requestFrom(CST816_ADDR, 1) != 1) return false;
    if (Wire.read() == 0) return false;
    Wire.beginTransmission(CST816_ADDR);
    Wire.write(0x03);
    Wire.endTransmission(false);
    if (Wire.requestFrom(CST816_ADDR, 4) != 4) return false;
    uint8_t xh = Wire.read(), xl = Wire.read();
    uint8_t yh = Wire.read(), yl = Wire.read();
    x = ((xh & 0x0F) << 8) | xl;
    y = ((yh & 0x0F) << 8) | yl;
    return true;
}

// ── Gesture detection ─────────────────────────────────────────────────────────
static bool     _wasDown     = false;
static int16_t  _downX = 0, _downY = 0;
static int16_t  _curX  = 0, _curY  = 0;
static uint32_t _downTime    = 0;
static uint32_t _lastTapTime = 0;

Gesture gestureUpdate() {
    int16_t x, y;
    bool down = touchRead(x, y);
    if (down) {
        _curX = x; _curY = y;
        if (!_wasDown) {
            _wasDown = true;
            _downX = x; _downY = y;
            _downTime = millis();
        }
    } else if (_wasDown) {
        _wasDown = false;
        int16_t dx = _curX - _downX;
        if (abs(dx) > 40) return dx < 0 ? Gesture::SWIPE_LEFT : Gesture::SWIPE_RIGHT;
        uint32_t now = millis();
        if (now - _lastTapTime < 400) { _lastTapTime = 0; return Gesture::DOUBLE_TAP; }
        _lastTapTime = now;
        return Gesture::TAP;
    }
    return Gesture::NONE;
}

// ── Helpers ───────────────────────────────────────────────────────────────────
uint32_t levelDurationMs() {
    uint32_t mins = gGame.blindMinutes;
    if (gGame.speedMode) mins = (mins / 2 < 1) ? 1 : mins / 2;
    return mins * 60000UL;
}

int32_t levelSecsRemaining() {
    if (!gGame.running) return (int32_t)(levelDurationMs() / 1000);
    uint32_t elapsed;
    if (gGame.paused) {
        elapsed = gGame.pausedAt - gGame.levelStart;
    } else {
        elapsed = millis() - gGame.levelStart;
    }
    return (int32_t)(levelDurationMs() / 1000) - (int32_t)(elapsed / 1000);
}

void togglePause() {
    if (!gGame.running || gGame.overtime) return;
    if (gGame.paused) {
        // Resume — shift levelStart forward by how long we were paused
        gGame.levelStart += millis() - gGame.pausedAt;
        gGame.paused = false;
    } else {
        gGame.pausedAt = millis();
        gGame.paused = true;
    }
}

void toggleSpeedMode() {
    if (!gGame.running || gGame.overtime) {
        gGame.speedMode = !gGame.speedMode;
        return;
    }
    // Calculate elapsed time so far at current speed
    uint32_t now = gGame.paused ? gGame.pausedAt : millis();
    uint32_t elapsed = now - gGame.levelStart;

    gGame.speedMode = !gGame.speedMode;

    // Adjust levelStart so remaining time halves or doubles instantly
    // New elapsed = old elapsed * (newDuration / oldDuration)
    // Since duration halves on speed-on and doubles on speed-off, elapsed scales same way
    if (gGame.speedMode) {
        // Switched to speed — elapsed effectively doubles (we're further into the shorter level)
        uint32_t newElapsed = elapsed * 2;
        gGame.levelStart = now - newElapsed;
    } else {
        // Switched off speed — elapsed halves (we're less far into the longer level)
        uint32_t newElapsed = elapsed / 2;
        gGame.levelStart = now - newElapsed;
    }
    if (gGame.paused) gGame.pausedAt = now; // keep pausedAt consistent
}

// ── Drawing helpers ───────────────────────────────────────────────────────────
void clearScreen() { gfx->fillScreen(COL_BG); }

void drawCenteredText(const char* text, int y, int size, uint16_t color) {
    gfx->setTextSize(size);
    gfx->setTextColor(color);
    int16_t tx = DISPLAY_CX - strlen(text) * 3 * size;
    gfx->setCursor(tx, y);
    gfx->print(text);
}

// Fit text into w pixels, returns text size 1-4
uint8_t fitTextSize(const char* text, int w) {
    for (uint8_t s = 4; s >= 1; s--) {
        if ((int)strlen(text) * 6 * s <= w) return s;
    }
    return 1;
}

void drawButton(int x, int y, int w, int h, const char* label,
                uint16_t bg=COL_DARKGREY, uint16_t fg=COL_WHITE, int pad=2) {
    gfx->fillRoundRect(x+pad, y+pad, w-pad*2, h-pad*2, 6, bg);
    uint8_t ts = fitTextSize(label, w - pad*2 - 4);
    gfx->setTextSize(ts);
    gfx->setTextColor(fg);
    int16_t tx = x + w/2 - strlen(label)*3*ts;
    int16_t ty = y + h/2 - 4*ts;
    gfx->setCursor(tx, ty);
    gfx->print(label);
}

// Interpolate between two RGB565 colours
uint16_t lerpColor(uint16_t a, uint16_t b, float t) {
    uint8_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    uint8_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    uint8_t r = ar + (int)((br-ar)*t);
    uint8_t g = ag + (int)((bg-ag)*t);
    uint8_t bv= ab + (int)((bb-ab)*t);
    return (r<<11)|(g<<5)|bv;
}

// Smooth ring: fraction=1.0 full, 0.0 empty
// Colour: green → blue → red as fraction decreases
void drawRing(float fraction) {
    float endDeg = 360.0f * fraction;
    int cx = DISPLAY_CX, cy = DISPLAY_CY, r1 = 118, r2 = 110;

    // Colour based on fraction
    uint16_t col;
    if (fraction > 0.1f) {
        // green (1.0) → blue (0.1): t goes 0→1
        float t = 1.0f - ((fraction - 0.1f) / 0.9f);
        col = lerpColor(0x07E0, 0x001F, t); // green → blue
    } else {
        // blue (0.1) → red (0.0): t goes 0→1
        float t = 1.0f - (fraction / 0.1f);
        col = lerpColor(0x001F, 0xF800, t); // blue → red
    }

    for (int deg = 0; deg < (int)endDeg; deg++) {
        float rad = (deg - 90) * DEG_TO_RAD;
        float cs = cos(rad), sn = sin(rad);
        for (int r = r2; r <= r1; r++) {
            gfx->drawPixel(cx + (int)(r*cs), cy + (int)(r*sn), col);
        }
    }
}

void clearRing() {
    // Erase ring area by redrawing black
    int cx = DISPLAY_CX, cy = DISPLAY_CY, r1 = 118, r2 = 110;
    for (int deg = 0; deg < 360; deg++) {
        float rad = (deg - 90) * DEG_TO_RAD;
        float cs = cos(rad), sn = sin(rad);
        for (int r = r2; r <= r1; r++) {
            gfx->drawPixel(cx + (int)(r*cs), cy + (int)(r*sn), COL_BG);
        }
    }
}

// ── Scene draws ───────────────────────────────────────────────────────────────
void drawBlindTimer() {
    clearScreen();
    int32_t secs = levelSecsRemaining();

    if (gGame.overtime) {
        uint32_t elapsed = (millis() - gGame.levelStart) - levelDurationMs();
        uint32_t s = elapsed / 1000;
        char buf[12];
        snprintf(buf, sizeof(buf), "%02lu:%02lu", s/60, s%60);
        drawCenteredText("OverTime", 72, 2, COL_RED);
        drawCenteredText(buf, 100, 4, COL_RED);
        drawCenteredText("double-tap for menu", 170, 1, COL_DARKGREY);
        return;
    }

    if (secs < 0) secs = 0;

    // SB / BB above timer
    if (gGame.hasValues && gGame.currentRound < gGame.numRounds) {
        char sb[20];
        snprintf(sb, sizeof(sb), "$%lu / $%lu",
                 gGame.rounds[gGame.currentRound].smallBlind,
                 gGame.rounds[gGame.currentRound].bigBlind);
        drawCenteredText(sb, 62, 2, COL_GREY);
    }

    // Timer — big, centered
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", (int)(secs/60), (int)(secs%60));
    uint16_t timerCol = gGame.paused ? COL_ORANGE : COL_WHITE;
    drawCenteredText(buf, 96, 5, timerCol);

    // PAUSED indicator
    if (gGame.paused) {
        drawCenteredText("PAUSED", 148, 2, COL_ORANGE);
    }

    // Round n/max below timer (or below PAUSED)
    if (gGame.numRounds > 0) {
        char rnd[16];
        snprintf(rnd, sizeof(rnd), "Round %d/%d", gGame.currentRound+1, gGame.numRounds);
        drawCenteredText(rnd, gGame.paused ? 172 : 155, 2, COL_GREY);
    }

    // Smooth ring — only when not paused
    if (gGame.running && !gGame.paused && secs <= RING_WARN_SECS && secs > 0) {
        float frac = (float)secs / (float)RING_WARN_SECS;
        drawRing(frac);
    }
}
// Zones (from spec):
//   A: Wheel   x:40-116  y:40-200
//   B: Next    x:124-200 y:170-200
//   C: Value   x:124-200 y:70-170
//   D: Back    x:124-200 y:40-70
//   E: L-swipe x:0-32    y:70-170
//   F: R-swipe x:208-240 y:70-170

#define SW_WX1  40
#define SW_WX2  116
#define SW_VX1  124
#define SW_VX2  200
#define SW_Y1   40
#define SW_Y2   200
#define SW_BACK_Y2  70
#define SW_VAL_Y1   70
#define SW_VAL_Y2   170
#define SW_NEXT_Y1  170

int32_t blindStep(int32_t val) {
    if (val <   20) return 1;
    if (val <  100) return 5;
    if (val <  500) return 25;
    if (val < 2000) return 50;
    return 100;
}

struct ScrollInput {
    int32_t  value      = 0;
    int32_t  minVal     = 0;
    int32_t  maxVal     = 60;
    int32_t  step       = 1;
    float    pxPerUnit  = 4.0f;
    bool     wheelLeft  = true;
    float    velocity   = 0;
    float    wheelOffset= 0;
    bool     dragging   = false;
    int16_t  lastDragY  = 0;
    int16_t  totalDrag  = 0;  // track total drag distance to kill inertia on taps
    bool     blindMode  = false;
};

static ScrollInput _scroll;

void scrollInputInit(int32_t initVal, int32_t minV, int32_t maxV,
                     int32_t step, float pxPerUnit, bool blindMode=false) {
    _scroll.value       = initVal;
    _scroll.minVal      = minV;
    _scroll.maxVal      = maxV;
    _scroll.step        = step;
    _scroll.pxPerUnit   = pxPerUnit;
    _scroll.velocity    = 0;
    _scroll.wheelOffset = 0;
    _scroll.dragging    = false;
    _scroll.totalDrag   = 0;
    _scroll.blindMode   = blindMode;
}

void drawScrollWheel(int wx, int wy, int wx2, int wy2, float offset) {
    int ww = wx2 - wx, wh = wy2 - wy;
    int cy = wy + wh / 2;
    gfx->fillRoundRect(wx+2, wy+2, ww-4, wh-4, 10, COL_DARKGREY);

    // Dense ticks at top, sparse in middle, dense at bottom
    auto drawTicks = [&](int y0, int y1, bool dense) {
        int spacing = dense ? 8 : 18;
        int idx = 0;
        for (int y = y0; y <= y1; y += spacing, idx++) {
            if (y < wy+3 || y > wy2-3) continue;
            bool major = dense ? (idx % 2 == 0) : (idx % 3 == 0);
            int lw = major ? ww-12 : ww-24;
            uint16_t col = major ? COL_WHITE : COL_GREY;
            gfx->drawFastHLine(wx + (ww-lw)/2, y, lw, col);
        }
    };

    drawTicks(wy,    cy-18, true);
    drawTicks(cy-18, cy+18, false);
    drawTicks(cy+18, wy2,   true);
}

void drawScrollValue(int vx, int vy, int vx2, int vy2, int32_t val) {
    int vw = vx2-vx, vh = vy2-vy;
    gfx->fillRoundRect(vx+2, vy+2, vw-4, vh-4, 8, 0x1008); // dark blue

    char buf[10];
    snprintf(buf, sizeof(buf), "%ld", val);

    // Scale text to fit
    uint8_t ts = 4;
    while (ts > 1 && (int)strlen(buf) * 6 * ts > vw - 8) ts--;

    gfx->setTextColor(COL_WHITE);
    gfx->setTextSize(ts);
    int tx = vx + vw/2 - strlen(buf)*3*ts;
    int ty = vy + vh/2 - 4*ts;
    gfx->setCursor(tx, ty);
    gfx->print(buf);
}

void drawScrollScreen(const char* title, bool backEnabled, bool backVisible,
                      const char* nextLabel) {
    clearScreen();

    // Title — centered, above y=40
    drawCenteredText(title, 14, 2, COL_WHITE);

    int wx1 = _scroll.wheelLeft ? SW_WX1 : SW_VX1;
    int wx2 = _scroll.wheelLeft ? SW_WX2 : SW_VX2;
    int vx1 = _scroll.wheelLeft ? SW_VX1 : SW_WX1;
    int vx2 = _scroll.wheelLeft ? SW_VX2 : SW_WX2;

    // A — Scroll wheel
    drawScrollWheel(wx1, SW_Y1, wx2, SW_Y2, _scroll.wheelOffset);

    // D — Back button (124,40 → 200,70)
    if (backVisible) {
        uint16_t backBg = backEnabled ? COL_RED : COL_DARKGREY;
        uint16_t backFg = backEnabled ? COL_WHITE : 0x39E7; // dimmed
        drawButton(vx1, SW_Y1, vx2-vx1, SW_BACK_Y2-SW_Y1, "Back", backBg, backFg);
    }

    // C — Value display (124,70 → 200,170)
    drawScrollValue(vx1, SW_VAL_Y1, vx2, SW_VAL_Y2, _scroll.value);

    // B — Next button (124,170 → 200,200)
    if (nextLabel) drawButton(vx1, SW_NEXT_Y1, vx2-vx1, SW_Y2-SW_NEXT_Y1, nextLabel, COL_GREEN);
}

bool scrollUpdate(int16_t tx, int16_t ty, bool isDown) {
    bool changed = false;
    if (isDown) {
        if (!_scroll.dragging) {
            _scroll.dragging  = true;
            _scroll.lastDragY = ty;
            _scroll.totalDrag = 0;
            _scroll.velocity  = 0;
        } else {
            int16_t dy = _scroll.lastDragY - ty;
            if (abs(dy) > 0) {
                _scroll.totalDrag += abs(dy);
                int32_t step = _scroll.blindMode ? blindStep(_scroll.value) : _scroll.step;
                float pxPerStep = _scroll.blindMode ? (3.0f * sqrtf((float)step)) : _scroll.pxPerUnit;
                float units = (float)dy / pxPerStep;
                int32_t delta = (int32_t)(units) * step;
                if (delta != 0) {
                    // Only seed velocity if this was a real flick (>20px total drag)
                    if (_scroll.totalDrag > 20)
                        _scroll.velocity = units * 0.8f;
                    _scroll.value      += delta;
                    _scroll.value       = constrain(_scroll.value, _scroll.minVal, _scroll.maxVal);
                    _scroll.wheelOffset -= dy;
                    _scroll.lastDragY   = ty;
                    changed = true;
                }
            }
        }
    } else {
        if (_scroll.dragging) {
            _scroll.dragging = false;
            // Kill inertia if total drag was small — it was a tap or micro-movement
            if (_scroll.totalDrag < 20) _scroll.velocity = 0;
        }
        if (abs(_scroll.velocity) > 0.5f) {
            int32_t step = _scroll.blindMode ? blindStep(_scroll.value) : _scroll.step;
            float pxPerStep = _scroll.blindMode ? (3.0f * sqrtf((float)step)) : _scroll.pxPerUnit;
            _scroll.value      += (int32_t)(_scroll.velocity) * step;
            _scroll.value       = constrain(_scroll.value, _scroll.minVal, _scroll.maxVal);
            _scroll.wheelOffset -= _scroll.velocity * pxPerStep;
            _scroll.velocity   *= 0.85f;
            changed = true;
        } else {
            _scroll.velocity = 0;
        }
    }
    return changed;
}

// ── Scene draws ───────────────────────────────────────────────────────────────
void drawBlindTimer() {
    clearScreen();
    int32_t secs = levelSecsRemaining();

    if (gGame.overtime) {
        uint32_t elapsed = (millis() - gGame.levelStart) - levelDurationMs();
        char buf[12];
        uint32_t s = elapsed / 1000;
        snprintf(buf, sizeof(buf), "%02lu:%02lu", s/60, s%60);
        drawCenteredText("OverTime", 80, 3, COL_RED);
        drawCenteredText(buf, 120, 4, COL_RED);
        return;
    }

    if (secs < 0) secs = 0;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", (int)(secs/60), (int)(secs%60));
    drawCenteredText(buf, 88, 4, COL_WHITE);

    if (gGame.hasValues && gGame.currentRound < gGame.numRounds) {
        char sb[16], bb[16];
        snprintf(sb, sizeof(sb), "SB $%lu", gGame.rounds[gGame.currentRound].smallBlind);
        snprintf(bb, sizeof(bb), "BB $%lu", gGame.rounds[gGame.currentRound].bigBlind);
        drawCenteredText(sb, 148, 2, COL_GREY);
        drawCenteredText(bb, 168, 2, COL_GREY);
    }

    if (gGame.numRounds > 0) {
        char rnd[16];
        snprintf(rnd, sizeof(rnd), "Round %d/%d", gGame.currentRound+1, gGame.numRounds);
        drawCenteredText(rnd, 210, 1, COL_GREY);
    }

    if (gGame.running && secs <= RING_WARN_SECS && secs > 0)
        drawRing((float)secs / (float)RING_WARN_SECS);
}

void drawClock() {
    clearScreen();
    char buf[10];
    if (timeClient.isTimeSet())
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                 timeClient.getHours(), timeClient.getMinutes(), timeClient.getSeconds());
    else strcpy(buf, "--:--:--");
    drawCenteredText(buf, 104, 3, COL_WHITE);
}

void drawMenu() {
    clearScreen();
    drawCenteredText("Options", 18, 2, COL_WHITE);

    // PAUSE — most prominent, big, centered
    bool isPaused = gGame.paused;
    uint16_t pauseCol = isPaused ? COL_GREEN : 0xFBE0; // amber when running, green when paused
    drawButton(30, 46, 180, 54, isPaused ? "RESUME" : "PAUSE", pauseCol);

    // Next Blinds
    drawButton(30, 108, 180, 38, "Next Blinds", COL_DARKGREY);

    // Speed mode
    char spd[16];
    snprintf(spd, sizeof(spd), "Speed: %s", gGame.speedMode ? "ON" : "OFF");
    drawButton(30, 154, 180, 38, spd, gGame.speedMode ? COL_ORANGE : COL_DARKGREY);

    // Edit (smallest, bottom)
    drawButton(30, 200, 180, 28, "Edit Times/Values", COL_DARKGREY);
}

void drawSetupTime() {
    clearScreen();
    drawCenteredText("Blind Time (min)", 14, 2, COL_WHITE);

    int wx1 = _scroll.wheelLeft ? SW_WX1 : SW_VX1;
    int wx2 = _scroll.wheelLeft ? SW_WX2 : SW_VX2;
    int vx1 = _scroll.wheelLeft ? SW_VX1 : SW_WX1;
    int vx2 = _scroll.wheelLeft ? SW_VX2 : SW_WX2;

    // Scroll wheel full height
    drawScrollWheel(wx1, SW_Y1, wx2, SW_Y2, _scroll.wheelOffset);

    // Value display full height, same as wheel — tap to proceed
    int vw = vx2 - vx1, vh = SW_Y2 - SW_Y1;
    gfx->fillRoundRect(vx1+2, SW_Y1+2, vw-4, vh-4, 10, 0x1008);

    // Show MM:SS format
    int32_t mins = _scroll.value;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02ld:00", mins);
    uint8_t ts = 3;
    while (ts > 1 && (int)strlen(buf) * 6 * ts > vw - 8) ts--;
    gfx->setTextColor(COL_WHITE);
    gfx->setTextSize(ts);
    int tx = vx1 + vw/2 - strlen(buf)*3*ts;
    int ty = SW_Y1 + vh/2 - 4*ts;
    gfx->setCursor(tx, ty);
    gfx->print(buf);

    // Hint text
    gfx->setTextSize(1);
    gfx->setTextColor(COL_GREY);
    int hw = 3 * 3; // "tap" = 3 chars * 3px * size1 = 9... use centered
    gfx->setCursor(vx1 + vw/2 - 9, SW_Y1 + vh - 16);
    gfx->print("tap");
}

void drawSetupBlinds() {
    char title[12];
    uint8_t r = gApp.setupRound + 1;
    if (!gApp.enteringBB)
        snprintf(title, sizeof(title), "%d - SB", r);
    else
        snprintf(title, sizeof(title), "%d - BB", r);

    // Back disabled (grey) only on round 1 SB, red otherwise
    bool isFirstSB = (gApp.setupRound == 0 && !gApp.enteringBB);
    drawScrollScreen(title, !isFirstSB, true, "Next");
}

void drawStartGame() {
    clearScreen();
    // Big green GO circle
    gfx->fillCircle(DISPLAY_CX, DISPLAY_CY, 70, COL_GREEN);
    drawCenteredText("GO", 112, 4, COL_WHITE);
    char summary[24];
    snprintf(summary, sizeof(summary), "%d min", gGame.blindMinutes);
    drawCenteredText(summary, 180, 1, COL_GREY);
    if (gGame.numRounds > 0) {
        char rnd[16];
        snprintf(rnd, sizeof(rnd), "%d rounds", gGame.numRounds);
        drawCenteredText(rnd, 195, 1, COL_GREY);
    }
}

// ── Loop state ────────────────────────────────────────────────────────────────
static Scene    _lastScene     = Scene::START_GAME;
static uint32_t _lastDraw      = 0;
static int32_t  _lastSecsShown = -999;
static bool     _forceRedraw   = true;

// ── Touch handling ────────────────────────────────────────────────────────────
void handleTouch(Gesture g, int16_t tx, int16_t ty) {
    switch (gApp.scene) {

    case Scene::BLIND_TIMER:
        if (g == Gesture::DOUBLE_TAP) gApp.scene = Scene::MENU;
        else if (g == Gesture::SWIPE_LEFT || g == Gesture::SWIPE_RIGHT) gApp.scene = Scene::CLOCK;
        break;

    case Scene::CLOCK:
        if (g == Gesture::SWIPE_LEFT || g == Gesture::SWIPE_RIGHT) gApp.scene = Scene::BLIND_TIMER;
        break;

    case Scene::MENU:
        if (g == Gesture::TAP) {
            // PAUSE/RESUME — y:46-100
            if (ty >= 46 && ty <= 100) {
                togglePause();
                _forceRedraw = true;
                gApp.scene = Scene::BLIND_TIMER;
            }
            // Next Blinds — y:108-146
            else if (ty >= 108 && ty <= 146) {
                if (gGame.running) {
                    if (gGame.paused) togglePause(); // resume before advancing
                    if (gGame.overtime) {
                        // End overtime, stop game
                        gGame.overtime = false;
                        gGame.running = false;
                    } else {
                        gGame.currentRound++;
                        if (gGame.numRounds > 0 && gGame.currentRound >= gGame.numRounds)
                            gGame.overtime = true;
                        gGame.levelStart = millis();
                    }
                }
                gApp.scene = Scene::BLIND_TIMER;
            }
            // Speed mode — y:154-192
            else if (ty >= 154 && ty <= 192) {
                toggleSpeedMode();
                _forceRedraw = true;
            }
            // Edit — y:200-228
            else if (ty >= 200 && ty <= 228) {
                gGame = GameState();
                gApp.setupRound = 0; gApp.enteringBB = false;
                gApp.scene = Scene::SETUP_TIME;
            }
        } else if (g == Gesture::SWIPE_LEFT || g == Gesture::SWIPE_RIGHT || g == Gesture::DOUBLE_TAP) {
            gApp.scene = Scene::BLIND_TIMER;
        }
        break;

    case Scene::SETUP_TIME:
        if ((g == Gesture::SWIPE_LEFT || g == Gesture::SWIPE_RIGHT) &&
            (_downX < 32 || _downX > 208)) {
            _scroll.wheelLeft = !_scroll.wheelLeft;
            _forceRedraw = true;
        }
        if (g == Gesture::TAP) {
            int vx1 = _scroll.wheelLeft ? SW_VX1 : SW_WX1;
            int vx2 = _scroll.wheelLeft ? SW_VX2 : SW_WX2;
            // Tap anywhere on value display = confirm
            if (tx >= vx1 && tx <= vx2 && ty >= SW_Y1 && ty <= SW_Y2) {
                gGame.blindMinutes = constrain(_scroll.value, 1, 60);
                scrollInputInit(0, 0, 5000, 1, 3.0f, true);
                gApp.setupRound = 0; gApp.enteringBB = false;
                gApp.scene = Scene::SETUP_BLINDS;
                _forceRedraw = true;
            }
        }
        break;

    case Scene::SETUP_BLINDS: {
        // Swipe from dead zones flips wheel side
        if ((g == Gesture::SWIPE_LEFT || g == Gesture::SWIPE_RIGHT) &&
            (_downX < 32 || _downX > 208)) {
            _scroll.wheelLeft = !_scroll.wheelLeft;
            _forceRedraw = true;
        }
        if (g == Gesture::TAP) {
            int vx1 = _scroll.wheelLeft ? SW_VX1 : SW_WX1;
            bool isFirstSB = (gApp.setupRound == 0 && !gApp.enteringBB);

            // Back button: vx1, y:40-70
            if (tx >= vx1 && ty >= SW_Y1 && ty < SW_BACK_Y2 && !isFirstSB) {
                if (gApp.enteringBB) {
                    // Go back to SB of same round
                    gApp.enteringBB = false;
                    scrollInputInit(gGame.rounds[gApp.setupRound].smallBlind, 0, 5000, 1, 3.0f, true);
                } else if (gApp.setupRound > 0) {
                    // Go back to BB of previous round
                    gApp.setupRound--;
                    gApp.enteringBB = true;
                    scrollInputInit(gGame.rounds[gApp.setupRound].bigBlind, 0, 5000, 1, 3.0f, true);
                }
                _forceRedraw = true;
            }
            // Next button: vx1, y:170-200
            else if (tx >= vx1 && ty >= SW_NEXT_Y1 && ty <= SW_Y2) {
                if (!gApp.enteringBB) {
                    // Tapping Next on SB with value=0 ends blind entry
                    if (_scroll.value == 0) {
                        gGame.numRounds = gApp.setupRound; // no rounds if first, else use what we have
                        gGame.hasValues = false;
                        gApp.scene = Scene::START_GAME;
                    } else {
                        gGame.rounds[gApp.setupRound].smallBlind = _scroll.value;
                        gGame.hasValues = true;
                        // min for next round is at least this value
                        int32_t minNext = _scroll.value;
                        scrollInputInit(minNext, minNext, 5000, 1, 3.0f, true);
                        gApp.enteringBB = true;
                    }
                } else {
                    // Save BB — must be >= SB
                    int32_t bb = max(_scroll.value, (int32_t)gGame.rounds[gApp.setupRound].smallBlind);
                    int32_t sb = gGame.rounds[gApp.setupRound].smallBlind;
                    gGame.rounds[gApp.setupRound].bigBlind = bb;
                    gGame.numRounds = gApp.setupRound + 1;

                    // If BB == SB for this round, or BB == previous round's BB → stop
                    bool bbEqualsSB = (bb == sb);
                    bool sameAsPrev = (gApp.setupRound > 0 &&
                                       bb == (int32_t)gGame.rounds[gApp.setupRound-1].bigBlind);

                    if (bbEqualsSB || sameAsPrev || gApp.setupRound + 1 >= MAX_ROUNDS) {
                        gApp.scene = Scene::START_GAME;
                    } else {
                        gApp.setupRound++;
                        scrollInputInit(bb, bb, 5000, 1, 3.0f, true);
                        gApp.enteringBB = false;
                    }
                }
                _forceRedraw = true;
            }
        }
        break;
    }

    case Scene::START_GAME:
        // Tap anywhere on GO circle
        if (g == Gesture::TAP) {
            int dx = tx - DISPLAY_CX, dy = ty - DISPLAY_CY;
            if (dx*dx + dy*dy <= 70*70) {
                gGame.running = true;
                gGame.currentRound = 0;
                gGame.overtime = false;
                gGame.levelStart = millis();
                gApp.scene = Scene::BLIND_TIMER;
            }
        }
        break;
    }
}

// ── Game logic ────────────────────────────────────────────────────────────────
void updateGameLogic() {
    if (!gGame.running || gGame.overtime || gGame.paused) return;
    if (levelSecsRemaining() <= 0) {
        gGame.currentRound++;
        if (gGame.numRounds > 0 && gGame.currentRound >= gGame.numRounds) {
            gGame.overtime = true;
            gGame.levelStart = millis() - levelDurationMs();
        } else {
            gGame.levelStart = millis();
        }
    }
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("[Boot] The Nut starting...");

    pinMode(PIN_BL, OUTPUT);
    digitalWrite(PIN_BL, HIGH);

    gfx->begin();
    gfx->fillScreen(COL_BG);
    drawCenteredText("The Nut", 110, 2, COL_WHITE);
    drawCenteredText("Starting...", 130, 2, COL_GREY);

    touchInit();

    for (auto& ap : WIFI_APS) wifiMulti.addAP(ap.ssid, ap.pass);
    if (wifiMulti.run(5000) == WL_CONNECTED) {
        Serial.printf("[WiFi] Connected: %s\n", WiFi.SSID().c_str());
        timeClient.begin();
        timeClient.update();
    } else {
        Serial.println("[WiFi] Not connected");
    }

    Serial.println("[Boot] Ready.");
    scrollInputInit(20, 1, 60, 1, 4.0f);
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
    if (WiFi.status() == WL_CONNECTED) timeClient.update();

    updateGameLogic();

    // Continuous scroll handling
    bool scrollActive = (gApp.scene == Scene::SETUP_TIME || gApp.scene == Scene::SETUP_BLINDS);
    bool scrollChanged = false;
    if (scrollActive) {
        int wheelX1 = _scroll.wheelLeft ? SW_WX1 : SW_VX1;
        int wheelX2 = _scroll.wheelLeft ? SW_WX2 : SW_VX2;
        bool inWheel = (_wasDown && _curX >= wheelX1 && _curX <= wheelX2);
        scrollChanged = scrollUpdate(_curX, _curY, _wasDown && inWheel);
    }

    Gesture g = gestureUpdate();
    Scene sceneBefore = gApp.scene;
    if (g != Gesture::NONE) {
        handleTouch(g, _downX, _downY);
        if (gApp.scene != sceneBefore) {
            if (gApp.scene == Scene::SETUP_TIME)
                scrollInputInit(20, 1, 60, 1, 4.0f);
            _forceRedraw = true;
            _lastSecsShown = -999;
        }
    }

    uint32_t now = millis();
    bool sceneChanged = (gApp.scene != _lastScene);
    bool timeChanged = false;

    if (gApp.scene == Scene::BLIND_TIMER && gGame.running) {
        int32_t secs = levelSecsRemaining();
        if (secs != _lastSecsShown) { timeChanged = true; _lastSecsShown = secs; }
        // Redraw every frame during ring (smooth animation)
        if (secs <= RING_WARN_SECS && secs > 0) timeChanged = true;
    } else if (gApp.scene == Scene::CLOCK) {
        if (now - _lastDraw > 1000) timeChanged = true;
    }

    if (_forceRedraw || sceneChanged || timeChanged || scrollChanged) {
        _lastDraw    = now;
        _lastScene   = gApp.scene;
        _forceRedraw = false;
        switch (gApp.scene) {
            case Scene::BLIND_TIMER:  drawBlindTimer();  break;
            case Scene::CLOCK:        drawClock();        break;
            case Scene::MENU:         drawMenu();         break;
            case Scene::SETUP_TIME:   drawSetupTime();   break;
            case Scene::SETUP_BLINDS: drawSetupBlinds(); break;
            case Scene::START_GAME:   drawStartGame();   break;
        }
    }

    delay(20);
}
