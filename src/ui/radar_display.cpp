#include "ui/radar_display.h"

#include <lgfx/v1/lgfx_fonts.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <time.h>

#include "config.h"
#include "catppuccin.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"
#include "ui/runway_overlay.h"
#include "ui/settings_menu.h"
#include "utils_math.h"



namespace ui {
namespace radar {

static uint16_t hex2rgb565(uint32_t hex) {
    uint8_t r = (hex >> 16) & 0xFF;
    uint8_t g = (hex >> 8) & 0xFF;
    uint8_t b = hex & 0xFF;
    if (config::kDisplayRgbOrder) {
        return tft.color565(b, g, r);
    }
    return tft.color565(r, g, b);
}

uint16_t kColorBackground = hex2rgb565(0x040A1C);
// uint16_t kColorGrid = hex2rgb565(0x106420);
uint16_t kColorGrid = hex2rgb565(0xA6E3A1);
uint16_t kColorLabel = hex2rgb565(0xFFFFFF);
uint16_t kColorCenter = hex2rgb565(0xFFFFFF);
uint16_t kColorAircraft = hex2rgb565(0xFF0000);
uint16_t kColorMilitary = hex2rgb565(0xFF8000);
uint16_t kColorHelicopter = hex2rgb565(0x00FFFF);
uint16_t kColorTrackVector = hex2rgb565(0xFF00FF);
uint16_t kColorTagType = hex2rgb565(0xFFC800);
uint16_t kColorTagAltitude = hex2rgb565(0x5AC8FF);
uint16_t kColorRunway = hex2rgb565(0x3896AA);
uint16_t kColorRunwayLabel = hex2rgb565(0xFFFFFF);
uint16_t kColorRunwayLine = hex2rgb565(0xFFFFFF);
uint16_t kColorAltCruise = hex2rgb565(0xFFFFFF);
// Light pink picked to survive the RGB332 frame sprite (0xFFB6C1 turns lavender there).
uint16_t kColorAltHigh = hex2rgb565(0xFFB6AA);
uint16_t kColorAltClimb = hex2rgb565(0x00FF00);
uint16_t kColorAltDescend = hex2rgb565(0xFF0000);
uint16_t kColorAltLevel = hex2rgb565(0xFFFF00);

}  // namespace radar

void updateThemeColors() {
    radar::kColorBackground = radar::hex2rgb565(0x040A1C);
    radar::kColorGrid = radar::hex2rgb565(0x106420);
    radar::kColorLabel = radar::hex2rgb565(0xFFFFFF);
    radar::kColorCenter = radar::hex2rgb565(0xFFFFFF);
    radar::kColorAircraft = radar::hex2rgb565(0xFF0000);
    radar::kColorMilitary = radar::hex2rgb565(0xFF8000);
    radar::kColorHelicopter = radar::hex2rgb565(0x00FFFF);
    radar::kColorTrackVector = radar::hex2rgb565(0xFF00FF);
    radar::kColorTagType = radar::hex2rgb565(0xFFC800);
    radar::kColorTagAltitude = radar::hex2rgb565(0x5AC8FF);
    radar::kColorRunway = radar::hex2rgb565(0x3896AA);
    radar::kColorRunwayLabel = radar::hex2rgb565(0xFFFFFF);
    radar::kColorRunwayLine = radar::hex2rgb565(0xFFFFFF);
    radar::kColorAltCruise = radar::hex2rgb565(0xFFFFFF);
    radar::kColorAltHigh = radar::hex2rgb565(0xFFB6AA);
    radar::kColorAltClimb = radar::hex2rgb565(0x00FF00);
    radar::kColorAltDescend = radar::hex2rgb565(0xFF0000);
    radar::kColorAltLevel = radar::hex2rgb565(0xFFFF00);
}

namespace {

bool s_label_metrics_ready = false;
bool s_cardinal_use_vlw = false;
bool s_scale_use_vlw = false;
float s_cardinal_vlw_size = 0.56f;
float s_scale_vlw_size = 0.50f;
float s_tag_vlw_size = 0.56f;
const lgfx::GFXfont* s_cardinal_gfx = &fonts::FreeSansBold12pt7b;
const lgfx::GFXfont* s_scale_gfx = &fonts::FreeSansBold9pt7b;
const lgfx::GFXfont* s_tag_gfx = &fonts::FreeSansBold12pt7b;

bool s_tag_label_metrics_ready = false;
bool s_tag_use_vlw = false;

int s_scale_label_max_w = 0;
int s_scale_label_h = 0;

lgfx::LovyanGFX* s_draw = &tft;
LGFX_Sprite s_frame(&tft);
bool s_frame_ready = false;
static char s_selected_hex[7] = {0};

static size_t s_visible_aircraft_count = 0;
bool s_data_stale = false;

class DrawScope {
 public:
  explicit DrawScope(lgfx::LovyanGFX& gfx) : prev_(s_draw) { s_draw = &gfx; }
  ~DrawScope() { s_draw = prev_; }

 private:
  lgfx::LovyanGFX* prev_;
};

int absDiff(int a, int b) { return std::abs(a - b); }

int measureGfxHeight(const lgfx::GFXfont& font) {
  tft.setFont(&font);
  tft.setTextSize(1);
  return tft.fontHeight();
}

int measureVlwHeight(float size) {
  tft.setTextSize(size);
  return tft.fontHeight();
}

float findVlwSizeForHeight(int target_px) {
  float lo = 0.25f;
  float hi = 1.2f;
  for (int i = 0; i < 16; ++i) {
    const float mid = (lo + hi) * 0.5f;
    if (measureVlwHeight(mid) < target_px) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return hi;
}

void applyScaleStyle();

const lgfx::GFXfont* pickGfxFontClosest(
    int target_px, const lgfx::GFXfont* const* candidates, size_t count) {
  const lgfx::GFXfont* best = candidates[0];
  int best_diff = absDiff(measureGfxHeight(*best), target_px);

  for (size_t i = 1; i < count; ++i) {
    const int diff = absDiff(measureGfxHeight(*candidates[i]), target_px);
    if (diff < best_diff) {
      best_diff = diff;
      best = candidates[i];
    }
  }
  return best;
}

void initLabelMetrics() {
  if (s_label_metrics_ready) {
    return;
  }

  const int cardinal_target = radar::kCardinalLabelHeightPx;

  if (displayFontIsSmooth()) {
    s_cardinal_use_vlw = true;
    s_cardinal_vlw_size = findVlwSizeForHeight(cardinal_target);
    const int cardinal_h = measureVlwHeight(s_cardinal_vlw_size);
    const int scale_target = cardinal_h - radar::kScaleBelowCardinalPx;
    s_scale_use_vlw = true;
    s_scale_vlw_size = findVlwSizeForHeight(scale_target);
  } else {
    const lgfx::GFXfont* cardinal_candidates[] = {&fonts::FreeSansBold12pt7b,
                                                  &fonts::FreeSansBold9pt7b};
    s_cardinal_gfx =
        pickGfxFontClosest(cardinal_target, cardinal_candidates, 2);
    s_cardinal_use_vlw = false;

    const int cardinal_h = measureGfxHeight(*s_cardinal_gfx);
    const int scale_target = cardinal_h - radar::kScaleBelowCardinalPx;
    const lgfx::GFXfont* scale_candidates[] = {&fonts::FreeSansBold9pt7b,
                                               &fonts::FreeSansBold12pt7b};
    s_scale_gfx = pickGfxFontClosest(scale_target, scale_candidates, 2);
    s_scale_use_vlw = false;
  }

  applyScaleStyle();
  s_scale_label_h = tft.fontHeight();
  s_scale_label_max_w = 0;
  char label[12];
  for (size_t i = 0; i < radar::kRangePresetCount; ++i) {
    for (bool miles : {false, true}) {
      utils::math::formatRing3Label(label, sizeof(label), radar::kRangePresets[i].ring3_km,
                              miles);
      const int w = tft.textWidth(label);
      if (w > s_scale_label_max_w) {
        s_scale_label_max_w = w;
      }
    }
  }

  s_label_metrics_ready = true;
}

void initTagLabelMetrics() {
  if (s_tag_label_metrics_ready) {
    return;
  }

  const int target = radar::kAircraftTagLabelHeightPx;
  if (displayFontIsSmooth()) {
    s_tag_use_vlw = true;
    s_tag_vlw_size = findVlwSizeForHeight(target);
  } else {
    const lgfx::GFXfont* tag_candidates[] = {&fonts::FreeSansBold12pt7b,
                                               &fonts::FreeSansBold9pt7b};
    s_tag_gfx = pickGfxFontClosest(target, tag_candidates, 2);
    s_tag_use_vlw = false;
  }

  s_tag_label_metrics_ready = true;
}

void initPalette() {
  updateThemeColors();
}

inline void offsetKmFromCenter(float lat, float lon, float* dx_km, float* dy_km, float* dist_km) {
  utils::math::equirectangularOffsetKm(
      static_cast<float>(services::location::lat()),
      static_cast<float>(services::location::lon()),
      lat, lon, dx_km, dy_km, dist_km);
}



float innerRingMaxKm() {
  const float outer_km = radar::rangeCurrent().outer_km;
  return outer_km * (static_cast<float>(radar::kGridOuterRadius -
                                       radar::kAircraftInsideRingInsetPx) /
                     static_cast<float>(radar::kGridOuterRadius));
}

/** Flat lat/lon as x/y: 1° ≈ 111 km, north = screen up. */
void latLonToScreen(float lat, float lon, int* out_x, int* out_y) {
  const float outer_km = radar::rangeCurrent().outer_km;
  const float px_per_km = static_cast<float>(radar::kGridOuterRadius) / outer_km;

  float dx_km = 0.0f;
  float dy_km = 0.0f;
  float dist_km = 0.0f;
  offsetKmFromCenter(lat, lon, &dx_km, &dy_km, &dist_km);

  *out_x = radar::kCenterX + static_cast<int>(lroundf(dx_km * px_per_km));
  *out_y = radar::kCenterY - static_cast<int>(lroundf(dy_km * px_per_km));
}

bool isInsideOuterRingKm(float dist_km) { return dist_km <= innerRingMaxKm(); }

int distSqFromCenter(int x, int y) {
  const int dx = x - radar::kCenterX;
  const int dy = y - radar::kCenterY;
  return dx * dx + dy * dy;
}

bool isInsideOuterRing(int x, int y) {
  const int max_r = radar::kGridOuterRadius - radar::kAircraftInsideRingInsetPx;
  return distSqFromCenter(x, y) <= max_r * max_r;
}

/** Rim dot from true bearing; always on screen edge (even if target is 50+ km away). */
bool beyondRingEdgeDotFromLatLon(float lat, float lon, int* out_x, int* out_y) {
  float dx_km = 0.0f;
  float dy_km = 0.0f;
  float dist_km = 0.0f;
  offsetKmFromCenter(lat, lon, &dx_km, &dy_km, &dist_km);
  if (dist_km < 0.01f) {
    return false;
  }
  if (isInsideOuterRingKm(dist_km)) {
    return false;
  }

  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int rim_r = radar::kCenterX - radar::kBeyondRingScreenMarginPx;
  const float angle_rad = atan2f(dx_km, dy_km);

  *out_x = cx + static_cast<int>(lroundf(sinf(angle_rad) * rim_r));
  *out_y = cy - static_cast<int>(lroundf(cosf(angle_rad) * rim_r));
  return true;
}

void drawBeyondRingDot(int x, int y, uint16_t color) {
  s_draw->fillSmoothCircle(x, y, radar::kBeyondRingDotRadiusPx, color);
}

void clipPointToOuterRing(int x0, int y0, int* x1, int* y1) {
  const int max_r = radar::kGridOuterRadius;
  const int max_r_sq = max_r * max_r;
  if (distSqFromCenter(*x1, *y1) <= max_r_sq) {
    return;
  }

  const int dx = *x1 - x0;
  const int dy = *y1 - y0;
  float t = 1.0f;
  for (int step = 0; step < 20; ++step) {
    const int px = x0 + static_cast<int>(lroundf(dx * t));
    const int py = y0 + static_cast<int>(lroundf(dy * t));
    if (distSqFromCenter(px, py) <= max_r_sq) {
      *x1 = px;
      *y1 = py;
      return;
    }
    t -= 0.05f;
    if (t <= 0.0f) {
      *x1 = x0;
      *y1 = y0;
      return;
    }
  }
}

int speedLineLengthPx(float gs_knots) {
  if (gs_knots <= 0.0f) {
    return 0;
  }

  // Fixed screen scale: 60 s horizon at gs, not tied to current range zoom.
  constexpr float kKmPerKnotPerHorizon =
      1.852f * radar::kAircraftTrackHorizonSec / 3600.0f;
  const float px =
      gs_knots * kKmPerKnotPerHorizon * radar::kGridOuterRadius /
      radar::kAircraftTrackRefOuterKm * radar::kAircraftTrackLengthScale;

  const int len = static_cast<int>(px + 0.5f);
  if (len < radar::kAircraftSpeedLineMinPx) {
    return radar::kAircraftSpeedLineMinPx;
  }
  return len;
}

void noseTip(int cx, int cy, float heading_deg, int* tip_x, int* tip_y) {
  constexpr float kDegToRad = 0.01745329252f;
  const float rad = heading_deg * kDegToRad;
  *tip_x = cx + static_cast<int>(lroundf(sinf(rad) * radar::kAircraftNoseLenPx));
  *tip_y = cy - static_cast<int>(lroundf(cosf(rad) * radar::kAircraftNoseLenPx));
}

// Full-size icon at or above kHighAltitudeFt; climbing / level / descending
// traffic below that is drawn at kPlaneIconSmallScale.
bool isHighAltitude(const services::adsb::Aircraft& plane) {
  return !isnan(plane.alt_ft) && plane.alt_ft >= radar::kHighAltitudeFt;
}

// Airplane silhouette: fuselage, swept wings, tail plane, all kPlane* px scaled by
// scale. (fwd, side) is a point in the icon's frame: fwd along the heading, side to its right.
void drawPlaneIcon(int cx, int cy, float heading_deg, uint16_t color, bool full_size) {
  constexpr float kDegToRad = 0.01745329252f;
  const float rad = heading_deg * kDegToRad;
  const float sin_h = sinf(rad);
  const float cos_h = cosf(rad);
  const float scale = full_size ? 1.0f : radar::kPlaneIconSmallScale;

  auto px = [&](int fwd, int side) {
    return cx + static_cast<int>(lroundf((sin_h * fwd + cos_h * side) * scale));
  };
  auto py = [&](int fwd, int side) {
    return cy + static_cast<int>(lroundf((-cos_h * fwd + sin_h * side) * scale));
  };
  const float half_width = std::max(1.0f, radar::kPlaneStrokeHalfWidth * scale);
  auto stroke = [&](int f0, int s0, int f1, int s1) {
    s_draw->drawWideLine(px(f0, s0), py(f0, s0), px(f1, s1), py(f1, s1), half_width, color);
  };
  stroke(radar::kPlaneNoseFwdPx, 0, radar::kPlaneTailFwdPx, 0);
  for (int side : {-1, 1}) {
    stroke(radar::kPlaneWingRootFwdPx, 0, radar::kPlaneWingTipFwdPx,
           side * radar::kPlaneWingHalfSpanPx);
    stroke(radar::kPlaneStabRootFwdPx, 0, radar::kPlaneStabTipFwdPx,
           side * radar::kPlaneStabHalfSpanPx);
  }
}

void drawHelicopterSymbol(int cx, int cy, float heading_deg, uint16_t color) {
  constexpr float kDegToRad = 0.01745329252f;
  const float rad = heading_deg * kDegToRad;
  const float sin_h = sinf(rad);
  const float cos_h = cosf(rad);

  s_draw->fillSmoothCircle(cx, cy, radar::kAircraftTailLenPx, color);
  
  const int tail_x = cx - static_cast<int>(lroundf(sin_h * radar::kAircraftNoseLenPx));
  const int tail_y = cy + static_cast<int>(lroundf(cos_h * radar::kAircraftNoseLenPx));
  s_draw->drawWideLine(cx, cy, tail_x, tail_y, radar::kAircraftTrackLineHalfWidth, color);
}

void drawSpeedVector(int cx, int cy, float heading_deg, float track_deg,
                     float gs_knots, uint16_t color) {
  const int len = speedLineLengthPx(gs_knots);
  if (len <= 0) {
    return;
  }

  int tip_x = 0;
  int tip_y = 0;
  noseTip(cx, cy, heading_deg, &tip_x, &tip_y);

  constexpr float kDegToRad = 0.01745329252f;
  const float rad = track_deg * kDegToRad;
  int ex = tip_x + static_cast<int>(lroundf(sinf(rad) * len));
  int ey = tip_y - static_cast<int>(lroundf(cosf(rad) * len));
  clipPointToOuterRing(tip_x, tip_y, &ex, &ey);
  if (ex == tip_x && ey == tip_y) {
    return;
  }
  s_draw->drawWideLine(tip_x, tip_y, ex, ey, radar::kAircraftTrackLineHalfWidth,
                       color);
}

void drawFlightTrail(int cx, int cy, const services::adsb::Aircraft& plane) {
  if (plane.trail_size < 2) return;
  
  int last_x = cx;
  int last_y = cy;
  for (size_t i = 0; i < plane.trail_size; ++i) {
    size_t idx = (plane.trail_head + services::adsb::kTrailLength - 1 - i) % services::adsb::kTrailLength;
    int px = 0;
    int py = 0;
    latLonToScreen(plane.trail[idx].lat, plane.trail[idx].lon, &px, &py);
    
    int ex = px, ey = py;
    clipPointToOuterRing(last_x, last_y, &ex, &ey);
    if (ex != last_x || ey != last_y) {
      s_draw->drawWideLine(last_x, last_y, ex, ey, radar::kAircraftTrackLineHalfWidth, radar::kColorRunway);
    }
    last_x = px;
    last_y = py;
  }
}

void drawSelectedHighlight(int cx, int cy) {
  s_draw->drawCircle(cx, cy, 14, radar::kColorTagType);
  s_draw->drawCircle(cx, cy, 15, radar::kColorTagType);
}

void applyTagStyle() {
  if (s_tag_use_vlw) {
    displayFontSetSmoothSize(*s_draw, s_tag_vlw_size);
  } else {
    displayFontSetBitmap(*s_draw, s_tag_gfx);
  }
}

// Main-display tag shows only the aircraft type; callsign and altitude are in
// the tap-to-select info panel, and altitude/trend is shown by icon color.
int measureTagBlockWidth(const services::adsb::Aircraft& plane) {
  applyTagStyle();
  return plane.type[0] != '\0' ? s_draw->textWidth(plane.type) : 0;
}

void drawAircraftTag(int x, int y, const services::adsb::Aircraft& plane) {
  if (plane.type[0] == '\0') {
    return;
  }
  initTagLabelMetrics();
  applyTagStyle();

  const int line_h = s_draw->fontHeight();
  const int block_w = measureTagBlockWidth(plane);
  const int block_h = line_h;
  int ly = y - block_h / 2;

  const int symbol_half =
      radar::kAircraftNoseLenPx + radar::kAircraftTailHalfPx;
  // West (left): tag toward center on the right; east (right): tag on the left.
  const bool tag_on_right = x < radar::kCenterX;
  int anchor_x = 0;
  if (tag_on_right) {
    anchor_x = x + symbol_half + radar::kAircraftLabelGapPx;
    anchor_x = std::min(anchor_x, radar::kSize - block_w - 1);
    s_draw->setTextDatum(textdatum_t::top_left);
  } else {
    anchor_x = x - symbol_half - radar::kAircraftLabelGapPx;
    anchor_x = std::max(anchor_x, block_w + 1);
    s_draw->setTextDatum(textdatum_t::top_right);
  }
  ly = std::max(1, std::min(ly, radar::kSize - block_h - 1));

  s_draw->setTextColor(radar::kColorTagType, radar::kColorBackground);
  s_draw->drawString(plane.type, anchor_x, ly);
}

/** Icon color: cruise (>= kCruiseAltitudeFt), high (>= kHighAltitudeFt), else
 *  climbing / descending / level. */
uint16_t aircraftAltitudeColor(const services::adsb::Aircraft& plane) {
  if (!isnan(plane.alt_ft) && plane.alt_ft >= radar::kCruiseAltitudeFt) {
    return radar::kColorAltCruise;
  }
  if (isHighAltitude(plane)) {
    return radar::kColorAltHigh;
  }
  if (plane.vrate_fpm >= radar::kLevelFlightFpm) {
    return radar::kColorAltClimb;
  }
  if (plane.vrate_fpm <= -radar::kLevelFlightFpm) {
    return radar::kColorAltDescend;
  }
  return radar::kColorAltLevel;
}

struct AircraftDrawItem {
  size_t index = 0;
  int x = 0;
  int y = 0;
  int dist_sq = 0;
};

struct BeyondDotDrawItem {
  int x = 0;
  int y = 0;
  int dist_sq = 0;
  uint16_t color = 0;
};

void sortDrawItemsFarFirst(AircraftDrawItem* items, size_t count) {
  for (size_t i = 1; i < count; ++i) {
    const AircraftDrawItem key = items[i];
    size_t j = i;
    while (j > 0 && items[j - 1].dist_sq < key.dist_sq) {
      items[j] = items[j - 1];
      --j;
    }
    items[j] = key;
  }
}

void sortBeyondDotsFarFirst(BeyondDotDrawItem* items, size_t count) {
  for (size_t i = 1; i < count; ++i) {
    const BeyondDotDrawItem key = items[i];
    size_t j = i;
    while (j > 0 && items[j - 1].dist_sq < key.dist_sq) {
      items[j] = items[j - 1];
      --j;
    }
    items[j] = key;
  }
}

void drawAircraft() {
  initLabelMetrics();

  const size_t n = services::adsb::aircraftCount();
  const services::adsb::Aircraft* planes = services::adsb::aircraftList();

  AircraftDrawItem items[services::adsb::kMaxAircraft];
  BeyondDotDrawItem dots[services::adsb::kMaxAircraft];
  size_t draw_count = 0;
  size_t dot_count = 0;

  for (size_t i = 0; i < n; ++i) {
    float dx_km = 0.0f;
    float dy_km = 0.0f;
    float dist_km = 0.0f;
    offsetKmFromCenter(planes[i].lat, planes[i].lon, &dx_km, &dy_km, &dist_km);

    if (isInsideOuterRingKm(dist_km)) {
      int x = 0;
      int y = 0;
      latLonToScreen(planes[i].lat, planes[i].lon, &x, &y);
      items[draw_count].index = i;
      items[draw_count].x = x;
      items[draw_count].y = y;
      items[draw_count].dist_sq = distSqFromCenter(x, y);
      ++draw_count;
      continue;
    }

    int dot_x = 0;
    int dot_y = 0;
    if (!beyondRingEdgeDotFromLatLon(planes[i].lat, planes[i].lon, &dot_x,
                                     &dot_y)) {
      continue;
    }
    dots[dot_count].x = dot_x;
    dots[dot_count].y = dot_y;
    dots[dot_count].dist_sq = distSqFromCenter(dot_x, dot_y);
    dots[dot_count].color = planes[i].is_heli ? radar::kColorHelicopter
                                              : aircraftAltitudeColor(planes[i]);
    ++dot_count;
  }

  sortBeyondDotsFarFirst(dots, dot_count);
  for (size_t d = 0; d < dot_count; ++d) {
    drawBeyondRingDot(dots[d].x, dots[d].y, dots[d].color);
  }
  
  s_visible_aircraft_count = draw_count + dot_count;

  sortDrawItemsFarFirst(items, draw_count);
  for (size_t d = 0; d < draw_count; ++d) {
    const size_t i = items[d].index;
    const int x = items[d].x;
    const int y = items[d].y;
    
    if (s_selected_hex[0] != '\0' && strcmp(planes[i].hex, s_selected_hex) == 0) {
      drawSelectedHighlight(x, y);
    }
    
    drawFlightTrail(x, y, planes[i]);
    drawSpeedVector(x, y, planes[i].nose_deg, planes[i].track_deg,
                    planes[i].gs_knots, radar::kColorTrackVector);
    if (planes[i].is_heli) {
      drawHelicopterSymbol(x, y, planes[i].nose_deg, radar::kColorHelicopter);
    } else {
      drawPlaneIcon(x, y, planes[i].nose_deg, aircraftAltitudeColor(planes[i]),
                    isHighAltitude(planes[i]));
    }
  }
  for (size_t d = 0; d < draw_count; ++d) {
    const size_t i = items[d].index;
    drawAircraftTag(items[d].x, items[d].y, planes[i]);
  }
}

void applyCardinalStyle() {
  if (s_cardinal_use_vlw) {
    displayFontSetSmoothSize(*s_draw, s_cardinal_vlw_size);
  } else {
    displayFontSetBitmap(*s_draw, s_cardinal_gfx);
  }
}

void applyScaleStyle() {
  if (s_scale_use_vlw) {
    displayFontSetSmoothSize(*s_draw, s_scale_vlw_size);
  } else {
    displayFontSetBitmap(*s_draw, s_scale_gfx);
  }
}

void drawCardinalLabel(const char* text, int x, int y, textdatum_t datum) {
  applyCardinalStyle();
  s_draw->setTextDatum(datum);
  s_draw->setTextColor(radar::kColorLabel, radar::kColorBackground);
  s_draw->drawString(text, x, y);
}

void drawScaleLabelWithBackground(const char* text, int x, int y) {
  applyScaleStyle();
  s_draw->setTextDatum(textdatum_t::bottom_right);

  const int tw = s_draw->textWidth(text);
  const int th = s_draw->fontHeight();
  constexpr int kPadX = 3;
  constexpr int kPadY = 2;

  const int left = x - tw - kPadX;
  const int top = y - th - kPadY;

  s_draw->fillRect(left, top, tw + kPadX * 2, th + kPadY * 2,
                   radar::kColorBackground);
  s_draw->setTextColor(radar::kColorGrid, radar::kColorBackground);
  s_draw->drawString(text, x, y);
}

void drawGridRing(int cx, int cy, int r, uint16_t color) {
  if (r <= 0) {
    return;
  }
  const int thickness =
      std::max(1, static_cast<int>(radar::kGridStrokeHalfWidth * 2.0f));
  for (int i = 0; i < thickness && r - i > 0; ++i) {
    s_draw->drawCircle(cx, cy, r - i, color);
  }
}

void drawRings(int cx, int cy, int outer_radius) {
  for (int i = 1; i <= radar::kRingCount; ++i) {
    const int r = (outer_radius * i) / radar::kRingCount;
    drawGridRing(cx, cy, r, radar::kColorGrid);
  }
}

void drawCrosshairs(int cx, int cy, int radius, uint16_t color) {
  s_draw->drawWideLine(cx, cy - radius, cx, cy + radius,
                       radar::kGridStrokeHalfWidth, color);
  s_draw->drawWideLine(cx - radius, cy, cx + radius, cy,
                       radar::kGridStrokeHalfWidth, color);
}

void drawCenterDot(int cx, int cy) {
  s_draw->fillSmoothCircle(cx, cy, radar::kCenterDotRadius, radar::kColorCenter);
}

void drawCardinalLabels() {
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int edge = radar::kSize - 1;
  const int margin = 4; // Margin from the edge

  drawCardinalLabel("N", cx, radar::kCardinalNorthOffsetY + margin, textdatum_t::top_center);
  drawCardinalLabel("S", cx, edge + radar::kCardinalSouthOffsetY - margin,
                    textdatum_t::bottom_center);
  drawCardinalLabel("W", margin, cy, textdatum_t::middle_left);
  drawCardinalLabel("E", edge - margin, cy, textdatum_t::middle_right);
}

// Scale label sits in the lower-right corner of the radar square, outside the
// outer ring; (x, y) is the label's bottom-right point.
constexpr int kScaleCornerMarginPx = 12;

int scaleLabelAnchorX() { return radar::kSize - 1 - kScaleCornerMarginPx; }

int scaleLabelAnchorY() { return radar::kSize - 1 - kScaleCornerMarginPx; }

void drawScaleLabel() {
  char scale_label[12];
  radar::formatCurrentRing3Label(scale_label, sizeof(scale_label));
  drawScaleLabelWithBackground(scale_label, scaleLabelAnchorX(),
                               scaleLabelAnchorY());
}

// Altitude color legend in the lower-left corner, outside the outer ring.
// Rows go bottom-up; the shortest words sit highest, where the ring curves in.
constexpr int kLegendTextHeightPx = 10;
constexpr int kLegendRowPx = 11;
constexpr int kLegendIconCx = 7;   // full-size silhouette spans x 1..15 pointing east
constexpr int kLegendTextX = 18;
constexpr int kLegendBottomRowCy = radar::kSize - 6;
// Row step is kLegendRowPx - 1 between small icons, plus this per full-size icon.
constexpr int kLegendFullExtraPx = 2;

// Small corner text (legend, coordinates, title). *vlw_size caches the smooth
// font size for height_px; the bitmap fallback is always Font0.
void applyCornerTextStyle(int height_px, float* vlw_size, uint16_t color) {
  if (displayFontIsSmooth()) {
    if (*vlw_size == 0.0f) {
      *vlw_size = findVlwSizeForHeight(height_px);
    }
    displayFontSetSmoothSize(*s_draw, *vlw_size);
  } else {
    s_draw->setFont(&fonts::Font0);  // GLCD font, so not displayFontSetBitmap
    s_draw->setTextSize(1);
  }
  s_draw->setTextColor(color, radar::kColorBackground);
}

constexpr int kCoordTextHeightPx = 12;
constexpr int kCoordRowPx = 13;
constexpr int kTitleTextHeightPx = 10;

// Upper-left corner, outside the ring: lat/lon (2 decimals) of the radar
// center, or, when exactly one airport is in range, its ident and lat/lon
// (its label is then left off the radar circle).
void drawCornerInfo(const runway::SingleAirport* airport) {
  const double lat = airport ? airport->lat : services::location::lat();
  const double lon = airport ? airport->lon : services::location::lon();
  char lat_buf[12];
  char lon_buf[12];
  snprintf(lat_buf, sizeof(lat_buf), "%.2f%c", fabs(lat), lat >= 0 ? 'N' : 'S');
  snprintf(lon_buf, sizeof(lon_buf), "%.2f%c", fabs(lon), lon >= 0 ? 'E' : 'W');

  static float s_vlw_size = 0.0f;
  applyCornerTextStyle(kCoordTextHeightPx, &s_vlw_size, radar::kColorLabel);
  s_draw->setTextDatum(textdatum_t::top_left);
  int y = 2;
  if (airport != nullptr) {
    s_draw->setTextColor(radar::kColorRunway, radar::kColorBackground);
    s_draw->drawString(airport->ident, 2, y);
    s_draw->setTextColor(radar::kColorLabel, radar::kColorBackground);
    y += kCoordRowPx;
  }
  s_draw->drawString(lat_buf, 2, y);
  s_draw->drawString(lon_buf, 2, y + kCoordRowPx);
}

// "Flight Radar" / "CYD" in the upper-right corner, outside the ring.
void drawTitle() {
  static float s_vlw_size = 0.0f;
  applyCornerTextStyle(kTitleTextHeightPx, &s_vlw_size, radar::kColorGrid);
  s_draw->setTextDatum(textdatum_t::top_right);
  const int right = radar::kSize - 3;
  s_draw->drawString("Flight Radar", right, 2);
  s_draw->drawString("CYD", right, 2 + kLegendRowPx);
}

void drawAltitudeLegend() {
  struct Row {
    const char* text;
    uint16_t color;
    bool full_size;
  };
  // Bottom to top. Full-size icons need more room for their wingspan; "25k" and
  // "15k" are short enough to stay clear of the outer ring up there.
  const Row rows[] = {
      {"descend", radar::kColorAltDescend, false},
      {"flat", radar::kColorAltLevel, false},
      {"climb", radar::kColorAltClimb, false},
      {"15k", radar::kColorAltHigh, true},
      {"25k", radar::kColorAltCruise, true},
  };

  static float s_vlw_size = 0.0f;
  applyCornerTextStyle(kLegendTextHeightPx, &s_vlw_size, radar::kColorLabel);
  s_draw->setTextDatum(textdatum_t::middle_left);

  constexpr int kRows = sizeof(rows) / sizeof(rows[0]);
  int cy = kLegendBottomRowCy;
  for (int i = 0; i < kRows; ++i) {
    drawPlaneIcon(kLegendIconCx, cy, 90.0f, rows[i].color, rows[i].full_size);
    s_draw->drawString(rows[i].text, kLegendTextX, cy);
    if (i + 1 < kRows) {
      cy -= kLegendRowPx - 1 + kLegendFullExtraPx * (rows[i].full_size + rows[i + 1].full_size);
    }
  }
}

template <typename Gfx>
void drawStaticGrid(Gfx& gfx) {
  initLabelMetrics();
  const DrawScope scope(gfx);
  displayFontEnsureLoaded(gfx);
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int grid_r = radar::kGridOuterRadius;

  gfx.fillScreen(radar::kColorBackground);
  drawRings(cx, cy, grid_r);
  drawCrosshairs(cx, cy, grid_r, radar::kColorGrid);
  initPalette();
  runway::SingleAirport single_airport;
  const bool has_single_airport = runway::drawAirports(
      gfx, settings::isAirportsEnabled(), settings::isMediumAirportsEnabled(),
      &single_airport);
  drawCenterDot(cx, cy);
  drawCardinalLabels();
  drawScaleLabel();
  drawAltitudeLegend();
  drawCornerInfo(has_single_airport ? &single_airport : nullptr);
  drawTitle();
  gfx.setTextDatum(textdatum_t::top_left);
}

bool ensureFrameSprite() {
  if (s_frame_ready) {
    return true;
  }
  static bool s_frame_alloc_failed = false;
  if (s_frame_alloc_failed) {
    return false;
  }
  // Use 8-bit color (RGB332) to halve the memory requirement (57 KB instead of 115 KB).
  // This allows the full 240x240 sprite to fit in the ESP32's fragmented SRAM!
  s_frame.setColorDepth(8);
  if (!s_frame.createSprite(radar::kSize, radar::kSize)) {
    Serial.println("radar: frame sprite alloc failed. Falling back to direct draw (expect flicker).");
    s_frame_alloc_failed = true;
    return false;
  }
  s_frame_ready = true;
  return true;
}

void drawRadarSweep(lgfx::LovyanGFX& gfx) {
  if (!settings::isRadarSweepEnabled()) {
    return;
  }

  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int r = radar::kGridOuterRadius;

  const uint32_t t_ms = static_cast<uint32_t>(settings::getSweepRotationSpeedMs());
  const float speed = 360.0f / t_ms;
  const float current_angle_deg = (millis() % t_ms) * speed;
  const float current_angle_rad = current_angle_deg * M_PI / 180.0f;
  
  // Draw fading trail. kSweepLines sets how many lines make up the beam
  // (was 20, about 30 deg wide); kSweepStepDeg is the gap between them.
#ifdef SWEEP_LINES
  constexpr int kSweepLines = SWEEP_LINES;
#else
  constexpr int kSweepLines = 7;
#endif
  constexpr float kSweepStepDeg = 1.5f;
  for (int i = kSweepLines - 1; i >= 0; --i) {
    const float trail_angle_deg = current_angle_deg - i * kSweepStepDeg;
    const float trail_angle_rad = trail_angle_deg * M_PI / 180.0f;
    const int tx = cx + static_cast<int>(lroundf(sinf(trail_angle_rad) * r));
    const int ty = cy - static_cast<int>(lroundf(cosf(trail_angle_rad) * r));
    
    // Blend from background to green
    uint32_t bg_hex = 0x040A1C;
    uint32_t fg_hex = 0x00FF00;
    uint8_t bg_r = (bg_hex >> 16) & 0xFF;
    uint8_t bg_g = (bg_hex >> 8) & 0xFF;
    uint8_t bg_b = bg_hex & 0xFF;
    uint8_t fg_r = (fg_hex >> 16) & 0xFF;
    uint8_t fg_g = (fg_hex >> 8) & 0xFF;
    uint8_t fg_b = fg_hex & 0xFF;

    const float t = 1.0f - (static_cast<float>(i) / kSweepLines);
    const uint8_t red = static_cast<uint8_t>(bg_r + (fg_r - bg_r) * t);
    const uint8_t green = static_cast<uint8_t>(bg_g + (fg_g - bg_g) * t);
    const uint8_t blue = static_cast<uint8_t>(bg_b + (fg_b - bg_b) * t);
    
    if (config::kDisplayRgbOrder) {
        gfx.drawLine(cx, cy, tx, ty, tft.color565(blue, green, red));
    } else {
        gfx.drawLine(cx, cy, tx, ty, tft.color565(red, green, blue));
    }
  }
}

// Blit the kSize frame sprite enlarged to kShownSize (nearest neighbour), one screen row at a
// time. pushRotateZoomWithAA did the same job at ~1 frame/s on the classic ESP32.
void pushFrameScaled() {
  constexpr int kOut = radar::kShownSize;
  static uint16_t s_lut[256];  // RGB332 sprite byte -> byte-swapped RGB565 for the panel
  static uint16_t s_src_x[kOut];
  static bool s_init = false;
  if (!s_init) {
    for (int c = 0; c < 256; ++c) {
      const uint8_t r3 = (c >> 5) & 0x07;
      const uint8_t g3 = (c >> 2) & 0x07;
      const uint8_t b2 = c & 0x03;
      const uint16_t rgb = lgfx::color565((r3 * 255) / 7, (g3 * 255) / 7, (b2 * 255) / 3);
      s_lut[c] = static_cast<uint16_t>((rgb >> 8) | (rgb << 8));
    }
    for (int x = 0; x < kOut; ++x) {
      s_src_x[x] = static_cast<uint16_t>(x * radar::kSize / kOut);
    }
    s_init = true;
  }
  static uint16_t s_line[kOut];
  const uint8_t* src = static_cast<const uint8_t*>(s_frame.getBuffer());
  tft.startWrite();
  tft.setAddrWindow(0, 0, kOut, kOut);
  int built_row = -1;
  for (int y = 0; y < kOut; ++y) {
    const int sy = y * radar::kSize / kOut;
    if (sy != built_row) {  // repeated source rows reuse the line already built
      const uint8_t* row = src + sy * radar::kSize;
      for (int x = 0; x < kOut; ++x) {
        s_line[x] = s_lut[row[s_src_x[x]]];
      }
      built_row = sy;
    }
    tft.writePixels(reinterpret_cast<const lgfx::swap565_t*>(s_line), kOut);
  }
  tft.endWrite();
}

// Double-buffered frame: composite the grid AND aircraft into the off-screen
// sprite, then blit it to the panel in a single pushSprite. Because the panel
// is updated in one pass, labels never show an erase/redraw gap — no flicker.
void renderFrame() {
  drawStaticGrid(s_frame);  // opens its own DrawScope(s_frame)
  drawRadarSweep(s_frame);
  {
    const DrawScope scope(s_frame);
    drawAircraft();
  }
  if (radar::kShownSize != radar::kSize) {
    pushFrameScaled();
  } else {
    s_frame.pushSprite(0, 0);
  }
  tft.setTextDatum(textdatum_t::top_left);
}

uint16_t downsampleColor(uint16_t c) {
  uint8_t r3 = (c >> 13) & 0x07;
  uint8_t g3 = (c >> 8) & 0x07;
  uint8_t b2 = (c >> 3) & 0x03;
  uint16_t r5 = (r3 << 2) | (r3 >> 1);
  uint16_t g6 = (g3 << 3) | (g3 << 1) | (g3 >> 2);
  uint16_t b5 = (b2 << 3) | (b2 << 1) | (b2 >> 1);
  return (r5 << 11) | (g6 << 5) | b5;
}

// Bottom status/details panel spans the full screen below the radar square.
constexpr int kScreenW = config::kDisplayWidth;
constexpr int kScreenH = config::kDisplayHeight;

void drawAppVersion() {
  tft.setTextDatum(textdatum_t::bottom_right);
  uint16_t bg = s_frame_ready ? downsampleColor(radar::kColorBackground) : radar::kColorBackground;
  tft.setTextColor(lgfx::color565(120, 120, 120), bg);
  displayFontEnsureLoaded(tft);
  if (displayFontIsSmooth()) {
    displayFontSetSmoothSize(tft, 0.80f);
  } else {
    displayFontSetBitmap(tft, &fonts::FreeSansBold12pt7b);
  }
  tft.setTextPadding(100);
  tft.drawString(config::kAppVersion, kScreenW - 2, kScreenH - 2);
  tft.setTextPadding(0);
}

void drawClock() {
  struct tm timeinfo;
  char time_str[16];
  
  if (!getLocalTime(&timeinfo, 0)) {
    strcpy(time_str, "--:--");
  } else {
    strftime(time_str, sizeof(time_str), "%H:%M", &timeinfo);
  }
  
  tft.setTextDatum(textdatum_t::bottom_left);
  uint16_t bg = s_frame_ready ? downsampleColor(radar::kColorBackground) : radar::kColorBackground;
  tft.setTextColor(lgfx::color565(120, 120, 120), bg);
  displayFontEnsureLoaded(tft);
  if (displayFontIsSmooth()) {
    displayFontSetSmoothSize(tft, 0.80f);
  } else {
    displayFontSetBitmap(tft, &fonts::FreeSansBold12pt7b);
  }
  tft.setTextPadding(50);
  tft.drawString(time_str, 2, kScreenH - 2);
  tft.setTextPadding(0);
}

void drawAircraftCount() {
  tft.setTextDatum(textdatum_t::bottom_center);
  uint16_t bg = s_frame_ready ? downsampleColor(radar::kColorBackground) : radar::kColorBackground;
  tft.setTextColor(lgfx::color565(120, 120, 120), bg);
  displayFontEnsureLoaded(tft);
  if (displayFontIsSmooth()) {
    displayFontSetSmoothSize(tft, 0.80f);
  } else {
    displayFontSetBitmap(tft, &fonts::FreeSansBold12pt7b);
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "%u aircraft", static_cast<unsigned>(s_visible_aircraft_count));
  tft.setTextPadding(80);
  tft.drawString(buf, kScreenW / 2, kScreenH - 2);
  tft.setTextPadding(0);
}

// "+N not shown" just under the radar when the Max Planes cap dropped aircraft
// (the farthest ones: adsb.fi lists them closest first). Drawn only while no
// aircraft is selected, since the selected details use this line.
void drawHiddenCount(uint16_t bg) {
  const size_t hidden = services::adsb::hiddenAircraftCount();
  if (hidden == 0) return;
  tft.setTextDatum(textdatum_t::top_center);
  tft.setTextColor(radar::kColorAltDescend, bg);
  displayFontEnsureLoaded(tft);
  if (displayFontIsSmooth()) {
    displayFontSetSmoothSize(tft, 0.7f);
  } else {
    displayFontSetBitmap(tft, &fonts::FreeSansBold9pt7b);
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "+%u not shown (max planes)", static_cast<unsigned>(hidden));
  tft.drawString(buf, kScreenW / 2, radar::kShownSize + 4);
}

// Selected aircraft: callsign/reg, type, altitude/speed, distance/route, from y = ly.
// Returns the y below the last line.
int drawSelectedLines(const services::adsb::Aircraft* ac, int ly, uint16_t bg) {
  tft.setTextDatum(textdatum_t::top_left);
  displayFontEnsureLoaded(tft);
  if (displayFontIsSmooth()) {
    displayFontSetSmoothSize(tft, 0.8f); // Increased from 0.5f for better readability
  } else {
    displayFontSetBitmap(tft, &fonts::FreeSansBold9pt7b);
  }
  
  const int lh = tft.fontHeight();
  char buf[64];
  tft.setTextColor(radar::kColorLabel, bg);
  tft.setTextPadding(kScreenW - 4);
  if (ac->reg[0] != '\0') {
    snprintf(buf, sizeof(buf), "%s (%s)%s", ac->callsign, ac->reg, ac->is_military ? " [MIL]" : "");
  } else {
    if (ac->callsign[0] == '~' || ac->callsign[0] == '\0') {
      snprintf(buf, sizeof(buf), "Hex: %s%s", ac->hex, ac->is_military ? " [MIL]" : "");
    } else {
      snprintf(buf, sizeof(buf), "%s%s", ac->callsign, ac->is_military ? " [MIL]" : "");
    }
  }
  tft.drawString(buf, 4, ly);
  ly += lh;
  
  tft.setTextColor(radar::kColorTagType, bg);
  if (ac->desc[0] != '\0') {
    tft.drawString(ac->desc, 4, ly);
  } else if (ac->type[0] != '\0') {
    tft.drawString(ac->type, 4, ly);
  } else {
    tft.drawString("Unknown Aircraft Type", 4, ly);
  }
  ly += lh;
  
  tft.setTextColor(radar::kColorTagAltitude, bg);
  if (!ui::radar::useMiles()) {
    snprintf(buf, sizeof(buf), "%s | %.0f km/h", ac->alt, ac->gs_knots * 1.852f);
  } else {
    snprintf(buf, sizeof(buf), "%s | %.0f mph", ac->alt, ac->gs_knots * 1.150779f);
  }
  tft.drawString(buf, 4, ly);
  ly += lh;
  
  float dx, dy, dist;
  offsetKmFromCenter(ac->lat, ac->lon, &dx, &dy, &dist);
  if (!ui::radar::useMiles()) {
    snprintf(buf, sizeof(buf), "Dst: %.1f km", dist);
  } else {
    snprintf(buf, sizeof(buf), "Dst: %.1f mi", dist / 1.60934f);
  }
  if (ac->route_origin[0] != '\0' && ac->route_destination[0] != '\0') {
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " | %s -> %s", ac->route_origin, ac->route_destination);
  }
  tft.drawString(buf, 4, ly);
  ly += lh;
  tft.setTextPadding(0);
  return ly;
}

#ifdef PLANE_LIST
// List layout (FNK0103N, 320x480): radar square top-left, a side strip to its right with
// clock / aircraft count / version, and every aircraft listed below the radar, closest
// first, two lines each in its icon colour. Tap an entry to select that aircraft.
constexpr int kSideX = radar::kShownSize + 1;
constexpr int kSideW = kScreenW - kSideX;
constexpr int kSideCx = kSideX + kSideW / 2;
constexpr int kListTop = radar::kShownSize + 1;
constexpr int kBannerH = 24;                      // "Connection Lost" strip at the very bottom
constexpr int kListBottom = kScreenH - kBannerH;
constexpr int kMaxListRows = services::adsb::kMaxAircraft;

char s_list_hex[kMaxListRows][7];
int s_list_rows = 0;      // aircraft rows currently on screen
int s_list_rows_top = 0;  // y of the first aircraft row
int s_list_row_h = 16;
bool s_list_had_details = false;
bool s_list_needs_clear = true;

void setListFont(float smooth_size) {
  displayFontEnsureLoaded(tft);
  if (displayFontIsSmooth()) {
    displayFontSetSmoothSize(tft, smooth_size);
  } else {
    displayFontSetBitmap(tft, &fonts::FreeSansBold9pt7b);
  }
}

void drawSidePanel(uint16_t bg) {
  tft.drawFastVLine(radar::kShownSize, 0, radar::kShownSize, radar::kColorGrid);
  const uint16_t grey = lgfx::color565(120, 120, 120);
  char buf[16];
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 0)) {
    strcpy(buf, "--:--");
  } else {
    strftime(buf, sizeof(buf), "%H:%M", &timeinfo);
  }
  tft.setTextDatum(textdatum_t::top_center);
  tft.setTextPadding(kSideW - 2);
  tft.setTextColor(radar::kColorLabel, bg);
  setListFont(0.9f);
  int y = 10;
  tft.drawString(buf, kSideCx, y);
  y += tft.fontHeight() + 40;

  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(services::adsb::aircraftCount()));
  setListFont(1.4f);
  tft.drawString(buf, kSideCx, y);
  y += tft.fontHeight();
  tft.setTextColor(grey, bg);
  setListFont(0.6f);
  tft.drawString("aircraft", kSideCx, y);

  tft.setTextDatum(textdatum_t::bottom_center);
  tft.drawString(config::kAppVersion, kSideCx, radar::kShownSize - 4);
  tft.setTextPadding(0);
}

// List columns: text at x (left edge, or right edge if right-aligned); its padding
// [x, x + pad) or (x - pad, x] is filled with the row background so lines overwrite in place.
struct ListCol {
  int x;
  int pad;
  bool right;
};
// Line 1: flight | type | description | heading.  Line 2: reg | alt | speed | climb | dist+dir.
constexpr ListCol kListLine1[] = {{3, 57, false}, {60, 38, false}, {98, 182, false}, {318, 38, true}};
constexpr ListCol kListLine2[] = {{3, 70, false}, {125, 52, true}, {190, 65, true},
                                  {252, 62, true}, {318, 66, true}};

void drawListLine(int y, int h, uint16_t fg, uint16_t row_bg, const ListCol* cols,
                  const char* const* texts, size_t n) {
  tft.setTextColor(fg, row_bg);
  tft.fillRect(0, y, 3, h, row_bg);
  tft.fillRect(kScreenW - 2, y, 2, h, row_bg);
  for (size_t c = 0; c < n; ++c) {
    tft.setTextDatum(cols[c].right ? textdatum_t::top_right : textdatum_t::top_left);
    tft.setTextPadding(cols[c].pad);
    tft.drawString(texts[c], cols[c].x, y);
  }
  tft.setTextPadding(0);
  const int text_h = tft.fontHeight();
  if (h > text_h) {
    tft.fillRect(0, y + text_h, kScreenW, h - text_h, row_bg);
  }
}

// Copy text into out, cut short (no partial glyphs) so it fits max_w px in the current font.
void fitText(const char* text, int max_w, char* out, size_t out_len) {
  strncpy(out, text, out_len - 1);
  out[out_len - 1] = '\0';
  size_t len = strlen(out);
  while (len > 0 && tft.textWidth(out) > max_w) {
    out[--len] = '\0';
  }
}

void drawPlaneList(uint16_t bg) {
  struct ListItem {
    size_t index;
    float dist_km;
    float bearing_deg;
  };
  const services::adsb::Aircraft* planes = services::adsb::aircraftList();
  const size_t n = services::adsb::aircraftCount();
  ListItem items[kMaxListRows];
  size_t count = 0;
  const services::adsb::Aircraft* selected = nullptr;
  for (size_t i = 0; i < n && count < static_cast<size_t>(kMaxListRows); ++i) {
    float dx_km = 0.0f;
    float dy_km = 0.0f;
    float dist_km = 0.0f;
    offsetKmFromCenter(planes[i].lat, planes[i].lon, &dx_km, &dy_km, &dist_km);
    items[count].index = i;
    items[count].dist_km = dist_km;
    float bearing = atan2f(dx_km, dy_km) * 180.0f / static_cast<float>(M_PI);
    if (bearing < 0.0f) bearing += 360.0f;
    items[count].bearing_deg = bearing;
    ++count;
    if (s_selected_hex[0] != '\0' && strcmp(planes[i].hex, s_selected_hex) == 0) {
      selected = &planes[i];
    }
  }
  for (size_t i = 1; i < count; ++i) {  // closest first
    const ListItem key = items[i];
    size_t j = i;
    while (j > 0 && items[j - 1].dist_km > key.dist_km) {
      items[j] = items[j - 1];
      --j;
    }
    items[j] = key;
  }

  // The details block shifts every row, so clear the whole list when it comes or goes.
  const bool has_details = selected != nullptr;
  if (s_list_needs_clear || has_details != s_list_had_details) {
    tft.fillRect(0, kListTop, kScreenW, kListBottom - kListTop, bg);
    s_list_rows = 0;
    s_list_had_details = has_details;
    s_list_needs_clear = false;
  }

  int y = kListTop + 3;
  if (selected) {
    y = drawSelectedLines(selected, y, bg);
    tft.drawFastHLine(0, y + 1, kScreenW, radar::kColorGrid);
    y += 4;
  }

  // Each aircraft takes two lines:
  //   flight | type | description          | heading   (icon colour)
  //   reg    | alt  | speed | climb | distance + compass direction   (grey)
  setListFont(0.7f);
  const int line_h = tft.fontHeight();
  const int row_h = 2 * line_h + 2;
  {
    const char* const h1[] = {"Flight", "Type", "Description", "Hdg"};
    const char* const h2[] = {"Reg", "Alt", "Speed", "Climb", "Dist"};
    drawListLine(y, line_h, radar::kColorGrid, bg, kListLine1, h1, 4);
    drawListLine(y + line_h, line_h + 2, radar::kColorGrid, bg, kListLine2, h2, 5);
  }
  y += row_h;
  s_list_rows_top = y;
  s_list_row_h = row_h;

  static const char* const kCompass[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  const uint16_t highlight_bg = lgfx::color565(55, 55, 85);
  const uint16_t line2_fg = lgfx::color565(170, 170, 170);
  const bool miles = ui::radar::useMiles();
  int rows = 0;
  for (size_t k = 0; k < count && y + row_h <= kListBottom; ++k) {
    const services::adsb::Aircraft& plane = planes[items[k].index];
    const uint16_t fg = plane.is_heli ? radar::kColorHelicopter : aircraftAltitudeColor(plane);
    const uint16_t row_bg = (&plane == selected) ? highlight_bg : bg;
    const char* flight =
        (plane.callsign[0] != '\0' && plane.callsign[0] != '~') ? plane.callsign : plane.hex;
    char desc[sizeof(plane.desc)];
    fitText(plane.desc[0] ? plane.desc : "-", kListLine1[2].pad - 4, desc, sizeof(desc));
    char heading[8];
    snprintf(heading, sizeof(heading), "%03.0f", plane.track_deg);
    char speed[16];
    char climb[16];
    char dist[20];
    const char* dir = kCompass[static_cast<int>((items[k].bearing_deg + 22.5f) / 45.0f) % 8];
    if (miles) {
      snprintf(speed, sizeof(speed), "%.0f mph", plane.gs_knots * 1.150779f);
      snprintf(climb, sizeof(climb), "%+.0f fpm", plane.vrate_fpm);
      snprintf(dist, sizeof(dist), "%.1f mi %s", items[k].dist_km / 1.60934f, dir);
    } else {
      snprintf(speed, sizeof(speed), "%.0f km/h", plane.gs_knots * 1.852f);
      snprintf(climb, sizeof(climb), "%+.1f m/s", plane.vrate_fpm * 0.00508f);
      snprintf(dist, sizeof(dist), "%.1f km %s", items[k].dist_km, dir);
    }
    const char* const line1[] = {flight, plane.type[0] ? plane.type : "-", desc, heading};
    const char* const line2[] = {plane.reg[0] ? plane.reg : "-", plane.alt[0] ? plane.alt : "-",
                                 speed, climb, dist};
    drawListLine(y, line_h, fg, row_bg, kListLine1, line1, 4);
    drawListLine(y + line_h, line_h + 2, line2_fg, row_bg, kListLine2, line2, 5);
    strcpy(s_list_hex[rows], plane.hex);
    ++rows;
    y += row_h;
  }
  if (rows < s_list_rows) {  // blank rows left over from a longer list
    const int clear_h = std::min((s_list_rows - rows) * row_h, kListBottom - y);
    if (clear_h > 0) {
      tft.fillRect(0, y, kScreenW, clear_h, bg);
    }
  }
  s_list_rows = rows;
}

void drawListLayout(uint16_t bg) {
  tft.drawFastHLine(0, radar::kShownSize, kScreenW, radar::kColorGrid);
  drawSidePanel(bg);
  drawPlaneList(bg);
  if (s_data_stale) {
    tft.fillRect(0, kListBottom, kScreenW, kBannerH, lgfx::color565(200, 0, 0));
    tft.setTextColor(lgfx::color565(255, 255, 255), lgfx::color565(200, 0, 0));
    tft.setTextDatum(textdatum_t::middle_center);
    setListFont(0.7f);
    tft.drawString("Connection Lost", kScreenW / 2, kScreenH - 12);
  } else {
    tft.fillRect(0, kListBottom, kScreenW, kBannerH, bg);
  }
}
#endif  // PLANE_LIST

void drawDetailsPanel() {
  uint16_t bg = s_frame_ready ? downsampleColor(radar::kColorBackground) : radar::kColorBackground;
#ifdef PLANE_LIST
  drawListLayout(bg);
  return;
#endif
  tft.drawFastHLine(0, radar::kShownSize, kScreenW, radar::kColorGrid);
  drawAppVersion();
  drawClock();
  drawAircraftCount();

  if (s_data_stale) {
    tft.fillRect(0, kScreenH - 24, kScreenW, 24, lgfx::color565(200, 0, 0));
    tft.setTextColor(lgfx::color565(255, 255, 255), lgfx::color565(200, 0, 0));
    tft.setTextDatum(textdatum_t::middle_center);
    displayFontEnsureLoaded(tft);
    if (displayFontIsSmooth()) {
      displayFontSetSmoothSize(tft, 0.7f);
    } else {
      displayFontSetBitmap(tft, &fonts::FreeSansBold9pt7b);
    }
    tft.drawString("Connection Lost", kScreenW / 2, kScreenH - 12);
    // Draw on top of everything at the bottom of the screen
  }

  if (s_selected_hex[0] == '\0') {
    tft.setTextDatum(textdatum_t::middle_center);
    tft.setTextColor(radar::kColorLabel, bg);
    displayFontEnsureLoaded(tft);
    if (displayFontIsSmooth()) {
      displayFontSetSmoothSize(tft, 1.2f);
    } else {
      displayFontSetBitmap(tft, &fonts::FreeSansBold12pt7b);
    }
    tft.setTextPadding(kScreenW);
    tft.drawString("CYD Flight Radar", kScreenW / 2, radar::kShownSize + (kScreenH - radar::kShownSize) / 2);
    tft.setTextPadding(0);
    // Clear the rest of the lines
    tft.fillRect(0, radar::kShownSize + 4, kScreenW, (kScreenH - radar::kShownSize) / 2 - 12, bg);
    tft.fillRect(0, radar::kShownSize + (kScreenH - radar::kShownSize) / 2 + 12, kScreenW, (kScreenH - radar::kShownSize) / 2 - 24, bg);
    drawHiddenCount(bg);
    return;
  }
  
  const services::adsb::Aircraft* planes = services::adsb::aircraftList();
  const size_t n = services::adsb::aircraftCount();
  const services::adsb::Aircraft* ac = nullptr;
  for (size_t i = 0; i < n; ++i) {
    if (strcmp(planes[i].hex, s_selected_hex) == 0) {
      ac = &planes[i];
      break;
    }
  }
  
  if (!ac) {
    return; // Selected aircraft disappeared
  }
  
  drawSelectedLines(ac, radar::kShownSize + 4, bg);
}

}  // namespace

namespace {
// Touch arrives in screen pixels; aircraft and labels live in frame-sprite pixels.
void screenToFrame(int* x, int* y) {
  *x = *x * radar::kSize / radar::kShownSize;
  *y = *y * radar::kSize / radar::kShownSize;
}
}  // namespace

void radarDisplaySetStale(bool is_stale) {
  if (s_data_stale != is_stale) {
    s_data_stale = is_stale;
    radarDisplayRefreshAircraft();
  }
}

bool radarDisplayHandleTouch(int x, int y) {
#ifdef PLANE_LIST
  if (y >= kListTop) {  // list area: a row selects (or deselects) that aircraft
    if (y >= s_list_rows_top && s_list_row_h > 0) {
      const int row = (y - s_list_rows_top) / s_list_row_h;
      if (row < s_list_rows) {
        if (strcmp(s_selected_hex, s_list_hex[row]) == 0) {
          s_selected_hex[0] = '\0';
        } else {
          strcpy(s_selected_hex, s_list_hex[row]);
        }
        return true;
      }
    }
    if (s_selected_hex[0] != '\0') {
      s_selected_hex[0] = '\0';
      return true;
    }
    return false;
  }
#endif
  screenToFrame(&x, &y);
  bool changed = false;
  if (y >= radar::kSize) {
    if (s_selected_hex[0] != '\0') {
      s_selected_hex[0] = '\0';
      changed = true;
    }
    return changed;
  }
  
  const services::adsb::Aircraft* planes = services::adsb::aircraftList();
  const size_t n = services::adsb::aircraftCount();
  int best_dist_sq = 2500; // ~50px radius to make it easier to hit with fat fingers
  const char* best_hex = nullptr;
  
  for (size_t i = 0; i < n; ++i) {
    int px, py;
    latLonToScreen(planes[i].lat, planes[i].lon, &px, &py);
    int d_sq = (px - x) * (px - x) + (py - y) * (py - y);
    if (d_sq < best_dist_sq) {
      best_dist_sq = d_sq;
      best_hex = planes[i].hex;
    }
  }
  
  if (best_hex) {
    if (strcmp(s_selected_hex, best_hex) != 0) {
      strcpy(s_selected_hex, best_hex);
      for (size_t i = 0; i < n; ++i) {
        if (strcmp(planes[i].hex, best_hex) == 0) {
          services::adsb::requestRouteFetch(planes[i].callsign, planes[i].hex);
          break;
        }
      }
      changed = true;
    }
    return true; // Handled
  } else {
    if (s_selected_hex[0] != '\0') {
      s_selected_hex[0] = '\0';
      changed = true;
      return true; // Handled
    }
  }
  return false; // Not handled, pass through
}

bool radarDisplayIsScaleTapped(int x, int y) {
  screenToFrame(&x, &y);
  if (y >= radar::kSize) return false;
  
  // Generous corner hit box around the bottom-right-anchored label, limited to
  // outside the outer ring so taps on aircraft near the ring still select them.
  const int min_x = scaleLabelAnchorX() - 60;
  const int min_y = scaleLabelAnchorY() - 35;
  if (x < min_x || y < min_y) return false;
  return distSqFromCenter(x, y) > radar::kGridOuterRadius * radar::kGridOuterRadius;
}

bool radarDisplayIsTitleTapped(int x, int y) {
  screenToFrame(&x, &y);
  if (y >= radar::kSize) return false;

  // Hit box around "Flight Radar" / "CYD" (drawTitle), outside the outer ring like the scale tap.
  const int min_x = radar::kSize - 75;
  const int max_y = 2 + kLegendRowPx + 20;
  if (x < min_x || y > max_y) return false;
  return distSqFromCenter(x, y) > radar::kGridOuterRadius * radar::kGridOuterRadius;
}

void radarDisplayDraw() {
  initPalette();
  initLabelMetrics();

  // Clear the full screen once to erase any previous status screens
  tft.fillRect(0, 0, config::kDisplayWidth, config::kDisplayHeight,
               radar::kColorBackground);
#ifdef PLANE_LIST
  s_list_needs_clear = true;
#endif

  if (ensureFrameSprite()) {
    renderFrame();
    drawDetailsPanel();
    return;
  }

  // Fallback when the sprite can't be allocated: draw straight to the panel.
  const DrawScope scope(tft);
  drawStaticGrid(tft);
  drawAircraft();
  tft.setTextDatum(textdatum_t::top_left);
  drawDetailsPanel();
}

void radarDisplayUpdateAnimation() {
  initPalette();
  
  size_t old_count = s_visible_aircraft_count;
  
  if (ensureFrameSprite()) {
    renderFrame();
  }
  
  bool footer_needs_update = false;

  if (old_count != s_visible_aircraft_count) {
    footer_needs_update = true;
  }
  
  // Update clock independently of ADS-B fetches
  static int s_last_minute = -1;
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 0)) {
    if (timeinfo.tm_min != s_last_minute) {
      s_last_minute = timeinfo.tm_min;
      footer_needs_update = true;
    }
  } else {
    if (s_last_minute != -2) {
      s_last_minute = -2;
      footer_needs_update = true;
    }
  }

  if (footer_needs_update) {
    drawDetailsPanel();
  }
}

void radarDisplayRefreshAircraft() {
  initPalette();

  if (ensureFrameSprite()) {
    renderFrame();
    drawDetailsPanel();
    return;
  }

  radarDisplayDraw();
}

}  // namespace ui
