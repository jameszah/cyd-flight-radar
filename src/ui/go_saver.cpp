#include "ui/go_saver.h"

#include <Arduino.h>
#include <string.h>

#include "hardware/display.h"
#include "hardware/display_font.h"

namespace ui::go_saver {
namespace {

// Looks like an LED matrix: every grid cell is a round dot, dark grey when unlit. Each cycle
// moves one cell per step and leaves its colour behind; its head is drawn near white. The AI
// keeps going straight unless that runs into something (or on a small random chance), and
// picks the turn with the most open space (flood fill). A cycle with nowhere to go crashes
// (head turns red); when all have crashed the grid is wiped row by row and a new round starts.

constexpr int kCycles = 2;
constexpr unsigned long kStepMs = 70;
constexpr unsigned long kCrashPauseMs = 2500;  // after the last crash, before the wipe
constexpr int kWipeRowsPerStep = 2;
constexpr int kTurnChancePct = 7;   // random turns while going straight is still fine
constexpr int kFloodCap = 400;      // flood fill stops counting here (enough to compare)

constexpr uint16_t kColorBg = TFT_BLACK;
constexpr uint16_t kColorOff = lgfx::color565(34, 34, 42);
constexpr uint16_t kColorCrash = lgfx::color565(255, 40, 30);

struct CycleColors {
  uint16_t trail, glow, head;
};
const CycleColors kColors[kCycles] = {
    {lgfx::color565(0, 215, 255), lgfx::color565(0, 60, 75), lgfx::color565(215, 255, 255)},
    {lgfx::color565(255, 60, 205), lgfx::color565(80, 15, 62), lgfx::color565(255, 215, 245)},
};

constexpr int8_t kDx[4] = {0, 1, 0, -1};  // up, right, down, left
constexpr int8_t kDy[4] = {-1, 0, 1, 0};

struct Cycle {
  int16_t x, y;
  uint8_t dir;
  bool alive;
};

enum Phase : uint8_t { kRunning, kCrashed, kWiping };

bool s_active = false;
int s_cols = 0, s_rows = 0, s_pitch = 0, s_x0 = 0, s_y0 = 0, s_dot_r = 0;
uint8_t* s_grid = nullptr;     // 0 empty, 1..kCycles owner
uint8_t* s_seen = nullptr;     // flood fill marks
uint16_t* s_queue = nullptr;   // flood fill queue
uint8_t s_seen_gen = 0;
Cycle s_cycles[kCycles];
Phase s_phase = kRunning;
unsigned long s_last_step_ms = 0;
unsigned long s_crash_ms = 0;
int s_wipe_row = 0;

inline int idx(int x, int y) { return y * s_cols + x; }
inline bool inside(int x, int y) { return x >= 0 && x < s_cols && y >= 0 && y < s_rows; }
inline bool freeCell(int x, int y) { return inside(x, y) && s_grid[idx(x, y)] == 0; }

void drawDot(int x, int y, uint16_t dot, uint16_t glow) {
  const int cx = s_x0 + x * s_pitch + s_pitch / 2;
  const int cy = s_y0 + y * s_pitch + s_pitch / 2;
  if (glow != kColorBg) {
    tft.fillCircle(cx, cy, s_dot_r + max(1, s_pitch / 10), glow);
  } else {
    tft.fillRect(cx - s_pitch / 2, cy - s_pitch / 2, s_pitch, s_pitch, kColorBg);
  }
  tft.fillCircle(cx, cy, s_dot_r, dot);
}

// Open cells reachable from (x, y), up to kFloodCap.
int openArea(int x, int y) {
  if (!freeCell(x, y)) return 0;
  if (++s_seen_gen == 0) {  // generation counter wrapped: clear the marks
    memset(s_seen, 0, s_cols * s_rows);
    s_seen_gen = 1;
  }
  int head = 0, tail = 0, count = 0;
  s_queue[tail++] = idx(x, y);
  s_seen[idx(x, y)] = s_seen_gen;
  while (head < tail && count < kFloodCap) {
    const int i = s_queue[head++];
    ++count;
    const int cx = i % s_cols, cy = i / s_cols;
    for (int d = 0; d < 4; ++d) {
      const int nx = cx + kDx[d], ny = cy + kDy[d];
      if (!freeCell(nx, ny)) continue;
      const int ni = idx(nx, ny);
      if (s_seen[ni] == s_seen_gen) continue;
      s_seen[ni] = s_seen_gen;
      s_queue[tail++] = ni;
    }
  }
  return count;
}

// Straight, then left / right turns; -1 when boxed in.
int chooseDir(const Cycle& c) {
  const uint8_t options[3] = {c.dir, static_cast<uint8_t>((c.dir + 3) & 3),
                              static_cast<uint8_t>((c.dir + 1) & 3)};
  int area[3];
  int best = -1;
  for (int i = 0; i < 3; ++i) {
    area[i] = openArea(c.x + kDx[options[i]], c.y + kDy[options[i]]);
    if (area[i] > 0 && (best < 0 || area[i] > area[best] ||
                        (area[i] == area[best] && random(2)))) {
      best = i;
    }
  }
  if (best < 0) return -1;
  // Keep going straight while it's about as roomy as the best turn, apart from the odd
  // random turn so the patterns vary.
  if (area[0] > 0 && area[0] * 10 >= area[best] * 9) {
    if (random(100) >= kTurnChancePct) return options[0];
    const int turn = random(2) ? 1 : 2;
    if (area[turn] * 10 >= area[0] * 9) return options[turn];
    return options[0];
  }
  return options[best];
}

void startRound() {
  memset(s_grid, 0, s_cols * s_rows);
  // Opposite quarters, facing each other's half, with a little randomness.
  s_cycles[0] = {static_cast<int16_t>(s_cols / 4 + random(-1, 2)),
                 static_cast<int16_t>(s_rows * 3 / 4 + random(-2, 3)), 0, true};
  s_cycles[1] = {static_cast<int16_t>(s_cols * 3 / 4 + random(-1, 2)),
                 static_cast<int16_t>(s_rows / 4 + random(-2, 3)), 2, true};
  tft.startWrite();
  for (int i = 0; i < kCycles; ++i) {
    Cycle& c = s_cycles[i];
    s_grid[idx(c.x, c.y)] = i + 1;
    drawDot(c.x, c.y, kColors[i].head, kColors[i].glow);
  }
  tft.endWrite();
  s_phase = kRunning;
}

void stepCycles() {
  int nx[kCycles], ny[kCycles];
  bool moving[kCycles];
  for (int i = 0; i < kCycles; ++i) {
    Cycle& c = s_cycles[i];
    moving[i] = false;
    if (!c.alive) continue;
    const int d = chooseDir(c);
    if (d < 0) {
      c.alive = false;
      drawDot(c.x, c.y, kColorCrash, kColors[i].glow);
      continue;
    }
    c.dir = d;
    nx[i] = c.x + kDx[d];
    ny[i] = c.y + kDy[d];
    moving[i] = true;
  }
  // Head-on into the same cell: both crash there.
  for (int i = 0; i < kCycles; ++i) {
    for (int j = i + 1; j < kCycles; ++j) {
      if (moving[i] && moving[j] && nx[i] == nx[j] && ny[i] == ny[j]) {
        for (int k : {i, j}) {
          moving[k] = false;
          s_cycles[k].alive = false;
          drawDot(s_cycles[k].x, s_cycles[k].y, kColorCrash, kColors[k].glow);
        }
      }
    }
  }
  for (int i = 0; i < kCycles; ++i) {
    if (!moving[i]) continue;
    Cycle& c = s_cycles[i];
    drawDot(c.x, c.y, kColors[i].trail, kColors[i].glow);  // old head becomes trail
    c.x = nx[i];
    c.y = ny[i];
    s_grid[idx(c.x, c.y)] = i + 1;
    drawDot(c.x, c.y, kColors[i].head, kColors[i].glow);
  }
}

void drawEmptyGrid() {
  tft.startWrite();
  tft.fillScreen(kColorBg);
  for (int y = 0; y < s_rows; ++y) {
    for (int x = 0; x < s_cols; ++x) drawDot(x, y, kColorOff, kColorBg);
  }
  tft.endWrite();
}

void freeBuffers() {
  free(s_grid);
  free(s_seen);
  free(s_queue);
  s_grid = nullptr;
  s_seen = nullptr;
  s_queue = nullptr;
}

}  // namespace

bool begin() {
  if (s_active) return true;
  const int W = tft.width(), H = tft.height();
  s_pitch = max(10, min(W, H) / 24);  // 20 px on the 480 wide FNK0115, 10 on 240 boards
  s_dot_r = s_pitch * 36 / 100;
  s_cols = W / s_pitch;
  s_rows = H / s_pitch;
  s_x0 = (W - s_cols * s_pitch) / 2;
  s_y0 = (H - s_rows * s_pitch) / 2;
  const int n = s_cols * s_rows;
  s_grid = static_cast<uint8_t*>(calloc(n, 1));
  s_seen = static_cast<uint8_t*>(calloc(n, 1));
  s_queue = static_cast<uint16_t*>(malloc(n * sizeof(uint16_t)));
  if (!s_grid || !s_seen || !s_queue) {
    freeBuffers();
    return false;
  }
  s_seen_gen = 0;
  s_active = true;
  drawEmptyGrid();
  startRound();
  s_last_step_ms = millis();
  return true;
}

void tick() {
  if (!s_active) return;
  const unsigned long now = millis();
  if (now - s_last_step_ms < kStepMs) return;
  s_last_step_ms = now;

  switch (s_phase) {
    case kRunning: {
      tft.startWrite();
      stepCycles();
      tft.endWrite();
      bool any_alive = false;
      for (const Cycle& c : s_cycles) any_alive |= c.alive;
      if (!any_alive) {
        s_phase = kCrashed;
        s_crash_ms = now;
      }
      break;
    }
    case kCrashed:
      if (now - s_crash_ms >= kCrashPauseMs) {
        s_phase = kWiping;
        s_wipe_row = 0;
      }
      break;
    case kWiping:
      tft.startWrite();
      for (int k = 0; k < kWipeRowsPerStep && s_wipe_row < s_rows; ++k, ++s_wipe_row) {
        for (int x = 0; x < s_cols; ++x) {
          if (s_grid[idx(x, s_wipe_row)]) drawDot(x, s_wipe_row, kColorOff, kColorBg);
        }
      }
      tft.endWrite();
      if (s_wipe_row >= s_rows) startRound();
      break;
  }
}

void end() {
  if (!s_active) return;
  s_active = false;
  freeBuffers();
  displayFontEnsureLoaded(tft);  // radar text uses the smooth vlw font
}

bool isActive() { return s_active; }

}  // namespace ui::go_saver
