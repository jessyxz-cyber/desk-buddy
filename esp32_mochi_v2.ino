// ============================================================================
//  MOCHI V2.1  -  ESP32 + SSD1306 OLED desk companion
//   - Robo eyes with an EMOTION SYSTEM (mood drives the expressions)
//   - Digital clock + calendar (NTP)
//   - Live YouTube lyrics, "written" word by word
//   - Spotify "now playing" card (from Chrome, via the extension)
//   - YouTube thumbnail (dithered to the OLED, via the extension)
//   - Stopwatch + Pomodoro
//   - Everything is controlled from a web page served by the ESP32
//   - Touch module on D4 (TTP223): tap = switch clock <-> eyes, hold = pet Mochi
//   - Synced lyrics for YouTube AND Spotify Web Player (Spotify lyrics can be switched off)
// Libraries (Library Manager): "Adafruit SSD1306" and "Adafruit GFX Library"
// ============================================================================

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Wire.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ------------------------------- SETTINGS -----------------------------------
const char* WIFI_SSID = "realme GT Master Edition";
const char* WIFI_PASS = "m5xejmf6";

#define TZ_OFFSET_SEC  19800    // time zone in seconds. 19800 = India (UTC+5:30)

#define SCREEN_W 128
#define SCREEN_H 64
#define OLED_ADDR 0x3C
#define SDA_PIN 21
#define SCL_PIN 22
#define I2C_HZ 800000           // lower to 400000 if the screen shows garbage

#define TOUCH_PIN         4     // touch sensor output pin (D4 = GPIO 4)
#define TOUCH_DIGITAL     1     // 1 = TTP223 touch module (digital output)  |  0 = bare wire on the ESP32's own touch input
#define TOUCH_ACTIVE_HIGH 1     // TTP223 default: pin goes HIGH while touched (set 0 if you soldered pad A = active LOW)
#define TOUCH_SENS        0.35f // only used when TOUCH_DIGITAL is 0: lower = more sensitive
#define LONG_PRESS_MS     700   // hold this long to pet Mochi

// lyrics
#define FONT_SIZE       2       // 2 = 10 chars x 4 rows
#define STEP_MS         300
#define MAX_ROWS        12
#define MAX_WORDS       48
#define MAX_WORD_MS     600
#define SPAN_PERCENT    90
#define AUTO_RETURN_MS  8000    // Auto: leave lyrics after this long without a new line

// eyes
#define EYE_FRAME_MS    20
#define CLOSE_MS        80
#define OPEN_MS         110

// misc
#define CAL_FLIP        1       // clock screen flips to the calendar for a few seconds every 30 s
#define MARQUEE_PPS     40      // scrolling speed of long song titles (pixels per second)
#define ALERT_MS        8000
#define NIGHT_DIM       1       // dim the screen between 23:00 and 06:00
#define MAX_LAPS        6

// ----------------------------------------------------------------------------
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1, I2C_HZ, I2C_HZ);
WebServer server(80);
Preferences prefs;

static inline int   mini(int a, int b)     { return a < b ? a : b; }
static inline int   maxi(int a, int b)     { return a > b ? a : b; }
static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float maxf(float a, float b) { return a > b ? a : b; }
float smooth(float x)  { return x * x * (3.0f - 2.0f * x); }
float easeIn(float x)  { return x * x; }
float easeOut(float x) { return 1.0f - (1.0f - x) * (1.0f - x); }

// ============================================================================
//                              GLOBAL STATE
// ============================================================================
enum View { V_AUTO, V_EYES, V_CLOCK, V_CAL, V_LYRICS, V_SPOTIFY, V_THUMB, V_STOPWATCH, V_POMO, V_COUNT };
const char* VIEW_NAME[V_COUNT] = { "auto", "eyes", "clock", "calendar", "lyrics", "spotify", "thumb", "stopwatch", "pomodoro" };

View     sel = V_AUTO;              // what the user picked (web page / touch)
View     view = V_EYES;             // what is on the screen right now
View     home = V_EYES;             // Auto mode's base screen: eyes or clock (touch toggles it)
bool     needFull = true;           // redraw the current screen from scratch
uint32_t viewEnteredAt = 0, viewFrameAt = 0, overlayBlockedUntil = 0;

// settings (saved in flash)
bool use24h = true;
bool spLyrics = true;               // show synced lyrics for Spotify songs (on/off from the web page)
int  lyOffYt = 300, lyOffSp = 300;  // lyric timing in ms, adjustable on the web page (+ = lyrics appear earlier)
int  focusMin = 25, shortMin = 5, longMin = 15;

// ---- emotions ----
enum { EX_NORMAL, EX_HAPPY, EX_SAD, EX_ANGRY, EX_SLEEPY, EX_SURPRISED, EX_LOVE, EX_WINK, EX_EXCITED, EX_BORED, EX_COUNT };
const char* EX_NAME[EX_COUNT] = { "normal", "happy", "sad", "angry", "sleepy", "surprised", "love", "wink", "excited", "bored" };

struct Expr {
  uint8_t w, h, r;             // eye width, height, corner radius
  float lidIn, lidOut;         // top eyelid height (fraction of eye) at the inner / outer side
  float cheek;                 // bottom "smile" cover (fraction of eye height)
  float look;                  // how far the eyes wander (0 = stay centered)
  bool heart, wink, shake;
};
const Expr EXPR[EX_COUNT] = {
  /* normal    */ { 34, 38, 10, 0.00f, 0.00f, 0.00f, 1.0f, false, false, false },
  /* happy     */ { 34, 36, 10, 0.00f, 0.00f, 0.50f, 0.6f, false, false, false },
  /* sad       */ { 32, 38, 10, 0.00f, 0.45f, 0.00f, 0.5f, false, false, false },
  /* angry     */ { 34, 36,  8, 0.50f, 0.08f, 0.00f, 0.6f, false, false, false },
  /* sleepy    */ { 34, 34, 10, 0.50f, 0.50f, 0.00f, 0.3f, false, false, false },
  /* surprised */ { 36, 46, 16, 0.00f, 0.00f, 0.00f, 0.5f, false, false, false },
  /* love      */ { 34, 38, 10, 0.00f, 0.00f, 0.00f, 0.0f, true,  false, false },
  /* wink      */ { 34, 38, 10, 0.00f, 0.00f, 0.00f, 0.8f, false, true,  false },
  /* excited   */ { 36, 40, 12, 0.00f, 0.00f, 0.45f, 0.5f, false, false, true  },
  /* bored     */ { 34, 34, 10, 0.38f, 0.38f, 0.00f, 0.8f, false, false, false }
};

int      curExpr = EX_NORMAL;
bool     autoExpr = true;      // "auto emotions": the mood picks the expression
float    lookX = 0, lookY = 0, tgtX = 0, tgtY = 0;
float    closeAmt = 1.0f;      // 0 = open, 1 = closed
bool     blinkActive = false;
uint32_t blinkT0 = 0;
uint16_t blinkHold = 20;
int      pendingExpr = -1;
uint32_t nextBlinkAt = 0, nextLookAt = 0, nextExprAt = 0, lastEyeFrame = 0;

float    moodHappy = 60, moodEnergy = 70, moodLove = 25, moodBored = 10, moodAnnoy = 0;
uint32_t lastMoodTick = 0;

// ---- lyrics ----
String   lyTxt[MAX_WORDS];
uint8_t  lyRow[MAX_WORDS], lyCol[MAX_WORDS];
int      lyStart[MAX_WORDS];
int      lyN = 0, lyRows = 0, lyShown = 0, lyCursor = 0;
uint32_t lyT0 = 0, lyFrameAt = 0, lastLyricAt = 0;
bool     lyricFromSpotify = false;
int      lyScrollTop = 0, lyScrollTo = 0;
float    lyScrollFrom = 0;
uint32_t lyScrollT0 = 0;
bool     lyScrolling = false, lyDraw = false, lyWaiting = false;

// ---- Spotify ----
String   spTitle, spArtist;
int      spPosMs = -1, spDurMs = -1;
bool     spPlaying = false;
uint32_t spUpdatedAt = 0, spLastPlayingAt = 0, spMarqueeT0 = 0;

// ---- thumbnail ----
uint8_t  thumbBuf[SCREEN_W * SCREEN_H / 8];
bool     thumbValid = false, thumbDirty = false;

// ---- stopwatch ----
bool     swRunning = false;
uint32_t swStartAt = 0, swAccum = 0;
uint32_t swLaps[MAX_LAPS];
int      nLaps = 0;

// ---- pomodoro ----
enum { PH_FOCUS, PH_SHORT, PH_LONG };
int      pomoPhase = PH_FOCUS, pomoCycle = 0;      // pomoCycle = focus sessions done in this set
bool     pomoRunning = false;
uint32_t pomoEndAt = 0, pomoRemain = 25UL * 60000UL;

// ---- alert (full-screen flash) ----
bool     alertActive = false;
uint32_t alertT0 = 0;
int      alertLastInv = -1;
String   alertL1, alertL2;

// ---- touch ----
float    touchBase = 60;
bool     touchDown = false, petFired = false, touchRawNow = false;
uint8_t  touchCnt = 0;
uint32_t touchDownAt = 0, lastTouchPoll = 0, touchTaps = 0, touchHolds = 0;

// forward declarations
void forceView(View v);
void setView(View v);
void setSel(View v);
void requestExpr(int e);
void startAlert(const String& l1, const String& l2);

// ============================================================================
//                                HELPERS
// ============================================================================
String asciiOnly(const String& s) {
  String o;
  for (unsigned int i = 0; i < s.length(); i++) { char c = s[i]; if (c >= 32 && c < 127) o += c; }
  return o;
}
String jsonEsc(const String& s) {
  String o;
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c < 32 || c >= 127) continue;
    if (c == '"' || c == '\\') o += '\\';
    o += c;
  }
  return o;
}
int hexv(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

void drawCentered(const char* s, int y, int size) {
  int w = (int)strlen(s) * 6 * size - size;
  display.setTextSize(size);
  display.setCursor((SCREEN_W - w) / 2, y);
  display.print(s);
}

// Text that fits is centered; long text scrolls sideways like a marquee.
void drawScrollText(const String& s, int y, int size, uint32_t now, uint32_t t0) {
  int w = (int)s.length() * 6 * size;
  display.setTextSize(size);
  if (w <= SCREEN_W) {
    display.setCursor((SCREEN_W - (w - size)) / 2, y);
    display.print(s);
    return;
  }
  int cycle = w + 36;
  uint32_t t = now - t0;
  int off = 0;
  if (t > 1200) off = (int)(((t - 1200) * MARQUEE_PPS / 1000) % (uint32_t)cycle);
  display.setCursor(-off, y);
  display.print(s);
  display.setCursor(cycle - off, y);
  display.print(s);
}

// ---- time ----
bool getTm(struct tm* t) {
  time_t n = time(nullptr);
  if (n < 1700000000) return false;        // NTP not synced yet
  localtime_r(&n, t);
  return true;
}
int currentHour() { struct tm t; return getTm(&t) ? t.tm_hour : -1; }

const char* DAY3[7]  = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
const char* MON3[12] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
const char* MONTH_FULL[12] = { "JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER" };
int daysInMonth(int year, int m) {
  static const uint8_t d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if (m == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) return 29;
  return d[m];
}

// ---- settings ----
void loadSettings() {
  prefs.begin("mochi", false);
  use24h   = prefs.getBool("h24", true);
  spLyrics = prefs.getBool("splyr", true);
  lyOffYt  = constrain(prefs.getInt("offyt", 300), -3000, 3000);
  lyOffSp  = constrain(prefs.getInt("offsp", 300), -3000, 3000);
  focusMin = constrain(prefs.getInt("focus", 25), 1, 180);
  shortMin = constrain(prefs.getInt("short", 5), 1, 60);
  longMin  = constrain(prefs.getInt("long", 15), 1, 120);
  home     = (prefs.getUChar("home", (uint8_t)V_EYES) == (uint8_t)V_CLOCK) ? V_CLOCK : V_EYES;
}
void saveSettings() {
  prefs.putBool("h24", use24h);
  prefs.putBool("splyr", spLyrics);
  prefs.putInt("offyt", lyOffYt);
  prefs.putInt("offsp", lyOffSp);
  prefs.putInt("focus", focusMin);
  prefs.putInt("short", shortMin);
  prefs.putInt("long", longMin);
  prefs.putUChar("home", (uint8_t)home);
}

// ============================================================================
//                       MUSIC / TIMER STATUS (used by mood)
// ============================================================================
bool musicActive(uint32_t now) {
  if (lastLyricAt && now - lastLyricAt < AUTO_RETURN_MS) return true;
  if (spPlaying && spLastPlayingAt && now - spLastPlayingAt < 6000) return true;
  return false;
}

// ============================================================================
//                            EMOTION SYSTEM
// ============================================================================
// Four moods drift over time and react to events:
//   happy   - music, petting, finished pomodoros make it grow
//   energy  - follows the time of day (sleepy at night), higher while you work
//   love    - petting (hold the touch pin); fades slowly
//   bored   - grows when nothing happens, resets when you interact
//   annoyed - rises if you poke it (tap) too fast
void moodTick(uint32_t now) {
  bool music = musicActive(now);
  bool working = pomoRunning && pomoPhase == PH_FOCUS;
  int hour = currentHour();

  float energyTarget = 75;
  if (hour >= 0) {
    if (hour >= 23 || hour < 6) energyTarget = 25;
    else if (hour >= 21 || hour < 8) energyTarget = 50;
  }
  if (music) energyTarget += 10;
  if (working) energyTarget = 80;
  moodEnergy += (energyTarget - moodEnergy) * 0.01f;

  moodHappy += (55.0f - moodHappy) * 0.008f;
  if (music) moodHappy = minf(100.0f, moodHappy + 0.5f);
  moodLove  = maxf(0.0f, moodLove - 0.12f);
  moodAnnoy = maxf(0.0f, moodAnnoy - 2.0f);

  if (music || pomoRunning || swRunning) moodBored = maxf(0.0f, moodBored - 0.5f);
  else moodBored = minf(100.0f, moodBored + 0.3f);
  if (moodBored > 70) moodHappy = maxf(10.0f, moodHappy - 0.15f);
}

// Pick the next expression: every mood adds weight to the expressions that fit it.
int moodPick() {
  float w[EX_COUNT];
  for (int i = 0; i < EX_COUNT; i++) w[i] = 0;
  w[EX_NORMAL]    = 28;
  w[EX_HAPPY]     = 6 + moodHappy * 0.45f;
  w[EX_EXCITED]   = (moodHappy > 70 && moodEnergy > 55) ? (moodHappy - 60) * 0.8f : 0;
  w[EX_LOVE]      = moodLove * 0.8f;
  w[EX_WINK]      = moodHappy > 50 ? 5 : 1;
  w[EX_SURPRISED] = 4;
  w[EX_SLEEPY]    = maxf(0.0f, 55.0f - moodEnergy) * 1.5f;
  w[EX_BORED]     = maxf(0.0f, moodBored - 35.0f) * 0.9f;
  w[EX_SAD]       = maxf(0.0f, 40.0f - moodHappy) + (moodBored > 80 ? 6 : 0);
  w[EX_ANGRY]     = moodAnnoy > 40 ? moodAnnoy * 1.2f : 0;

  float total = 0;
  for (int i = 0; i < EX_COUNT; i++) total += w[i];
  float r = (random(0, 10000) / 10000.0f) * total;
  for (int i = 0; i < EX_COUNT; i++) { r -= w[i]; if (r <= 0) return i; }
  return EX_NORMAL;
}

// Show an expression now and keep it for a while (the mood picker waits).
void react(int expr, uint32_t holdMs) {
  requestExpr(expr);
  nextExprAt = millis() + holdMs;
}

void onPet() {
  moodLove  = minf(100.0f, moodLove + 30);
  moodHappy = minf(100.0f, moodHappy + 12);
  moodBored = 0; moodAnnoy = 0;
  react(EX_LOVE, 5000);
}
void onMusicStart() {
  moodHappy = minf(100.0f, moodHappy + 12);
  moodBored = 0;
  react(moodHappy > 75 ? EX_EXCITED : EX_HAPPY, 4000);
}
void onPomoDone() {
  moodHappy = minf(100.0f, moodHappy + 25);
  moodBored = 0;
  react(EX_EXCITED, 8000);
}

// ============================================================================
//                                  EYES
// ============================================================================
void startBlink(uint16_t hold) {
  if (blinkActive) return;
  blinkActive = true; blinkT0 = millis(); blinkHold = hold;
}

// Change expression with a blink: the swap happens while the eyes are shut, so it never "pops".
void requestExpr(int e) {
  if (view != V_EYES) { curExpr = e; return; }
  if (blinkActive) { pendingExpr = e; return; }
  if (e == curExpr) { startBlink(30); return; }
  blinkActive = true; blinkT0 = millis(); blinkHold = 70; pendingExpr = e;
}

void updateBlink(uint32_t now) {
  if (!blinkActive) return;
  uint32_t dt = now - blinkT0;
  if (dt < CLOSE_MS) {
    closeAmt = easeIn(dt / (float)CLOSE_MS);
  } else if (dt < (uint32_t)(CLOSE_MS + blinkHold)) {
    closeAmt = 1.0f;
    if (pendingExpr >= 0) { curExpr = pendingExpr; pendingExpr = -1; }
  } else if (dt < (uint32_t)(CLOSE_MS + blinkHold + OPEN_MS)) {
    closeAmt = 1.0f - easeOut((dt - CLOSE_MS - blinkHold) / (float)OPEN_MS);
    if (pendingExpr >= 0) { curExpr = pendingExpr; pendingExpr = -1; }
  } else {
    closeAmt = 0.0f;
    blinkActive = false;
    if (pendingExpr >= 0) { curExpr = pendingExpr; pendingExpr = -1; }
  }
}

void drawHeart(int cx, int cy, int s) {
  int r = maxi(2, s / 4);
  display.fillCircle(cx - r, cy - r / 2, r, SSD1306_WHITE);
  display.fillCircle(cx + r, cy - r / 2, r, SSD1306_WHITE);
  display.fillTriangle(cx - 2 * r + 1, cy - r / 2 + 2, cx + 2 * r - 1, cy - r / 2 + 2, cx, cy + 2 * r, SSD1306_WHITE);
}

void drawEyes(uint32_t now) {
  const Expr& e = EXPR[curExpr];
  const int gap = 14;
  int ox = (int)lroundf(lookX), oy = (int)lroundf(lookY);
  if (e.shake) {                                            // excited: little tremble
    ox += (int)lroundf(2.0f * sinf(now / 35.0f));
    oy += (int)lroundf(1.5f * sinf(now / 47.0f));
  }

  display.clearDisplay();

  if (e.heart) {                                            // beating heart eyes
    int s = 40 + (int)(4.0f * sinf(now / 110.0f));
    s = (int)(s * (1.0f - closeAmt * 0.85f));
    int half = 20 + gap / 2;
    drawHeart(64 - half + ox, 30 + oy, s);
    drawHeart(64 + half + ox, 30 + oy, s);
  } else {
    float hEff = maxf(3.0f, e.h * (1.0f - closeAmt));
    for (int side = 0; side < 2; side++) {                  // 0 = left eye, 1 = right eye
      bool left = (side == 0);
      bool winking = e.wink && !left;
      int h = winking ? 4 : (int)hEff;
      int cx = 64 + (left ? -1 : 1) * (e.w / 2 + gap / 2) + ox;
      int x = cx - e.w / 2;
      int y = 32 + oy - h / 2;
      int rr = mini((int)e.r, h / 2);

      display.fillRoundRect(x, y, e.w, h, rr, SSD1306_WHITE);

      if (!winking && h > 6) {
        if (e.lidIn > 0 || e.lidOut > 0) {                  // angry / sad / sleepy / bored eyelid
          int inner = (int)(e.lidIn * h), outer = (int)(e.lidOut * h);
          int lidL = left ? outer : inner;
          int lidR = left ? inner : outer;
          int x0 = x - 1, x1 = x + e.w, yt = y - 1;
          display.fillTriangle(x0, yt, x1, yt, x1, y + lidR, SSD1306_BLACK);
          display.fillTriangle(x0, yt, x1, y + lidR, x0, y + lidL, SSD1306_BLACK);
        }
        if (e.cheek > 0) {                                  // happy squint
          int off = (int)(e.cheek * h);
          display.fillRoundRect(x - 1, y + h - off, e.w + 2, h, rr, SSD1306_BLACK);
        }
      }
    }
  }
  display.display();
}

void scheduleEyes(uint32_t now) {
  nextBlinkAt = now + random(1500, 3500);
  nextLookAt  = now + random(600, 1800);
  nextExprAt  = now + random(5000, 9000);
}

void enterEyes() {
  uint32_t now = millis();
  closeAmt = 1.0f;                                          // eyes open up when they appear
  blinkActive = true; blinkT0 = now - CLOSE_MS; blinkHold = 0; pendingExpr = -1;
  lastEyeFrame = now;
  scheduleEyes(now);
  if (autoExpr) react(moodPick(), 3000);                    // how does Mochi feel right now?
}

void eyesLoop(uint32_t now) {
  if (now - lastEyeFrame < EYE_FRAME_MS) return;
  uint32_t dt = now - lastEyeFrame;
  if (dt > 100) dt = 100;
  lastEyeFrame = now;

  if (musicActive(now) && EXPR[curExpr].look > 0) {         // sway to the music
    tgtX = 10.0f * sinf(now / 450.0f) * EXPR[curExpr].look;
    tgtY = 2.0f * sinf(now / 220.0f);
    nextLookAt = now + 400;
  } else if (now >= nextLookAt) {                           // glance somewhere
    float sc = EXPR[curExpr].look;
    if (random(4) == 0) { tgtX = 0; tgtY = 0; }
    else { tgtX = (float)random(-12, 13) * sc; tgtY = (float)random(-6, 7) * sc; }
    nextLookAt = now + random(1200, 3500);
  }
  if (now >= nextBlinkAt) {                                 // blink
    startBlink(20);
    nextBlinkAt = now + (curExpr == EX_SLEEPY ? random(4000, 8000) : random(2200, 5500));
  }
  if (autoExpr && now >= nextExprAt && !blinkActive) {      // let the mood choose
    int pick = moodPick();
    if (pick == curExpr) pick = EX_NORMAL;
    requestExpr(pick);
    nextExprAt = now + (pick == EX_NORMAL ? random(5000, 10000) : random(3500, 7000));
  }

  updateBlink(now);
  float a = 1.0f - expf(-(float)dt / 80.0f);                // smooth eye movement
  lookX += (tgtX - lookX) * a;
  lookY += (tgtY - lookY) * a;
  drawEyes(now);
}

// ============================================================================
//                            CLOCK + CALENDAR
// ============================================================================
int lastClockSec = -9;
bool lastClockCal = false;
int lastCalMin = -9;

void drawClock(bool ok, const struct tm& t) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  if (!ok) {
    drawCentered("SYNCING TIME", 22, 1);
    drawCentered("(WiFi + internet)", 34, 1);
    display.display();
    return;
  }
  int h = t.tm_hour;
  const char* ampm = "";
  if (!use24h) { ampm = h >= 12 ? "PM" : "AM"; h = h % 12; if (h == 0) h = 12; }
  bool colon = (t.tm_sec % 2 == 0);

  char buf[12];
  if (use24h) snprintf(buf, sizeof(buf), "%02d%c%02d", h, colon ? ':' : ' ', t.tm_min);
  else        snprintf(buf, sizeof(buf), "%d%c%02d",   h, colon ? ':' : ' ', t.tm_min);
  drawCentered(buf, 9, 4);

  if (!use24h) { display.setTextSize(1); display.setCursor(SCREEN_W - 13, 0); display.print(ampm); }

  char d[16];
  snprintf(d, sizeof(d), "%s %d %s", DAY3[t.tm_wday], t.tm_mday, MON3[t.tm_mon]);
  drawCentered(d, 45, 2);

  int w = (t.tm_sec * SCREEN_W) / 60;                       // seconds as a thin progress line
  display.fillRect(0, 62, w, 2, SSD1306_WHITE);
  display.display();
}

void drawCalendar(const struct tm& t) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  int year = t.tm_year + 1900;
  char title[24];
  snprintf(title, sizeof(title), "%s %d", MONTH_FULL[t.tm_mon], year);
  drawCentered(title, 0, 1);

  const char* hdr = "SMTWTFS";
  for (int c = 0; c < 7; c++) {
    display.setCursor(c * 18 + 6, 8);
    display.print(hdr[c]);
  }

  int firstWday = ((t.tm_wday - ((t.tm_mday - 1) % 7)) + 7) % 7;
  int dim = daysInMonth(year, t.tm_mon);
  int weeks = (firstWday + dim + 6) / 7;
  bool tight = (weeks >= 6);                                // 6-week months need a more compact grid
  int gridY = tight ? 16 : 18;
  int pitch = tight ? 8 : 9;
  if (!tight) display.drawFastHLine(0, 16, SCREEN_W, SSD1306_WHITE);

  for (int day = 1; day <= dim; day++) {
    int idx = firstWday + day - 1;
    int col = idx % 7, row = idx / 7;
    int y = gridY + row * pitch;
    int digits = day >= 10 ? 2 : 1;
    int x = col * 18 + (18 - (digits * 6 - 1)) / 2;
    bool today = (day == t.tm_mday);
    if (today) {                                            // highlight today
      display.fillRoundRect(col * 18 + 1, y - 1, 17, pitch, 2, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    }
    display.setCursor(x, y);
    display.print(day);
    if (today) display.setTextColor(SSD1306_WHITE);
  }
  display.display();
}

void clockLoop(uint32_t now) {
  struct tm t;
  bool ok = getTm(&t);
  int sec = ok ? t.tm_sec : -2;
  bool cal = CAL_FLIP && ((now - viewEnteredAt) % 30000UL) >= 26000UL;
  if (!needFull && sec == lastClockSec && cal == lastClockCal) return;
  needFull = false; lastClockSec = sec; lastClockCal = cal;
  if (cal && ok) drawCalendar(t); else drawClock(ok, t);
}

void calLoop(uint32_t now) {
  (void)now;
  struct tm t;
  bool ok = getTm(&t);
  int key = ok ? (t.tm_mday * 100 + t.tm_min) : -2;
  if (!needFull && key == lastCalMin) return;
  needFull = false; lastCalMin = key;
  if (!ok) { drawClock(false, t); return; }
  drawCalendar(t);
}

// ============================================================================
//                        LYRICS (written word by word)
// ============================================================================
void lyPut(const String& s) {
  if (lyN >= MAX_WORDS) return;
  int len = s.length();
  int r = lyRows, c = lyCursor;
  const int chars = SCREEN_W / (6 * FONT_SIZE);
  if (r == 0)                          { r = 1; c = 0; }
  else if (c > 0 && c + 1 + len > chars) { r++; c = 0; }
  else if (c > 0)                      { c++; }
  if (r > MAX_ROWS) return;
  lyRows = r;
  lyTxt[lyN] = s;
  lyRow[lyN] = r - 1;
  lyCol[lyN] = c;
  lyN++;
  lyCursor = c + len;
}

void lyLayout(const String& text) {
  const int chars = SCREEN_W / (6 * FONT_SIZE);
  lyN = 0; lyRows = 0; lyCursor = 0;
  String t = text + " ", tok = "";
  for (unsigned int i = 0; i < t.length(); i++) {
    if (t[i] != ' ') { tok += t[i]; continue; }
    while ((int)tok.length() > chars) {
      lyPut(tok.substring(0, chars));
      tok = tok.substring(chars);
    }
    if (tok.length()) lyPut(tok);
    tok = "";
  }
}

float lyScrollOffset(uint32_t now) {
  if (!lyScrolling) return (float)lyScrollTo;
  float p = (now - lyScrollT0) / (float)STEP_MS;
  if (p >= 1.0f) return (float)lyScrollTo;
  return lyScrollFrom + (lyScrollTo - lyScrollFrom) * smooth(p);
}

void lyStartScroll(int newTop, uint32_t now) {
  lyScrollFrom = lyScrollOffset(now);
  lyScrollTop = newTop;
  lyScrollTo = newTop * (8 * FONT_SIZE);
  lyScrollT0 = now;
  lyScrolling = true;
}

void lyRender(uint32_t now) {
  const int lineH = 8 * FONT_SIZE;
  const int rowsVisible = SCREEN_H / lineH;
  int off = (int)(lyScrollOffset(now) + 0.5f);
  int y0 = (lyRows <= rowsVisible) ? (SCREEN_H - lyRows * lineH) / 2 : 0;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  if (lyWaiting) {
    display.setTextSize(1);
    display.setCursor(10, 28);
    display.print("waiting for lyrics");
  }
  display.setTextSize(FONT_SIZE);
  for (int i = 0; i < lyShown; i++) {
    int y = y0 + lyRow[i] * lineH - off;
    if (y <= -lineH || y >= SCREEN_H) continue;
    display.setCursor(lyCol[i] * 6 * FONT_SIZE, y);
    display.print(lyTxt[i]);
  }
  display.display();

  if (lyScrolling) {
    if (now - lyScrollT0 >= STEP_MS) lyScrolling = false;
    else lyDraw = true;
  }
}

void enterLyrics() {
  lyN = 0; lyRows = 0; lyShown = 0;
  lyScrollTop = 0; lyScrollTo = 0; lyScrollFrom = 0; lyScrolling = false;
  lyWaiting = true;
  lyDraw = true;
}

void startLyric(const String& text, int durMs) {
  lyLayout(text);

  int wt[MAX_WORDS], total = 0;
  for (int i = 0; i < lyN; i++) {
    char last = lyTxt[i][lyTxt[i].length() - 1];
    wt[i] = lyTxt[i].length() + 2 + (strchr(",.;:!?", last) ? 3 : 0);
    total += wt[i];
  }
  durMs = constrain(durMs, 500, 12000);
  int span = mini(durMs * SPAN_PERCENT / 100, lyN * MAX_WORD_MS);
  int cum = 0;
  for (int i = 0; i < lyN; i++) {
    lyStart[i] = total ? (int)((long)span * cum / total) : 0;
    cum += wt[i];
  }

  lyShown = 0;
  lyScrollTop = 0; lyScrollTo = 0; lyScrollFrom = 0; lyScrolling = false;
  lyWaiting = false;
  lyT0 = millis();
  lyDraw = true;
}

void lyricsLoop(uint32_t now) {
  const int rowsVisible = SCREEN_H / (8 * FONT_SIZE);
  uint32_t el = now - lyT0;
  while (lyShown < lyN && el >= (uint32_t)lyStart[lyShown]) {
    int r = lyRow[lyShown];
    if (lyRows > rowsVisible && r > lyScrollTop + rowsVisible - 1) lyStartScroll(r - rowsVisible + 1, now);
    lyShown++;
    lyDraw = true;
  }
  if (needFull) { needFull = false; lyDraw = true; }
  if (lyDraw && now - lyFrameAt >= 10) {
    lyDraw = false;
    lyFrameAt = now;
    lyRender(now);
  }
}

// ============================================================================
//                              SPOTIFY NOW PLAYING
// ============================================================================
void drawSpotify(uint32_t now) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  bool fresh = spTitle.length() > 0 && (now - spUpdatedAt < 20000);

  if (!fresh) {
    drawCentered("NOTHING PLAYING", 20, 1);
    drawCentered("Spotify in Chrome", 34, 1);
    display.display();
    return;
  }

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("NOW PLAYING");
  if (spPlaying) {
    display.fillTriangle(120, 0, 120, 6, 126, 3, SSD1306_WHITE);
  } else {
    display.fillRect(120, 0, 2, 7, SSD1306_WHITE);
    display.fillRect(124, 0, 2, 7, SSD1306_WHITE);
  }

  drawScrollText(spTitle, 12, 2, now, spMarqueeT0);
  drawScrollText(spArtist, 32, 1, now, spMarqueeT0);

  if (spDurMs > 0 && spPosMs >= 0) {
    uint32_t pos = (uint32_t)spPosMs + (spPlaying ? (now - spUpdatedAt) : 0);
    if (pos > (uint32_t)spDurMs) pos = spDurMs;
    int fill = (int)((uint64_t)pos * 124 / (uint32_t)spDurMs);
    display.drawRect(0, 47, SCREEN_W, 6, SSD1306_WHITE);
    display.fillRect(2, 49, fill, 2, SSD1306_WHITE);

    char a[12], b[12];
    snprintf(a, sizeof(a), "%lu:%02lu", (unsigned long)(pos / 60000), (unsigned long)((pos / 1000) % 60));
    snprintf(b, sizeof(b), "%lu:%02lu", (unsigned long)((uint32_t)spDurMs / 60000), (unsigned long)(((uint32_t)spDurMs / 1000) % 60));
    display.setTextSize(1);
    display.setCursor(0, 56);
    display.print(a);
    display.setCursor(SCREEN_W - (int)strlen(b) * 6, 56);
    display.print(b);
  }
  display.display();
}

void spotifyLoop(uint32_t now) {
  if (!needFull && now - viewFrameAt < 30) return;
  needFull = false; viewFrameAt = now;
  drawSpotify(now);
}

// ============================================================================
//                              YOUTUBE THUMBNAIL
// ============================================================================
void thumbLoop(uint32_t now) {
  (void)now;
  if (!needFull && !thumbDirty) return;
  needFull = false; thumbDirty = false;
  if (thumbValid) {
    memcpy(display.getBuffer(), thumbBuf, sizeof(thumbBuf));
    display.display();
  } else {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    drawCentered("NO THUMBNAIL YET", 20, 1);
    drawCentered("play a YouTube video", 34, 1);
    display.display();
  }
}

// ============================================================================
//                                STOPWATCH
// ============================================================================
uint32_t swElapsed(uint32_t now) { return swAccum + (swRunning ? (now - swStartAt) : 0); }

void fmtStopwatch(char* out, size_t n, uint32_t ms) {
  uint32_t s = ms / 1000;
  if (s < 3600) snprintf(out, n, "%02lu:%02lu.%lu", (unsigned long)(s / 60), (unsigned long)(s % 60), (unsigned long)((ms % 1000) / 100));
  else          snprintf(out, n, "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60), (unsigned long)(s % 60));
}

void swToggle(uint32_t now) {
  if (swRunning) { swAccum += now - swStartAt; swRunning = false; }
  else { swStartAt = now; swRunning = true; }
  needFull = true;
}
void swReset() { swRunning = false; swAccum = 0; nLaps = 0; needFull = true; }
void swLap(uint32_t now) {
  if (!swRunning) return;
  if (nLaps == MAX_LAPS) { memmove(swLaps, swLaps + 1, sizeof(uint32_t) * (MAX_LAPS - 1)); nLaps--; }
  swLaps[nLaps++] = swElapsed(now);
  needFull = true;
}

void drawStopwatch(uint32_t now) {
  uint32_t e = swElapsed(now);
  char buf[16];
  fmtStopwatch(buf, sizeof(buf), e);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("STOPWATCH");
  const char* st = swRunning ? "RUNNING" : (e > 0 ? "PAUSED" : "READY");
  display.setCursor(SCREEN_W - (int)strlen(st) * 6, 0);
  display.print(st);

  drawCentered(buf, 14, 3);

  if (nLaps > 0) {
    uint32_t prev = nLaps > 1 ? swLaps[nLaps - 2] : 0;
    char lap[16], line[28];
    fmtStopwatch(lap, sizeof(lap), swLaps[nLaps - 1] - prev);
    snprintf(line, sizeof(line), "LAP %d  %s", nLaps, lap);
    drawCentered(line, 50, 1);
  } else if (!swRunning && e == 0) {
    drawCentered("start on web page", 50, 1);
  }
  display.display();
}

void stopwatchLoop(uint32_t now) {
  if (!needFull && !(swRunning && now - viewFrameAt >= 50)) return;
  needFull = false; viewFrameAt = now;
  drawStopwatch(now);
}

// ============================================================================
//                                POMODORO
// ============================================================================
uint32_t phaseMs(int ph) {
  if (ph == PH_FOCUS) return (uint32_t)focusMin * 60000UL;
  if (ph == PH_SHORT) return (uint32_t)shortMin * 60000UL;
  return (uint32_t)longMin * 60000UL;
}
uint32_t pomoRemainNow(uint32_t now) {
  if (!pomoRunning) return pomoRemain;
  return pomoEndAt > now ? pomoEndAt - now : 0;
}
void pomoReset() {
  pomoRunning = false; pomoPhase = PH_FOCUS; pomoCycle = 0;
  pomoRemain = phaseMs(PH_FOCUS);
  needFull = true;
}
void pomoStart(uint32_t now) {
  if (pomoRunning) return;
  pomoRunning = true; pomoEndAt = now + pomoRemain;
  needFull = true;
}
void pomoPause(uint32_t now) {
  if (!pomoRunning) return;
  pomoRemain = pomoEndAt > now ? pomoEndAt - now : 0;
  pomoRunning = false;
  needFull = true;
}
// Move to the next phase (focus -> break -> focus ...). alertUser = flash the screen.
void pomoNext(bool alertUser) {
  if (pomoPhase == PH_FOCUS) {
    pomoCycle++;
    if (pomoCycle >= 4) { pomoPhase = PH_LONG; pomoCycle = 0; }
    else pomoPhase = PH_SHORT;
    if (alertUser) {
      startAlert("BREAK!", pomoPhase == PH_LONG ? "Long break - relax" : "Short break - stretch");
      onPomoDone();
    }
  } else {
    pomoPhase = PH_FOCUS;
    if (alertUser) startAlert("FOCUS!", "Back to work");
  }
  pomoRemain = phaseMs(pomoPhase);
  if (pomoRunning) pomoEndAt = millis() + pomoRemain;
  needFull = true;
}
void pomoTick(uint32_t now) {
  if (pomoRunning && now >= pomoEndAt) pomoNext(true);
}

void drawPomo(uint32_t now) {
  uint32_t rem = pomoRemainNow(now), total = phaseMs(pomoPhase);
  uint32_t sec = (rem + 999) / 1000;
  char buf[12];
  snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  if (pomoPhase == PH_FOCUS) display.printf("FOCUS %d/4", pomoCycle + 1);
  else display.print(pomoPhase == PH_LONG ? "LONG BREAK" : "SHORT BREAK");
  const char* st = pomoRunning ? "RUNNING" : "PAUSED";
  display.setCursor(SCREEN_W - (int)strlen(st) * 6, 0);
  display.print(st);

  int size = ((int)strlen(buf) * 24 <= SCREEN_W) ? 4 : 3;
  drawCentered(buf, size == 4 ? 13 : 17, size);

  uint32_t elapsed = (rem >= total) ? 0 : (total - rem);
  int fill = total ? (int)((uint64_t)elapsed * 124 / total) : 0;
  display.drawRect(0, 52, SCREEN_W, 10, SSD1306_WHITE);
  display.fillRect(2, 54, fill, 6, SSD1306_WHITE);
  display.display();
}

void pomoLoop(uint32_t now) {
  if (!needFull && now - viewFrameAt < 250) return;
  needFull = false; viewFrameAt = now;
  drawPomo(now);
}

// ============================================================================
//                                  ALERT
// ============================================================================
void startAlert(const String& l1, const String& l2) {
  alertActive = true; alertT0 = millis(); alertL1 = l1; alertL2 = l2; alertLastInv = -1;
}
void dismissAlert() {
  if (!alertActive) return;
  alertActive = false;
  forceView(view);                                          // redraw whatever was on screen
}
void alertLoop(uint32_t now) {
  if (now - alertT0 > ALERT_MS) { dismissAlert(); return; }
  int inv = (int)(((now - alertT0) / 350) % 2);
  if (inv == alertLastInv) return;
  alertLastInv = inv;
  display.clearDisplay();
  if (inv == 0) display.fillRect(0, 0, SCREEN_W, SCREEN_H, SSD1306_WHITE);
  display.setTextColor(inv == 0 ? SSD1306_BLACK : SSD1306_WHITE);
  drawCentered(alertL1.c_str(), 14, 2);
  drawCentered(alertL2.c_str(), 40, 1);
  display.display();
}

// ============================================================================
//                           VIEW MANAGER + TOUCH
// ============================================================================
void forceView(View v) {
  view = v;
  viewEnteredAt = millis();
  needFull = true;
  switch (v) {
    case V_EYES:    enterEyes(); break;
    case V_LYRICS:  enterLyrics(); break;
    case V_SPOTIFY: spMarqueeT0 = millis(); break;
    case V_CLOCK:   lastClockSec = -9; break;
    case V_CAL:     lastCalMin = -9; break;
    default: break;
  }
}
void setView(View v) { if (v != view) forceView(v); }

void setSel(View v) {
  sel = v;
  if (v == V_AUTO) { overlayBlockedUntil = 0; forceView(home); }
  else forceView(v);
}

// Auto mode: lyrics / Spotify pop up while music plays, otherwise show the home screen.
void autoTick(uint32_t now) {
  if (sel != V_AUTO) return;
  View want = home;
  if (now >= overlayBlockedUntil) {
    if (lastLyricAt && now - lastLyricAt < AUTO_RETURN_MS) want = V_LYRICS;
    else if (spPlaying && spLastPlayingAt && now - spLastPlayingAt < 6000) want = V_SPOTIFY;
  }
  setView(want);
}

void onTap(uint32_t now) {
  if (alertActive) { dismissAlert(); return; }
  moodBored = 0;
  moodAnnoy += 14;
  sel = V_AUTO;
  home = (view == V_EYES) ? V_CLOCK : V_EYES;               // tap: eyes <-> clock
  saveSettings();
  overlayBlockedUntil = now + 15000;                        // let it show the clock/eyes even if music is playing
  forceView(home);
  if (home == V_EYES && moodAnnoy > 55) react(EX_ANGRY, 3500);   // poked too much!
}

void onLongPress(uint32_t now) {
  (void)now;
  if (alertActive) { dismissAlert(); return; }
  onPet();
}

void touchBegin() {
#if TOUCH_DIGITAL
  pinMode(TOUCH_PIN, TOUCH_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);   // a known idle level if nothing is connected
#else
  float sum = 0;
  for (int i = 0; i < 30; i++) { sum += (float)touchRead(TOUCH_PIN); delay(10); }
  touchBase = sum / 30.0f;
#endif
}

// true while the sensor is being touched
bool touchSample() {
#if TOUCH_DIGITAL
  return digitalRead(TOUCH_PIN) == (TOUCH_ACTIVE_HIGH ? HIGH : LOW);
#else
  float v = (float)touchRead(TOUCH_PIN);
  bool touched = fabsf(v - touchBase) > touchBase * TOUCH_SENS;
  if (!touched) touchBase += (v - touchBase) * 0.01f;       // follow slow drift while untouched
  return touched;
#endif
}

void touchLoop(uint32_t now) {
  if (now - lastTouchPoll < 20) return;
  lastTouchPoll = now;
  bool raw = touchSample();
  touchRawNow = raw;

  if (raw != touchDown) {                                   // debounce: 3 equal reads in a row
    if (++touchCnt >= 3) {
      touchCnt = 0;
      touchDown = raw;
      if (touchDown) { touchDownAt = now; petFired = false; }
      else if (!petFired) { touchTaps++; Serial.println("touch: tap"); onTap(now); }
    }
  } else {
    touchCnt = 0;
  }
  if (touchDown && !petFired && now - touchDownAt >= LONG_PRESS_MS) {
    petFired = true;
    touchHolds++;
    Serial.println("touch: hold");
    onLongPress(now);
  }
}

void housekeeping(uint32_t now) {
  (void)now;
#if NIGHT_DIM
  int h = currentHour();
  display.dim(h >= 0 && (h >= 23 || h < 6));
#endif
}

// ============================================================================
//                                WEB PAGE
// ============================================================================
const char PAGE[] PROGMEM = R"rawliteral(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Mochi</title>
<style>
*{box-sizing:border-box}
body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:#10131a;color:#e8ecf3;margin:0 auto;padding:20px;max-width:480px}
h1{font-size:24px;margin:0}
h1 small{font-size:12px;color:#3b82f6;margin-left:6px}
#st{color:#8b95a7;font-size:13px;margin:2px 0 4px}
h2{font-size:12px;letter-spacing:.09em;text-transform:uppercase;color:#8b95a7;margin:26px 0 8px}
.grid{display:grid;gap:8px}
.g2{grid-template-columns:repeat(2,1fr)}.g3{grid-template-columns:repeat(3,1fr)}.g4{grid-template-columns:repeat(4,1fr)}.g5{grid-template-columns:repeat(5,1fr)}
button{background:#1b2130;color:#e8ecf3;border:1px solid #2a3245;border-radius:12px;padding:10px 2px;font-size:12px;cursor:pointer}
button:active{transform:scale(.97)}
button.on{background:#3b82f6;border-color:#3b82f6;color:#fff}
.e{display:block;font-size:22px;margin-bottom:2px}
.hint{color:#8b95a7;font-size:12px;margin-top:8px;line-height:1.45}
.card{background:#161b27;border:1px solid #232b3d;border-radius:14px;padding:14px;margin-top:8px}
.big{font-size:38px;font-weight:600;text-align:center;font-variant-numeric:tabular-nums;margin:4px 0 10px}
.mid{text-align:center;color:#8b95a7;font-size:13px;margin-bottom:6px}
.bar{display:flex;align-items:center;gap:8px;font-size:12px;color:#8b95a7;margin:5px 0}
.bar span{width:58px}.bar i{flex:1;height:8px;background:#232b3d;border-radius:4px;overflow:hidden}
.bar b{display:block;height:100%;background:#3b82f6;width:0;transition:width .6s}
.tim{display:flex;align-items:center;gap:10px;margin-top:8px;font-size:13px}.tim span{width:64px;color:#8b95a7}.tim b{flex:1;text-align:center;font-variant-numeric:tabular-nums}.tim button{width:46px;padding:8px 0;font-size:16px}
.cfg{display:flex;gap:8px;margin-top:10px;align-items:center;font-size:12px;color:#8b95a7}
.cfg input{width:100%;background:#10131a;color:#e8ecf3;border:1px solid #2a3245;border-radius:8px;padding:8px;font-size:14px}
#laps{font-size:12px;color:#8b95a7;margin-top:8px;text-align:center;line-height:1.6}
#npt{font-size:16px;font-weight:600}
</style></head><body>
<h1>Mochi<small>V2.1</small></h1>
<div id="st">Connecting...</div>

<h2>Screen</h2>
<div class="grid g3" id="screens"></div>
<div class="hint">Auto shows lyrics / Spotify while music plays, otherwise your home screen. Touch: tap = clock &#8596; eyes, hold = pet Mochi.</div>
<div class="hint" id="tch">Touch sensor: ...</div>

<h2>Emotions</h2>
<div class="card">
<div id="bars"></div>
<div class="grid g5" id="ex" style="margin-top:12px"></div>
<div class="grid g2" style="margin-top:8px">
<button id="auto">&#129504; Auto emotions</button><button id="pet">&#128150; Pet</button>
</div>
</div>

<h2>Clock</h2>
<div class="card"><div class="big" id="clk">--:--</div>
<div class="grid g2"><button id="h24">24-hour</button><button id="cal">Show calendar</button></div></div>

<h2>Stopwatch</h2>
<div class="card"><div class="big" id="sw">00:00.0</div>
<div class="grid g3"><button id="swt">Start</button><button id="swl">Lap</button><button id="swr">Reset</button></div>
<div id="laps"></div></div>

<h2>Pomodoro</h2>
<div class="card"><div class="big" id="pm">25:00</div><div class="mid" id="pph"></div>
<div class="grid g3"><button id="pt">Start</button><button id="pn">Skip</button><button id="pr">Reset</button></div>
<div class="cfg">Focus<input id="cf" type="number" min="1" max="180">Short<input id="cs" type="number" min="1" max="60">Long<input id="cl" type="number" min="1" max="120"><button id="csv" style="padding:9px 12px">Save</button></div></div>

<h2>Now playing</h2>
<div class="card"><div id="npt">-</div><div class="hint" id="npa" style="margin-top:2px"></div>
<button id="spl" style="width:100%;margin-top:10px;padding:12px">Spotify lyrics</button>
<div class="hint">When ON, Auto shows synced lyrics for Spotify songs (Spotify Web Player in Chrome). OFF = only the Now Playing card.</div>
<div class="hint" style="margin-top:14px">Lyrics timing: lyrics too late? press +. Too early? press &minus;. It applies within a few seconds.</div>
<div class="tim"><span>YouTube</span><button id="yd">&minus;</button><b id="yv">+0.3 s</b><button id="yu">+</button></div>
<div class="tim"><span>Spotify</span><button id="sd">&minus;</button><b id="sv">+0.3 s</b><button id="su">+</button></div>
<div class="hint" id="th"></div></div>

<script>
var $=function(i){return document.getElementById(i)};
var SC=[["auto","&#10024;","Auto"],["eyes","&#128064;","Eyes"],["clock","&#128339;","Clock"],["calendar","&#128197;","Calendar"],["lyrics","&#127908;","Lyrics"],["spotify","&#127911;","Spotify"],["thumb","&#128444;","Thumbnail"],["stopwatch","&#9201;","Stopwatch"],["pomodoro","&#127813;","Pomodoro"]];
var EX=[["normal","&#128528;"],["happy","&#128522;"],["sad","&#128546;"],["angry","&#128544;"],["sleepy","&#128564;"],["surprised","&#128558;"],["love","&#128525;"],["wink","&#128521;"],["excited","&#129321;"],["bored","&#128529;"]];
var MD=[["happy","Happy"],["energy","Energy"],["love","Love"],["bored","Bored"]];
function mk(id,arr,fn){arr.forEach(function(x){var b=document.createElement('button');b.dataset.k=x[0];b.innerHTML=(x[1]?'<span class="e">'+x[1]+'</span>':'')+(x[2]||x[0]);b.onclick=function(){fn(x[0])};$(id).appendChild(b)})}
mk('screens',SC,function(k){api('/api/screen?v='+k)});
mk('ex',EX,function(k){api('/api/expr?e='+k)});
MD.forEach(function(m){var d=document.createElement('div');d.className='bar';d.innerHTML='<span>'+m[1]+'</span><i><b id="m_'+m[0]+'"></b></i>';$('bars').appendChild(d)});
var S={};
$('auto').onclick=function(){api('/api/emotions?on='+(S.autoExpr?0:1))};
$('pet').onclick=function(){api('/api/pet')};
$('h24').onclick=function(){api('/api/cfg?h24='+(S.h24?0:1))};
$('spl').onclick=function(){api('/api/cfg?splyrics='+(S.spLyrics?0:1))};
function adj(k,d){api('/api/cfg?off'+k+'='+(S.off[k]+d))}
$('yd').onclick=function(){adj('yt',-100)};$('yu').onclick=function(){adj('yt',100)};
$('sd').onclick=function(){adj('sp',-100)};$('su').onclick=function(){adj('sp',100)};
function fo(ms){return(ms>=0?'+':'')+(ms/1000).toFixed(1)+' s'}
$('cal').onclick=function(){api('/api/screen?v=calendar')};
$('swt').onclick=function(){api('/api/sw?a=toggle')};
$('swl').onclick=function(){api('/api/sw?a=lap')};
$('swr').onclick=function(){api('/api/sw?a=reset')};
$('pt').onclick=function(){api('/api/pomo?a=toggle')};
$('pn').onclick=function(){api('/api/pomo?a=skip')};
$('pr').onclick=function(){api('/api/pomo?a=reset')};
$('csv').onclick=function(){api('/api/cfg?focus='+$('cf').value+'&short='+$('cs').value+'&long='+$('cl').value)};
function p2(n){return('0'+n).slice(-2)}
function mmss(ms){var s=Math.floor(ms/1000);return p2(Math.floor(s/60))+':'+p2(s%60)}
function sw(ms){return mmss(ms)+'.'+Math.floor(ms%1000/100)}
function render(s){
  S=s;
  $('st').textContent='Showing: '+s.view+'  |  Screen: '+s.sel+'  |  Home: '+s.home;
  document.querySelectorAll('#screens button').forEach(function(b){b.classList.toggle('on',b.dataset.k===s.sel)});
  document.querySelectorAll('#ex button').forEach(function(b){b.classList.toggle('on',b.dataset.k===s.expr)});
  MD.forEach(function(m){$('m_'+m[0]).style.width=Math.round(s.mood[m[0]])+'%'});
  $('auto').classList.toggle('on',s.autoExpr);
  $('h24').classList.toggle('on',s.h24);
  $('spl').classList.toggle('on',s.spLyrics);
  $('yv').textContent=fo(s.off.yt);$('sv').textContent=fo(s.off.sp);
  $('spl').textContent='Spotify lyrics: '+(s.spLyrics?'ON':'OFF');
  $('tch').textContent='Touch sensor: '+(s.touch.on?'TOUCHED':'idle')+'  |  taps '+s.touch.taps+'  |  holds '+s.touch.holds+'  (counters move when the sensor works)';
  $('clk').textContent=s.time;
  $('sw').textContent=sw(s.sw.ms);
  $('swt').textContent=s.sw.run?'Stop':'Start';
  $('laps').innerHTML=s.sw.laps.map(function(l,i){return 'Lap '+(i+1)+': '+sw(l)}).join('<br>');
  $('pm').textContent=mmss(Math.ceil(s.pm.rem/1000)*1000);
  $('pph').textContent=(s.pm.phase==='focus'?'Focus '+(s.pm.cycle+1)+'/4':(s.pm.phase==='long'?'Long break':'Short break'))+(s.pm.run?' - running':' - paused');
  $('pt').textContent=s.pm.run?'Pause':'Start';
  var a=document.activeElement;
  if(!a||a.tagName!=='INPUT'){$('cf').value=s.pm.focus;$('cs').value=s.pm.short;$('cl').value=s.pm.long}
  $('npt').textContent=s.np.title||'Nothing playing';
  $('npa').textContent=s.np.title?(s.np.artist+(s.np.playing?'  (playing)':'  (paused)')):'Open Spotify in Chrome with the Mochi extension.';
  $('th').textContent=s.thumb?'YouTube thumbnail ready.':'No YouTube thumbnail yet - play a YouTube video.';
}
function api(p){
  fetch(p).then(function(r){return r.json()}).then(render).catch(function(){$('st').textContent='Cannot reach Mochi...'});
}
api('/api/state');
setInterval(function(){api('/api/state')},1000);
</script></body></html>)rawliteral";

void sendState() {
  uint32_t now = millis();
  String s;
  s.reserve(1400);
  char b[96];

  s += "{\"sel\":\"";  s += VIEW_NAME[sel];
  s += "\",\"view\":\""; s += VIEW_NAME[view];
  s += "\",\"home\":\""; s += VIEW_NAME[home];
  s += "\",\"expr\":\""; s += EX_NAME[curExpr];
  s += "\",\"autoExpr\":"; s += autoExpr ? "true" : "false";
  s += ",\"h24\":"; s += use24h ? "true" : "false";
  s += ",\"spLyrics\":"; s += spLyrics ? "true" : "false";
  snprintf(b, sizeof(b), ",\"off\":{\"yt\":%d,\"sp\":%d}", lyOffYt, lyOffSp);
  s += b;
  snprintf(b, sizeof(b), ",\"touch\":{\"on\":%d,\"taps\":%lu,\"holds\":%lu}", touchRawNow ? 1 : 0, (unsigned long)touchTaps, (unsigned long)touchHolds);
  s += b;

  snprintf(b, sizeof(b), ",\"mood\":{\"happy\":%d,\"energy\":%d,\"love\":%d,\"bored\":%d}",
           (int)moodHappy, (int)moodEnergy, (int)moodLove, (int)moodBored);
  s += b;

  struct tm t;
  if (getTm(&t)) {
    int h = t.tm_hour;
    if (!use24h) { h = h % 12; if (h == 0) h = 12; }
    snprintf(b, sizeof(b), ",\"time\":\"%02d:%02d\"", h, t.tm_min);
  } else snprintf(b, sizeof(b), ",\"time\":\"--:--\"");
  s += b;

  snprintf(b, sizeof(b), ",\"sw\":{\"run\":%s,\"ms\":%lu,\"laps\":[", swRunning ? "true" : "false", (unsigned long)swElapsed(now));
  s += b;
  for (int i = 0; i < nLaps; i++) {
    uint32_t prev = i > 0 ? swLaps[i - 1] : 0;
    snprintf(b, sizeof(b), "%s%lu", i ? "," : "", (unsigned long)(swLaps[i] - prev));
    s += b;
  }
  s += "]}";

  const char* ph = pomoPhase == PH_FOCUS ? "focus" : (pomoPhase == PH_SHORT ? "short" : "long");
  snprintf(b, sizeof(b), ",\"pm\":{\"run\":%s,\"phase\":\"%s\",\"cycle\":%d,\"rem\":%lu,\"focus\":%d,\"short\":%d,\"long\":%d}",
           pomoRunning ? "true" : "false", ph, pomoCycle, (unsigned long)pomoRemainNow(now), focusMin, shortMin, longMin);
  s += b;

  bool fresh = spTitle.length() > 0 && (now - spUpdatedAt < 20000);
  s += ",\"np\":{\"title\":\"";  s += fresh ? jsonEsc(spTitle) : String("");
  s += "\",\"artist\":\"";        s += fresh ? jsonEsc(spArtist) : String("");
  s += "\",\"playing\":";         s += (fresh && spPlaying) ? "true" : "false";
  s += "},\"thumb\":";            s += thumbValid ? "true" : "false";
  s += "}";

  server.send(200, "application/json", s);
}

void handleRoot() { server.send_P(200, "text/html", PAGE); }

void handleScreen() {
  String v = server.arg("v");
  for (int i = 0; i < V_COUNT; i++) if (v == VIEW_NAME[i]) { setSel((View)i); break; }
  sendState();
}

void showEyesForWebAction() { if (view != V_EYES) setSel(V_EYES); }

void handleExpr() {
  String e = server.arg("e");
  for (int i = 0; i < EX_COUNT; i++) {
    if (e == EX_NAME[i]) {
      autoExpr = false;                       // choosing by hand pauses auto emotions
      showEyesForWebAction();
      requestExpr(i);
      break;
    }
  }
  sendState();
}

void handleEmotions() {
  autoExpr = (server.arg("on") == "1");
  nextExprAt = millis() + 1500;
  sendState();
}

void handlePet() {
  showEyesForWebAction();
  onPet();
  sendState();
}

void handleCfg() {
  if (server.hasArg("h24")) use24h = (server.arg("h24") == "1");
  if (server.hasArg("offyt")) lyOffYt = constrain((int)server.arg("offyt").toInt(), -3000, 3000);
  if (server.hasArg("offsp")) lyOffSp = constrain((int)server.arg("offsp").toInt(), -3000, 3000);
  if (server.hasArg("splyrics")) {
    spLyrics = (server.arg("splyrics") == "1");
    if (!spLyrics && lyricFromSpotify) lastLyricAt = 0;      // drop Spotify lyrics right away
  }
  if (server.hasArg("focus")) focusMin = constrain((int)server.arg("focus").toInt(), 1, 180);
  if (server.hasArg("short")) shortMin = constrain((int)server.arg("short").toInt(), 1, 60);
  if (server.hasArg("long"))  longMin  = constrain((int)server.arg("long").toInt(), 1, 120);
  if (!pomoRunning) pomoRemain = phaseMs(pomoPhase);
  saveSettings();
  needFull = true;
  lastClockSec = -9;
  sendState();
}

void handleSw() {
  String a = server.arg("a");
  uint32_t now = millis();
  if (a == "toggle" || a == "start" || a == "stop") {
    if (a == "toggle" || (a == "start" && !swRunning) || (a == "stop" && swRunning)) swToggle(now);
    if (swRunning && view != V_STOPWATCH) setSel(V_STOPWATCH);
  } else if (a == "lap") swLap(now);
  else if (a == "reset") swReset();
  sendState();
}

void handlePomo() {
  String a = server.arg("a");
  uint32_t now = millis();
  if (a == "toggle") { if (pomoRunning) pomoPause(now); else pomoStart(now); }
  else if (a == "start") pomoStart(now);
  else if (a == "pause") pomoPause(now);
  else if (a == "skip") pomoNext(false);
  else if (a == "reset") pomoReset();
  if (pomoRunning && view != V_POMO) setSel(V_POMO);
  sendState();
}

// Every reply to the extension carries the lyric timing set on the web page: "ok|<YouTube ms>|<Spotify ms>"
String offReply(const char* msg) {
  return String(msg) + "|" + String(lyOffYt) + "|" + String(lyOffSp);
}

// Chrome extension -> one lyric line (same as before). ?ms= is how long the line lasts.
void handleLyric() {
  String body = server.arg("plain");
  body.trim();
  int ms = server.hasArg("ms") ? (int)server.arg("ms").toInt() : 3000;
  bool fromSp = (server.arg("src") == "spotify");
  uint32_t now = millis();

  if (fromSp && !spLyrics) { server.send(200, "text/plain", offReply("off")); return; }

  if (body.length()) {
    if (!musicActive(now)) onMusicStart();                  // music just started: Mochi gets excited
    lastLyricAt = now;
    lyricFromSpotify = fromSp;
  }
  bool accept = (sel == V_LYRICS) || (sel == V_AUTO && now >= overlayBlockedUntil);
  if (!accept) { server.send(200, "text/plain", offReply("ignored")); return; }

  if (view != V_LYRICS) {
    if (body.length() == 0) { server.send(200, "text/plain", offReply("ok")); return; }
    setView(V_LYRICS);
  }
  startLyric(body, ms);
  server.send(200, "text/plain", offReply("ok"));
}

// Chrome extension -> Spotify now playing: /np?t=title&a=artist&p=posSec&d=durSec&pl=1
void handleNp() {
  String t = asciiOnly(server.arg("t")), a = asciiOnly(server.arg("a"));
  int p = server.hasArg("p") ? (int)server.arg("p").toInt() : -1;
  int d = server.hasArg("d") ? (int)server.arg("d").toInt() : -1;
  bool pl = (server.arg("pl") == "1");
  uint32_t now = millis();

  bool newTrack = (t != spTitle || a != spArtist);
  if (pl && (!spPlaying || newTrack)) onMusicStart();
  spTitle = t; spArtist = a;
  spPosMs = p >= 0 ? p * 1000 : -1;
  spDurMs = d > 0 ? d * 1000 : -1;
  spPlaying = pl;
  spUpdatedAt = now;
  if (pl) spLastPlayingAt = now;
  if (newTrack) spMarqueeT0 = now;
  server.send(200, "text/plain", offReply("ok"));
}

// Chrome extension -> YouTube thumbnail as 2048 hex chars (1024 bytes of OLED pixels).
void handleThumb() {
  String b = server.arg("plain");
  if (b.length() < sizeof(thumbBuf) * 2) { server.send(400, "text/plain", "bad length"); return; }
  for (unsigned int i = 0; i < sizeof(thumbBuf); i++) {
    int hi = hexv(b[2 * i]), lo = hexv(b[2 * i + 1]);
    if (hi < 0 || lo < 0) { server.send(400, "text/plain", "bad hex"); return; }
    thumbBuf[i] = (uint8_t)((hi << 4) | lo);
  }
  thumbValid = true;
  thumbDirty = true;
  server.send(200, "text/plain", "ok");
}

// ============================================================================
//                                SETUP / LOOP
// ============================================================================
void setup() {
  Serial.begin(115200);
  loadSettings();

  Wire.begin(SDA_PIN, SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR, true, false)) {
    Serial.println("OLED not found - check wiring/address");
    while (true) delay(1000);
  }
  display.setTextWrap(false);
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Mochi V2.1");
  display.println("Connecting WiFi...");
  display.display();

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(300);
  WiFi.setSleep(false);
  MDNS.begin("mochi");
  configTime(TZ_OFFSET_SEC, 0, "pool.ntp.org", "time.google.com");
  touchBegin();                                             // (non-digital mode: don't touch the sensor while booting)

  server.on("/", handleRoot);
  server.on("/api/state", sendState);
  server.on("/api/screen", handleScreen);
  server.on("/api/expr", handleExpr);
  server.on("/api/emotions", handleEmotions);
  server.on("/api/pet", handlePet);
  server.on("/api/cfg", handleCfg);
  server.on("/api/sw", handleSw);
  server.on("/api/pomo", handlePomo);
  server.on("/lyric", HTTP_POST, handleLyric);
  server.on("/np", HTTP_POST, handleNp);
  server.on("/thumb", HTTP_POST, handleThumb);
  server.begin();

  String ip = WiFi.localIP().toString();
  Serial.println(ip);
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Mochi V2.1 ready!");
  display.println();
  display.println("Open in browser:");
  display.println("http://" + ip);
  display.println("or http://mochi.local");
  display.display();
  delay(6000);

  pomoReset();
  sel = V_AUTO;
  forceView(home);
}

void loop() {
  server.handleClient();
  uint32_t now = millis();

  touchLoop(now);
  if (now - lastMoodTick >= 1000) { lastMoodTick = now; moodTick(now); housekeeping(now); }
  pomoTick(now);

  if (alertActive) { alertLoop(now); return; }
  autoTick(now);

  switch (view) {
    case V_EYES:      eyesLoop(now); break;
    case V_CLOCK:     clockLoop(now); break;
    case V_CAL:       calLoop(now); break;
    case V_LYRICS:    lyricsLoop(now); break;
    case V_SPOTIFY:   spotifyLoop(now); break;
    case V_THUMB:     thumbLoop(now); break;
    case V_STOPWATCH: stopwatchLoop(now); break;
    case V_POMO:      pomoLoop(now); break;
    default: break;
  }
}
