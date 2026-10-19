#include "ui/runway_overlay.h"

#include <lgfx/v1/lgfx_fonts.hpp>

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "data/large_airports.h"
#include "data/medium_airports.h"
#include "hardware/display_font.h"
#include "services/radar_location.h"
#include "utils_math.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"



namespace ui::runway {
namespace {

constexpr float kKmPerDeg = 111.0f;
constexpr float kDegToRad = 3.14159265f / 180.0f;
constexpr size_t kMaxAirportLabels = 32;

bool s_in_range[data::large_airports::kAirportCount];
bool s_label_pending[data::large_airports::kAirportCount];

bool s_med_in_range[data::medium_airports::kAirportCount];
bool s_med_label_pending[data::medium_airports::kAirportCount];


bool s_runway_label_ready = false;
bool s_runway_label_use_vlw = false;
float s_runway_label_vlw_size = 0.38f;
const lgfx::GFXfont* s_runway_label_gfx = &fonts::FreeSansBold12pt7b;

int measureVlwHeight(lgfx::LGFXBase& gfx, float size) {
  gfx.setTextSize(size);
  return gfx.fontHeight();
}

float findVlwSizeForHeight(lgfx::LGFXBase& gfx, int target_px) {
  float lo = 0.2f;
  float hi = 1.2f;
  for (int i = 0; i < 14; ++i) {
    const float mid = (lo + hi) * 0.5f;
    if (measureVlwHeight(gfx, mid) < target_px) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return hi;
}

void initRunwayLabelStyle(lgfx::LGFXBase& gfx) {
  if (s_runway_label_ready) {
    return;
  }

  const int target = radar::kRunwayLabelHeightPx;
  if (displayFontIsSmooth()) {
    s_runway_label_use_vlw = true;
    s_runway_label_vlw_size = findVlwSizeForHeight(gfx, target);
  } else {
    s_runway_label_gfx = &fonts::FreeSansBold12pt7b;
    s_runway_label_use_vlw = false;
  }
  s_runway_label_ready = true;
}

void applyRunwayLabelStyle(lgfx::LGFXBase& gfx) {
  if (s_runway_label_use_vlw) {
    displayFontSetSmoothSize(gfx, s_runway_label_vlw_size);
  } else {
    displayFontSetBitmap(gfx, s_runway_label_gfx);
  }
}

inline float e7ToDeg(int32_t e7) { return utils::math::e7ToDeg(e7); }
inline void offsetKmFromCenter(float lat, float lon, float* dx_km, float* dy_km, float* dist_km) {
  utils::math::equirectangularOffsetKm(
      static_cast<float>(services::location::lat()),
      static_cast<float>(services::location::lon()),
      lat, lon, dx_km, dy_km, dist_km);
}

void latLonToScreen(float lat, float lon, int* out_x, int* out_y) {
  const float outer_km = radar::rangeCurrent().outer_km;
  const float px_per_km =
      static_cast<float>(radar::kGridOuterRadius) / outer_km;

  float dx_km = 0.0f;
  float dy_km = 0.0f;
  float dist_km = 0.0f;
  offsetKmFromCenter(lat, lon, &dx_km, &dy_km, &dist_km);

  *out_x = radar::kCenterX + static_cast<int>(lroundf(dx_km * px_per_km));
  *out_y = radar::kCenterY - static_cast<int>(lroundf(dy_km * px_per_km));
}

int distSqFromCenter(int x, int y) {
  const int dx = x - radar::kCenterX;
  const int dy = y - radar::kCenterY;
  return dx * dx + dy * dy;
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

bool segmentIntersectsDisc(int x0, int y0, int x1, int y1) {
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int r = radar::kGridOuterRadius;
  const int r_sq = r * r;

  if (distSqFromCenter(x0, y0) <= r_sq || distSqFromCenter(x1, y1) <= r_sq) {
    return true;
  }

  const int dx = x1 - x0;
  const int dy = y1 - y0;
  const int fx = x0 - cx;
  const int fy = y0 - cy;
  const int a = dx * dx + dy * dy;
  if (a == 0) {
    return false;
  }
  const int b = 2 * (fx * dx + fy * dy);
  const int c = fx * fx + fy * fy - r_sq;
  int disc = b * b - 4 * a * c;
  if (disc < 0) {
    return false;
  }
  disc = static_cast<int>(sqrtf(static_cast<float>(disc)));
  const float inv2a = 1.0f / (2.0f * static_cast<float>(a));
  const float t0 = (-static_cast<float>(b) - disc) * inv2a;
  const float t1 = (-static_cast<float>(b) + disc) * inv2a;
  return (t0 >= 0.0f && t0 <= 1.0f) || (t1 >= 0.0f && t1 <= 1.0f);
}

void drawBoldRunwayLabel(lgfx::LGFXBase& gfx, const char* ident, int mx, int my) {
  const int tw = gfx.textWidth(ident);
  const int th = gfx.fontHeight();
  constexpr int kPadX = 2;
  constexpr int kPadY = 1;

  gfx.setTextDatum(textdatum_t::bottom_center);
  const int left = mx - tw / 2 - kPadX;
  const int top = my - th - kPadY;
  gfx.fillRect(left, top, tw + kPadX * 2, th + kPadY, radar::kColorBackground);
  gfx.setTextColor(radar::kColorRunwayLabel, radar::kColorBackground);
  gfx.drawString(ident, mx - 1, my);
  gfx.drawString(ident, mx + 1, my);
  gfx.drawString(ident, mx, my);
}

template <typename RunwayT>
bool drawRunwayLine(lgfx::LGFXBase& gfx, const RunwayT& rw, float half_width,
                    uint16_t color, int* out = nullptr) {
  const float le_lat = e7ToDeg(rw.le_lat_e7);
  const float le_lon = e7ToDeg(rw.le_lon_e7);
  const float he_lat = e7ToDeg(rw.he_lat_e7);
  const float he_lon = e7ToDeg(rw.he_lon_e7);

  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
  latLonToScreen(le_lat, le_lon, &x0, &y0);
  latLonToScreen(he_lat, he_lon, &x1, &y1);

  if (!segmentIntersectsDisc(x0, y0, x1, y1)) {
    return false;
  }

  clipPointToOuterRing(x0, y0, &x1, &y1);
  clipPointToOuterRing(x1, y1, &x0, &y0);

  gfx.drawWideLine(x0, y0, x1, y1, half_width, color);
  if (out != nullptr) {
    out[0] = x0;
    out[1] = y0;
    out[2] = x1;
    out[3] = y1;
  }
  return true;
}

void offsetLabelFromCenter(int ax, int ay, int* lx, int* ly) {
  const int dx = ax - radar::kCenterX;
  const int dy = ay - radar::kCenterY;
  const float len = sqrtf(static_cast<float>(dx * dx + dy * dy));
  const int gap = radar::kRunwayLabelGapPx;
  if (len < 1.0f) {
    *lx = ax;
    *ly = ay - gap;
    return;
  }
  *lx = ax + static_cast<int>(lroundf(dx / len * static_cast<float>(gap)));
  *ly = ay + static_cast<int>(lroundf(dy / len * static_cast<float>(gap)));
}

void clipPointOntoOuterRing(int* x, int* y) {
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int r = radar::kGridOuterRadius;
  const int dx = *x - cx;
  const int dy = *y - cy;
  const int d_sq = dx * dx + dy * dy;
  const int r_sq = r * r;
  if (d_sq <= r_sq || d_sq == 0) {
    return;
  }
  const float scale = static_cast<float>(r) / sqrtf(static_cast<float>(d_sq));
  *x = cx + static_cast<int>(lroundf(static_cast<float>(dx) * scale));
  *y = cy + static_cast<int>(lroundf(static_cast<float>(dy) * scale));
}

// Draws the ident with its bottom-left corner at (x, y), kept inside the radar square.
void drawLabelUpRight(lgfx::LGFXBase& gfx, const char* ident, int x, int y) {
  const int tw = gfx.textWidth(ident);
  const int th = gfx.fontHeight();
  constexpr int kPadX = 2;
  constexpr int kPadY = 1;
  constexpr int kEdge = 1;

  if (x + tw + kPadX > radar::kSize - kEdge) x = radar::kSize - kEdge - kPadX - tw;
  if (x - kPadX < kEdge) x = kEdge + kPadX;
  if (y - th - kPadY < kEdge) y = kEdge + kPadY + th;
  if (y > radar::kSize - kEdge) y = radar::kSize - kEdge;

  const int mx = x + tw / 2;  // drawBoldRunwayLabel centres on mx, bottom on y
  drawBoldRunwayLabel(gfx, ident, mx, y);
}

void drawAirportLabel(lgfx::LGFXBase& gfx, const char* ident, int32_t lat_e7,
                      int32_t lon_e7) {
  int ax = 0;
  int ay = 0;
  latLonToScreen(e7ToDeg(lat_e7), e7ToDeg(lon_e7), &ax, &ay);
  clipPointOntoOuterRing(&ax, &ay);

  int lx = 0;
  int ly = 0;
  offsetLabelFromCenter(ax, ay, &lx, &ly);
  drawBoldRunwayLabel(gfx, ident, lx, ly);
}

// In-range airports from one data set plus where their runways landed on screen.
struct LabelSet {
  size_t count = 0;
  const char* ident[kMaxAirportLabels];
  int32_t lat_e7[kMaxAirportLabels];
  int32_t lon_e7[kMaxAirportLabels];
  int max_x[kMaxAirportLabels];
  int min_y[kMaxAirportLabels];
  bool has_rw[kMaxAirportLabels];
};

void drawLabels(lgfx::LGFXBase& gfx, const LabelSet& set) {
  for (size_t i = 0; i < set.count; ++i) {
    if (set.has_rw[i]) {
      const int gap = radar::kRunwayLabelGapPx;
      drawLabelUpRight(gfx, set.ident[i], set.max_x[i] + gap, set.min_y[i] - gap);
    } else {
      drawAirportLabel(gfx, set.ident[i], set.lat_e7[i], set.lon_e7[i]);
    }
  }
}

}  // namespace


// Draws in-range runways and fills *out with the airports to label.
template <size_t N_AIRPORTS, size_t N_RUNWAYS, typename AirportT, typename RunwayT>
void drawAirportRunways_impl(lgfx::LGFXBase& gfx,
                            const AirportT* airports,
                            const RunwayT* runways,
                            bool* in_range,
                            bool* label_pending,
                            LabelSet* out) {
  out->count = 0;
  if (!radar::showRunways()) return;
  displayFontEnsureLoaded(gfx);
  const float radius_km = radar::fetchRadiusKm();
  const float current_lat = services::location::lat();
  const float current_lon = services::location::lon();

  static float s_last_radius_km = -1.0f;
  static float s_last_lat = -999.0f;
  static float s_last_lon = -999.0f;

  static uint16_t s_cached_runways[512];
  static size_t s_cached_runway_count = 0;
  static uint16_t s_cached_airports[kMaxAirportLabels];
  static size_t s_cached_airport_count = 0;

  if (s_last_radius_km != radius_km || s_last_lat != current_lat || s_last_lon != current_lon) {
    s_last_radius_km = radius_km;
    s_last_lat = current_lat;
    s_last_lon = current_lon;

    s_cached_runway_count = 0;
    s_cached_airport_count = 0;

    for (size_t i = 0; i < N_AIRPORTS; ++i) {
      in_range[i] = false;
      label_pending[i] = false;
    }

    for (size_t i = 0; i < N_RUNWAYS; ++i) {
      const auto& rw = runways[i];
      const uint16_t ap_idx = rw.airport_idx;
      if (!in_range[ap_idx]) {
        const auto& ap = airports[ap_idx];
        float dx_km = 0.0f;
        float dy_km = 0.0f;
        float dist_km = 0.0f;
        offsetKmFromCenter(e7ToDeg(ap.lat_e7), e7ToDeg(ap.lon_e7), &dx_km, &dy_km, &dist_km);
        in_range[ap_idx] = (dist_km <= radius_km);
      }
      if (!in_range[ap_idx]) continue;
      
      if (s_cached_runway_count < sizeof(s_cached_runways) / sizeof(s_cached_runways[0])) {
        s_cached_runways[s_cached_runway_count++] = i;
      }
      
      if (!label_pending[ap_idx] && s_cached_airport_count < kMaxAirportLabels) {
        label_pending[ap_idx] = true;
        s_cached_airports[s_cached_airport_count++] = ap_idx;
      }
    }
  }

  // Per labelled airport: right-most x and top-most y of its runways on screen,
  // so the name can sit above-right of them instead of on top of them.
  int label_max_x[kMaxAirportLabels];
  int label_min_y[kMaxAirportLabels];
  bool label_has_rw[kMaxAirportLabels];
  for (size_t j = 0; j < s_cached_airport_count; ++j) label_has_rw[j] = false;

  // Blank the grid around every runway first, so one runway's clear zone
  // never covers another runway.
  for (size_t i = 0; i < s_cached_runway_count; ++i) {
    drawRunwayLine(gfx, runways[s_cached_runways[i]],
                   radar::kRunwayLineHalfWidth + radar::kRunwayClearMarginPx,
                   radar::kColorBackground);
  }

  // Draw cached runways
  for (size_t i = 0; i < s_cached_runway_count; ++i) {
    const auto& rw = runways[s_cached_runways[i]];
    int pts[4];
    if (!drawRunwayLine(gfx, rw, radar::kRunwayLineHalfWidth,
                        radar::kColorRunwayLine, pts)) continue;
    for (size_t j = 0; j < s_cached_airport_count; ++j) {
      if (s_cached_airports[j] != rw.airport_idx) continue;
      const int rx = pts[0] > pts[2] ? pts[0] : pts[2];
      const int ry = pts[1] < pts[3] ? pts[1] : pts[3];
      if (!label_has_rw[j]) {
        label_max_x[j] = rx;
        label_min_y[j] = ry;
        label_has_rw[j] = true;
      } else {
        if (rx > label_max_x[j]) label_max_x[j] = rx;
        if (ry < label_min_y[j]) label_min_y[j] = ry;
      }
      break;
    }
  }

  for (size_t i = 0; i < s_cached_airport_count; ++i) {
    const auto& ap = airports[s_cached_airports[i]];
    out->ident[i] = ap.ident;
    out->lat_e7[i] = ap.lat_e7;
    out->lon_e7[i] = ap.lon_e7;
    out->has_rw[i] = label_has_rw[i];
    out->max_x[i] = label_has_rw[i] ? label_max_x[i] : 0;
    out->min_y[i] = label_has_rw[i] ? label_min_y[i] : 0;
  }
  out->count = s_cached_airport_count;
}

bool drawAirports(lgfx::LGFXBase& gfx, bool large, bool medium,
                  SingleAirport* single) {
  static LabelSet s_large;
  static LabelSet s_medium;
  s_large.count = 0;
  s_medium.count = 0;
  if (large) {
    drawAirportRunways_impl<data::large_airports::kAirportCount, data::large_airports::kRunwayCount>(
        gfx, data::large_airports::kAirports, data::large_airports::kRunways, s_in_range,
        s_label_pending, &s_large);
  }
  if (medium) {
    drawAirportRunways_impl<data::medium_airports::kAirportCount, data::medium_airports::kRunwayCount>(
        gfx, data::medium_airports::kAirports, data::medium_airports::kRunways, s_med_in_range,
        s_med_label_pending, &s_medium);
  }

  const size_t total = s_large.count + s_medium.count;
  if (total == 1) {
    const LabelSet& set = s_large.count == 1 ? s_large : s_medium;
    strncpy(single->ident, set.ident[0], sizeof(single->ident) - 1);
    single->ident[sizeof(single->ident) - 1] = '\0';
    single->lat = e7ToDeg(set.lat_e7[0]);
    single->lon = e7ToDeg(set.lon_e7[0]);
    return true;
  }
  if (total == 0) return false;

  initRunwayLabelStyle(gfx);
  applyRunwayLabelStyle(gfx);
  drawLabels(gfx, s_large);
  drawLabels(gfx, s_medium);
  return false;
}

}  // namespace ui::runway
