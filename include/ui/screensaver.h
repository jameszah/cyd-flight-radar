#pragma once

namespace ui::screensaver {

// Matrix-rain screensaver. Non-blocking: call tick() from loop().
void begin();
void tick();
void end();
bool isActive();

}  // namespace ui::screensaver
