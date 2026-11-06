#pragma once

namespace ui::go_saver {

// "Go" screensaver: two light cycles playing themselves on an LED dot grid (cyan and pink trails,
// white heads), wiped and restarted when both have crashed. Draws straight to the panel.
bool begin();  // false if the grid buffers can't be allocated
void tick();
void end();
bool isActive();

}  // namespace ui::go_saver
