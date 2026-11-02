#pragma once

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw aircraft only (blits cached grid; no full-screen clear). */
void radarDisplayUpdateAnimation();
void radarDisplayRefreshAircraft();
void radarDisplaySetStale(bool is_stale);
void updateThemeColors();

bool radarDisplayHandleTouch(int x, int y);
bool radarDisplayIsScaleTapped(int x, int y);
bool radarDisplayIsAltitudeLegendTapped(int x, int y);  // lower-left corner: opens the calculator
bool radarDisplayIsClassLegendTapped(int x, int y);     // lower-right corner: starts the matrix rain
bool radarDisplayIsTitleTapped(int x, int y);          // upper-right corner: starts the watch face

}  // namespace ui
