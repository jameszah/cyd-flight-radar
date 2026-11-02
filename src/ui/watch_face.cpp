#include "ui/watch_face.h"

#include <Arduino.h>
#include <math.h>

#include <algorithm>
#include <sys/time.h>
#include <time.h>

#include "config.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "utils_time.h"

namespace ui::watch_face {
namespace {

// 1960s moonwatch-style chronograph: black dial, tachymeter bezel, running seconds at 9,
// 30-minute counter at 3, 12-hour counter at 6, central chronograph seconds hand.
// The face is drawn in horizontal bands through one small sprite (a full-face buffer doesn't
// fit next to the radar frame on the boards without PSRAM), and each frame only redraws the
// bands that a moved hand covered before or covers now.

constexpr int kBandH = 16;
constexpr int kMaxBands = 32;
constexpr float kDegToRad = 0.01745329252f;

constexpr uint16_t kColorCase = lgfx::color565(170, 172, 176);
constexpr uint16_t kColorBezel = lgfx::color565(14, 14, 16);
constexpr uint16_t kColorBezelText = lgfx::color565(235, 235, 235);
constexpr uint16_t kColorFlange = lgfx::color565(70, 70, 74);
constexpr uint16_t kColorDial = lgfx::color565(6, 6, 8);
constexpr uint16_t kColorPrint = lgfx::color565(240, 240, 240);
constexpr uint16_t kColorFinePrint = lgfx::color565(150, 150, 150);
constexpr uint16_t kColorSubdialRing = lgfx::color565(110, 110, 114);
constexpr uint16_t kColorSubdial = lgfx::color565(232, 230, 222);  // off-white "panda" subdials
constexpr uint16_t kColorSubPrint = lgfx::color565(20, 20, 22);
constexpr uint16_t kColorSubFinePrint = lgfx::color565(100, 100, 104);
constexpr uint16_t kColorSubHand = lgfx::color565(15, 15, 18);
constexpr uint16_t kColorHand = lgfx::color565(250, 250, 250);
constexpr uint16_t kColorHandEdge = lgfx::color565(120, 120, 125);
constexpr uint16_t kColorHourHand = lgfx::color565(230, 40, 35);
constexpr uint16_t kColorMinuteHand = lgfx::color565(40, 110, 255);
constexpr float kHandPointerFrac = 0.22f;  // coloured part of the hour / minute hand tips

// Tachymeter: units per hour for an event timed by the chronograph seconds hand,
// printed at 3600 / value seconds.
const int kTachyLabels[] = {500, 400, 300, 250, 200, 175, 150, 125, 110, 100, 90, 80, 75, 70, 65, 60};

enum HandId { kHour, kMinute, kChrono, kRunSec, kChronoMin, kChronoHour, kHandCount };

struct Hand {
  float px, py;    // pivot
  float tip, tail;  // lengths from the pivot
  float width;     // half-width at the pivot, for the dirty-row margin
};

struct Geometry {
  int cx, cy, r;           // face centre and case radius
  int bezel_out, bezel_in;  // tachymeter ring
  int dial;                // dial radius
  int sub_r, sub_dist;     // subdial radius and centre distance
  int top, bands;          // first face row, band count
};

bool s_active = false;
unsigned long s_last_frame_ms = 0;
unsigned long s_chrono_start_ms = 0;  // when it last started running
unsigned long s_chrono_accum_ms = 0;  // time banked by earlier runs
bool s_chrono_running = false;

// Pusher buttons in the strip under the face (Start/Stop, Reset, Exit).
enum Button { kStartStop, kReset, kExit, kButtonCount };
struct ButtonRect {
  int x, y, w, h;
};
ButtonRect s_buttons[kButtonCount];
LGFX_Sprite s_band(&tft);
Geometry g;
Hand s_hands[kHandCount];
float s_angle[kHandCount];
float s_prev_angle[kHandCount];
bool s_dirty[kMaxBands];
int s_oy = 0;  // screen row of the band being drawn
const lgfx::IFont* s_small_font = &fonts::Font0;

// Strips around the face, in order of priority when the screen is short: buttons, interval
// timer, then the time of day at the top. Each is 0 when it doesn't fit. On a 240x320 screen
// they use the compact sizes and smaller fonts, and the face shrinks to make room.
struct StripSizes {
  int buttons, timer, time;
  const lgfx::IFont* time_font;
  const lgfx::IFont* zone_font;
  const lgfx::IFont* timer_font;
};
const StripSizes kStrips = {52, 40, 36, &fonts::FreeSansBold18pt7b, &fonts::FreeSansBold12pt7b,
                            &fonts::FreeSansBold18pt7b};
const StripSizes kCompactStrips = {34, 28, 26, &fonts::FreeSansBold12pt7b,
                                   &fonts::FreeSansBold9pt7b, &fonts::FreeSansBold12pt7b};
constexpr int kMinFaceR = 80;
const StripSizes* s_strips = &kStrips;
int s_buttons_h = 0;
int s_timer_h = 0;
int s_time_h = 0;
int s_pad = 0;
char s_shown_time[24];   // last drawn, so they're only redrawn on a change
char s_shown_timer[22];

float px(float r, float deg) { return g.cx + r * sinf(deg * kDegToRad); }
float py(float r, float deg) { return g.cy - r * cosf(deg * kDegToRad); }

bool rowsVisible(float y0, float y1) {
  if (y0 > y1) std::swap(y0, y1);
  return y1 >= s_oy - 2 && y0 <= s_oy + kBandH + 2;
}

// Line from radius r0 to r1 at angle deg around (ox, oy), in screen coordinates.
void radial(float ox, float oy, float r0, float r1, float deg, float half_w, uint16_t color) {
  const float s = sinf(deg * kDegToRad);
  const float c = cosf(deg * kDegToRad);
  const float y0 = oy - r0 * c;
  const float y1 = oy - r1 * c;
  if (!rowsVisible(y0, y1)) return;
  const float x0 = ox + r0 * s;
  const float x1 = ox + r1 * s;
  if (half_w <= 0.0f) {
    s_band.drawLine(lroundf(x0), lroundf(y0) - s_oy, lroundf(x1), lroundf(y1) - s_oy, color);
  } else {
    s_band.drawWideLine(lroundf(x0), lroundf(y0) - s_oy, lroundf(x1), lroundf(y1) - s_oy, half_w, color);
  }
}

void text(const char* str, float x, float y, uint16_t color) {
  if (!rowsVisible(y - 8, y + 8)) return;
  s_band.setTextColor(color);
  s_band.drawString(str, lroundf(x), lroundf(y) - s_oy);
}

uint16_t blend565(uint16_t fg, uint16_t bg, float a) {
  const int r = lroundf(((fg >> 11) & 31) * a + ((bg >> 11) & 31) * (1 - a));
  const int gr = lroundf(((fg >> 5) & 63) * a + ((bg >> 5) & 63) * (1 - a));
  const int b = lroundf((fg & 31) * a + (bg & 31) * (1 - a));
  return (r << 11) | (gr << 5) | b;
}

// Anti-aliased filled disc over a known background colour, row by row inside the band.
// (fillSmoothCircle painted whole bands in the disc colour when the disc reached far
// outside the 16-row sprite.)
void disc(int cx, int cy, int r, uint16_t color, uint16_t bg) {
  for (int row = 0; row < kBandH; ++row) {
    const float dy = s_oy + row - cy;
    if (fabsf(dy) > r + 0.5f) continue;
    const float outer = sqrtf((r + 0.5f) * (r + 0.5f) - dy * dy);
    const float inner2 = (r - 0.5f) * (r - 0.5f) - dy * dy;
    const int inner = inner2 > 0 ? int(floorf(sqrtf(inner2))) : -1;
    if (inner >= 0) s_band.drawFastHLine(cx - inner, row, 2 * inner + 1, color);
    for (int dx = inner + 1; dx <= int(ceilf(outer)); ++dx) {
      const float cover = r + 0.5f - sqrtf(dx * dx + dy * dy);
      if (cover <= 0) break;
      const uint16_t c = cover >= 1 ? color : blend565(color, bg, cover);
      s_band.drawPixel(cx - dx, row, c);
      s_band.drawPixel(cx + dx, row, c);
    }
  }
}

void circle(float x, float y, int r, uint16_t color) {
  if (!rowsVisible(y - r, y + r)) return;
  s_band.drawCircle(lroundf(x), lroundf(y) - s_oy, r, color);
}

void drawBezel() {
  disc(g.cx, g.cy, g.r, kColorCase, TFT_BLACK);
  disc(g.cx, g.cy, g.bezel_out, kColorBezel, kColorCase);

  const float tick_in = g.bezel_in + 1;
  const float num_r = (tick_in + 4 + g.bezel_out) / 2.0f;
  // Minor ticks every 5 units up to 100, 10 up to 200, 50 above.
  for (int v = 60; v <= 500; v += (v < 100 ? 5 : v < 200 ? 10 : 50)) {
    radial(g.cx, g.cy, tick_in, tick_in + 2, 3600.0f / v * 6.0f, 0, kColorBezelText);
  }
  s_band.setFont(s_small_font);
  s_band.setTextDatum(textdatum_t::middle_center);
  char buf[8];
  for (int v : kTachyLabels) {
    const float deg = 3600.0f / v * 6.0f;
    if (v == 90) {  // the dot over 90, in place of its tick
      const float dot_r = tick_in + 2;
      if (rowsVisible(py(dot_r, deg) - 3, py(dot_r, deg) + 3)) {
        s_band.drawSpot(lroundf(px(dot_r, deg)), lroundf(py(dot_r, deg)) - s_oy, 1.6f, kColorBezelText);
      }
    } else {
      radial(g.cx, g.cy, tick_in, tick_in + 4, deg, 0.6f, kColorBezelText);
    }
    snprintf(buf, sizeof(buf), "%d", v);
    text(buf, px(num_r, deg), py(num_r, deg), kColorBezelText);
  }
  disc(g.cx, g.cy, g.bezel_in, kColorFlange, kColorBezel);
  disc(g.cx, g.cy, g.dial, kColorDial, kColorFlange);
}

void drawSubdial(float deg, int ticks, int long_every, const int* numbers, int number_count,
                 int number_span) {
  const float sx = px(g.sub_dist, deg);
  const float sy = py(g.sub_dist, deg);
  disc(lroundf(sx), lroundf(sy), g.sub_r, kColorSubdial, kColorDial);
  circle(sx, sy, g.sub_r + 1, kColorSubdialRing);
  for (int i = 0; i < ticks; ++i) {
    const bool is_long = i % long_every == 0;
    radial(sx, sy, g.sub_r * (is_long ? 0.72f : 0.84f), g.sub_r * 0.96f, i * 360.0f / ticks,
           is_long ? 0.5f : 0, is_long ? kColorSubPrint : kColorSubFinePrint);
  }
  s_band.setFont(&fonts::Font0);
  s_band.setTextDatum(textdatum_t::middle_center);
  char buf[4];
  for (int i = 0; i < number_count; ++i) {
    const float a = numbers[i] * 360.0f / number_span;
    const float r = g.sub_r * 0.50f;
    snprintf(buf, sizeof(buf), "%d", numbers[i]);
    text(buf, sx + r * sinf(a * kDegToRad), sy - r * cosf(a * kDegToRad), kColorSubPrint);
  }
}

void drawDial() {
  // Minute track: fifth-of-a-second divisions, minute marks.
  const float d = g.dial;
  for (int i = 0; i < 300; ++i) {
    if (i % 5 == 0) {
      radial(g.cx, g.cy, d * 0.89f, d * 0.98f, i * 1.2f, i % 25 == 0 ? 0.7f : 0, kColorPrint);
    } else {
      radial(g.cx, g.cy, d * 0.94f, d * 0.98f, i * 1.2f, 0, kColorFinePrint);
    }
  }
  // Applied hour batons, shortened at 3, 6 and 9 where the subdials are.
  const float baton_w = g.r >= 150 ? 2.6f : 2.0f;
  for (int h = 0; h < 12; ++h) {
    const bool sub = h == 3 || h == 6 || h == 9;
    radial(g.cx, g.cy, d * (sub ? 0.78f : 0.66f), d * 0.86f, h * 30.0f, baton_w, kColorPrint);
  }
  s_band.setFont(&fonts::Font0);
  s_band.setTextDatum(textdatum_t::middle_center);
  text("CHRONOGRAPH", g.cx, g.cy - d * 0.47f, kColorPrint);
  text("PROFESSIONAL", g.cx, g.cy - d * 0.47f + 10, kColorFinePrint);

  static const int kSecNums[] = {20, 40, 60};
  static const int kMinNums[] = {10, 20, 30};
  static const int kHourNums[] = {3, 6, 9, 12};
  drawSubdial(270, 60, 5, kSecNums, 3, 60);
  drawSubdial(90, 30, 5, kMinNums, 3, 30);
  drawSubdial(180, 36, 3, kHourNums, 4, 12);
}

void drawHand(const Hand& h, float deg, int id) {
  const float lo = h.py - h.tip - h.width - 2;
  const float hi = h.py + h.tip + h.width + 2;
  if (!rowsVisible(lo, hi)) return;
  if (id == kHour || id == kMinute) {
    radial(h.px, h.py, -h.tail, h.tip, deg, h.width + 1.0f, kColorHandEdge);
    radial(h.px, h.py, -h.tail, h.tip, deg, h.width, kColorHand);
    // Coloured pointer on the outer end: red hour, blue minute.
    radial(h.px, h.py, h.tip * (1.0f - kHandPointerFrac), h.tip, deg, h.width,
           id == kHour ? kColorHourHand : kColorMinuteHand);
  } else if (id == kChrono) {
    radial(h.px, h.py, -h.tail, h.tip, deg, h.width, kColorHand);
    const float s = sinf(deg * kDegToRad);
    const float c = cosf(deg * kDegToRad);
    s_band.drawSpot(lroundf(h.px - h.tail * s), lroundf(h.py + h.tail * c) - s_oy, 2.4f, kColorHand);
  } else {
    radial(h.px, h.py, -h.tail, h.tip, deg, h.width, kColorSubHand);
    s_band.drawSpot(lroundf(h.px), lroundf(h.py) - s_oy, 2.0f, kColorSubHand);
  }
}

void drawHands() {
  for (int i = 0; i < kHandCount; ++i) {
    if (i == kChrono) continue;  // on top of the time hands
    drawHand(s_hands[i], s_angle[i], i);
  }
  drawHand(s_hands[kChrono], s_angle[kChrono], kChrono);
  if (rowsVisible(g.cy - 5, g.cy + 5)) {
    s_band.drawSpot(g.cx, g.cy - s_oy, 4.0f, kColorHandEdge);
    s_band.drawSpot(g.cx, g.cy - s_oy, 2.0f, kColorHand);
  }
}

void setupGeometry() {
  const int w = tft.width();
  const int h = tft.height();
  const int size = min(w, h);
  auto bandsFor = [](int r) { return min(kMaxBands, (2 * r + 3 + kBandH - 1) / kBandH); };
  g.r = size / 2 - 1;
  const StripSizes& full = kStrips;
  s_strips = h - bandsFor(g.r) * kBandH >= full.buttons + full.timer + full.time
                 ? &kStrips
                 : &kCompactStrips;
  const StripSizes& st = *s_strips;
  if (s_strips == &kCompactStrips) {
    // Shrink the face until all three compact strips fit, with a few px of padding.
    const int face_rows = h - st.buttons - st.timer - st.time - 8;
    while (g.r > kMinFaceR && bandsFor(g.r) * kBandH > face_rows) --g.r;
  }
  g.bands = bandsFor(g.r);
  int spare = h - g.bands * kBandH;
  s_buttons_h = spare >= st.buttons ? st.buttons : 0;
  spare -= s_buttons_h;
  s_timer_h = spare >= st.timer ? st.timer : 0;
  spare -= s_timer_h;
  s_time_h = spare >= st.time ? st.time : 0;
  spare -= s_time_h;
  s_pad = max(0, spare / 4);
  g.top = s_pad + s_time_h + s_pad;
  g.cx = w / 2;
  g.cy = g.top + g.r + 1;
  g.bezel_out = g.r - 3;
  g.bezel_in = lroundf(g.r * 0.78f);
  g.dial = g.bezel_in - 2;
  g.sub_r = lroundf(g.dial * 0.25f);
  g.sub_dist = lroundf(g.dial * 0.46f);
  s_small_font = g.r >= 150 ? static_cast<const lgfx::IFont*>(&fonts::Font2) : &fonts::Font0;

  const float d = g.dial;
  const float main_w = g.r >= 150 ? 2.6f : 2.0f;
  s_hands[kHour] = {float(g.cx), float(g.cy), d * 0.56f, d * 0.12f, main_w};
  s_hands[kMinute] = {float(g.cx), float(g.cy), d * 0.88f, d * 0.12f, main_w};
  s_hands[kChrono] = {float(g.cx), float(g.cy), d * 0.97f, d * 0.22f, 0.6f};
  const float sub_tip = g.sub_r * 0.88f;
  const float sub_tail = g.sub_r * 0.22f;
  s_hands[kRunSec] = {px(g.sub_dist, 270), py(g.sub_dist, 270), sub_tip, sub_tail, 1.1f};
  s_hands[kChronoMin] = {px(g.sub_dist, 90), py(g.sub_dist, 90), sub_tip, sub_tail, 1.1f};
  s_hands[kChronoHour] = {px(g.sub_dist, 180), py(g.sub_dist, 180), sub_tip, sub_tail, 1.1f};
}

unsigned long chronoElapsedMs() {
  return s_chrono_accum_ms + (s_chrono_running ? millis() - s_chrono_start_ms : 0);
}

// Below the last band (which can run past the face), under the timer.
int timerTop() { return g.top + g.bands * kBandH + s_pad; }

void layoutButtons() {
  const int top = timerTop() + s_timer_h + s_pad;
  const int avail = tft.height() - top;
  const int h = min(44, avail - 6);
  constexpr int kMargin = 8;
  constexpr int kGap = 8;
  const int w = (tft.width() - 2 * kMargin - (kButtonCount - 1) * kGap) / kButtonCount;
  for (int i = 0; i < kButtonCount; ++i) {
    s_buttons[i] = {kMargin + i * (w + kGap), top + (avail - h) / 2, w, h};
  }
}

void drawButton(int i) {
  const ButtonRect& b = s_buttons[i];
  if (b.h < 12) return;  // no room under the face
  uint16_t fill = kColorCase;
  uint16_t ink = TFT_BLACK;
  const char* label = "RESET";
  if (i == kStartStop) {
    label = s_chrono_running ? "STOP" : "START";
    fill = s_chrono_running ? lgfx::color565(200, 50, 40) : lgfx::color565(30, 150, 60);
    ink = TFT_WHITE;
  } else if (i == kExit) {
    label = "EXIT";
    fill = lgfx::color565(60, 60, 66);
    ink = TFT_WHITE;
  }
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 6, fill);
  tft.setFont(b.h >= 36 ? static_cast<const lgfx::IFont*>(&fonts::FreeSansBold9pt7b) : &fonts::Font2);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setTextColor(ink);
  tft.drawString(label, b.x + b.w / 2, b.y + b.h / 2);
}

void drawButtons() {
  for (int i = 0; i < kButtonCount; ++i) drawButton(i);
}

// Digital readouts in fixed cells: digits and blanks as wide as "8", separators as wide as
// ":", so a narrow "1" doesn't shift the rest. Only cells whose character changed since
// `shown` are redrawn (with the current text colour's background and padding, so no
// flicker). Uses the current font and datum's vertical alignment at y.
int cellWidth(char ch, int digit_w, int sep_w) {
  return isdigit(static_cast<unsigned char>(ch)) || ch == ' ' || ch == '-' ? digit_w : sep_w;
}

int cellsWidth(const char* str, int digit_w, int sep_w) {
  int w = 0;
  for (const char* p = str; *p; ++p) w += cellWidth(*p, digit_w, sep_w);
  return w;
}

void drawCells(const char* str, char* shown, int x, int y, int digit_w, int sep_w) {
  for (int i = 0; str[i]; ++i) {
    const int cw = cellWidth(str[i], digit_w, sep_w);
    if (str[i] != shown[i]) {
      const char s[2] = {str[i], 0};
      tft.setTextPadding(cw);
      tft.drawString(s, x + cw / 2, y);
      shown[i] = str[i];
    }
    x += cw;
  }
  tft.setTextPadding(0);
}

char s_time_chars[12];  // what each time-of-day cell shows now

// Time of day with the timezone abbreviation (from the POSIX TZ string, e.g. MDT), centred
// above the face. Redrawn when the second changes.
void drawTimeOfDay() {
  if (s_time_h == 0) return;
  struct tm t;
  char digits[12];  // "%2d:%02d:%02d": the hour always takes two cells, so nothing moves
  char suffix[16];  // "AM MDT": AM/PM and timezone, only change rarely
  char zone[8] = "";
  const char* ampm = "";
  if (utils::time::localTimeNow(&t)) {
    const int hour12 = t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12;
    snprintf(digits, sizeof(digits), "%2d:%02d:%02d", hour12, t.tm_min, t.tm_sec);
    ampm = t.tm_hour < 12 ? "AM" : "PM";
    strftime(zone, sizeof(zone), "%Z", &t);
  } else {
    strcpy(digits, "--:--:--");
  }
  snprintf(suffix, sizeof(suffix), "%s %s", ampm, zone);
  char key[24];
  snprintf(key, sizeof(key), "%s %s", digits, suffix);
  if (strcmp(key, s_shown_time) == 0) return;
  // AM/PM or zone changed (or first draw): clear the row and draw every cell.
  const bool full = strcmp(key + strlen(digits), s_shown_time + strlen(digits)) != 0 ||
                    s_shown_time[0] == '\0';
  strlcpy(s_shown_time, key, sizeof(s_shown_time));

  constexpr int kGap = 8;
  tft.setTextSize(1);
  tft.setFont(s_strips->zone_font);
  const int zw = zone[0] ? tft.textWidth(zone) + kGap : 0;
  tft.setFont(s_strips->time_font);
  const int digit_w = tft.textWidth("8");
  const int sep_w = tft.textWidth(":");
  const int ampm_w = max(tft.textWidth("AM"), tft.textWidth("PM"));
  const int cells_w = cellsWidth(digits, digit_w, sep_w);
  const int x = (tft.width() - cells_w - kGap - ampm_w - zw) / 2;
  const int base = s_pad + s_time_h * 3 / 4 + 1;
  tft.startWrite();
  if (full) {
    tft.fillRect(0, s_pad, tft.width(), s_time_h, TFT_BLACK);
    memset(s_time_chars, 0, sizeof(s_time_chars));
  }
  tft.setTextDatum(textdatum_t::baseline_center);
  tft.setTextColor(kColorPrint, TFT_BLACK);
  drawCells(digits, s_time_chars, x, base, digit_w, sep_w);
  if (full) {
    tft.setTextDatum(textdatum_t::baseline_left);
    tft.setTextColor(kColorPrint);
    tft.drawString(ampm, x + cells_w + kGap, base);
    if (zone[0]) {
      tft.setFont(s_strips->zone_font);
      tft.setTextColor(kColorFinePrint);
      tft.drawString(zone, x + cells_w + kGap + ampm_w + kGap, base);
    }
  }
  tft.endWrite();
}

// Interval timer (the chronograph) in digits between the face and the buttons, to hundredths;
// amber while stopped. Running, it's redrawn at most every kTimerRedrawMs (20 a second), so
// the hundredths step by ~5; stopped, it shows the exact time. Each character has a fixed
// cell (digits as wide as "8"), so nothing shifts as digits change; only changed cells are
// redrawn, each with a background colour and padding, so it doesn't flicker.
constexpr unsigned long kTimerRedrawMs = 50;
unsigned long s_last_timer_draw_ms = 0;
char s_timer_chars[20];  // what each cell shows now; reset when the layout or colour changes

void drawIntervalTimer() {
  if (s_timer_h == 0) return;
  if (s_chrono_running && millis() - s_last_timer_draw_ms < kTimerRedrawMs) return;
  const unsigned long ms = chronoElapsedMs();
  char buf[20];
  snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu.%02lu", ms / 3600000UL, (ms / 60000UL) % 60,
           (ms / 1000UL) % 60, (ms / 10UL) % 100);
  char key[22];
  snprintf(key, sizeof(key), "%s%c", buf, s_chrono_running ? 'r' : 's');
  if (strcmp(key, s_shown_timer) == 0) return;
  // A new length (hours reaching 10) or colour redraws every cell.
  const bool full = s_shown_timer[0] == '\0' || strlen(key) != strlen(s_shown_timer) ||
                    key[strlen(key) - 1] != s_shown_timer[strlen(s_shown_timer) - 1];
  strlcpy(s_shown_timer, key, sizeof(s_shown_timer));
  s_last_timer_draw_ms = millis();

  tft.setTextSize(1);
  tft.setFont(s_strips->timer_font);
  const int digit_w = tft.textWidth("8");
  const int sep_w = max(tft.textWidth(":"), tft.textWidth("."));
  const int x = (tft.width() - cellsWidth(buf, digit_w, sep_w)) / 2;
  tft.startWrite();
  if (full) {
    tft.fillRect(0, timerTop(), tft.width(), s_timer_h, TFT_BLACK);
    memset(s_timer_chars, 0, sizeof(s_timer_chars));
  }
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setTextColor(s_chrono_running ? kColorPrint : lgfx::color565(240, 180, 40), TFT_BLACK);
  drawCells(buf, s_timer_chars, x, timerTop() + s_timer_h / 2, digit_w, sep_w);
  tft.endWrite();
}

// Hand angles in degrees clockwise from 12, stepped once a second (frames still run at
// kWatchFaceFrameMs for the interval timer's tenths, but the face only redraws on a step).
void updateAngles() {
  constexpr float beats = 1.0f;
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  struct tm t;
  localtime_r(&tv.tv_sec, &t);
  const float sec = t.tm_sec + floorf(tv.tv_usec / 1e6f * beats) / beats;
  s_angle[kHour] = (t.tm_hour % 12 + t.tm_min / 60.0f + sec / 3600.0f) * 30.0f;
  s_angle[kMinute] = (t.tm_min + sec / 60.0f) * 6.0f;
  s_angle[kRunSec] = sec * 6.0f;

  const unsigned long ms = chronoElapsedMs();
  const float chrono_s = floorf(ms / 1000.0f * beats) / beats;
  // Stopped: the sweep hand shows the exact time, matching the digital timer's tenths.
  const float sweep_s = s_chrono_running ? chrono_s : (ms % 60000UL) / 1000.0f;
  s_angle[kChrono] = fmodf(sweep_s, 60.0f) * 6.0f;
  s_angle[kChronoMin] = float((ms / 60000UL) % 30) * 12.0f;  // jumps once a minute
  s_angle[kChronoHour] = fmodf(chrono_s / 3600.0f, 12.0f) * 30.0f;
}

void markHandRows(const Hand& h, float deg) {
  const float c = cosf(deg * kDegToRad);
  float y0 = h.py - h.tip * c;
  float y1 = h.py + h.tail * c;
  if (y0 > y1) std::swap(y0, y1);
  y0 -= h.width + 4;  // AA fringe and the chrono counterweight
  y1 += h.width + 4;
  const int b0 = max(0, int(floorf((y0 - g.top) / kBandH)));
  const int b1 = min(g.bands - 1, int(floorf((y1 - g.top) / kBandH)));
  for (int b = b0; b <= b1; ++b) s_dirty[b] = true;
}

void renderDirtyBands() {
  tft.startWrite();
  for (int b = 0; b < g.bands; ++b) {
    if (!s_dirty[b]) continue;
    s_dirty[b] = false;
    s_oy = g.top + b * kBandH;
    tft.waitDMA();  // pushSprite returns while DMA still reads the buffer
    s_band.fillScreen(TFT_BLACK);
    drawBezel();
    drawDial();
    drawHands();
    s_band.pushSprite(0, s_oy);
  }
  tft.endWrite();
}

}  // namespace

bool begin() {
  setupGeometry();
  s_band.setColorDepth(16);  // 8-bit loses the anti-aliasing
  if (!s_band.createSprite(tft.width(), kBandH)) return false;
  s_active = true;
  s_chrono_start_ms = millis();  // runs from the start, so it shows idle time
  s_chrono_accum_ms = 0;
  s_chrono_running = true;
  s_last_frame_ms = millis();
  tft.fillScreen(TFT_BLACK);
  layoutButtons();
  drawButtons();
  s_shown_time[0] = s_shown_timer[0] = '\0';
  drawTimeOfDay();
  drawIntervalTimer();
  updateAngles();
  for (int b = 0; b < g.bands; ++b) s_dirty[b] = true;
  renderDirtyBands();
  memcpy(s_prev_angle, s_angle, sizeof(s_angle));
  return true;
}

void tick() {
  if (!s_active) return;
  // Every loop (~10 ms): it only draws when the tenths digit changes, so each tenth shows.
  drawIntervalTimer();
  if (millis() - s_last_frame_ms < config::kWatchFaceFrameMs) return;
  s_last_frame_ms = millis();
  updateAngles();
  bool any = false;
  for (int i = 0; i < kHandCount; ++i) {
    if (s_angle[i] == s_prev_angle[i]) continue;
    markHandRows(s_hands[i], s_prev_angle[i]);
    markHandRows(s_hands[i], s_angle[i]);
    any = true;
  }
  memcpy(s_prev_angle, s_angle, sizeof(s_angle));
  if (any) renderDirtyBands();
  drawTimeOfDay();
  drawIntervalTimer();
}

void end() {
  if (!s_active) return;
  s_active = false;
  tft.waitDMA();
  s_band.deleteSprite();
  displayFontEnsureLoaded(tft);  // radar text uses the smooth vlw font
}

bool isActive() { return s_active; }

bool handleTap(int x, int y, unsigned long press_ms) {
  if (!s_active) return false;
  constexpr int kSlopPx = 6;
  // Anywhere on the watch (case included) is a big Start/Stop button.
  const int dx = x - g.cx;
  const int dy = y - g.cy;
  const bool on_face = dx * dx + dy * dy <= g.r * g.r;
  for (int i = 0; i < kButtonCount; ++i) {
    const ButtonRect& b = s_buttons[i];
    const bool on_button = x >= b.x - kSlopPx && x < b.x + b.w + kSlopPx &&
                           y >= b.y - kSlopPx && y < b.y + b.h + kSlopPx;
    if (!on_button && !(on_face && i == kStartStop)) continue;
    if (i == kExit) return true;
    if (i == kStartStop) {
      if (s_chrono_running) {
        s_chrono_accum_ms += press_ms - s_chrono_start_ms;
      } else {
        s_chrono_start_ms = press_ms;
      }
      s_chrono_running = !s_chrono_running;
    } else {  // reset; keeps running if it was (flyback)
      s_chrono_accum_ms = 0;
      s_chrono_start_ms = press_ms;
    }
    drawButton(kStartStop);
    s_last_frame_ms = 0;  // move the hands now
    tick();
    return false;
  }
  return false;
}

}  // namespace ui::watch_face
