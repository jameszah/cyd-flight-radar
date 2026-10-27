#pragma once

#include <LovyanGFX.hpp>

namespace ui::runway {

struct SingleAirport {
  char ident[5];
  float lat;
  float lon;
};

/**
 * Draw runways for the enabled airport sets and label their idents on the radar,
 * except the biggest one in range (large set first, then most runway length): that
 * one is left off, *single is filled (shown in the corner by the caller) and it
 * returns true. Returns false when no airport is in range.
 */
bool drawAirports(lgfx::LGFXBase& gfx, bool large, bool medium,
                  SingleAirport* single);

}  // namespace ui::runway
