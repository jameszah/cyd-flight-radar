#include "ui/tron_saver.h"

#include <Arduino.h>

#include "hardware/display.h"
#include "hardware/display_font.h"

namespace ui::tron_saver {
namespace {

// Each trail moves s_step px every kStepMs. Turns happen only on a lane grid of two steps
// (s_lane), wider than the line, so parallel runs sit side by side with a small gap. It runs a
// random number of lanes, then turns 90 degrees left or right (towards the side with room near
// a wall). Trails never collide: they cross over each other. The nose is whiter: a near-white
// tip with a half-white block behind it. When the trails have covered about kFillPct of the
// screen (or after kMaxRoundMs) the screen is wiped top to bottom and new trails start.

constexpr int kTrails = 4;
constexpr unsigned long kStepMs = 20;
constexpr int kMinSegLanes = 3;
constexpr int kMaxSegLanes = 20;
constexpr int kFillPct = 40;
constexpr int kNoseTipWhitePct = 88;
constexpr int kNoseMidWhitePct = 50;
constexpr unsigned long kMaxRoundMs = 60000;
constexpr unsigned long kWipeStepMs = 10;

constexpr uint16_t kColorBg = TFT_BLACK;

// Bright pastels: pink, mint, lavender, peach, sky, lemon.
constexpr uint8_t kPastels[][3] = {
    {255, 130, 200}, {100, 255, 180}, {175, 140, 255},
    {255, 175, 100}, {110, 200, 255}, {255, 245, 110},
};
constexpr int kPastelCount = sizeof(kPastels) / sizeof(kPastels[0]);

constexpr int8_t kDx[4] = {0, 1, 0, -1};  // up, right, down, left
constexpr int8_t kDy[4] = {-1, 0, 1, 0};

struct Rect {
  int16_t x, y, w, h;
};

struct Trail {
  int16_t x, y;      // centre of the head
  uint8_t dir;
  int16_t steps_left;
  int16_t run_px;    // distance since the last turn, so the nose doesn't reach round a corner
  uint16_t color, mid, tip;
  Rect nose;         // where the nose was drawn, repainted in the trail colour next step
};

enum Phase : uint8_t { kRunning, kWiping };

bool s_active = false;
int s_step = 7;    // px per step
int s_lane = 14;   // lane spacing = 2 steps
int s_width = 9;   // line width (odd, so the head centre is a pixel)
int s_w = 0, s_h = 0;
Trail s_trails[kTrails];
Phase s_phase = kRunning;
unsigned long s_last_ms = 0;
unsigned long s_round_ms = 0;
uint32_t s_drawn_px = 0;  // rough coverage: steps x step x width
int s_wipe_y = 0;

uint16_t blendToWhite(const uint8_t rgb[3], int pct) {
  return lgfx::color565(rgb[0] + (255 - rgb[0]) * pct / 100, rgb[1] + (255 - rgb[1]) * pct / 100,
                        rgb[2] + (255 - rgb[2]) * pct / 100);
}

// Lanes: x = s_lane * k for k in 1..cols-1, same for y, so everything stays on screen.
int laneCount(int extent) { return extent / s_lane - 1; }
int randomLane(int extent) { return s_lane * (1 + random(laneCount(extent))); }

// Lanes available before the wall in direction d (the head is always on a lane here).
int room(const Trail& t, int d) {
  switch (d) {
    case 0: return (t.y - s_lane) / s_lane;
    case 1: return (s_lane * laneCount(s_w) - t.x) / s_lane;
    case 2: return (s_lane * laneCount(s_h) - t.y) / s_lane;
    default: return (t.x - s_lane) / s_lane;
  }
}

void newSegment(Trail& t, bool turn) {
  if (turn) {
    const int a = (t.dir + 1) & 3, b = (t.dir + 3) & 3;
    const int ra = room(t, a), rb = room(t, b);
    if (ra < kMinSegLanes && rb < kMinSegLanes) {
      t.dir = ra >= rb ? a : b;  // cornered: take the roomier side, or reverse if both are 0
      if (room(t, t.dir) == 0) t.dir = (t.dir + 2) & 3;
    } else if (ra < kMinSegLanes) {
      t.dir = b;
    } else if (rb < kMinSegLanes) {
      t.dir = a;
    } else {
      t.dir = random(2) ? a : b;
    }
    t.run_px = 0;
  }
  const int lanes = min(room(t, t.dir), static_cast<int>(random(kMinSegLanes, kMaxSegLanes + 1)));
  t.steps_left = lanes * (s_lane / s_step);
}

// The stretch of line from `back` to `back + len` px behind the head's leading edge.
Rect behindHead(const Trail& t, int back, int len) {
  const int half = s_width / 2;
  const int dx = kDx[t.dir], dy = kDy[t.dir];
  if (len <= 0) return {0, 0, 0, 0};
  if (dx != 0) {
    const int lead = t.x + dx * half;
    const int x = dx > 0 ? lead - back - len + 1 : lead + back;
    return {static_cast<int16_t>(x), static_cast<int16_t>(t.y - half),
            static_cast<int16_t>(len), static_cast<int16_t>(s_width)};
  }
  const int lead = t.y + dy * half;
  const int y = dy > 0 ? lead - back - len + 1 : lead + back;
  return {static_cast<int16_t>(t.x - half), static_cast<int16_t>(y),
          static_cast<int16_t>(s_width), static_cast<int16_t>(len)};
}

void fill(const Rect& r, uint16_t c) {
  if (r.w > 0 && r.h > 0) tft.fillRect(r.x, r.y, r.w, r.h, c);
}

void startRound() {
  // Distinct pastel per trail.
  uint8_t order[kPastelCount];
  for (int i = 0; i < kPastelCount; ++i) order[i] = i;
  for (int i = kPastelCount - 1; i > 0; --i) {
    const int j = random(i + 1);
    const uint8_t t = order[i];
    order[i] = order[j];
    order[j] = t;
  }
  for (int i = 0; i < kTrails; ++i) {
    Trail& t = s_trails[i];
    t.x = randomLane(s_w);
    t.y = randomLane(s_h);
    t.dir = random(4);
    t.color = blendToWhite(kPastels[order[i]], 0);
    t.mid = blendToWhite(kPastels[order[i]], kNoseMidWhitePct);
    t.tip = blendToWhite(kPastels[order[i]], kNoseTipWhitePct);
    t.run_px = 0;
    t.nose = {0, 0, 0, 0};
    newSegment(t, room(t, t.dir) < kMinSegLanes);
  }
  s_drawn_px = 0;
  s_round_ms = millis();
  s_phase = kRunning;
}

void stepTrail(Trail& t) {
  if (t.steps_left <= 0) newSegment(t, true);
  if (t.steps_left <= 0) return;  // boxed in this step; try again next time
  const int nx = t.x + kDx[t.dir] * s_step;
  const int ny = t.y + kDy[t.dir] * s_step;
  const int half = s_width / 2;
  fill(t.nose, t.color);  // last step's nose becomes ordinary trail
  // Segment from the old head to the new one, in the trail colour (covers the old head).
  const int left = min(static_cast<int>(t.x), nx) - half;
  const int top = min(static_cast<int>(t.y), ny) - half;
  tft.fillRect(left, top, abs(nx - t.x) + s_width, abs(ny - t.y) + s_width, t.color);
  t.x = nx;
  t.y = ny;
  t.run_px += s_step;
  // Nose: near-white tip (one line width long) and a half-white block behind it, cut short
  // just after a turn so they stay on the new straight.
  const int straight = t.run_px + half + 1;  // line length since the turn, up to the lead edge
  const int tip_len = min(s_width, straight);
  const int mid_len = min(s_width, straight - tip_len);
  fill(behindHead(t, tip_len, mid_len), t.mid);
  fill(behindHead(t, 0, tip_len), t.tip);
  t.nose = behindHead(t, 0, tip_len + mid_len);
  --t.steps_left;
  s_drawn_px += s_step * s_width;
}

}  // namespace

bool begin() {
  if (s_active) return true;
  s_w = tft.width();
  s_h = tft.height();
  const int short_side = min(s_w, s_h);
  s_step = max(4, short_side / 68);           // 7 px on the 480 wide FNK0115 (350 px/s at 20 ms)
  s_lane = 2 * s_step;                        // 14 px there
  s_width = max(5, short_side * 3 / 160) | 1;  // 9 px there (was 3); odd
  if (s_width >= s_lane) s_width = (s_lane - 2) | 1;
  s_active = true;
  tft.fillScreen(kColorBg);
  startRound();
  s_last_ms = millis();
  return true;
}

void tick() {
  if (!s_active) return;
  const unsigned long now = millis();
  if (s_phase == kWiping) {
    if (now - s_last_ms < kWipeStepMs) return;
    s_last_ms = now;
    const int band = max(8, s_h / 80);
    tft.fillRect(0, s_wipe_y, s_w, band, kColorBg);
    s_wipe_y += band;
    if (s_wipe_y >= s_h) startRound();
    return;
  }
  if (now - s_last_ms < kStepMs) return;
  // Catch up if the loop was late (it sleeps 10 ms), up to a few steps.
  int steps = 0;
  while (now - s_last_ms >= kStepMs && steps < 3) {
    s_last_ms += kStepMs;
    ++steps;
  }
  if (now - s_last_ms >= kStepMs) s_last_ms = now;
  tft.startWrite();
  for (int i = 0; i < steps; ++i) {
    for (Trail& t : s_trails) stepTrail(t);
  }
  tft.endWrite();
  if (s_drawn_px * 100 >= static_cast<uint32_t>(s_w) * s_h * kFillPct ||
      now - s_round_ms >= kMaxRoundMs) {
    s_phase = kWiping;
    s_wipe_y = 0;
  }
}

void end() {
  if (!s_active) return;
  s_active = false;
  displayFontEnsureLoaded(tft);  // radar text uses the smooth vlw font
}

bool isActive() { return s_active; }

}  // namespace ui::tron_saver
