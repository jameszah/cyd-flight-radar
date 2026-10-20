#include "ui/screensaver.h"

#include <Arduino.h>

#include "config.h"
#include "hardware/display.h"
#include "hardware/display_font.h"

namespace ui::screensaver {
namespace {

// Matrix-style rain: one stream per column, halfwidth katakana + digits, each trail
// fading from a pale head through bright green to dark green.
constexpr int kCellW = 7;   // halfwidth glyphs in lgfxJapanGothic_12 are ~6px wide
constexpr int kCellH = 12;
constexpr int kMaxTicksPerRow = 3;    // stream speed: 1 = fastest
constexpr int kGlitchesPerFrame = 8;  // random characters changed inside trails

const uint16_t kGlyphs[] = {
    // Halfwidth katakana U+FF66..U+FF9D
    0xFF66, 0xFF67, 0xFF68, 0xFF69, 0xFF6A, 0xFF6B, 0xFF6C, 0xFF6D, 0xFF6E, 0xFF6F,
    0xFF70, 0xFF71, 0xFF72, 0xFF73, 0xFF74, 0xFF75, 0xFF76, 0xFF77, 0xFF78, 0xFF79,
    0xFF7A, 0xFF7B, 0xFF7C, 0xFF7D, 0xFF7E, 0xFF7F, 0xFF80, 0xFF81, 0xFF82, 0xFF83,
    0xFF84, 0xFF85, 0xFF86, 0xFF87, 0xFF88, 0xFF89, 0xFF8A, 0xFF8B, 0xFF8C, 0xFF8D,
    0xFF8E, 0xFF8F, 0xFF90, 0xFF91, 0xFF92, 0xFF93, 0xFF94, 0xFF95, 0xFF96, 0xFF97,
    0xFF98, 0xFF99, 0xFF9A, 0xFF9B, 0xFF9C, 0xFF9D,
    // A few digits and symbols, as in the film
    '0', '1', '2', '3', '4', '5', '7', '8', '9', ':', '.', '=', '*', '+', '-', '<', '>', '|',
};
constexpr int kGlyphCount = sizeof(kGlyphs) / sizeof(kGlyphs[0]);

// Head first, then fading to dark green.
const uint16_t kShades[] = {
    lgfx::color565(200, 255, 200), lgfx::color565(110, 255, 110), lgfx::color565(0, 230, 40),
    lgfx::color565(0, 190, 25),    lgfx::color565(0, 150, 15),    lgfx::color565(0, 115, 10),
    lgfx::color565(0, 85, 5),      lgfx::color565(0, 60, 0),
};
constexpr int kShadeCount = sizeof(kShades) / sizeof(kShades[0]);

struct Stream {
  int16_t y;      // head row; negative = waiting above the screen
  int16_t trail;  // rows, including the head
  uint8_t ticks_per_row;
  uint8_t ticks;
};

bool s_active = false;
unsigned long s_last_frame_ms = 0;
int s_cols = 0;
int s_rows = 0;
Stream* s_streams = nullptr;  // one per column
uint8_t* s_cells = nullptr;   // glyph index per cell, so trails fade without changing glyph

int shadeFor(int dist, int trail) {
  if (dist <= 1) return dist;
  const int s = 2 + (dist - 2) * (kShadeCount - 2) / max(1, trail - 2);
  return min(s, kShadeCount - 1);
}

void drawCell(int col, int row, uint16_t color, bool new_glyph) {
  if (row < 0 || row >= s_rows) return;
  uint8_t& cell = s_cells[row * s_cols + col];
  const int x = col * kCellW;
  const int y = row * kCellH;
  if (new_glyph) {
    cell = random(0, kGlyphCount);
    tft.fillRect(x, y, kCellW, kCellH, TFT_BLACK);
  }
  // UTF-8 encode (all glyphs are below U+10000).
  const uint16_t cp = kGlyphs[cell];
  char buf[4] = {0};
  if (cp < 0x80) {
    buf[0] = static_cast<char>(cp);
  } else {
    buf[0] = static_cast<char>(0xE0 | (cp >> 12));
    buf[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    buf[2] = static_cast<char>(0x80 | (cp & 0x3F));
  }
  tft.setTextColor(color);  // transparent: the same glyph redrawn darker covers the old pixels
  tft.drawString(buf, x, y);
}

void eraseCell(int col, int row) {
  if (row < 0 || row >= s_rows) return;
  tft.fillRect(col * kCellW, row * kCellH, kCellW, kCellH, TFT_BLACK);
}

void resetStream(Stream& s, bool start_on_screen) {
  s.trail = random(s_rows / 3, s_rows + 1);
  s.y = start_on_screen ? random(0, s_rows + s.trail) : -random(0, s_rows / 2);
  s.ticks_per_row = random(1, kMaxTicksPerRow + 1);
  s.ticks = 0;
}

void stepStream(int col, Stream& s) {
  ++s.y;
  eraseCell(col, s.y - s.trail);
  drawCell(col, s.y, kShades[0], true);  // new head
  // Redraw only the cells whose shade changes now that the head moved down.
  for (int d = 1; d < s.trail; ++d) {
    const int shade = shadeFor(d, s.trail);
    if (shade != shadeFor(d - 1, s.trail)) {
      drawCell(col, s.y - d, kShades[shade], false);
    }
  }
  if (s.y - s.trail >= s_rows) {
    resetStream(s, false);
  }
}

void glitch() {
  const int col = random(0, s_cols);
  const Stream& s = s_streams[col];
  if (s.y < 1) return;
  const int d = random(1, s.trail);
  drawCell(col, s.y - d, kShades[shadeFor(d, s.trail)], true);
}

}  // namespace

void begin() {
  s_cols = tft.width() / kCellW;
  s_rows = tft.height() / kCellH;
  s_streams = static_cast<Stream*>(malloc(s_cols * sizeof(Stream)));
  s_cells = static_cast<uint8_t*>(calloc(s_cols * s_rows, 1));
  if (!s_streams || !s_cells) {  // low heap: skip the screensaver rather than crash
    free(s_streams);
    free(s_cells);
    s_streams = nullptr;
    s_cells = nullptr;
    return;
  }
  s_active = true;
  s_last_frame_ms = 0;
  tft.fillScreen(TFT_BLACK);
  tft.setFont(&fonts::lgfxJapanGothic_12);
  tft.setTextSize(1);
  tft.setTextDatum(textdatum_t::top_left);
  tft.startWrite();
  for (int c = 0; c < s_cols; ++c) {
    // Start mid-fall with the trail already drawn, so the screen is full straight away.
    Stream& s = s_streams[c];
    resetStream(s, true);
    for (int d = s.trail - 1; d >= 0; --d) {
      drawCell(c, s.y - d, kShades[shadeFor(d, s.trail)], true);
    }
  }
  tft.endWrite();
}

void tick() {
  if (!s_active || millis() - s_last_frame_ms < config::kScreensaverFrameMs) return;
  s_last_frame_ms = millis();

  tft.startWrite();
  for (int c = 0; c < s_cols; ++c) {
    Stream& s = s_streams[c];
    if (++s.ticks >= s.ticks_per_row) {
      s.ticks = 0;
      stepStream(c, s);
    }
  }
  for (int i = 0; i < kGlitchesPerFrame; ++i) {
    glitch();
  }
  tft.endWrite();
}

void end() {
  if (!s_active) return;
  s_active = false;
  free(s_streams);
  free(s_cells);
  s_streams = nullptr;
  s_cells = nullptr;
  displayFontEnsureLoaded(tft);  // radar text uses the smooth vlw font
}

bool isActive() { return s_active; }

}  // namespace ui::screensaver
