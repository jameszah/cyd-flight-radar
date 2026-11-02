#pragma once

namespace ui::screensaver {

// Screensaver: matrix rain or the chronograph watch face (settings "Saver Style").
// Non-blocking: call tick() from loop().
constexpr int kStyleMatrix = 0;
constexpr int kStyleWatch = 1;
constexpr int kStyleSetting = -1;  // the "Saver Style" setting (Both alternates)

void begin(int style = kStyleSetting);
void tick();
void end();
bool isActive();

}  // namespace ui::screensaver
