#include "hardware/display.h"

#include "hardware/display_font.h"

LGFX tft;

void displayInit() {
  tft.init();
#ifdef DISPLAY_ROTATION
  tft.setRotation(DISPLAY_ROTATION);  // FNK0115: 1 or 3 = portrait (touch follows)
#else
  tft.setRotation(0);
#endif
  tft.setBrightness(255);
  tft.setTextWrap(false);
  displayFontInit();
}
