#pragma once

namespace ui::calculator {

// Pocket calculator with a 10-digit seven-segment LCD, opened from the radar's upper-right
// corner. Draws straight to the panel (no sprite). Memory and the display survive closing it.
void begin();
void end();
bool isActive();
// Tap on the calculator screen. Returns true for Exit.
bool handleTap(int x, int y);

}  // namespace ui::calculator
