#pragma once

#include <LovyanGFX.hpp>

namespace ui::runway {

struct SingleAirport {
  char ident[5];
  float lat;
  float lon;
};

/**
 * Draw runways for the enabled airport sets. Airport idents are labelled on
 * the radar only when more than one airport is in range; with exactly one,
 * returns true and fills *single instead (shown in the corner by the caller).
 */
bool drawAirports(lgfx::LGFXBase& gfx, bool large, bool medium,
                  SingleAirport* single);

}  // namespace ui::runway
