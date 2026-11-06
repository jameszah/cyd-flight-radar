#pragma once

namespace ui::tron_saver {

// "Tron" screensaver: a few pastel light trails running as continuous lines with 90 degree
// turns, free to cross each other; wiped and restarted when the screen gets busy.
// Draws straight to the panel, no buffers.
bool begin();
void tick();
void end();
bool isActive();

}  // namespace ui::tron_saver
