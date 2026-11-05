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
  tool update racing the VS Code extension). Root cause: the `pio` on PATH is a pip-installed Core
  6.1.19 (MS Store Python 3.13) that wants tool-scons 4.8.1, while VS Code's `~/.platformio/penv` Core
  6.2.0 wants 4.11.1; they swap the shared package. From a shell use
  `~/.platformio/penv/Scripts/pio.exe`.
- `lv_conf.h`: `LV_MEM_CUSTOM 1` (malloc) for FNK0104B too. The 24 KB pool ran out with the lat/lon/Wi-Fi
  rows and LVGL's `LV_ASSERT_HANDLER while(1);` froze the board right after drawing the title and Wi-Fi
  icon. Verified on hardware: settings opens, heap -22 KB while open.
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
- `src/ui/radar_display.cpp`: radar sweep cut from 21 lines to 7, now 1 on every board (`kSweepLines`; `SWEEP_LINES` overrides);
  trail drawing uses `services::adsb::kTrailLength`.
- `include/services/adsb_client.h` / `src/services/adsb_client.cpp`: flight-path trail length is now
  `kTrailLength = 18` (was a hard-coded 6).
- `src/ui/runway_overlay.cpp`: airport ident labels placed above-right of each airport's on-screen
  runway extent (`drawLabelUpRight`), clamped inside the 240px radar square; falls back to old placement
  if no runway is visible. `include/ui/radar_theme.h`: `kRunwayLabelGapPx` 3 -> 4.
  Runways are 4px wide (was 2) in white `kColorRunwayLine` (trails keep `kColorRunway`), and
  a background-colored pass `kRunwayClearMarginPx` (5px) wider blanks rings/crosshairs around them first.
- `src/main.cpp`: re-enabled tap-to-cycle-range on the scale label (upstream commit c1436fc had
  disabled it; `radarDisplayIsScaleTapped` was still present). Label is now in the upper-left corner under the lat/lon (`drawScaleLabel(top_y)`, y from
  `drawCornerInfo`'s return; one row lower when the airport ident row is shown, where its box covers a
  few px of the outer ring). Tap zone = the drawn label box (`s_scale_box_*`) + 16 px (8 was hit-and-miss on hardware), out to the left edge; it's checked before
  aircraft taps. Not yet seen on hardware. Drawn as a button: `kColorScaleButton` (0x1E8C32) rounded box,
  black text, `kScaleLabelX` 5 so the box's left edge stays on screen.
- `README.md`: added an FNK0104B section.
- `src/services/adsb_client.cpp` (branch `feat/route-lookup`): restored the adsbdb.com route lookup that
  upstream `aba1fc6` stubbed out. Tapping a plane queues its callsign; the next `fetchUpdate()` calls
  `fetchRoute()` first (same `s_client`, raw HTTP/1.0, stopped before the adsb.fi request), keeps one
  cached result (`s_route_result_*`), and copies it onto that aircraft. Info panel shows `| LAX -> JFK`.

- Main-display aircraft tag now shows only the type (callsign + altitude removed; still in the info panel).
  Icon color by altitude (`aircraftAltitudeColor` in `radar_display.cpp`, thresholds in `radar_theme.h`):
  >= `kCruiseAltitudeFt` (25000) white, >= `kHighAltitudeFt` (10000) green `0x00FF00` (same as climbing, full-size icon; was 15000 light pink `0xFFB6AA`
  ); below that green climbing /
  red descending / yellow level (|rate| < 200 fpm), ground or unknown. Every plane (not helicopters)
  is an airplane silhouette (`drawPlaneIcon`: fuselage, swept wings, tail plane as 2-3 px wide lines;
  `kPlane*` in `radar_theme.h`): full size at >= 10000, `kPlaneIconSmallScale` (0.75) below. Replaced
  the filled triangle + red cruise dot (two stacked triangles were tried: unreadable). Legend rows
  bottom-up: descend / flat / climb (small), "10k", "25k" (full size). Uses adsb.fi `baro_rate`/`geom_rate` (new `Aircraft::alt_ft`, `vrate_fpm`), falling
  back to the altitude change since the previous fetch. Applies to military too; helicopters stay cyan.
  A legend (`drawAltitudeLegend`, icons + labels) sits in the lower-left
  corner outside the outer ring, drawn with the static grid. `drawCenterCoordinates` writes the radar
  center (e.g. `51.05N` / `114.07W`, 12px) in the upper-left corner (`drawCornerInfo`). When any
  airport is in range, `runway::drawAirports` picks the biggest (any large-set airport beats medium;
  within a set, most total in-range runway `length_m`, `LabelSet::runway_m`), skips its on-circle label
  and returns it, and the corner shows its ident (runway color) + its lat/lon instead (was: only when
  exactly one airport was in range; not yet seen on hardware); `drawTitle` puts
  "jameszah/cyd-flight-radar" (`kTitleText`, grid green; 10px, or 8px if it would reach the "N", which it
  does on the 240px radar: ~124 vs ~110 px free) in the upper-right, and under it local time `2:05 PM` and
  `21:05 UTC` in white at the lat/lon size (12px, `kCoordRowPx`), redrawn with every frame. On the 240px
  radar the UTC line's lower-left sits close to the outer ring; not yet seen on hardware. Range label is now the same height as N/S/E/W
  (`kScaleBelowCardinalPx` 3 -> 0).

- Aircraft beyond the outer ring: was a filled dot on the screen rim at its bearing; now its icon
  (`drawPlaneIcon` with the normal 10k size rule / `drawHelicopterSymbol`, real heading, altitude colour,
  trainer shape), no tag or vector. Icon centre = outer ring + icon radius (`kPlaneIconExtentPx` 9 /
  `kPlaneIconSmallExtentPx` 7) + `kBeyondRingIconGapPx`, clamped on screen (overlaps the ring a bit near
  N/S/E/W). Drawing the inside-the-ring part of their trails was tried and removed (looked
  disconnected at the ring edge on hardware). Fetch radius has its own `kFetchScreenMarginPx`
  (2; it used to share the old dot margin). Not yet seen on hardware.
- Trainer icon: `utils::adsb::isTrainerType` (C150/152/162/172, P28A/P28R, PA38, DV20, DA40/42, SR20; now the `kTrainer` class, see Aircraft icon classes;
  owner's local school flies C172 + P28A); `drawPlaneIcon(..., trainer)`
  draws straight wings + straight tail plane (`kTrainer*` in `radar_theme.h`) instead of swept. Colour and
  size rules unchanged. Trainers get no radar tag (no type / callsign; `drawAircraft` skips
  `drawAircraftTag`), still listed and tappable. Not in the legend (no room above "25k" before the outer ring). Not yet seen on hardware.
  Ideas discussed, not built: arriving/departing from runway alignment + vrate, circling from accumulated
  heading change (trail too short at 3 s sweeps), `ownOp` (medevac marker), airport elevation, gliders /
  balloons / drones / ground vehicles shapes, a legend page for the classes.
- Class legend (`drawClassLegend`, static grid) in the lower-right corner where the range label was: icons
  pointing west on the right edge, labels to their left, bottom-up heavy / prop / jet / biz / mil
  (narrowbody + orange marker), all at scale 1.0, heavy included, so "jet" matches "25k" in the altitude
  legend (owner's request; `drawPlaneIcon`'s `scale_override` param; heavy size was tried first, too tall).
  All in `kColorLabel`. GA, trainer and fighter aren't in it (no room). Row heights /
  nose / tail px are hand-set per row, `kClassLegendRowGapPx` (2) between rows. On the 320 radar the
  top "mil" label just clears the outer ring (computed); tighter on 240 boards. Not yet seen on hardware.

## Aircraft icon classes (all boards)
- `Aircraft::aircraft_class` (`services::adsb::AircraftClass`, replaces `is_trainer`) from
  `utils::adsb::classifyAircraft(type, category)`: type lists first (trainer, fighter, heavy quad, heavy,
  bizjet, turboprop, light GA; entries ending in `*` are prefixes, order matters: "C1*"/"C2*" light GA comes
  after C130/C25x/C208), then adsb.fi `category` (A5 heavy, A6 fighter, A1/B1/B4 light GA), else narrowbody.
- Shapes in `drawPlaneIcon(..., cls, military)` (constants `kHeavy*`, `kBizjet*`, `kTurboprop*`,
  `kFighter*` in `radar_theme.h`): narrowbody = old swept icon; heavy = same at 1.25x on any altitude, thicker
  fuselage, 1 engine dot per wing (quad: 2); bizjet = wing set aft, rear engine dots, tail; turboprop =
  long straight wing + forward nacelle stubs; light GA = trainer shape but tagged; fighter = filled delta.
  Military (non-fighter) adds a 4px orange `kColorMilitary` square behind the tail. Altitude colours unchanged.
- `isMilitary` was broken: it read `dbFlags` as a bool, but it's an int bitfield (bit 0 = military), so
  nothing was ever flagged. Fixed; expect `[MIL]` in the info panel and markers now.
- Emergency: `squawk` + `emergency` added to the JSON filter; 7500/7600/7700 or an `emergency` other than
  none/lifeguard/minfuel sets `is_emergency`: 2px red ring blinking every 500 ms (only as often as frames
  redraw), and the info panel ident gets " SQ 7700". `drawAircraftSymbol` / `aircraftIconExtentPx` are
  shared by in-ring and beyond-ring drawing. Not yet seen on hardware.

- `config/config.h`: `kDisplayRgbOrder` is false under `BOARD_FNK0104B` (it's a software R/B swap in
  `hex2rgb565` etc. meant for GC9A01 boards; on the ILI9341 it turned red planes blue).

- `src/ui/settings_menu.cpp`: timezone is now saved (`"tz"` = POSIX string, matched back to a preset in
  `setup()`); it was never persisted, so it reset to UTC every boot. `airports`, `med_airports`, `auto_dim`
  were saved but never loaded; `setup()` now reads them back too.

- Settings menu has text rows (`create_edit_row`, `kEditFields`) for Latitude, Longitude, WiFi 1
  SSID/Pass, WiFi 2 SSID/Pass (passwords shown in plain text). They are the first rows of the menu:
  Wi-Fi 1/2, then a "Preset" `lv_dropdown` (`kLocationPresets`: Windsor Castle, LAX, Toronto, Calgary, Hawaii, Nice;
  shows fixed text "Choose", picking one saves lat/lon via `saveFromStrings`), then Latitude / Longitude.
  Not yet seen on hardware. Tapping one opens a full-screen
  `lv_keyboard` editor (number mode for lat/lon) with Save / Cancel `lv_btn`s under the text field;
  bad lat/lon shows "Invalid ...". The keyboard's own OK / keyboard keys (CLICK_TRIG: fire on release)
  did nothing on the FNK0104B; they are still wired to the same `editor_save` / `close_editor`. Lat/lon go through `services::location::saveFromStrings` (radar re-centres on the next
  fetch); Wi-Fi goes to prefs `"wifi"`: `ssid`/`pass` (same keys as the portal) and `ssid2`/`pass2`.
- `WifiManager` keeps two networks (`_ssids[2]`): tries the last-used one, then the other
  (`tryOtherNetwork`), and only then opens the captive portal. Credentials are re-read from prefs on
  every reconnect cycle, so settings edits apply after reboot or a dropped connection. The Connecting
  screen shows the SSID actually being tried (`getSSID()`). Not yet verified on hardware.

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
  `TFT_SPI_HZ=80000000`, `FETCH_CORE=0` (fetch task was pinned to core 1 with the UI).
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

## FNK0104S port (ESP32-S3, 4.0" ST7796 320x480, FT6336 capacitive)
- `[env:fnk0104s]` (own section, same platform/board as fnk0104b; no `extends`: suspected cause of the VS Code env switcher error "Cannot read properties of undefined (reading 'id')"; unconfirmed). Pins from Freenove's
  `Libraries/FNK0104S/TFT_eSPI_Setups` (FNK0104S_4.0_320x480_ST7796.h): same LCD pins as the FNK0104B
  (MOSI 11, SCLK 12, CS 10, DC 46, BL 45, no MISO), inversion on, 80 MHz. Touch is the same FT6336
  (SDA 16, SCL 15, RST 18, INT 17), but `lgfx::Touch_FT5x06` never detected it (no taps at all), so the
  FNK0104S uses `include/hardware/touch_ft6336.hpp` (`Touch_FT6336`: 300 ms after reset, no vendor-ID
  check, polls TD_STATUS, ignores INT; `TOUCH_DEBUG` prints probe/read-fail status every 5 s).
  Touch axes are NOT flipped on the FNK0104S (the FNK0104B flip made both axes backwards; checked on hardware).
- USB is native (COM6, VID 303A), like the FNK0104B. COM4 (CH340) is a different device.
- Same layout as the 2.8": `radar::kSize` = 320 (full width), bottom panel 160px. Pixel pitch is about
  the same as the 2.8", so fonts/icons keep their pixel sizes. 320x320 RGB332 sprite = 100 KB in internal
  RAM (PSRAM not enabled: module type unknown); falls back to direct draw if the alloc fails.
- `TAG_CALLSIGN` (env flag): radar tags show the callsign above the type (`tagCallsign`).
  `PANEL_LIST` (env flag): the bottom panel lists aircraft closest first, one line each
  (flight | type | alt | speed | dist+compass, `kPanelLine`, `drawPanelList`), in icon colour, with a header
  row (or the selected aircraft's details above the rows), "+N not shown" last. Under PANEL_LIST
  `drawSelectedLines` uses two lines: alt/speed and distance/route right-aligned beside ident and type. Rows are tappable
  (shared `s_list_hex` map with PLANE_LIST). Shared list helpers (`ListCol`, `drawListLine`, `fitText`,
  `collectByDistance`, `compassPoint`, `flightName`) are under `PLANE_LIST || PANEL_LIST`. Not yet seen on hardware.
- Verified on hardware: display, colours, touch detection and direction. Heap with the 100 KB sprite and
  TLS: free ~61-104 KB, largest block ~37-49 KB during fetches.

## FNK0115Q port (ESP32-S3 N16R8, 5.0" IPS 800x480 RGB parallel, GT911 capacitive)
- `[env:fnk0115]` (own section like fnk0104s; `qio_opi` memory type, 16 MB flash, `BOARD_HAS_PSRAM`).
  Freenove repo `Freenove/Freenove_ESP32_S3_Display_FNK0115` covers four variants (B 4.3" TN, L 4.3" IPS,
  N 5.0" TN with XPT2046, Q 5.0" IPS with GT911); this env is the Q. USB is a CH340C (COM port), not native.
- Panel: `RGB_PANEL` selects a separate `LGFX` class in `lgfx_config.hpp` (`lgfx::Bus_RGB` + `Panel_RGB`,
  framebuffer in PSRAM). Pins from Freenove's TFT_Display.h: DE 40, VSYNC 41, HSYNC 39, PCLK 42,
  R 45/48/47/21/14, G 5/6/7/15/16/4, B 8/3/46/9/1 (Bus_RGB d0-d4 = B, d5-d10 = G, d11-d15 = R);
  13 MHz, h/v porch 4/4/8, polarity 0, pclk_active_neg. Backlight GPIO 2 (PWM). BOOT = GPIO 0.
- Touch: `lgfx::Touch_GT911`, SDA 19, SCL 20, INT 18, RST 38, addr 0x5D (driver also tries 0x14). Raw
  coordinates = screen coordinates (Freenove's sketches map them 1:1), so no axis flip.
- Landscape layout: `radar::kSize` defaults to the screen's short side (480). `radar_display.cpp` has
  panel geometry (`kSidePanel` = width > height, `kPanelX/Top/W/H/R/Cx`, `drawPanelDivider`): on the FNK0115 the
  panel is the 319 px strip right of the radar, full height; on portrait boards it's the same as before
  (below the radar). `drawListLine` column x values are now relative to `kPanelX`, so `kPanelLine`
  (sized for 320 px) fits as is. Touch: x >= `kPanelX` goes to the list; radar hit tests also reject
  x >= `kSize`. Frame sprite (480x480 RGB332, 225 KB) is created in PSRAM (`setPsram(psramFound())`).
- Watch face / calculator / matrix rain / settings size themselves from `tft.width/height`; the watch face
  uses its compact strips (480 tall) centred on the 800 width. Runs on hardware: display, colours,
  radar, list and watch face all OK (first photos), but text was far too small.
- `UI_SCALE_PCT=150` (fnk0115 env): `radar::kUiScale` / `radar::ui(px)` in `radar_theme.h`.
  `displayFontSetSmoothSize` multiplies every smooth-font size by it (radar labels, list, status
  screens; watch face / calculator / LVGL don't use it). Also scaled: plane / heli icons
  (`drawPlaneIcon` scale), icon extents, ring inset, selection ring, trail / vector widths, runway width
  and clear margin, tag offset, scale button padding, legend and class-legend geometry, corner row
  spacing, wind arrow, degree ring. Since 480 = 1.5 x 320, the radar should look like the FNK0104S's.
- `PANEL_LIST_TWO_LINES` (fnk0115 env): two lines per aircraft, `kPanelLineA` flight | type | alt |
  dist | dir (icon colour), `kPanelLineB` reg | climb | speed | description (grey), header in two lines
  too. Selected-aircraft details use the 4-line form (one item per line), not the 4" two-column one.
  Column widths estimated for the list font at 1.05 (0.7 x 1.5). Seen on hardware: scaled UI and list OK.
- Pixel clock edge: LovyanGFX's `Bus_RGB` ignores `pclk_active_neg`; it sets the PCLK output edge
  (`lcd_ck_out_edge`) from `pclk_idle_high`. With it at 0, text had green/orange fringes and the
  rings broke into chevrons and gaps on hardware. Now `pclk_idle_high = 1` (= Freenove's PCLK_ACTIVE_NEG 1).
- `FRAME_16BIT` (fnk0115 env): radar frame sprite is RGB565 (450 KB, PSRAM) instead of RGB332;
  `downsampleColor` then returns the colour unchanged. Seen on hardware: clean text and rings.
- Portrait (owner's request): `DISPLAY_ROTATION=1` in the env (3 = other way up). `TFT_WIDTH/HEIGHT`
  stay the panel's 800x480 (LGFX panel + touch config); `config::kDisplayWidth/Height` swap to
  480x800 for odd rotations; `displayInit` calls `setRotation(DISPLAY_ROTATION)`; touch follows the
  rotation inside LovyanGFX. Layout = the FNK0104S one at 1.5x: radar 480 on top, one-line
  `PANEL_LIST` below (`kPanelLine` x / pad now go through `radar::ui()`, identity on other boards).
  `PANEL_LIST_TWO_LINES` dropped from the env (still there for landscape). Not yet seen on hardware;
  the rotated framebuffer write may make the push slower.

## CYD2USB (ESP32-2432S028 with ST7789, XPT2046 resistive)
- `[env:cyd2usb]` (extends `cyd_base`): the "Cheap Yellow Display" version with micro-USB + USB-C, whose
  2.8" 240x320 panel is an ST7789 (`cyd_28r` is the original ILI9341 one). Same pins as `cyd_28r`
  (LCD HSPI 13/12/14, CS 15, DC 2, BL 21; touch on its own SPI: CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36).
- `lgfx::Panel_ST7789`, no `TFT_INVERT` (with it every colour was inverted on hardware: black -> light blue, green -> magenta); touch shares the `cyd_28r` XPT2046 block, but with both axes reversed (x 3900..300, y 200..3700; the
  `cyd_28r` limits were mirrored left-right and top-bottom on hardware). `BOARD_CYD2USB` turns
  off the software R/B swap and uses malloc for LVGL. Heap savers as FNK0103N (`TRAIL_LENGTH=6`,
  `MAX_AIRCRAFT=20`, `FETCH_CORE=0`), `TOUCH_DEBUG` on. Runs on hardware; not yet checked: colours after
  the inversion fix, touch after the axis flip).

## Screensaver (all boards)
- `src/ui/screensaver.cpp`: matrix rain (from a pasted blocking sketch, made non-blocking:
  `begin/tick/end`, ticked from `loop()` every `kScreensaverFrameMs` (50), straight to `tft`).
  Reworked to look like the film: one stream per 7x12 column, halfwidth katakana (U+FF66-FF9D) + digits
  from `fonts::lgfxJapanGothic_12` (+~110 KB flash), 8-shade fade (pale head -> dark green), trails
  rows/3..rows, a few random glyph changes inside trails per frame. A malloc'd glyph grid (cols*rows bytes,
  ~1.8 KB) lets trails fade without changing glyphs; freed on exit. Not yet seen on hardware.
  Starts after `getScreensaverTimeoutMs()` with no touch/BOOT press, only while the radar is up and settings
  closed; backlight drops to `kScreensaverBrightness` (80; 40 hid the dark greens). Fetches keep running, radar redraws are skipped.
  Tapping a lower corner of the radar outside the outer ring (80 px box, `lowerCornerTapped`) starts one
  now: lower-left (altitude legend) = calculator, lower-right (class legend) = matrix rain, upper-right (title / clock) = watch face (swapped with the calculator)
  (`screensaver::begin(kStyleWatch / kStyleMatrix)`). Replaced the old title-corner tap. Not yet seen on hardware.
  The waking touch/press is swallowed, then `radarDisplayDraw()`. Wi-Fi state changes end it.
  Settings row "Screensaver": Off/1/5/10/30/60 min, pref `"ss_min"`, default `kScreensaverDefaultMin` = 1
  (for testing; raise to 30 later).
- Watch face (`src/ui/watch_face.cpp`): moonwatch-style chronograph, no brand names. Bezel
  (was a tachymeter 500..60, then minutes; now a 24-hour "Pepsi" GMT bezel, see below), black dial with 1/5 s track, batons (short at
  3/6/9), running seconds at 9, 30-min counter at 3, 12-h counter at 6. The chrono is stopped at boot and
  `begin()` no longer touches it (owner's request): it keeps running, or stays stopped, while the face is closed,
  like the calculator keeps its value. Hands step once a second (`updateAngles`, `beats` = 1), except the stopped chrono sweep hand, which shows the exact elapsed time; the digital timer is checked every loop (~10 ms), redrawn up to 20x a second; adsb.fi fetches stay paused while it runs. Drawn through one `tft.width()` x 16 px
  16-bit band sprite (~8-10 KB; a full-face buffer doesn't fit beside the radar sprite on the no-PSRAM
  boards); each frame redraws only the bands an old or new hand position covers. Face = screen width,
  centred vertically. Off-white "panda" subdials (`kColorSubdial`) with black print and hands. Hands white (red / blue tips removed); hour / minute later turned gold, see below.
  Verified on the FNK0104S. Two fixes found on hardware: `fillSmoothCircle` on a disc much bigger than the
  16-row sprite painted whole bands in its colour, so fills use `disc()` (row spans + blended edges over
  a known bg colour); and `pushSprite` returns while DMA still reads the buffer, so `tft.waitDMA()` comes
  before reusing it (without it most bands were garbled).
- "Pepsi" GMT bezel: 24-hour scale (even hours numbered, odd hours dots, triangle at 24), coloured
  per pixel (`pepsiRing`): red `kColorBezelDay` from today's sunrise to sunset, blue `kColorBezelNight`
  for night. `updateSunTimes` (NOAA approximate solar equations) uses `services::location` lat/lon,
  converts to local clock hours through `localtime_r`; recomputed at `begin()` and when the local day
  changes; serial prints `watch: sunrise ... sunset ...`; 6/18 fallback before the clock is set; polar
  night / midnight sun = all blue / all red. New `kGmt` hand (first in `HandId`, drawn under the others):
  thin white, red arrowhead at d*0.97, local 24-hour time. Hour / minute hands are diver-style (`diverHand`): dark edge, bright gold frame
  (`kColorTimeHand` 255,200,40) and lume fill (`kColorLume`) from 22% out, gold tail. Hour hand (1.3x
  width): baton, ringed circle near the tip (radius 13% of length) with three gold spokes, short point
  beyond. Minute hand: baton with a 3x-width pointed tip. Small AA discs via `spot` (blends over
  `readPixel`), no arrowheads (filled, open V, hollow swept were tried and removed). Central sweep hand (`kSecond`) is now the time-of-day
  seconds; the chrono seconds moved to the subdial at 9 (`kChronoSec`, was running seconds). Checked off-board: Calgary 2026-11-04 = 7:32 / 17:07 MST. Not yet seen on hardware.
- Watch face buttons under the face (below the last band, `layoutButtons`): START/STOP (green/red),
  RESET (flyback: zero, keeps running), EXIT (back to the radar). Chrono persists across opens (see above);
  state `s_chrono_running` / `s_chrono_accum_ms`. While the watch face is up, touches don't wake it:
  main sends the release to `watch_face::handleTap`, which returns true for Exit. BOOT still wakes it;
  the matrix rain still wakes on any touch. Not yet seen on hardware.
- Watch face strips (`setupGeometry`): the face is no longer centred. Top to bottom: time of day
  "2:05:33 PM" + timezone abbreviation (`strftime %Z`, grey, smaller; digits in fixed cells via `drawCells`, hour always two cells, AM/PM + zone redrawn only when they change), the face bands, the interval
  timer (chrono: "0:12:34.5" to tenths while running, redrawn when the tenth changes; "0:12:34.567" to the ms when stopped (owner's request; was hundredths every 50 ms); start/stop/reset are stamped at touch-down (`handleTap(x, y, press_ms)`), and a tap anywhere on the watch circle (`g.r`) is Start/Stop; white running / amber stopped; fixed-width cells (digits as wide as "8") so the decimal never moves, only changed cells redrawn, with bg colour + padding so it
  doesn't flicker), then the buttons. Spare height goes to buttons (52), timer (40), time (36) in that
  order; when all three full-size strips don't fit (240x320: FNK0104B, CYD2USB) it uses `kCompactStrips` (buttons 34 / timer 28 / time 26, 12pt + 9pt fonts, Font2 button labels) and shrinks the face (r 119 -> 110) until they fit. Both strips redraw only when their text changes. Not yet
  seen on hardware.
- adsb.fi fetches are paused while any screensaver runs (smoother animation); waking sets `g_fetch_now`.
  Trails start over when the previous good update is older than `kTrailMaxGapMs` (30 s), so after the
  screensaver or an outage old and new positions aren't joined by a long line. Not yet seen on hardware.
- Owner's preference: while testing, build only the env being tested (currently `fnk0104s`), not all four.
- `screensaver.cpp` picks the style: settings row "Saver Style" Matrix / Watch / Both (alternates on each
  start), pref `"ss_style"`, default `kScreensaverDefaultStyle` = 0 (matrix; owner's choice for the timeout). If one can't allocate, the
  other runs. `create_cycle_row` in `settings_menu.cpp` is the shared title + `< value >` row.

## Calculator (all boards)
- `src/ui/calculator.cpp`: tap the lower-left corner of the radar outside the outer ring (altitude legend; was the upper-right title /
  clock corner, `radarDisplayIsAltitudeLegendTapped`, 80 px box, shares `cornerTapped` with the legend corners) to open it.
  Draws straight to `tft`, no sprite (~0 heap; ~7 KB flash). Layout from `tft.width/height`: 10-digit
  seven-segment LCD (+ sign cell, decimal points, unlit segments drawn in the glass colour (blank; they were faint "ghosts" before) so it never clears/flickers;
  annunciators M / E / pending op / HEX, only lit ones drawn), then a 4x6 key grid: EXIT STO RCL M+ / CLR HEX <-(back arrow) ÷ /
  7 8 9 × / 4 5 6 − / 1 2 3 + / 0 +/- . = (`Key::x100`/`w100` in hundredths of a column, so keys can
  be uneven). HEX (labelled DEC in hex mode, same spot) swaps rows 2-5 for a hex pad of five 0.8-key
  columns: C D E F NOT / 8 9 A B − / 4 5 6 7 + / 0 1 2 3 = (`Key::pad` kBoth/kDecOnly/kHexOnly; ÷ slot
  empty) and converts the display: whole part only, sign-magnitude ("-FF"), > 0xFFFFFFFFFF = Error.
  Hex results are truncated in `setResult`. NOT inverts within the smallest of 16/32/64 bits that holds
  the number (64-bit results never fit 10 digits: Error); negative x gives -x-1. Ops pending from
  decimal carry across the switch. Hex-mode digits are drawn with FreeSansBold18pt7b stretched to the
  digit height, one per cell (`drawHexText`), so B and D are upper case; no ghost segments there, the
  digit area is cleared each redraw (and once on the way back to segments, `s_shown_hex`). Special key ids are lower case so 'A'-'F' are digits.
  Immediate execution, repeated "=" repeats the last op, CLR = clear entry then
  all clear. Thousands commas: a tail under the decimal-point dot (blank when unlit). Values >= 1e10
  or < 1e-6 show as mantissa + "E" + 2-digit exponent at the right (7 mantissa digits, 6 with a negative
  exponent); |exp| > 99 or /0 = "Error" until CLR. State and memory survive closing.
  Op icons are drawn with fills (no AA: the FNK0104S panel has no MISO). EXIT or BOOT closes it.
  While open: adsb.fi fetches and radar redraws stop (`radarCovered()` in `main.cpp`), no screensaver;
  closing forces a fetch. Built for fnk0104s only; not yet seen on hardware.

## Airport weather (all boards)
- METAR for the "big" airport (the one `runway::drawAirports` returns): `drawStaticGrid` calls
  `services::adsb::requestWeather(ident)` every frame; `fetchUpdate()` calls `fetchWeather` first (same
  `s_client`, raw HTTP/1.0 to `aviationweather.gov/api/data/metar?ids=X&format=json`, filter temp/wdir/wspd)
  when the ident changes, then every 10 min (1 min after a failure). 204 = no METAR (no display).
  `s_weather` / `s_weather_want` are shared with the fetch core under `s_weather_mux`.
  Serial: `metar: CYYC HTTP 200`, `metar: CYYC 9.0 C, wind 150 deg 7 kt`.
- Upper-left corner (`drawCornerInfo`, owner's layout): top line = airport ident (runway colour) then
  its lat/lon (radar centre lat/lon when no airport); next line (`drawWeatherRow`) = temperature (C, or F
  when the range units are miles; degree sign is a drawn ring) and wind speed in knots ("12kt",
  "VRB 4kt", "calm"). Wind arrow (`drawWindArrow`, slender: shaft 0.05 / head 0.18 x 0.34 of its length,
  white, pointing downwind) in an AA ring (`kWindRingR` 15 x UI scale, 1.5 px wide) at the left edge of the
  screen just under the weather row (centre ~(16, 45) on the 320 radar), used when that's
  `kWindRingClearPx` (ui 4) clear of the outer ring; otherwise (240 px radars, stacked rows) the ring
  goes off the lower right of the text, nudged clear of the outer ring, drawn with `fillArc`. No ring or
  arrow when calm or VRB. The range button sits right under the ring, its box overlapping the ring's
  bottom by ~2 px (owner: "touch or overlap slightly"); without a ring it's `kScaleBelowCoordsPx` = ui(8)
  under the last text line. A 4-char label ("25mi") may touch the outer ring at its lower right.
  Earlier spots: the 45 deg line between the radar and the corner, between / after temp and speed. If the top line would reach
  the "N" (240 px radars) the ident, lat and lon get a line each. History: a big faint arrow over the
  airport, a 26 px arrow below the ident row, rings between / after temp and speed. Not yet seen on hardware.

## 25 mi range (all boards)
- 5th preset in `kRangePresets`: ring 3 = 40.2336 km (labels "25mi" / "40km"), outer ~33 mi, fetch ~32 nm.
  Added at the end so saved `rangeIdx` values keep their meaning.
- `services::adsb::hiddenAircraftCount()`: aircraft that passed the filters but were over the Max Planes
  cap (the farthest ones). `drawHiddenCount` shows "+N not shown (max planes)" in red at the top of the
  bottom panel while nothing is selected (not in the FNK0103N `PLANE_LIST` layout). Serial prints
  `adsb: N aircraft, M over cap`. Not yet verified on hardware.

## adsb.fi rate limit (all boards)
- adsb.fi allows ~1 request/s per IP; three boards at the old 3 s interval hit it (shown as "Connection Lost").
- Fetches are aligned to the sweep (`fetchIntervalMs()` in `main.cpp`): one fetch per sweep turn, fired
  when `millis()` crosses a period boundary (beam at north). No 6 s floor any more (owner's call: sweep
  speed setting goes down to 1 s, and staying under adsb.fi's limit is up to the user).
  Sweep off: plain `kAdsbFetchIntervalMs` (6 s). Forced fetches (`g_fetch_now`: startup, range tap, plane tap)
  wait `kAdsbMinFetchGapMs` (2 s) after the last one; a scheduled one is skipped if it comes within
  min(2 s, half a turn) of the last. A fetch still running when the next is due is skipped (`triggerFetch`).
- `fetchUpdate` now reads the status line (`readHttpHeaders`); HTTP 429 sets `lastFetchRateLimited()`.
  Main then skips the next period and doesn't count it as a failure; stale only after `kAdsbStaleAfterMs` (60 s)
  with no good fetch. Serial: `adsb: HTTP 429 (rate limited by adsb.fi)`. Not yet verified on hardware.
- `radarDisplaySetStale` only records the flag now (it used to redraw over the screensaver); callers redraw.

## Bottom footer removed
- The clock / "N aircraft" / version line at the bottom of the screen (`drawClock`, `drawAircraftCount`,
  `drawAppVersion`) is gone. The FNK0103N `PLANE_LIST` side strip still has its own clock/count/version.
- List Dist column (`kPanelLine`, `kListLine2`): distance right-aligned at x 294 and the compass point in
  its own left-aligned column at 298 (was one right-aligned "12.3 mi SW" string, so N vs NW shifted the
  numbers). Widths from the vlw at 0.7: "NW" ~19 px, "12.3 mi" ~36 px.
- FNK0104S `PANEL_LIST` row is now flight | type | alt | climb | speed | dist | dir: Alt moved left next to
  Type, new climb column (`vrate_fpm`, "+1200" in fpm or "+6.1" m/s; header is the unit; "-" on the ground
  or unknown altitude), Speed right edge 238 -> 244. Column x/pad in `kPanelLine`.
- `PANEL_LIST` rows now run to the bottom of the screen (`list_bottom` in `drawPanelList`), except the last
  `kBannerH` px while "Connection Lost" is up.
- `radarDisplayDraw` clears the screen with `downsampleColor(kColorBackground)` when the RGB332 sprite is in
  use (it used the full-colour background, so strips nothing redrew, like the old footer row, were a shade off).

## Clock reads (all boards)
- Never use `getLocalTime(&tm, 0)`: if `millis()` ticks between its start and its loop check it returns
  false without reading the clock (the corner time blinked to `--:--`). Use `utils::time::localTimeNow`
  (`include/utils_time.h`, `time()` + `localtime_r`). Not yet seen on hardware.

## Not yet verified on hardware
- Range-label tap (just re-enabled) and the airport label placement.
- Route lookup: watch serial for `route: ... HTTP 200` / `route: X -> Y` and the `heap[...]` lines.
