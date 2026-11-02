#include "ui/calculator.h"

#include <Arduino.h>
#include <math.h>

#include "hardware/display.h"
#include "hardware/display_font.h"

namespace ui::calculator {
namespace {

// Basic pocket calculator: immediate execution (2 + 3 x 4 = 20), repeated "=" repeats the
// last operation, one memory register (STO / RCL / M+). 10-digit seven-segment LCD. Every
// segment is drawn either lit or in the glass colour (blank), so the display never needs
// clearing and doesn't flicker.

constexpr int kDigits = 10;
constexpr int kCells = kDigits + 1;  // leftmost cell only shows the minus sign

constexpr uint16_t kColorCase = lgfx::color565(38, 40, 44);
constexpr uint16_t kColorBezel = lgfx::color565(16, 16, 18);
constexpr uint16_t kColorLcd = lgfx::color565(168, 184, 150);
constexpr uint16_t kColorLcdGhost = kColorLcd;  // unlit segments, points, commas: blank
                                                // (was a faint 152,168,136)
constexpr uint16_t kColorLcdInk = lgfx::color565(28, 34, 28);
constexpr uint16_t kColorKeyDigit = lgfx::color565(72, 74, 80);
constexpr uint16_t kColorKeyFunc = lgfx::color565(112, 114, 120);
constexpr uint16_t kColorKeyOp = lgfx::color565(225, 135, 30);
constexpr uint16_t kColorKeyClear = lgfx::color565(200, 80, 40);
constexpr uint16_t kColorKeyExit = lgfx::color565(160, 40, 36);
constexpr uint16_t kColorKeyFlash = lgfx::color565(235, 235, 235);
constexpr unsigned long kKeyFlashMs = 70;

// Key ids: digits (0-9, A-F) and ops are their own characters, the rest lower case.
enum : char {
  kNeg = 'n', kClear = 'c', kBack = 'b', kSto = 's', kRcl = 'r', kMemAdd = 'm', kExit = 'x',
  kHex = 'h', kNot = 'i',
};

// Which keypad a key belongs to: the top two rows are shared, below them it's either the
// decimal keys + operators or a 4x4 grid of hex digits.
enum : uint8_t { kBoth, kDecOnly, kHexOnly };

struct Key {
  char id;
  const char* label;  // nullptr: drawn as an icon (operators, back arrow) or HEX / DEC
  uint16_t x100;      // left edge, in hundredths of a column
  uint8_t row;
  uint16_t w100;      // width, in hundredths of a column (100 = one key)
  uint16_t color;
  uint8_t pad;        // kBoth / kDecOnly / kHexOnly
};

const Key kKeys[] = {
    {kExit, "EXIT", 0, 0, 100, kColorKeyExit, kBoth},
    {kSto, "STO", 100, 0, 100, kColorKeyFunc, kBoth},
    {kRcl, "RCL", 200, 0, 100, kColorKeyFunc, kBoth},
    {kMemAdd, "M+", 300, 0, 100, kColorKeyFunc, kBoth},
    {kClear, "CLR", 0, 1, 100, kColorKeyClear, kBoth},
    {kHex, nullptr, 100, 1, 100, kColorKeyFunc, kBoth},
    {kBack, nullptr, 200, 1, 100, kColorKeyFunc, kBoth},
    {'/', nullptr, 300, 1, 100, kColorKeyOp, kDecOnly},
    {'7', "7", 0, 2, 100, kColorKeyDigit, kDecOnly},
    {'8', "8", 100, 2, 100, kColorKeyDigit, kDecOnly},
    {'9', "9", 200, 2, 100, kColorKeyDigit, kDecOnly},
    {'*', nullptr, 300, 2, 100, kColorKeyOp, kDecOnly},
    {'4', "4", 0, 3, 100, kColorKeyDigit, kDecOnly},
    {'5', "5", 100, 3, 100, kColorKeyDigit, kDecOnly},
    {'6', "6", 200, 3, 100, kColorKeyDigit, kDecOnly},
    {'-', nullptr, 300, 3, 100, kColorKeyOp, kDecOnly},
    {'1', "1", 0, 4, 100, kColorKeyDigit, kDecOnly},
    {'2', "2", 100, 4, 100, kColorKeyDigit, kDecOnly},
    {'3', "3", 200, 4, 100, kColorKeyDigit, kDecOnly},
    {'+', nullptr, 300, 4, 100, kColorKeyOp, kDecOnly},
    {'0', "0", 0, 5, 100, kColorKeyDigit, kDecOnly},
    {kNeg, "+/-", 100, 5, 100, kColorKeyFunc, kDecOnly},
    {'.', ".", 200, 5, 100, kColorKeyDigit, kDecOnly},
    {'=', nullptr, 300, 5, 100, kColorKeyOp, kDecOnly},
    // Hex pad: 4x4 digits plus NOT - + = on the right, five 0.8-key columns.
    {'C', "C", 0, 2, 80, kColorKeyDigit, kHexOnly},
    {'D', "D", 80, 2, 80, kColorKeyDigit, kHexOnly},
    {'E', "E", 160, 2, 80, kColorKeyDigit, kHexOnly},
    {'F', "F", 240, 2, 80, kColorKeyDigit, kHexOnly},
    {kNot, "NOT", 320, 2, 80, kColorKeyOp, kHexOnly},
    {'8', "8", 0, 3, 80, kColorKeyDigit, kHexOnly},
    {'9', "9", 80, 3, 80, kColorKeyDigit, kHexOnly},
    {'A', "A", 160, 3, 80, kColorKeyDigit, kHexOnly},
    {'B', "B", 240, 3, 80, kColorKeyDigit, kHexOnly},
    {'-', nullptr, 320, 3, 80, kColorKeyOp, kHexOnly},
    {'4', "4", 0, 4, 80, kColorKeyDigit, kHexOnly},
    {'5', "5", 80, 4, 80, kColorKeyDigit, kHexOnly},
    {'6', "6", 160, 4, 80, kColorKeyDigit, kHexOnly},
    {'7', "7", 240, 4, 80, kColorKeyDigit, kHexOnly},
    {'+', nullptr, 320, 4, 80, kColorKeyOp, kHexOnly},
    {'0', "0", 0, 5, 80, kColorKeyDigit, kHexOnly},
    {'1', "1", 80, 5, 80, kColorKeyDigit, kHexOnly},
    {'2', "2", 160, 5, 80, kColorKeyDigit, kHexOnly},
    {'3', "3", 240, 5, 80, kColorKeyDigit, kHexOnly},
    {'=', nullptr, 320, 5, 80, kColorKeyOp, kHexOnly},
};
constexpr int kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);
constexpr int kCols = 4;
constexpr int kRows = 6;

// Segment bits a..g.
const uint8_t kDigitSegs[] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F,
                              0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71};  // A b C d E F
constexpr uint8_t kSegMinus = 0x40;
constexpr uint8_t kSegE = 0x79;

struct Layout {
  int lcd_x, lcd_y, lcd_w, lcd_h;  // LCD glass (inside the bezel)
  int ann_y, ann_h;                // annunciator row
  int cell_x0, pitch;              // digit cells
  int dig_y, dig_w, dig_h, seg_t;
  int keys_x, keys_y, key_w, key_h, gap;
};

Layout L;
bool s_active = false;

// Calculator state (kept between openings).
double s_x = 0.0;      // value on the display when not typing
double s_acc = 0.0;    // left operand of the pending operation
double s_mem = 0.0;
char s_pending = 0;    // pending operator, 0 = none
char s_last_op = 0;    // for repeated "="
double s_last_operand = 0.0;
bool s_typing = false;  // digits go into s_entry
bool s_have_x = false;  // a new operand was typed or recalled since the last operator
bool s_error = false;
bool s_hex = false;     // hex mode: whole numbers, hex keypad
char s_entry[16] = "0";

constexpr double kHexMax = 1099511627775.0;  // 0xFFFFFFFFFF, the most 10 digits can show

// ---- number handling ----

bool isDigitKey(char id) {
  return isdigit(static_cast<unsigned char>(id)) || (id >= 'A' && id <= 'F');
}

int digitValue(char ch) { return ch <= '9' ? ch - '0' : ch - 'A' + 10; }

int entryDigits() {
  int n = 0;
  for (const char* p = s_entry; *p; ++p) n += isDigitKey(*p) ? 1 : 0;
  return n;
}

double currentValue() {
  if (!s_typing) return s_x;
  return s_hex ? static_cast<double>(strtoll(s_entry, nullptr, 16)) : atof(s_entry);
}

// Whole part in hex, '-' in front if negative. False if it needs more than 10 digits.
bool formatHex(double v, char* out, size_t len) {
  const double t = trunc(v);
  if (isnan(t) || fabs(t) > kHexMax) return false;
  snprintf(out, len, "%s%llX", t < 0 ? "-" : "", static_cast<unsigned long long>(fabs(t)));
  return true;
}

void stripZeros(char* buf) {
  if (!strchr(buf, '.')) return;
  char* end = buf + strlen(buf) - 1;
  while (*end == '0') *end-- = '\0';
  if (*end == '.') *end = '\0';
}

// Exponential: "1.234567e12" (7 mantissa digits) or "1.23456e-12" (6; the exponent's minus
// takes a cell). Two exponent digits, so |exponent| > 99 doesn't fit.
bool formatExp(double v, char* out, size_t len) {
  const double a = fabs(v);
  const int digits = a >= 1.0 ? 7 : 6;
  char buf[32];
  snprintf(buf, sizeof(buf), "%.*e", digits - 1, a);
  char* e = strchr(buf, 'e');
  const int exp = atoi(e + 1);
  if (exp > 99 || exp < -99) return false;
  *e = '\0';
  stripZeros(buf);
  snprintf(out, len, "%s%se%d", v < 0 ? "-" : "", buf, exp);
  return true;
}

// Up to 10 significant digits, trailing zeros dropped; exponential above 10 digits or below
// 1e-6. False if it doesn't fit even then.
bool formatValue(double v, char* out, size_t len) {
  if (isnan(v) || isinf(v)) return false;
  const double a = fabs(v);
  if (a >= 9999999999.5 || (a != 0.0 && a < 1e-6)) return formatExp(v, out, len);
  int int_digits = a < 1.0 ? 1 : static_cast<int>(floor(log10(a))) + 1;
  char buf[32];
  for (int decimals = kDigits - int_digits; decimals >= 0; --decimals) {
    snprintf(buf, sizeof(buf), "%.*f", decimals, a);
    int n = 0;
    for (const char* p = buf; *p; ++p) n += isdigit(static_cast<unsigned char>(*p)) ? 1 : 0;
    if (n <= kDigits) break;  // rounding can add a digit (99.999... -> 100.00...)
  }
  stripZeros(buf);
  const bool zero = strcmp(buf, "0") == 0;  // tiny values underflow to 0
  snprintf(out, len, "%s%s", (v < 0 && !zero) ? "-" : "", buf);
  return true;
}

// Fails (and sets the error) on overflow or division by zero. Hex mode keeps the whole part.
bool setResult(double v) {
  if (s_hex) {
    const double t = trunc(v);
    if (isnan(t) || fabs(t) > kHexMax) {
      s_error = true;
      return false;
    }
    s_x = t;
    return true;
  }
  char buf[24];
  if (!formatValue(v, buf, sizeof(buf))) {
    s_error = true;
    return false;
  }
  s_x = atof(buf);  // keep exactly what's shown
  return true;
}

double apply(double a, char op, double b) {
  switch (op) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case '/': return b == 0.0 ? NAN : a / b;
  }
  return b;
}

void clearAll() {
  s_x = s_acc = 0.0;
  s_pending = s_last_op = 0;
  s_typing = s_have_x = s_error = false;
  strcpy(s_entry, "0");
}

void pressKey(char id) {
  if (s_error && id != kClear) return;
  if (isDigitKey(id) || id == '.') {
    if (!s_typing) {
      strcpy(s_entry, "0");
      s_typing = s_have_x = true;
    }
    if (id == '.') {
      if (!strchr(s_entry, '.')) strcat(s_entry, ".");
    } else if (strcmp(s_entry, "0") == 0 || strcmp(s_entry, "-0") == 0) {
      s_entry[strlen(s_entry) - 1] = id;  // replace the leading zero
    } else if (entryDigits() < kDigits) {
      const size_t n = strlen(s_entry);
      s_entry[n] = id;
      s_entry[n + 1] = '\0';
    }
    return;
  }
  switch (id) {
    case '+': case '-': case '*': case '/': {
      const double v = currentValue();
      if (s_pending && s_have_x) {
        if (!setResult(apply(s_acc, s_pending, v))) return;
      } else if (!s_pending) {
        if (!setResult(v)) return;
      }
      s_acc = s_x;
      s_pending = id;  // pressing another operator just replaces it
      s_typing = s_have_x = false;
      break;
    }
    case '=': {
      const double v = currentValue();
      if (s_pending) {
        const double operand = s_have_x ? v : s_acc;  // "5 x =" squares
        s_last_op = s_pending;
        s_last_operand = operand;
        s_pending = 0;
        if (!setResult(apply(s_acc, s_last_op, operand))) return;
      } else if (s_last_op) {
        if (!setResult(apply(v, s_last_op, s_last_operand))) return;
      } else if (!setResult(v)) {
        return;
      }
      s_typing = s_have_x = false;
      break;
    }
    case kNeg:
      if (s_typing) {
        if (s_entry[0] == '-') {
          memmove(s_entry, s_entry + 1, strlen(s_entry));
        } else {
          memmove(s_entry + 1, s_entry, strlen(s_entry) + 1);
          s_entry[0] = '-';
        }
      } else {
        s_x = -s_x;
        s_have_x = true;
      }
      break;
    case kBack:
      if (s_typing) {
        size_t n = strlen(s_entry);
        if (n > 0) s_entry[--n] = '\0';
        if (n == 0 || strcmp(s_entry, "-") == 0) strcpy(s_entry, "0");
      }
      break;
    case kClear:
      // First press clears the number being typed, the next one everything.
      if (s_typing && strcmp(s_entry, "0") != 0) {
        strcpy(s_entry, "0");
      } else {
        clearAll();
      }
      break;
    case kSto:
      if (!setResult(currentValue())) return;
      s_mem = s_x;
      s_typing = false;
      break;
    case kRcl:
      s_x = s_hex ? trunc(s_mem) : s_mem;
      if (s_hex && fabs(s_x) > kHexMax) s_error = true;
      s_typing = false;
      s_have_x = true;
      break;
    case kNot: {
      // Bitwise NOT within the smallest of 16 / 32 / 64 bits that holds the number
      // (a 64-bit result never fits 10 hex digits: Error). Negative numbers: -x - 1.
      const double v = trunc(currentValue());
      double r;
      if (v < 0) {
        r = -v - 1.0;
      } else {
        const uint64_t u = static_cast<uint64_t>(v);
        const int bits = u <= 0xFFFFULL ? 16 : u <= 0xFFFFFFFFULL ? 32 : 64;
        const uint64_t mask = bits == 64 ? ~0ULL : (1ULL << bits) - 1;
        r = static_cast<double>(~u & mask);
      }
      if (!setResult(r)) return;
      s_typing = false;
      s_have_x = true;
      break;
    }
    case kHex: {
      // Switch keypads and convert the display. Hex drops the fraction; an operation pending
      // from decimal finishes after switching back.
      const double v = currentValue();
      s_hex = !s_hex;
      s_typing = false;
      if (s_hex) {
        s_x = trunc(v);
        if (fabs(s_x) > kHexMax) s_error = true;
      } else if (!setResult(v)) {
        return;
      }
      break;
    }
    case kMemAdd:
      if (!setResult(currentValue())) return;
      {
        char buf[24];
        if (formatValue(s_mem + s_x, buf, sizeof(buf))) {
          s_mem = atof(buf);
        } else {
          s_error = true;
        }
      }
      s_typing = false;
      break;
  }
}

// ---- drawing ----

void quad(int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3, uint16_t c) {
  tft.fillTriangle(x0, y0, x1, y1, x2, y2, c);
  tft.fillTriangle(x0, y0, x2, y2, x3, y3, c);
}

// Thick line as a quad (no anti-aliasing: it would read back from a panel with no MISO).
void thickLine(float x0, float y0, float x1, float y1, float hw, uint16_t c) {
  const float dx = x1 - x0, dy = y1 - y0;
  const float len = sqrtf(dx * dx + dy * dy);
  if (len < 0.5f) return;
  const float nx = -dy / len * hw, ny = dx / len * hw;
  quad(lroundf(x0 + nx), lroundf(y0 + ny), lroundf(x1 + nx), lroundf(y1 + ny),
       lroundf(x1 - nx), lroundf(y1 - ny), lroundf(x0 - nx), lroundf(y0 - ny), c);
}

// Segments with pointed ends: tips at xa / xb (horizontal) or ya / yb (vertical).
void hSeg(int xa, int xb, int yc, int t, uint16_t c) {
  const int h = t / 2;
  tft.fillRect(xa + h, yc - h, xb - xa - 2 * h + 1, t, c);
  tft.fillTriangle(xa, yc, xa + h, yc - h, xa + h, yc - h + t - 1, c);
  tft.fillTriangle(xb, yc, xb - h, yc - h, xb - h, yc - h + t - 1, c);
}

void vSeg(int xc, int ya, int yb, int t, uint16_t c) {
  const int h = t / 2;
  tft.fillRect(xc - h, ya + h, t, yb - ya - 2 * h + 1, c);
  tft.fillTriangle(xc, ya, xc - h, ya + h, xc - h + t - 1, ya + h, c);
  tft.fillTriangle(xc, yb, xc - h, yb - h, xc - h + t - 1, yb - h, c);
}

// After each digit: a point, plus a tail under it for the thousands comma.
void drawCell(int cell, uint8_t segs, bool dp, bool comma) {
  const int x = L.cell_x0 + cell * L.pitch;
  const int y = L.dig_y;
  const int w = L.dig_w, hgt = L.dig_h, t = L.seg_t;
  const int h = t / 2;
  const int g = 1;  // gap between segments
  const int xl = x + h, xr = x + w - 1 - h;
  const int yt = y + h, ym = y + hgt / 2, yb = y + hgt - 1 - h;
  auto col = [&](int bit) { return (segs >> bit) & 1 ? kColorLcdInk : kColorLcdGhost; };
  hSeg(xl + g, xr - g, yt, t, col(0));  // a
  vSeg(xr, yt + g, ym - g, t, col(1));  // b
  vSeg(xr, ym + g, yb - g, t, col(2));  // c
  hSeg(xl + g, xr - g, yb, t, col(3));  // d
  vSeg(xl, ym + g, yb - g, t, col(4));  // e
  vSeg(xl, yt + g, ym - g, t, col(5));  // f
  hSeg(xl + g, xr - g, ym, t, col(6));  // g
  if (cell > 0) {
    const int d = max(3, t - 1);
    const int px = x + w + (L.pitch - w - d) / 2;
    const int py = y + hgt - d;
    tft.fillRect(px, py, d, d, dp || comma ? kColorLcdInk : kColorLcdGhost);
    thickLine(px + d - 1, py + d, px, py + 2 * d, 1.0f, comma ? kColorLcdInk : kColorLcdGhost);
  }
}

// Operator icon centred on (cx, cy), r = half its size.
void drawOpIcon(char op, int cx, int cy, int r, int t, uint16_t c) {
  const int h = t / 2;
  switch (op) {
    case '+':
      tft.fillRect(cx - r, cy - h, 2 * r + 1, t, c);
      tft.fillRect(cx - h, cy - r, t, 2 * r + 1, c);
      break;
    case '-':
      tft.fillRect(cx - r, cy - h, 2 * r + 1, t, c);
      break;
    case '*': {
      const float d = r * 0.8f;
      thickLine(cx - d, cy - d, cx + d, cy + d, t / 2.0f, c);
      thickLine(cx - d, cy + d, cx + d, cy - d, t / 2.0f, c);
      break;
    }
    case '/':
      tft.fillRect(cx - r, cy - h, 2 * r + 1, t, c);
      tft.fillRect(cx - h, cy - r, t, t, c);
      tft.fillRect(cx - h, cy + r - t + 1, t, t, c);
      break;
    case '=':
      tft.fillRect(cx - r, cy - r / 2 - h, 2 * r + 1, t, c);
      tft.fillRect(cx - r, cy + r / 2 - h, 2 * r + 1, t, c);
      break;
    case kBack: {  // <- arrow
      const int head = r * 3 / 4;
      tft.fillRect(cx - r + head - 1, cy - h, 2 * r - head + 2, t, c);
      tft.fillTriangle(cx - r, cy, cx - r + head, cy - head, cx - r + head, cy + head, c);
      break;
    }
  }
}

// Annunciators along the top of the glass: M (memory not zero), E (error), and the pending
// operator, HEX. Only lit ones are drawn; the row is blanked first.
void drawAnnunciators() {
  const int cy = L.ann_y + L.ann_h / 2;
  tft.fillRect(L.lcd_x + 2, L.ann_y - 1, L.lcd_w - 4, L.ann_h + 2, kColorLcd);
  tft.setFont(&fonts::Font2);
  tft.setTextSize(1);
  tft.setTextDatum(textdatum_t::middle_left);
  tft.setTextColor(kColorLcdInk);
  const int x0 = L.lcd_x + 8;
  if (s_mem != 0.0) tft.drawString("M", x0, cy);
  if (s_error) tft.drawString("E", x0 + 18, cy);
  const char ops[] = {'+', '-', '*', '/'};
  const int r = max(3, L.ann_h / 3);
  for (int i = 0; i < 4; ++i) {
    if (s_pending != ops[i]) continue;
    drawOpIcon(ops[i], x0 + 44 + i * (2 * r + 10) + r, cy, r, 2, kColorLcdInk);
  }
  if (s_hex) {
    tft.setTextDatum(textdatum_t::middle_right);
    tft.drawString("HEX", L.lcd_x + L.lcd_w - 8, cy);
  }
}

// Hex mode: digits in a font, one per cell, so B and D can be upper case (on seven segments
// they'd look like 8 and 0). Cleared and redrawn each time, no ghost segments.
constexpr float kHexFontCapPx = 25.0f;    // cap height of FreeSansBold18pt7b
constexpr float kHexFontWidestPx = 24.0f;  // its widest of 0-9 A-F

bool s_shown_hex = false;  // the display holds font digits (segments don't cover them)

void clearDigitArea() {
  const int y0 = L.dig_y - 1;
  const int y1 = L.lcd_y + L.lcd_h - 2;  // down to the glass edge: covers the comma tails
  tft.fillRect(L.cell_x0, y0, L.pitch * kCells, y1 - y0, kColorLcd);
}

void drawHexText() {
  char cells[kCells] = {0};
  if (s_error) {
    memcpy(cells + kCells - 5, "Error", 5);
  } else {
    char buf[24];
    if (s_typing) {
      strlcpy(buf, s_entry, sizeof(buf));
    } else if (!formatHex(s_x, buf, sizeof(buf))) {
      strcpy(buf, "0");
    }
    const bool neg = buf[0] == '-';
    const char* digits = neg ? buf + 1 : buf;
    const int nd = min<int>(strlen(digits), kDigits);
    memcpy(cells + kCells - nd, digits, nd);
    if (neg) cells[0] = '-';
  }
  const int x0 = L.cell_x0;
  clearDigitArea();
  s_shown_hex = true;
  tft.setFont(&fonts::FreeSansBold18pt7b);
  tft.setTextSize(min(1.0f, L.pitch * 0.9f / kHexFontWidestPx), L.dig_h / kHexFontCapPx);
  tft.setTextDatum(textdatum_t::baseline_center);
  tft.setTextColor(kColorLcdInk);
  for (int i = 0; i < kCells; ++i) {
    if (!cells[i]) continue;
    const char s[2] = {cells[i], 0};
    tft.drawString(s, x0 + i * L.pitch + L.dig_w / 2, L.dig_y + L.dig_h);
  }
  tft.setTextSize(1);
}

void drawDisplay() {
  if (s_hex) {
    tft.startWrite();
    drawHexText();
    drawAnnunciators();
    tft.endWrite();
    return;
  }
  uint8_t segs[kCells] = {0};
  bool dps[kCells] = {false};
  bool commas[kCells] = {false};
  if (s_error) {
    const uint8_t err[] = {0x79, 0x50, 0x50, 0x5C, 0x50};  // "Error"
    for (int i = 0; i < 5; ++i) segs[kCells - 5 + i] = err[i];
  } else {
    char buf[24];
    if (s_typing) {
      strlcpy(buf, s_entry, sizeof(buf));
    } else if (!(s_hex ? formatHex(s_x, buf, sizeof(buf)) : formatValue(s_x, buf, sizeof(buf)))) {
      strcpy(buf, "0");
    }
    // Exponent at the right: "E", its minus if any, two digits.
    int last = kCells - 1;  // rightmost cell for the number
    char* e = strchr(buf, 'e');  // hex digits are upper case, so no clash
    const bool exp_form = e != nullptr;
    if (exp_form) {
      const int exp = atoi(e + 1);
      *e = '\0';
      segs[last--] = kDigitSegs[abs(exp) % 10];
      segs[last--] = kDigitSegs[abs(exp) / 10];
      if (exp < 0) segs[last--] = kSegMinus;
      segs[last--] = kSegE;
    }
    // Digits right-aligned to `last`; a '.' lights the point after the digit before it, and
    // the integer part gets a comma every three digits (not in exponential form).
    int digits[kDigits];
    bool point_after[kDigits] = {false};
    int nd = 0;
    int n_int = -1;  // digits before the '.'
    for (const char* p = buf; *p && nd < kDigits; ++p) {
      if (*p == '.') {
        if (nd > 0) point_after[nd - 1] = true;
        n_int = nd;
      } else if (isDigitKey(*p)) {
        digits[nd++] = digitValue(*p);
      }
    }
    if (n_int < 0) n_int = nd;
    const int first = last - nd + 1;
    for (int k = 0; k < nd && first + k >= 1; ++k) {
      const int cell = first + k;
      segs[cell] = kDigitSegs[digits[k]];
      dps[cell] = point_after[k];
      commas[cell] = !exp_form && !s_hex && k < n_int - 1 && (n_int - 1 - k) % 3 == 0;
    }
    if (buf[0] == '-') segs[0] = kSegMinus;
  }
  tft.startWrite();
  if (s_shown_hex) {
    clearDigitArea();
    s_shown_hex = false;
  }
  for (int i = 0; i < kCells; ++i) drawCell(i, segs[i], dps[i], commas[i]);
  drawAnnunciators();
  tft.endWrite();
}

void keyRect(const Key& k, int* x, int* y, int* w, int* h) {
  const int pitch = L.key_w + L.gap;
  *x = L.keys_x + k.x100 * pitch / 100;
  *y = L.keys_y + k.row * (L.key_h + L.gap);
  *w = L.keys_x + (k.x100 + k.w100) * pitch / 100 - L.gap - *x;
  *h = L.key_h;
}

bool keyShown(const Key& k) { return k.pad == kBoth || (k.pad == kHexOnly) == s_hex; }

void drawKey(const Key& k, bool pressed) {
  int x, y, w, h;
  keyRect(k, &x, &y, &w, &h);
  const uint16_t fill = pressed ? kColorKeyFlash : k.color;
  const uint16_t ink = pressed ? TFT_BLACK : TFT_WHITE;
  tft.fillRoundRect(x, y, w, h, 7, fill);
  const char* label = k.label;
  if (k.id == kHex) label = s_hex ? "DEC" : "HEX";  // goes to the other mode
  if (!label) {
    const int r = min(w, h) / 5;
    drawOpIcon(k.id, x + w / 2, y + h / 2, r, max(3, r / 3), ink);
    return;
  }
  const bool digit = isDigitKey(k.id) || k.id == '.';
  if (h >= 40) {
    tft.setFont(digit ? &fonts::FreeSansBold18pt7b : &fonts::FreeSansBold12pt7b);
  } else {
    tft.setFont(digit ? &fonts::FreeSansBold12pt7b : &fonts::FreeSansBold9pt7b);
  }
  tft.setTextSize(1);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setTextColor(ink);
  tft.drawString(label, x + w / 2, y + h / 2);
}

void computeLayout() {
  const int W = tft.width();
  const int H = tft.height();
  constexpr int kMargin = 8;
  constexpr int kBezel = 6;
  constexpr int kPad = 8;
  // The width sets the digit size, the digits set the glass height, the keys get the rest.
  L.lcd_x = kMargin + kBezel;
  L.lcd_y = kMargin + kBezel;
  L.lcd_w = W - 2 * L.lcd_x;
  L.pitch = (L.lcd_w - 2 * kPad) / kCells;
  L.cell_x0 = L.lcd_x + kPad + (L.lcd_w - 2 * kPad - L.pitch * kCells) / 2;
  L.dig_w = L.pitch * 78 / 100;
  L.dig_h = min(L.dig_w * 2, H / 6);
  L.seg_t = max(3, L.dig_w / 4) | 1;  // odd, so the tips are symmetric
  L.ann_h = 14;
  L.ann_y = L.lcd_y + 3;
  L.dig_y = L.ann_y + L.ann_h + kPad;
  L.lcd_h = L.dig_y + L.dig_h + kPad - L.lcd_y;
  const int bezel_h = L.lcd_h + 2 * kBezel;

  L.gap = 6;
  L.keys_x = kMargin;
  L.keys_y = kMargin + bezel_h + 10;
  L.key_w = (W - 2 * kMargin - (kCols - 1) * L.gap) / kCols;
  L.key_h = (H - L.keys_y - kMargin - (kRows - 1) * L.gap) / kRows;
}

void drawKeypad() {
  tft.startWrite();
  tft.fillRect(0, L.keys_y - L.gap / 2, tft.width(), tft.height() - (L.keys_y - L.gap / 2),
               kColorCase);
  for (int i = 0; i < kKeyCount; ++i) {
    if (keyShown(kKeys[i])) drawKey(kKeys[i], false);
  }
  tft.endWrite();
}

void drawAll() {
  tft.startWrite();
  tft.fillScreen(kColorCase);
  tft.fillRoundRect(L.lcd_x - 6, L.lcd_y - 6, L.lcd_w + 12, L.lcd_h + 12, 8, kColorBezel);
  tft.fillRoundRect(L.lcd_x, L.lcd_y, L.lcd_w, L.lcd_h, 3, kColorLcd);
  tft.endWrite();
  drawKeypad();
  drawDisplay();
}

}  // namespace

void begin() {
  if (s_active) return;
  s_active = true;
  s_shown_hex = false;  // fresh screen
  computeLayout();
  drawAll();
}

void end() {
  if (!s_active) return;
  s_active = false;
  displayFontEnsureLoaded(tft);  // radar text uses the smooth vlw font
}

bool isActive() { return s_active; }

bool handleTap(int x, int y) {
  if (!s_active) return false;
  const int slop = L.gap / 2;
  for (int i = 0; i < kKeyCount; ++i) {
    const Key& k = kKeys[i];
    if (!keyShown(k)) continue;
    int kx, ky, kw, kh;
    keyRect(k, &kx, &ky, &kw, &kh);
    if (x < kx - slop || x >= kx + kw + slop || y < ky - slop || y >= ky + kh + slop) continue;
    if (k.id == kExit) return true;
    drawKey(k, true);
    pressKey(k.id);
    drawDisplay();
    delay(kKeyFlashMs);
    if (k.id == kHex) {
      drawKeypad();  // the other keypad
    } else {
      drawKey(k, false);
    }
    return false;
  }
  return false;
}

}  // namespace ui::calculator
