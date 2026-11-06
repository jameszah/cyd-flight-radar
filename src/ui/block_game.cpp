#include "ui/block_game.h"

#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include <string.h>

#include "hardware/display.h"
#include "hardware/display_font.h"

namespace ui::block_game {
namespace {

// Falling-block puzzle: 10x20 well, the seven four-cell pieces from a shuffled bag, ghost piece,
// simple wall kicks, lock delay, level speed-up every 10 lines. Only cells that change are
// redrawn (s_shown), so nothing flickers. Everything is sized from tft.width/height.

constexpr int kCols = 10;
constexpr int kRows = 20;
constexpr int kPieces = 7;

constexpr unsigned long kLockDelayMs = 450;
constexpr int kMaxLockResets = 15;
constexpr unsigned long kClearFlashMs = 220;
constexpr unsigned long kRepeatDelayMs = 170;  // left / right: first repeat
constexpr unsigned long kRepeatMs = 55;        // left / right: then every
constexpr int kLineScore[5] = {0, 100, 300, 500, 800};

constexpr uint16_t kColorBg = lgfx::color565(14, 16, 26);
constexpr uint16_t kColorWell = lgfx::color565(4, 6, 14);
constexpr uint16_t kColorGrid = lgfx::color565(22, 26, 40);
constexpr uint16_t kColorFrame = lgfx::color565(90, 100, 130);
constexpr uint16_t kColorLabel = lgfx::color565(150, 160, 185);
constexpr uint16_t kColorValue = TFT_WHITE;
constexpr uint16_t kColorFlash = TFT_WHITE;
constexpr uint16_t kColorBtnMove = lgfx::color565(50, 90, 160);
constexpr uint16_t kColorBtnRotate = lgfx::color565(140, 60, 170);
constexpr uint16_t kColorBtnDrop = lgfx::color565(220, 120, 20);
constexpr uint16_t kColorBtnPause = lgfx::color565(80, 84, 96);
constexpr uint16_t kColorBtnExit = lgfx::color565(170, 40, 36);
constexpr uint16_t kColorBtnPressed = lgfx::color565(235, 235, 235);

// Piece colours: I O T S Z J L.
constexpr uint8_t kPieceRgb[kPieces][3] = {
    {0, 200, 230}, {240, 210, 0}, {170, 70, 220}, {40, 200, 70},
    {230, 50, 50}, {40, 90, 235}, {245, 140, 20},
};

// Spawn shapes as (row, col) in an N x N box; rotation turns the box clockwise.
struct Shape {
  uint8_t n;
  int8_t cell[4][2];
};
const Shape kShapes[kPieces] = {
    {4, {{1, 0}, {1, 1}, {1, 2}, {1, 3}}},  // I
    {2, {{0, 0}, {0, 1}, {1, 0}, {1, 1}}},  // O
    {3, {{0, 1}, {1, 0}, {1, 1}, {1, 2}}},  // T
    {3, {{0, 1}, {0, 2}, {1, 0}, {1, 1}}},  // S
    {3, {{0, 0}, {0, 1}, {1, 1}, {1, 2}}},  // Z
    {3, {{0, 0}, {1, 0}, {1, 1}, {1, 2}}},  // J
    {3, {{0, 2}, {1, 0}, {1, 1}, {1, 2}}},  // L
};

enum State : uint8_t { kReady, kPlaying, kPaused, kClearing, kOver };
enum ButtonId : uint8_t { kLeft, kRotateL, kDrop, kRotateR, kRight, kPause, kExit, kButtonCount };

struct Rect {
  int x, y, w, h;
  bool hit(int px, int py, int slop) const {
    return px >= x - slop && px < x + w + slop && py >= y - slop && py < y + h + slop;
  }
};

struct Layout {
  int cs;           // cell size
  int bx, by;       // well origin (inside the frame)
  int side_x, side_w;
  Rect next_box;
  int pc;           // preview cell size
  int score_y, lines_y, level_y, best_y;  // value baselines (labels above)
  int value_h;
  Rect btn[kButtonCount];
  const lgfx::IFont* label_font;
  const lgfx::IFont* value_font;
  const lgfx::IFont* big_font;
} L;

// Game state (kept while closed).
uint8_t s_grid[kRows][kCols];  // 0 empty, 1..7 piece type + 1
State s_state = kReady;
int s_type = 0, s_rot = 0, s_px = 0, s_py = 0;
int s_next = 0;
uint8_t s_bag[kPieces];
int s_bag_pos = kPieces;
uint32_t s_score = 0, s_lines = 0, s_best = 0;
int s_level = 0;
unsigned long s_last_fall_ms = 0;
bool s_grounded = false;
unsigned long s_ground_ms = 0;
int s_lock_resets = 0;
uint32_t s_clear_mask = 0;
unsigned long s_clear_ms = 0;
bool s_best_loaded = false;

// Screen state.
bool s_active = false;
uint8_t s_shown[kRows][kCols];  // what's on the panel: 0 empty, 1..7 solid, 9..15 ghost, 16 flash
uint32_t s_shown_score = ~0u, s_shown_lines = ~0u, s_shown_best = ~0u;
int s_shown_level = -1, s_shown_next = -1;
bool s_shown_pause_label_play = false;
bool s_overlay_drawn = false;

// Touch.
bool s_was_touched = false;
int s_held = -1;
unsigned long s_next_repeat_ms = 0;

uint16_t pieceColor(int type, int pct) {  // pct: 100 = base, <100 darker, >100 lighter
  int rgb[3];
  for (int i = 0; i < 3; ++i) {
    int v = kPieceRgb[type][i];
    v = pct <= 100 ? v * pct / 100 : v + (255 - v) * (pct - 100) / 100;
    rgb[i] = constrain(v, 0, 255);
  }
  return lgfx::color565(rgb[0], rgb[1], rgb[2]);
}

void pieceCells(int type, int rot, int out[4][2]) {
  const Shape& s = kShapes[type];
  for (int i = 0; i < 4; ++i) {
    int r = s.cell[i][0], c = s.cell[i][1];
    for (int k = 0; k < (rot & 3); ++k) {
      const int nr = c;
      c = s.n - 1 - r;
      r = nr;
    }
    out[i][0] = r;
    out[i][1] = c;
  }
}

bool fits(int type, int rot, int px, int py) {
  int cells[4][2];
  pieceCells(type, rot, cells);
  for (auto& cell : cells) {
    const int r = py + cell[0], c = px + cell[1];
    if (c < 0 || c >= kCols || r >= kRows) return false;
    if (r >= 0 && s_grid[r][c]) return false;
  }
  return true;
}

int nextFromBag() {
  if (s_bag_pos >= kPieces) {
    for (int i = 0; i < kPieces; ++i) s_bag[i] = i;
    for (int i = kPieces - 1; i > 0; --i) {
      const int j = esp_random() % (i + 1);
      const uint8_t t = s_bag[i];
      s_bag[i] = s_bag[j];
      s_bag[j] = t;
    }
    s_bag_pos = 0;
  }
  return s_bag[s_bag_pos++];
}

unsigned long fallIntervalMs() {
  return max(60UL, static_cast<unsigned long>(800.0f * powf(0.85f, s_level)));
}

void loadBest() {
  if (s_best_loaded) return;
  Preferences p;
  if (p.begin("blocks", true)) {
    s_best = p.getUInt("best", 0);
    p.end();
  }
  s_best_loaded = true;
}

void saveBest() {
  Preferences p;
  if (p.begin("blocks", false)) {
    p.putUInt("best", s_best);
    p.end();
  }
}

void gameOver() {
  s_state = kOver;
  s_held = -1;
  if (s_score > s_best) {
    s_best = s_score;
    saveBest();
  }
}

void spawn() {
  s_type = s_next;
  s_next = nextFromBag();
  s_rot = 0;
  s_px = (kCols - kShapes[s_type].n) / 2;
  s_py = 0;
  s_grounded = false;
  s_lock_resets = 0;
  s_last_fall_ms = millis();
  if (!fits(s_type, s_rot, s_px, s_py)) gameOver();
}

void newGame() {
  memset(s_grid, 0, sizeof(s_grid));
  s_score = 0;
  s_lines = 0;
  s_level = 0;
  s_clear_mask = 0;
  s_bag_pos = kPieces;
  s_next = nextFromBag();
  s_state = kPlaying;
  spawn();
}

// A successful move or rotation while on the ground restarts the lock delay (a few times).
void movedOk() {
  if (s_grounded && s_lock_resets < kMaxLockResets) {
    s_ground_ms = millis();
    ++s_lock_resets;
  }
}

bool tryMove(int dx, int dy) {
  if (!fits(s_type, s_rot, s_px + dx, s_py + dy)) return false;
  s_px += dx;
  s_py += dy;
  movedOk();
  return true;
}

void tryRotate(bool clockwise) {
  const int rot = (s_rot + (clockwise ? 1 : 3)) & 3;
  static const int8_t kKicks[][2] = {{0, 0}, {-1, 0}, {1, 0}, {-2, 0}, {2, 0},
                                     {0, -1}, {-1, -1}, {1, -1}};
  for (auto& k : kKicks) {
    if (fits(s_type, rot, s_px + k[0], s_py + k[1])) {
      s_rot = rot;
      s_px += k[0];
      s_py += k[1];
      movedOk();
      return;
    }
  }
}

void lockPiece() {
  int cells[4][2];
  pieceCells(s_type, s_rot, cells);
  for (auto& cell : cells) {
    const int r = s_py + cell[0], c = s_px + cell[1];
    if (r < 0) {
      gameOver();
      return;
    }
    s_grid[r][c] = s_type + 1;
  }
  s_clear_mask = 0;
  for (int r = 0; r < kRows; ++r) {
    bool full = true;
    for (int c = 0; c < kCols && full; ++c) full = s_grid[r][c] != 0;
    if (full) s_clear_mask |= 1u << r;
  }
  if (s_clear_mask) {
    s_state = kClearing;
    s_clear_ms = millis();
  } else {
    spawn();
  }
}

void finishClear() {
  int n = 0;
  int dst = kRows - 1;
  for (int r = kRows - 1; r >= 0; --r) {
    if (s_clear_mask & (1u << r)) {
      ++n;
      continue;
    }
    if (dst != r) memcpy(s_grid[dst], s_grid[r], kCols);
    --dst;
  }
  for (; dst >= 0; --dst) memset(s_grid[dst], 0, kCols);
  s_clear_mask = 0;
  s_score += kLineScore[min(n, 4)] * (s_level + 1);
  s_lines += n;
  s_level = s_lines / 10;
  s_state = kPlaying;
  spawn();
}

void hardDrop() {
  int rows = 0;
  while (fits(s_type, s_rot, s_px, s_py + 1)) {
    ++s_py;
    ++rows;
  }
  s_score += 2 * rows;
  lockPiece();
}

// ---- Drawing ----

void drawCell(int r, int c, uint8_t code) {
  const int x = L.bx + c * L.cs;
  const int y = L.by + r * L.cs;
  const int s = L.cs - 1;  // 1 px grid line right / below
  tft.fillRect(x + s, y, 1, L.cs, kColorGrid);
  tft.fillRect(x, y + s, s, 1, kColorGrid);
  if (code == 0) {
    tft.fillRect(x, y, s, s, kColorWell);
  } else if (code == 16) {
    tft.fillRect(x, y, s, s, kColorFlash);
  } else if (code >= 9) {  // ghost: outline in a dim piece colour
    const int t = max(1, L.cs / 14);
    const uint16_t col = pieceColor(code - 9, 55);
    tft.fillRect(x, y, s, s, kColorWell);
    for (int i = 0; i < t; ++i) tft.drawRect(x + 1 + i, y + 1 + i, s - 2 - 2 * i, s - 2 - 2 * i, col);
  } else {  // solid block with a bevel
    const int type = code - 1;
    const int b = max(2, L.cs / 7);
    tft.fillRect(x, y, s, s, pieceColor(type, 100));
    tft.fillRect(x, y, s, b, pieceColor(type, 150));
    tft.fillRect(x, y, b, s, pieceColor(type, 135));
    tft.fillRect(x, y + s - b, s, b, pieceColor(type, 60));
    tft.fillRect(x + s - b, y, b, s, pieceColor(type, 70));
    tft.fillTriangle(x, y + s - 1, x + b - 1, y + s - b, x, y + s - b, pieceColor(type, 135));
    tft.fillTriangle(x + s - 1, y, x + s - b, y + b - 1, x + s - b, y, pieceColor(type, 150));
  }
}

void drawPreviewCell(int x, int y, int sz, int type) {
  const int s = sz - 1;
  const int b = max(1, sz / 7);
  tft.fillRect(x, y, s, s, pieceColor(type, 100));
  tft.fillRect(x, y, s, b, pieceColor(type, 150));
  tft.fillRect(x, y, b, s, pieceColor(type, 135));
  tft.fillRect(x, y + s - b, s, b, pieceColor(type, 60));
  tft.fillRect(x + s - b, y, b, s, pieceColor(type, 70));
}

void renderBoard() {
  uint8_t want[kRows][kCols];
  memcpy(want, s_grid, sizeof(want));
  if (s_state == kPlaying || s_state == kPaused) {
    int cells[4][2];
    pieceCells(s_type, s_rot, cells);
    int gy = s_py;
    while (fits(s_type, s_rot, s_px, gy + 1)) ++gy;
    for (auto& cell : cells) {
      const int r = gy + cell[0], c = s_px + cell[1];
      if (r >= 0 && !want[r][c]) want[r][c] = s_type + 9;
    }
    for (auto& cell : cells) {
      const int r = s_py + cell[0], c = s_px + cell[1];
      if (r >= 0) want[r][c] = s_type + 1;
    }
  } else if (s_state == kClearing) {
    for (int r = 0; r < kRows; ++r) {
      if (s_clear_mask & (1u << r)) memset(want[r], 16, kCols);
    }
  }
  tft.startWrite();
  for (int r = 0; r < kRows; ++r) {
    for (int c = 0; c < kCols; ++c) {
      if (want[r][c] != s_shown[r][c]) {
        drawCell(r, c, want[r][c]);
        s_shown[r][c] = want[r][c];
      }
    }
  }
  tft.endWrite();
}

void drawValue(int y, const char* text) {
  tft.fillRect(L.side_x, y - L.value_h, L.side_w, L.value_h + 2, kColorBg);
  tft.setFont(L.value_font);
  tft.setTextSize(1);
  tft.setTextDatum(textdatum_t::baseline_left);
  tft.setTextColor(kColorValue);
  tft.drawString(text, L.side_x, y);
}

void drawNext() {
  const Rect& b = L.next_box;
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 6, kColorWell);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 6, kColorFrame);
  int cells[4][2];
  pieceCells(s_next, 0, cells);
  int rmin = 9, rmax = -9, cmin = 9, cmax = -9;
  for (auto& cell : cells) {
    rmin = min(rmin, static_cast<int>(cell[0]));
    rmax = max(rmax, static_cast<int>(cell[0]));
    cmin = min(cmin, static_cast<int>(cell[1]));
    cmax = max(cmax, static_cast<int>(cell[1]));
  }
  const int w = (cmax - cmin + 1) * L.pc;
  const int h = (rmax - rmin + 1) * L.pc;
  const int x0 = b.x + (b.w - w) / 2;
  const int y0 = b.y + (b.h - h) / 2;
  for (auto& cell : cells) {
    drawPreviewCell(x0 + (cell[1] - cmin) * L.pc, y0 + (cell[0] - rmin) * L.pc, L.pc, s_next);
  }
}

void renderSide() {
  char buf[16];
  tft.startWrite();
  if (s_shown_next != s_next) {
    s_shown_next = s_next;
    drawNext();
  }
  if (s_shown_score != s_score) {
    s_shown_score = s_score;
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(s_score));
    drawValue(L.score_y, buf);
  }
  if (s_shown_lines != s_lines) {
    s_shown_lines = s_lines;
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(s_lines));
    drawValue(L.lines_y, buf);
  }
  if (s_shown_level != s_level) {
    s_shown_level = s_level;
    snprintf(buf, sizeof(buf), "%d", s_level + 1);
    drawValue(L.level_y, buf);
  }
  if (s_shown_best != s_best) {
    s_shown_best = s_best;
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(s_best));
    drawValue(L.best_y, buf);
  }
  tft.endWrite();
}

void drawArrow(int cx, int cy, int r, int dir, uint16_t col) {  // dir: 0 left, 1 right, 2 down
  if (dir == 0) tft.fillTriangle(cx - r, cy, cx + r / 2, cy - r, cx + r / 2, cy + r, col);
  if (dir == 1) tft.fillTriangle(cx + r, cy, cx - r / 2, cy - r, cx - r / 2, cy + r, col);
  if (dir == 2) tft.fillTriangle(cx, cy + r, cx - r, cy - r / 2, cx + r, cy - r / 2, col);
}

// Circular arrow with a gap at the top. Clockwise: head at the upper-left end (210 deg) pointing
// up-right; counter-clockwise is the mirror image (head at the upper right pointing up-left).
void drawRotateIcon(int cx, int cy, int r, bool clockwise, uint16_t col) {
  const int t = max(3, r / 3);
  if (clockwise) {
    tft.fillArc(cx, cy, r, r - t, 0, 210, col);
    tft.fillArc(cx, cy, r, r - t, 300, 360, col);
  } else {  // angles mirrored: a -> 180 - a
    tft.fillArc(cx, cy, r, r - t, 0, 180, col);
    tft.fillArc(cx, cy, r, r - t, 330, 360, col);
    tft.fillArc(cx, cy, r, r - t, 180, 240, col);
  }
  const float a = 210.0f * DEG_TO_RAD;
  const float rm = r - t / 2.0f;
  const float s = clockwise ? 1.0f : -1.0f;  // mirror x for counter-clockwise
  const float px = cx + s * rm * cosf(a), py = cy + rm * sinf(a);
  const float tx = -s * sinf(a), ty = cosf(a);  // direction of travel
  const float nx = s * cosf(a), ny = sinf(a);   // radial
  const float h = t * 1.6f;
  tft.fillTriangle(static_cast<int>(px + tx * h), static_cast<int>(py + ty * h),
                   static_cast<int>(px + nx * h), static_cast<int>(py + ny * h),
                   static_cast<int>(px - nx * h), static_cast<int>(py - ny * h), col);
}

uint16_t buttonColor(int id) {
  switch (id) {
    case kRotateL:
    case kRotateR: return kColorBtnRotate;
    case kDrop: return kColorBtnDrop;
    case kPause: return kColorBtnPause;
    case kExit: return kColorBtnExit;
    default: return kColorBtnMove;
  }
}

void drawButton(int id, bool pressed) {
  const Rect& b = L.btn[id];
  const uint16_t fill = pressed ? kColorBtnPressed : buttonColor(id);
  const uint16_t ink = pressed ? TFT_BLACK : TFT_WHITE;
  tft.startWrite();
  tft.fillRoundRect(b.x, b.y, b.w, b.h, max(4, b.h / 8), fill);
  const int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
  const int r = min(b.w, b.h) * 28 / 100;
  switch (id) {
    case kLeft: drawArrow(cx, cy, r, 0, ink); break;
    case kRight: drawArrow(cx, cy, r, 1, ink); break;
    case kRotateL: drawRotateIcon(cx, cy, r, false, ink); break;
    case kRotateR: drawRotateIcon(cx, cy, r, true, ink); break;
    case kDrop:  // arrow onto a bar
      drawArrow(cx, cy - r / 4, r * 3 / 4, 2, ink);
      tft.fillRect(cx - r, cy + r * 3 / 4, 2 * r + 1, max(3, r / 4), ink);
      break;
    default: {
      const bool play = id == kPause && s_state != kPlaying && s_state != kClearing;
      if (id == kPause) s_shown_pause_label_play = play;
      tft.setFont(L.label_font);
      tft.setTextSize(1);
      tft.setTextDatum(textdatum_t::middle_center);
      tft.setTextColor(ink);
      tft.drawString(id == kExit ? "EXIT" : (play ? "PLAY" : "PAUSE"), cx, cy);
      break;
    }
  }
  tft.endWrite();
}

void drawOverlay() {
  const char* title = s_state == kPaused ? "PAUSED" : (s_state == kOver ? "GAME OVER" : "BLOCKS");
  const char* hint = s_state == kPaused ? "tap to resume" : "tap to play";
  const int w = kCols * L.cs * 86 / 100;
  const int h = L.cs * 4;
  const int x = L.bx + (kCols * L.cs - w) / 2;
  const int y = L.by + (kRows * L.cs - h) / 2;
  tft.startWrite();
  tft.fillRoundRect(x, y, w, h, 8, kColorBg);
  tft.drawRoundRect(x, y, w, h, 8, kColorFrame);
  tft.setTextSize(1);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setFont(L.big_font);
  tft.setTextColor(s_state == kOver ? lgfx::color565(240, 70, 60) : kColorValue);
  if (tft.textWidth(title) > w - 8) tft.setFont(L.value_font);
  tft.drawString(title, x + w / 2, y + h * 38 / 100);
  tft.setFont(L.label_font);
  tft.setTextColor(kColorLabel);
  tft.drawString(hint, x + w / 2, y + h * 75 / 100);
  tft.endWrite();
  s_overlay_drawn = true;
}

void computeLayout() {
  const int W = tft.width();
  const int H = tft.height();
  const bool big = W >= 400;
  L.label_font = big ? static_cast<const lgfx::IFont*>(&fonts::FreeSansBold12pt7b) : &fonts::Font2;
  L.value_font = big ? &fonts::FreeSansBold18pt7b : &fonts::FreeSansBold9pt7b;
  L.big_font = big ? &fonts::FreeSansBold24pt7b : &fonts::FreeSansBold12pt7b;

  const int m = max(4, W / 60);
  const int frame = 3;
  const int btn_h = H * 12 / 100;
  const int btn_y = H - m - btn_h;
  const int avail_h = btn_y - m - m - 2 * frame;
  const int side_min = W * 25 / 100;
  L.cs = min(avail_h / kRows, (W - 3 * m - 2 * frame - side_min) / kCols);
  L.bx = m + frame;
  L.by = m + frame + (avail_h - kRows * L.cs) / 2;
  L.side_x = L.bx + kCols * L.cs + frame + m;
  L.side_w = W - m - L.side_x;

  // Bottom row: left, rotate left, drop (centre), rotate right, right.
  const int gap = max(4, m);
  const int bw = (W - 2 * m - 4 * gap) / 5;
  const int order[5] = {kLeft, kRotateL, kDrop, kRotateR, kRight};
  for (int i = 0; i < 5; ++i) L.btn[order[i]] = {m + i * (bw + gap), btn_y, bw, btn_h};

  // Side panel: NEXT box, four label / value pairs, PAUSE and EXIT at the bottom.
  tft.setTextSize(1);
  tft.setFont(L.label_font);
  const int label_h = tft.fontHeight();
  tft.setFont(L.value_font);
  L.value_h = tft.fontHeight();
  int y = L.by;
  y += label_h;  // "NEXT"
  L.pc = min(L.cs * 3 / 4, (L.side_w - 8) / 4);
  L.next_box = {L.side_x, y, L.side_w, L.pc * 3};
  y += L.next_box.h + label_h / 2;
  const int sb_h = btn_h * 70 / 100;
  const int panel_bottom = L.by + kRows * L.cs;
  L.btn[kExit] = {L.side_x, panel_bottom - sb_h, L.side_w, sb_h};
  L.btn[kPause] = {L.side_x, panel_bottom - 2 * sb_h - gap, L.side_w, sb_h};
  // Four label / value pairs between the NEXT box and PAUSE (squeezed on small screens).
  const int pair = min(label_h + L.value_h + label_h / 3, (L.btn[kPause].y - gap - y) / 4);
  L.score_y = y + pair - label_h / 3;
  L.lines_y = L.score_y + pair;
  L.level_y = L.lines_y + pair;
  L.best_y = L.level_y + pair;
}

void drawLabel(int value_y, const char* text) {
  tft.setFont(L.label_font);
  tft.setTextSize(1);
  tft.setTextDatum(textdatum_t::baseline_left);
  tft.setTextColor(kColorLabel);
  tft.drawString(text, L.side_x, value_y - L.value_h - 2);
}

void drawAll() {
  tft.startWrite();
  tft.fillScreen(kColorBg);
  tft.fillRoundRect(L.bx - 3, L.by - 3, kCols * L.cs + 5, kRows * L.cs + 5, 4, kColorFrame);
  tft.fillRect(L.bx - 1, L.by - 1, kCols * L.cs + 1, kRows * L.cs + 1, kColorGrid);
  tft.setFont(L.label_font);
  tft.setTextSize(1);
  tft.setTextDatum(textdatum_t::bottom_left);
  tft.setTextColor(kColorLabel);
  tft.drawString("NEXT", L.side_x, L.next_box.y - 2);
  drawLabel(L.score_y, "SCORE");
  drawLabel(L.lines_y, "LINES");
  drawLabel(L.level_y, "LEVEL");
  drawLabel(L.best_y, "BEST");
  tft.endWrite();
  memset(s_shown, 0xFF, sizeof(s_shown));
  s_shown_score = s_shown_lines = s_shown_best = ~0u;
  s_shown_level = s_shown_next = -1;
  for (int i = 0; i < kButtonCount; ++i) drawButton(i, false);
  renderBoard();
  renderSide();
  s_overlay_drawn = false;
}

void setState(State st) {
  const bool was_overlay = s_state == kReady || s_state == kPaused || s_state == kOver;
  s_state = st;
  const bool overlay = st == kReady || st == kPaused || st == kOver;
  if (was_overlay && !overlay) {
    memset(s_shown, 0xFF, sizeof(s_shown));  // repaint the cells under the overlay
    s_last_fall_ms = millis();
  }
  s_overlay_drawn = false;
}

// Start / resume from an overlay.
void play() {
  if (s_state == kPaused) {
    setState(kPlaying);
  } else if (s_state == kReady || s_state == kOver) {
    setState(kPlaying);
    newGame();
  }
}

void pressButton(int id) {
  const bool playing = s_state == kPlaying;
  switch (id) {
    case kPause:
      if (playing || s_state == kClearing) {
        if (s_state == kClearing) finishClear();
        setState(kPaused);
      } else {
        play();
      }
      break;
    case kLeft: if (playing) tryMove(-1, 0); break;
    case kRight: if (playing) tryMove(1, 0); break;
    case kRotateL: if (playing) tryRotate(false); break;
    case kRotateR: if (playing) tryRotate(true); break;
    case kDrop: if (playing) hardDrop(); break;
    default: break;
  }
}

}  // namespace

void begin() {
  if (s_active) return;
  s_active = true;
  loadBest();
  s_was_touched = false;
  s_held = -1;
  computeLayout();
  drawAll();
}

void end() {
  if (!s_active) return;
  s_active = false;
  if (s_state == kClearing) finishClear();
  if (s_state == kPlaying) s_state = kPaused;  // resume where it was left
  displayFontEnsureLoaded(tft);  // radar text uses the smooth vlw font
}

bool isActive() { return s_active; }

bool handleTouch(bool touched, int x, int y) {
  if (!s_active) return false;
  const bool pressed_now = touched && !s_was_touched;
  const bool released = !touched && s_was_touched;
  s_was_touched = touched;
  if (released) {
    if (s_held >= 0) drawButton(s_held, false);
    s_held = -1;
    return false;
  }
  if (!pressed_now) return false;

  const int slop = max(2, tft.width() / 120);
  for (int i = 0; i < kButtonCount; ++i) {
    if (!L.btn[i].hit(x, y, slop)) continue;
    if (i == kExit) return true;
    drawButton(i, true);
    s_held = i;
    pressButton(i);
    s_next_repeat_ms = millis() + kRepeatDelayMs;
    return false;
  }
  // Tap on the well: start / resume from an overlay; while playing the left half rotates
  // counter-clockwise, the right half clockwise.
  const Rect well = {L.bx, L.by, kCols * L.cs, kRows * L.cs};
  if (well.hit(x, y, 0)) {
    if (s_state == kPlaying) {
      tryRotate(x >= L.bx + kCols * L.cs / 2);
    } else if (s_state != kClearing) {
      play();
    }
  }
  return false;
}

void tick() {
  if (!s_active) return;
  const unsigned long now = millis();

  // Held left / right repeat.
  if (s_held == kLeft || s_held == kRight) {
    if (static_cast<long>(now - s_next_repeat_ms) >= 0) {
      pressButton(s_held);
      s_next_repeat_ms = now + kRepeatMs;
    }
  }

  if (s_state == kPlaying) {
    const bool grounded = !fits(s_type, s_rot, s_px, s_py + 1);
    if (grounded) {
      if (!s_grounded) {
        s_grounded = true;
        s_ground_ms = now;
      }
      if (now - s_ground_ms >= kLockDelayMs) lockPiece();
    } else {
      s_grounded = false;
      if (now - s_last_fall_ms >= fallIntervalMs()) {
        s_last_fall_ms = now;
        ++s_py;
      }
    }
  } else if (s_state == kClearing) {
    if (now - s_clear_ms >= kClearFlashMs) finishClear();
  }

  const bool overlay = s_state == kReady || s_state == kPaused || s_state == kOver;
  if (overlay) {
    if (!s_overlay_drawn) {
      renderBoard();  // last piece / cleared rows, then the box on top
      drawOverlay();
    }
  } else {
    renderBoard();
  }
  renderSide();
  const bool play_label = s_state != kPlaying && s_state != kClearing;
  if (play_label != s_shown_pause_label_play && s_held != kPause) drawButton(kPause, false);
}

}  // namespace ui::block_game
