#pragma once

namespace ui::watch_face {

// Chronograph watch face screensaver (moonwatch style). Non-blocking: call tick() from loop().
// The chronograph is stopped at boot and keeps its state (running or not) while the face is closed.
bool begin();  // false if the band sprite couldn't be allocated
void tick();
void end();
bool isActive();
// Tap on the face screen: Start/Stop and Reset work the chronograph, timed from press_ms
// (touch-down, so the length of the press isn't counted). Returns true for Exit.
bool handleTap(int x, int y, unsigned long press_ms);

}  // namespace ui::watch_face
