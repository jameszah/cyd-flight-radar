/**
 * Plane Radar — WiFi setup, then radar UI on the round GC9A01 display.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "services/wifi_manager.h"
#include "services/button_manager.h"
#include "secrets.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/screensaver.h"
#include "ui/status_screens.h"
#include "ui/settings_menu.h"

namespace {

bool g_radar_visible = false;
bool g_screen_on = true;
unsigned long g_last_adsb_fetch_ms = 0;
bool g_fetch_now = true;                  // fetch as soon as allowed (startup, range change, tap)
unsigned long g_fetch_backoff_until_ms = 0;  // after an HTTP 429: no fetches before this
unsigned long g_last_fetch_ok_ms = 0;
unsigned long g_last_activity_ms = 0;  // last touch / BOOT press, for the screensaver

// Fetch period: one sweep turn, so new positions land just after the beam passes north.
// Fast sweeps mean more requests to adsb.fi (~1 req/s per IP); that's up to the user.
// Sweep off: kAdsbFetchIntervalMs.
unsigned long fetchIntervalMs() {
  if (!ui::settings::isRadarSweepEnabled()) {
    return config::kAdsbFetchIntervalMs;
  }
  const unsigned long turn_ms = static_cast<unsigned long>(ui::settings::getSweepRotationSpeedMs());
  if (turn_ms == 0) {
    return config::kAdsbFetchIntervalMs;
  }
  return turn_ms;
}

WifiManager wifiManager(WIFI_SSID, WIFI_PASSWORD);
ButtonManager bootButton(0);
WifiState g_last_wifi_state = WIFI_STATE_DISCONNECTED;
String g_connecting_ssid;  // SSID shown on the "Connecting" screen

void showRadarIfConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    g_radar_visible = false;
    return;
  }
  ui::radarDisplayDraw();
  g_radar_visible = true;
}

void updateBrightness() {
  if (!g_screen_on) return;

  static uint8_t s_current_brightness = 255;
  uint8_t target_brightness = 255;

  if (ui::screensaver::isActive()) {
    target_brightness = config::kScreensaverBrightness;
  } else if (ui::settings::isAutoDimmingEnabled()) {
    static unsigned long s_last_time_check = 0;
    static struct tm s_timeinfo;
    static bool s_time_valid = false;

    if (millis() - s_last_time_check > 5000) {
      s_last_time_check = millis();
      s_time_valid = getLocalTime(&s_timeinfo, 0);
    }

    if (s_time_valid) {
      if (s_timeinfo.tm_hour >= 20 || s_timeinfo.tm_hour < 6) {
        target_brightness = 100; // Dim level
      }
    }
  }

  if (s_current_brightness != target_brightness) {
    s_current_brightness = target_brightness;
    tft.setBrightness(s_current_brightness);
  }
}

void onRangeTap() {
  ui::radar::rangeNext();
  char range_label[12];
  ui::radar::formatCurrentRing3Label(range_label, sizeof(range_label));
  Serial.printf("Range: %s (outer ~%.0f km)\n", range_label,
                ui::radar::rangeCurrent().outer_km);

  if (g_radar_visible && wifiManager.getState() == WIFI_STATE_CONNECTED) {
    if (!ui::settings::isVisible()) {
      ui::radarDisplayDraw();
    }
    g_fetch_now = true; // fetch with the new radius as soon as allowed
  }
}

void wakeFromScreensaver() {
  ui::screensaver::end();
  if (g_radar_visible) {
    ui::radarDisplayDraw();
  }
}

void handleInput() {
  ButtonAction action = bootButton.update(millis());
  if (action != ButtonAction::NONE) {
    g_last_activity_ms = millis();
    if (ui::screensaver::isActive()) {
      wakeFromScreensaver();  // the press only wakes the screen
      action = ButtonAction::NONE;
    }
  }
  if (action == ButtonAction::SINGLE_PRESS) {
    if (!g_screen_on) {
      g_screen_on = true;
      tft.setBrightness(255);
    } else {
      onRangeTap();
    }
  } else if (action == ButtonAction::LONG_PRESS) {
    if (g_screen_on) {
      ui::settings::show();
    }
  }

  if (!g_screen_on) {
    return; // Ignore touch input while screen is off
  }

  static bool s_was_touched = false;
  static unsigned long s_touch_start_ms = 0;
  static uint16_t s_tap_x = 0;
  static uint16_t s_tap_y = 0;
  static bool s_ignore_until_release = false;  // the touch that woke the screensaver
  uint16_t tx = 0, ty = 0;
  bool is_touched = tft.getTouch(&tx, &ty);
#ifdef TOUCH_DEBUG
  if (is_touched && !s_was_touched) {
    lgfx::touch_point_t raw;
    tft.getTouchRaw(&raw, 1);
    Serial.printf("touch: x=%u y=%u (raw %d,%d)\n", tx, ty, raw.x, raw.y);
  }
#endif
  
  if (is_touched) {
    g_last_activity_ms = millis();
    if (ui::screensaver::isActive()) {
      wakeFromScreensaver();
      s_ignore_until_release = true;
    }
    if (!s_was_touched) {
      s_was_touched = true;
      s_touch_start_ms = millis();
    }
    // Constantly update tap coordinates to avoid garbage data from the first touch frame
    s_tap_x = tx;
    s_tap_y = ty;
  } else if (!is_touched && s_was_touched) {
    s_was_touched = false;
    unsigned long duration = millis() - s_touch_start_ms;
    if (s_ignore_until_release) {
      s_ignore_until_release = false;
    } else if (ui::settings::isVisible()) {
       // LVGL handles touch when visible
    } else if (duration > 40 && duration < 600) {
      // Tap the range scale label (bottom right of the radar) to cycle ranges.
      if (ui::radarDisplayIsScaleTapped(s_tap_x, s_tap_y)) {
        onRangeTap();
      } else if (g_radar_visible && ui::radarDisplayIsTitleTapped(s_tap_x, s_tap_y)) {
        ui::screensaver::begin();  // the title corner starts the screensaver now
      } else if (ui::radarDisplayHandleTouch(s_tap_x, s_tap_y)) {
        ui::radarDisplayRefreshAircraft();
        g_fetch_now = true;
      }
    } else if (duration >= 600) {
      ui::settings::show();
    }
  }
}

TaskHandle_t g_fetch_task = nullptr;
volatile bool g_fetch_finished = false;
volatile bool g_fetch_success = false;

void adsbFetchTask(void* pvParameters) {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // Wait for triggerFetch() to signal

    const float fetch_km = ui::radar::fetchRadiusKm();
    bool success = services::adsb::fetchUpdate(services::location::lat(),
                                services::location::lon(), fetch_km);
    
    g_fetch_success = success;
    g_fetch_finished = true;
  }
}

void triggerFetch() {
  if (g_fetch_task == nullptr) {
    xTaskCreatePinnedToCore(
        adsbFetchTask,      // Function to implement the task
        "ADSB_Fetch",       // Name of the task
        8192,               // Stack size in words (bytes on ESP32) - Reduced from 16KB to 8KB to save heap
        NULL,               // Task input parameter
        1,                  // Priority of the task
        &g_fetch_task,      // Task handle
#ifdef FETCH_CORE
        FETCH_CORE);        // Core where the task should run (0 = network, 1 = UI)
#else
        1);                 // Core where the task should run (0 = network, 1 = UI)
#endif
  }
  xTaskNotifyGive(g_fetch_task);
}

}  // namespace

#include <nvs_flash.h>

void setup() {
  Serial.begin(115200);
  delay(500);
  
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    err = nvs_flash_init();
  }

  bootButton.begin();
  displayInit();
  
  services::location::init();
  ui::radar::rangeInit();

  ui::settings::setup();
  ui::updateThemeColors();

  services::adsb::init();

  wifiManager.begin();
  if (wifiManager.getState() == WIFI_STATE_CONNECTING || wifiManager.getState() == WIFI_STATE_DISCONNECTED) {
    g_connecting_ssid = wifiManager.getSSID();
    statusScreenConnectingBegin(g_connecting_ssid.c_str());
  }
}

void loop() {
  handleInput();
  wifiManager.update();
  
  bool was_settings_visible = ui::settings::isVisible();
  ui::settings::loop();
  if (was_settings_visible && !ui::settings::isVisible()) {
    if (g_radar_visible) {
      ui::radarDisplayDraw();
    }
  }
  
  updateBrightness();

  WifiState current_state = wifiManager.getState();
  
  if (ui::settings::isTimezoneChanged()) {
    ui::settings::clearTimezoneChanged();
    configTzTime(ui::settings::getTimezoneStr(), config::kNtpServer);
  }


  if (current_state != g_last_wifi_state) {
    ui::screensaver::end();  // let the status screens / radar redraw
    g_last_activity_ms = millis();
    if (current_state == WIFI_STATE_CONNECTED) {
      configTzTime(ui::settings::getTimezoneStr(), config::kNtpServer);
      showRadarIfConnected();
    } else if (current_state == WIFI_STATE_AP_MODE) {
      // In AP Mode
      if (g_radar_visible) {
        g_radar_visible = false;
      }
      statusScreenPortal();
    } else if (current_state == WIFI_STATE_CONNECTING || current_state == WIFI_STATE_DISCONNECTED) {
      if (g_radar_visible) {
        g_radar_visible = false;
      }
      g_connecting_ssid = wifiManager.getSSID();
      statusScreenConnectingBegin(g_connecting_ssid.c_str());
    }
    g_last_wifi_state = current_state;
  }

  if (current_state == WIFI_STATE_CONNECTING) {
    if (wifiManager.getSSID() != g_connecting_ssid) {  // failed over to the other network
      g_connecting_ssid = wifiManager.getSSID();
      statusScreenConnectingBegin(g_connecting_ssid.c_str());
    }
    statusScreenConnectingTick();
  } else if (current_state == WIFI_STATE_CONNECTED) {
    if (!g_radar_visible) {
      showRadarIfConnected();
    } else {
      const unsigned long ss_timeout_ms = ui::settings::getScreensaverTimeoutMs();
      if (ss_timeout_ms > 0 && !ui::screensaver::isActive() && !ui::settings::isVisible() &&
          millis() - g_last_activity_ms >= ss_timeout_ms) {
        ui::screensaver::begin();
      }

      if (ui::screensaver::isActive()) {
        ui::screensaver::tick();
      } else if (!ui::settings::isVisible() && g_screen_on) {
        static unsigned long s_last_sweep_ms = 0;
        unsigned long interval = ui::settings::isRadarSweepEnabled() ? 50 : 1000;
        if (millis() - s_last_sweep_ms >= interval) {
          s_last_sweep_ms = millis();
          ui::radarDisplayUpdateAnimation();
        }
      }
      
      // Scheduled fetch each time millis() crosses a period boundary: the sweep angle is
      // millis() % turn, so that's when the beam passes north. A forced fetch (g_fetch_now)
      // waits kAdsbMinFetchGapMs after the last one, and a scheduled one that comes that soon
      // after a forced one is skipped (half a period for sweeps under 4 s, so 1-3 s sweeps
      // still fetch every turn).
      {
        const unsigned long now = millis();
        const unsigned long since_last = now - g_last_adsb_fetch_ms;
        static unsigned long s_last_period = ~0UL;
        static bool s_fetch_due = false;
        const unsigned long interval = fetchIntervalMs();
        const unsigned long period = now / interval;
        const unsigned long scheduled_gap = min(config::kAdsbMinFetchGapMs, interval / 2);
        const bool backing_off = static_cast<long>(now - g_fetch_backoff_until_ms) < 0;
        if (period != s_last_period) {
          s_last_period = period;
          if (since_last >= scheduled_gap && !backing_off) {
            s_fetch_due = true;
          }
        }
        if (g_fetch_now && since_last >= config::kAdsbMinFetchGapMs) {
          s_fetch_due = true;
        }
        if (s_fetch_due && !backing_off) {
          s_fetch_due = false;
          g_fetch_now = false;
          g_last_adsb_fetch_ms = now;
          triggerFetch();
        }
      }

      if (g_fetch_finished) {
        g_fetch_finished = false;
        static int consecutive_failures = 0;
        if (g_fetch_success) {
          consecutive_failures = 0;
          g_last_fetch_ok_ms = millis();
          ui::radarDisplaySetStale(false);
          if (!ui::settings::isVisible() && !ui::screensaver::isActive()) {
            ui::radarDisplayRefreshAircraft();
          }
        } else if (services::adsb::lastFetchRateLimited()) {
          // Not a connection problem: skip the next period, and only call it stale if
          // nothing has come through for a while.
          g_fetch_backoff_until_ms = millis() + fetchIntervalMs();
          if (millis() - g_last_fetch_ok_ms >= config::kAdsbStaleAfterMs) {
            ui::radarDisplaySetStale(true);
            if (!ui::settings::isVisible() && !ui::screensaver::isActive()) {
              ui::radarDisplayRefreshAircraft();
            }
          }
        } else {
          consecutive_failures++;
          if (consecutive_failures >= 3) {
            ui::radarDisplaySetStale(true);
            if (!ui::settings::isVisible() && !ui::screensaver::isActive()) {
              ui::radarDisplayRefreshAircraft();
            }
          }
        }
      }
    }
  }

  delay(10);
}
