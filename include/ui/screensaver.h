#pragma once

namespace ui::screensaver {

// Screensaver: matrix rain, the chronograph watch face, Go (light cycles on an LED grid) or
// Tron (pastel lines), picked by the settings "Saver Style". Non-blocking: call tick() from loop().
// Values are the saved "ss_style" pref, so new ones go at the end.
constexpr int kStyleMatrix = 0;
constexpr int kStyleWatch = 1;
constexpr int kStyleBoth = 2;  // alternates watch face and matrix rain
constexpr int kStyleGo = 3;
constexpr int kStyleTron = 4;
constexpr int kStyleSetting = -1;  // the "Saver Style" setting (Both alternates)

void begin(int style = kStyleSetting);
void tick();
void end();
bool isActive();

}  // namespace ui::screensaver
