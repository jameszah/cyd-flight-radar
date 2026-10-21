# Project notes: CYD flight radar on Freenove FNK0104B and FNK0103N

Fork of nicholaswilde/cyd-flight-radar, ported to the owner's **Freenove FNK0104B**
(ESP32-S3, 2.8" ILI9341 240x320, FT6336 capacitive touch, native USB-C) and **FNK0103N**
(see its section below). Both live on `main` as separate envs (the `FNK0103N` branch was merged in).
Built and flashed from VS Code + PlatformIO on Windows. `fnk0104b` is the default env; pick the
board with the env switcher in the VS Code status bar or `-e`.

## Build / flash
- `pio run -e fnk0104b` (build), `pio run -e fnk0104b -t upload`, `pio device monitor -e fnk0104b`
- Same with `-e fnk0103n` for the FNK0103N (pinned to COM10; fnk0104b auto-detects its port).
- Native USB: if upload can't connect, hold BOOT, tap RST, release BOOT, retry.
- `pio run` sometimes fails first time with `No module named 'SCons.Tool.FortranCommon'` (PlatformIO
  tool update racing the VS Code extension); just rerun.
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

## FNK0103N port (ESP32 classic, 3.5" ST7796 320x480, XPT2046 resistive)
- `[env:fnk0103n]` (extends `cyd_base`, esp32dev). Shared code is guarded by `BOARD_FNK0103N` /
  `PLANE_LIST` etc., so changes for one board must keep the other env building.
- Pins from Freenove's FNK0114N TFT_eSPI setup: same as `cyd_35c` (MOSI 13, MISO 12, SCLK 14, CS 15,
  DC 2, BL 27). Touch is an XPT2046 on the LCD's SPI bus, CS 33 (`XPT2046_SHARED_CS`, `bus_shared`, no IRQ).
- `radar::kSize` = `RADAR_SIZE` / `TFT_WIDTH` / 240; `kGridOuterRadius` scales with it (107 @ 240).
  The bottom panel uses `config::kDisplayWidth/Height` instead of literal 240/320.
- `RADAR_SIZE=320` gave "Connection Lost" on hardware (probably heap: 100 KB sprite + TLS), so the env
  uses `RADAR_SIZE=240` (heap log: largest free block ~39 KB, so a 102 KB sprite can't fit). The 240 frame
  is scaled to 320 on push (`radar::kShownSize`, `pushFrameScaled`: nearest-neighbour rows via an RGB332
  LUT; `pushRotateZoomWithAA` managed ~1 fps); touch goes through `screenToFrame`. Env also sets
  `TFT_SPI_HZ=80000000`, `SWEEP_LINES=1`, `FETCH_CORE=0` (fetch task was pinned to core 1 with the UI).
  Heap: with 115 KB static RAM the TLS handshake failed (`SSL - Memory allocation failed`, largest free
  block ~32 KB). Env now sets `TRAIL_LENGTH=6`, `MAX_AIRCRAFT=20`, and lv_conf.h uses `LV_MEM_CUSTOM 1`
  (malloc) under `BOARD_FNK0103N`: static RAM 115.9 KB -> 76.6 KB. Verified: TLS works again.
- The 1.33x zoom looked blocky, so zoom is now opt-in (`RADAR_ZOOM`); the env sets `PLANE_LIST` instead
  (`drawListLayout` in `radar_display.cpp`): 240 radar top-left; 80px side strip with clock, aircraft
  count, version; below it every aircraft closest-first, two lines each (font 0.7 of the 15px vlw):
  flight/type/description/heading in its icon colour, then reg/alt/speed/climb/distance+compass in grey
  (existing fields only, no extra queries). Column geometry: `kListLine1/2` (`ListCol`).
  Tapping a row selects it (`s_list_hex` row map in `radarDisplayHandleTouch`); the selected aircraft's
  details (`drawSelectedLines`) go above the list. Not yet verified on hardware. `NO_ROUTE_LOOKUP` turns the
  adsbdb route query off for this env (`requestRouteFetch` returns early). Board is on COM10.
- `kDisplayRgbOrder` false for FNK0103N too. `TOUCH_DEBUG` prints tap coordinates on serial.
- Not verified: touch calibration (raw 300..3900, y reversed: a guess), colours, whether the
  100 KB 320x320 sprite plus TLS fit in heap (fallback: `RADAR_SIZE=240`).

## Screensaver (both boards)
- `src/ui/screensaver.cpp`: matrix rain (from a pasted blocking sketch, made non-blocking:
  `begin/tick/end`, ticked from `loop()` every `kScreensaverFrameMs` (50), straight to `tft`).
  Reworked to look like the film: one stream per 7x12 column, halfwidth katakana (U+FF66-FF9D) + digits
  from `fonts::lgfxJapanGothic_12` (+~110 KB flash), 8-shade fade (pale head -> dark green), trails
  rows/3..rows, a few random glyph changes inside trails per frame. A malloc'd glyph grid (cols*rows bytes,
  ~1.8 KB) lets trails fade without changing glyphs; freed on exit. Not yet seen on hardware.
  Starts after `getScreensaverTimeoutMs()` with no touch/BOOT press, only while the radar is up and settings
  closed; backlight drops to `kScreensaverBrightness` (80; 40 hid the dark greens). Fetches keep running, radar redraws are skipped.
  The waking touch/press is swallowed, then `radarDisplayDraw()`. Wi-Fi state changes end it.
  Settings row "Screensaver": Off/1/5/10/30/60 min, pref `"ss_min"`, default `kScreensaverDefaultMin` = 1
  (for testing; raise to 30 later).

## Not yet verified on hardware
- Range-label tap (just re-enabled) and the airport label placement.
- Route lookup: watch serial for `route: ... HTTP 200` / `route: X -> Y` and the `heap[...]` lines.
