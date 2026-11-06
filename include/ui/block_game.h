#pragma once

namespace ui::block_game {

// Falling-block puzzle game (10x20 well, 7 four-cell pieces, on-screen buttons), opened from the
// radar's lower-right corner (class legend). Draws straight to the panel, sized from
// tft.width/height. The game in progress survives closing (it is paused).
void begin();
void end();
bool isActive();
// Call every loop with the raw touch state. Returns true when Exit was pressed.
bool handleTouch(bool touched, int x, int y);
// Gravity, key repeat, line-clear animation and drawing. Call every loop while active.
void tick();

}  // namespace ui::block_game
