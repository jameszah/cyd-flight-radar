# Project notes: CYD flight radar on Freenove FNK0104B

Fork of nicholaswilde/cyd-flight-radar, ported to the owner's **Freenove FNK0104B**
(ESP32-S3, 2.8" ILI9341 240x320, FT6336 capacitive touch, native USB-C).
Built and flashed from VS Code + PlatformIO on Windows. `fnk0104b` is the default env.

## Build / flash
- `pio run -e fnk0104b` (build), `pio run -e fnk0104b -t upload`, `pio device monitor -e fnk0104b`
- Native USB: if upload can't connect, hold BOOT, tap RST, release BOOT, retry.
- `config/secrets.h` exists with blank SSID/password on purpose (gitignored): the board
  opens the `cyd-flight-radar-XXXX` captive portal for Wi-Fi and location.

## Board pins (from Freenove's FNK0104AB TFT_eSPI setup + touch sketch)
- LCD SPI: MOSI 11, SCLK 12, MISO 13, CS 10, DC 46, BL 45, RST -1. Needs colour inversion (`TFT_INVERT`).
- Touch FT6336 (I2C 0x38): SDA 16, SCL 15, RST 18, INT 17. BOOT button GPIO 0. WS2812 LED GPIO 42.

## Changes made vs upstream
- `platformio.ini`: added `[env:fnk0104b]` (esp32-s3-devkitc-1, USB CDC on boot) and made it default.
- `include/hardware/lgfx_config.hpp`: `FT6336_TOUCH` branch using `lgfx::Touch_FT5x06`;
  touch axes reversed (x_min=W-1..0, y_min=H-1..0) because the panel is mounted 180° from the LCD;
  `TFT_INVERT` option for ILI9341.
- `src/ui/radar_display.cpp`: radar sweep cut from 21 lines to 7 (`kSweepLines`, `kSweepStepDeg`);
  trail drawing uses `services::adsb::kTrailLength`.
- `include/services/adsb_client.h` / `src/services/adsb_client.cpp`: flight-path trail length is now
  `kTrailLength = 18` (was a hard-coded 6).
- `src/ui/runway_overlay.cpp`: airport ident labels placed above-right of each airport's on-screen
  runway extent (`drawLabelUpRight`), clamped inside the 240px radar square; falls back to old placement
  if no runway is visible. `include/ui/radar_theme.h`: `kRunwayLabelGapPx` 3 -> 4.
  Runways are 4px wide (was 2) in white `kColorRunwayLine` (trails keep `kColorRunway`), and
  a background-colored pass `kRunwayClearMarginPx` (5px) wider blanks rings/crosshairs around them first.
- `src/main.cpp`: re-enabled tap-to-cycle-range on the scale label (upstream commit c1436fc had
  disabled it; `radarDisplayIsScaleTapped` was still present). Label and tap zone are in the lower-right corner of the 240px radar square, outside the outer ring
  (`scaleLabelAnchorX/Y`).
- `README.md`: added an FNK0104B section.
- `src/services/adsb_client.cpp` (branch `feat/route-lookup`): restored the adsbdb.com route lookup that
  upstream `aba1fc6` stubbed out. Tapping a plane queues its callsign; the next `fetchUpdate()` calls
  `fetchRoute()` first (same `s_client`, raw HTTP/1.0, stopped before the adsb.fi request), keeps one
  cached result (`s_route_result_*`), and copies it onto that aircraft. Info panel shows `| LAX -> JFK`.

- Main-display aircraft tag now shows only the type (callsign + altitude removed; still in the info panel).
  Icon color by altitude (`aircraftAltitudeColor` in `radar_display.cpp`, thresholds in `radar_theme.h`):
  >= 10000 ft white (was 20000); below that green climbing / red descending / yellow level (|rate| < 200 fpm),
  ground or unknown. Uses adsb.fi `baro_rate`/`geom_rate` (new `Aircraft::alt_ft`, `vrate_fpm`), falling
  back to the altitude change since the previous fetch. Applies to military too; helicopters stay cyan.
  A legend (`drawAltitudeLegend`: high / climb / flat / descend with icons) sits in the lower-left
  corner outside the outer ring, drawn with the static grid. `drawCenterCoordinates` writes the radar
  center (e.g. `51.05N` / `114.07W`, 12px) in the upper-left corner (`drawCornerInfo`). When exactly
  one airport is in range, `runway::drawAirports` skips its on-circle label and returns it, and the
  corner shows its ident (runway color) + its lat/lon instead; `drawTitle` puts "Flight Radar" /
  "CYD" (10px, grid green) in the upper-right. Range label is now the same height as N/S/E/W
  (`kScaleBelowCardinalPx` 3 -> 0).

- `config/config.h`: `kDisplayRgbOrder` is false under `BOARD_FNK0104B` (it's a software R/B swap in
  `hex2rgb565` etc. meant for GC9A01 boards; on the ILI9341 it turned red planes blue).

- `src/ui/settings_menu.cpp`: timezone is now saved (`"tz"` = POSIX string, matched back to a preset in
  `setup()`); it was never persisted, so it reset to UTC every boot. `airports`, `med_airports`, `auto_dim`
  were saved but never loaded; `setup()` now reads them back too.

## Not yet verified on hardware
- Range-label tap (just re-enabled) and the airport label placement.
- Route lookup: watch serial for `route: ... HTTP 200` / `route: X -> Y` and the `heap[...]` lines.
