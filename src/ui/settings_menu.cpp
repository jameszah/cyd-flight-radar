#include "ui/settings_menu.h"
#include <Arduino.h>
#include <lvgl.h>
#include "hardware/display.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "catppuccin.h"
#include "config.h"
#include <Preferences.h>
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "secrets.h"
#include <WiFi.h>

extern LGFX tft;

namespace ui::settings {

static lv_color_t get_lv_color(uint32_t hex) {
    uint8_t r = (hex >> 16) & 0xFF;
    uint8_t g = (hex >> 8) & 0xFF;
    uint8_t b = hex & 0xFF;
    if (config::kDisplayRgbOrder) {
        return lv_color_make(b, g, r);
    }
    return lv_color_make(r, g, b);
}

static bool s_visible = false;
static bool s_pending_hide = false;
static bool s_show_airports = true;
static bool s_show_medium_airports = false;
static bool s_show_ground_aircraft = false;
static bool s_show_radar_sweep = true;
static bool s_auto_dimming = false;
static int s_max_altitude = 50000;
static int s_max_aircraft = 40;
static int s_sweep_speed = 6000;
static Preferences s_prefs;
static int s_theme_flavor = CATPPUCCIN_MOCHA;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t* buf1 = nullptr;
static lv_obj_t* settings_screen = nullptr;
static uint32_t last_tick_millis = 0;
static unsigned long s_show_ms = 0;

static lv_obj_t* wifi_info_dialog = nullptr;

static void close_wifi_info_cb(lv_event_t * e) {
    if (wifi_info_dialog != nullptr) {
        lv_obj_del(wifi_info_dialog);
        wifi_info_dialog = nullptr;
    }
}

static void wifi_icon_click_cb(lv_event_t * e) {
    if (WiFi.status() != WL_CONNECTED && WiFi.getMode() != WIFI_AP) return;
    if (wifi_info_dialog != nullptr) return;

    bool isLargeScreen = (lv_disp_get_hor_res(NULL) >= 480 || lv_disp_get_ver_res(NULL) >= 480);

    wifi_info_dialog = lv_obj_create(lv_scr_act());
    lv_obj_set_size(wifi_info_dialog, lv_pct(85), lv_pct(85));
    lv_obj_align(wifi_info_dialog, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(wifi_info_dialog, get_lv_color(getCatppuccinFlavor(s_theme_flavor).mantle), 0);
    lv_obj_set_style_border_color(wifi_info_dialog, get_lv_color(getCatppuccinFlavor(s_theme_flavor).overlay), 0);
    lv_obj_set_style_border_width(wifi_info_dialog, 2, 0);
    lv_obj_set_style_radius(wifi_info_dialog, 10, 0);
    lv_obj_clear_flag(wifi_info_dialog, LV_OBJ_FLAG_SCROLLABLE);

    // Title label
    lv_obj_t * lbl_title = lv_label_create(wifi_info_dialog);
    lv_label_set_text(lbl_title, "WiFi Info");
    lv_obj_set_style_text_color(lbl_title, get_lv_color(getCatppuccinFlavor(s_theme_flavor).text), 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, isLargeScreen ? 10 : 5);

    // Status label
    lv_obj_t * lbl_status = lv_label_create(wifi_info_dialog);
    if (WiFi.getMode() == WIFI_AP) {
        lv_label_set_text(lbl_status, "AP Mode Active");
        lv_obj_set_style_text_color(lbl_status, get_lv_color(getCatppuccinFlavor(s_theme_flavor).mauve), 0);
    } else {
        lv_label_set_text(lbl_status, "Connected");
        lv_obj_set_style_text_color(lbl_status, get_lv_color(getCatppuccinFlavor(s_theme_flavor).green), 0);
    }
    lv_obj_align(lbl_status, LV_ALIGN_TOP_MID, 0, isLargeScreen ? 40 : 25);

    // Info details
    lv_obj_t * lbl_info = lv_label_create(wifi_info_dialog);
    char infoBuf[256];
    if (WiFi.getMode() == WIFI_AP) {
        snprintf(infoBuf, sizeof(infoBuf), 
                 "SSID: %s\nIP: %s\nMAC: %s", 
                 WiFi.softAPSSID().c_str(), 
                 WiFi.softAPIP().toString().c_str(), 
                 WiFi.softAPmacAddress().c_str());
    } else {
        snprintf(infoBuf, sizeof(infoBuf), 
                 "SSID: %s\nIP: %s\nMAC: %s\nRSSI: %d dBm", 
                 WiFi.SSID().c_str(), 
                 WiFi.localIP().toString().c_str(), 
                 WiFi.macAddress().c_str(), 
                 WiFi.RSSI());
    }
    lv_label_set_text(lbl_info, infoBuf);
    lv_obj_set_style_text_align(lbl_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(lbl_info, get_lv_color(getCatppuccinFlavor(s_theme_flavor).text), 0);
    lv_obj_align(lbl_info, LV_ALIGN_CENTER, 0, isLargeScreen ? 10 : 15);

    // Close Button
    lv_obj_t * btn_close = lv_btn_create(wifi_info_dialog);
    lv_obj_set_size(btn_close, isLargeScreen ? 120 : 80, isLargeScreen ? 40 : 30);
    lv_obj_align(btn_close, LV_ALIGN_BOTTOM_MID, 0, isLargeScreen ? -15 : -5);
    lv_obj_set_style_bg_color(btn_close, get_lv_color(getCatppuccinFlavor(s_theme_flavor).overlay), 0);
    lv_obj_add_event_cb(btn_close, close_wifi_info_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, "Close");
    lv_obj_set_style_text_color(lbl_close, get_lv_color(getCatppuccinFlavor(s_theme_flavor).crust), 0);
    lv_obj_align(lbl_close, LV_ALIGN_CENTER, 0, 0);
}

static void my_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushPixels((uint16_t *)&color_p->full, w * h, true);
    tft.endWrite();
    lv_disp_flush_ready(disp_drv);
}

static void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    if (!s_visible || (millis() - s_show_ms < 500)) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    uint16_t tx = 0, ty = 0;
    bool touched = tft.getTouch(&tx, &ty);
#ifdef TOUCH_DEBUG
    static bool s_was_touched = false;
    if (touched && !s_was_touched) {
        lgfx::touch_point_t raw;
        tft.getTouchRaw(&raw, 1);
        Serial.printf("settings touch: x=%u y=%u (raw %d,%d)\n", tx, ty, raw.x, raw.y);
    }
    s_was_touched = touched;
#endif
    if (touched) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = tx;
        data->point.y = ty;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

static void close_btn_event_handler(lv_event_t * e) {
    s_pending_hide = true;
}

static void airports_switch_event_cb(lv_event_t * e) {
    if (!s_visible) return;
    lv_obj_t * sw = lv_event_get_target(e);
    s_show_airports = lv_obj_has_state(sw, LV_STATE_CHECKED);
    s_prefs.putBool("airports", s_show_airports);
}


static void medium_airports_switch_event_cb(lv_event_t * e) {
    if (!s_visible) return;
    lv_obj_t * sw = lv_event_get_target(e);
    s_show_medium_airports = lv_obj_has_state(sw, LV_STATE_CHECKED);
    s_prefs.putBool("med_airports", s_show_medium_airports);
}

static void units_switch_event_cb(lv_event_t * e) {
    if (!s_visible) return;
    lv_obj_t * sw = lv_event_get_target(e);
    ui::radar::setUseMiles(!lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void ground_aircraft_switch_event_cb(lv_event_t * e) {
    if (!s_visible) return;
    lv_obj_t * sw = lv_event_get_target(e);
    s_show_ground_aircraft = lv_obj_has_state(sw, LV_STATE_CHECKED);
}

static void radar_sweep_switch_event_cb(lv_event_t * e) {
    if (!s_visible) return;
    lv_obj_t * sw = lv_event_get_target(e);
    s_show_radar_sweep = lv_obj_has_state(sw, LV_STATE_CHECKED);
}

static void auto_dimming_switch_event_cb(lv_event_t * e) {
    if (!s_visible) return;
    lv_obj_t * sw = lv_event_get_target(e);
    s_auto_dimming = lv_obj_has_state(sw, LV_STATE_CHECKED);
    s_prefs.putBool("auto_dim", s_auto_dimming);
}

static void max_altitude_slider_event_cb(lv_event_t * e) {
    if (!s_visible) return;
    lv_obj_t * slider = lv_event_get_target(e);
    s_max_altitude = lv_slider_get_value(slider);
    lv_obj_t * label = (lv_obj_t *)lv_event_get_user_data(e);
    if(label) lv_label_set_text_fmt(label, "%d ft", s_max_altitude);
}

static void max_aircraft_slider_event_cb(lv_event_t * e) {
    lv_obj_t * slider = lv_event_get_target(e);
    s_max_aircraft = lv_slider_get_value(slider);
    lv_obj_t * label = (lv_obj_t *)lv_event_get_user_data(e);
    if(label) lv_label_set_text_fmt(label, "%d", s_max_aircraft);
    s_prefs.putInt("max_ac", s_max_aircraft);
}

static void sweep_speed_slider_event_cb(lv_event_t * e) {
    lv_obj_t * slider = lv_event_get_target(e);
    s_sweep_speed = lv_slider_get_value(slider);
    lv_obj_t * label = (lv_obj_t*)lv_event_get_user_data(e);
    if(label) lv_label_set_text_fmt(label, "%d ms", s_sweep_speed);
}

static lv_obj_t* create_toggle_row(lv_obj_t * parent, const char * text, bool initial_state, lv_event_cb_t event_cb) {
    lv_obj_t * row = lv_obj_create(parent);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, LV_PCT(100), 40);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(row, get_lv_color(getCatppuccinFlavor(s_theme_flavor).mantle), 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 10, 0);

    lv_obj_t * lbl = lv_label_create(row);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, get_lv_color(getCatppuccinFlavor(s_theme_flavor).text), 0);

    lv_obj_t * sw = lv_switch_create(row);
    if (initial_state) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw, event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    return row;
}

static lv_obj_t* create_slider_row(lv_obj_t * parent, const char * text, int min_val, int max_val, int initial_val, const char* unit, lv_event_cb_t event_cb) {
    lv_obj_t * row = lv_obj_create(parent);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, LV_PCT(100), 70);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(row, get_lv_color(getCatppuccinFlavor(s_theme_flavor).mantle), 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 10, 0);

    lv_obj_t * header = lv_obj_create(row);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * lbl = lv_label_create(header);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, get_lv_color(getCatppuccinFlavor(s_theme_flavor).text), 0);
    
    lv_obj_t * val_lbl = lv_label_create(header);
    lv_label_set_text_fmt(val_lbl, "%d %s", initial_val, unit);
    lv_obj_set_style_text_color(val_lbl, get_lv_color(getCatppuccinFlavor(s_theme_flavor).blue), 0);

    lv_obj_t * slider = lv_slider_create(row);
    lv_slider_set_range(slider, min_val, max_val);
    lv_slider_set_value(slider, initial_val, LV_ANIM_OFF);
    lv_obj_set_width(slider, LV_PCT(100));
    lv_obj_add_event_cb(slider, event_cb, LV_EVENT_VALUE_CHANGED, val_lbl);
    
    return row;
}

// < / > buttons on the stepper rows. Taps for "<" landed ~10px right of the old
// 35px button, so the buttons are wider and also accept taps just outside.
static constexpr lv_coord_t kArrowBtnW = 50;
static constexpr lv_coord_t kArrowBtnH = 30;
static constexpr lv_coord_t kArrowBtnExtClickPx = 12;

struct TimezonePreset {
    const char* label;
    const char* value;
};
static const TimezonePreset tz_presets[] = {
    {"UTC", "UTC0"},
    {"London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"CET", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"EET", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"US East", "EST5EDT,M3.2.0,M11.1.0"},
    {"US Central", "CST6CDT,M3.2.0,M11.1.0"},
    {"US Mount.", "MST7MDT,M3.2.0,M11.1.0"},
    {"US Pacific", "PST8PDT,M3.2.0,M11.1.0"},
    {"US Alaska", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"US Hawaii", "HST10"},
    {"AU East", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"AU Central", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
    {"AU West", "AWST-8"}
};
static const int num_tz_presets = sizeof(tz_presets)/sizeof(tz_presets[0]);
static int s_tz_idx = 0;
static lv_obj_t * tz_val_label = nullptr;
static bool s_tz_changed = false;

static void tz_btn_event_cb(lv_event_t * e) {
    intptr_t dir = (intptr_t)lv_event_get_user_data(e);
    s_tz_idx += dir;
    if (s_tz_idx < 0) s_tz_idx = num_tz_presets - 1;
    if (s_tz_idx >= num_tz_presets) s_tz_idx = 0;
    
    s_tz_changed = true;
    // Saved as the POSIX TZ string so it survives presets being reordered.
    s_prefs.putString("tz", tz_presets[s_tz_idx].value);
    lv_label_set_text(tz_val_label, tz_presets[s_tz_idx].label);
}

static const int ss_presets_min[] = {0, 1, 5, 10, 30, 60};  // 0 = off
static const int num_ss_presets = sizeof(ss_presets_min)/sizeof(ss_presets_min[0]);
static int s_ss_idx = 1;
static lv_obj_t * ss_val_label = nullptr;

static void update_ss_label() {
    if (ss_presets_min[s_ss_idx] == 0) {
        lv_label_set_text(ss_val_label, "Off");
    } else {
        lv_label_set_text_fmt(ss_val_label, "%d min", ss_presets_min[s_ss_idx]);
    }
}

static void ss_btn_event_cb(lv_event_t * e) {
    intptr_t dir = (intptr_t)lv_event_get_user_data(e);
    s_ss_idx += dir;
    if (s_ss_idx < 0) s_ss_idx = num_ss_presets - 1;
    if (s_ss_idx >= num_ss_presets) s_ss_idx = 0;
    s_prefs.putInt("ss_min", ss_presets_min[s_ss_idx]);
    update_ss_label();
}

static lv_obj_t* create_screensaver_row(lv_obj_t * parent) {
    lv_obj_t * row = lv_obj_create(parent);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, LV_PCT(100), 75);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(row, get_lv_color(getCatppuccinFlavor(s_theme_flavor).mantle), 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 10, 0);

    lv_obj_t * lbl = lv_label_create(row);
    lv_label_set_text(lbl, "Screensaver");
    lv_obj_set_style_text_color(lbl, get_lv_color(getCatppuccinFlavor(s_theme_flavor).text), 0);
    lv_obj_set_width(lbl, LV_PCT(100));

    lv_obj_t * controls = lv_obj_create(row);
    lv_obj_set_size(controls, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(controls, 0, 0);
    lv_obj_set_style_border_width(controls, 0, 0);
    lv_obj_set_style_pad_all(controls, 0, 0);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * btn_minus = lv_btn_create(controls);
    lv_obj_set_size(btn_minus, kArrowBtnW, kArrowBtnH);
    lv_obj_set_ext_click_area(btn_minus, kArrowBtnExtClickPx);
    lv_obj_add_event_cb(btn_minus, ss_btn_event_cb, LV_EVENT_CLICKED, (void*)(intptr_t)-1);
    lv_obj_set_style_bg_color(btn_minus, get_lv_color(getCatppuccinFlavor(s_theme_flavor).overlay), 0);
    lv_obj_t * lbl_minus = lv_label_create(btn_minus);
    lv_label_set_text(lbl_minus, "<");
    lv_obj_center(lbl_minus);

    ss_val_label = lv_label_create(controls);
    update_ss_label();
    lv_obj_set_style_text_color(ss_val_label, get_lv_color(getCatppuccinFlavor(s_theme_flavor).blue), 0);
    lv_obj_set_flex_grow(ss_val_label, 1);
    lv_obj_set_style_text_align(ss_val_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t * btn_plus = lv_btn_create(controls);
    lv_obj_set_size(btn_plus, kArrowBtnW, kArrowBtnH);
    lv_obj_set_ext_click_area(btn_plus, kArrowBtnExtClickPx);
    lv_obj_add_event_cb(btn_plus, ss_btn_event_cb, LV_EVENT_CLICKED, (void*)(intptr_t)1);
    lv_obj_set_style_bg_color(btn_plus, get_lv_color(getCatppuccinFlavor(s_theme_flavor).overlay), 0);
    lv_obj_t * lbl_plus = lv_label_create(btn_plus);
    lv_label_set_text(lbl_plus, ">");
    lv_obj_center(lbl_plus);

    return row;
}

static lv_obj_t * range_val_label = nullptr;

static void update_range_label() {
    char range_label[12];
    ui::radar::formatCurrentRing3Label(range_label, sizeof(range_label));
    lv_label_set_text(range_val_label, range_label);
}

static void range_btn_event_cb(lv_event_t * e) {
    intptr_t dir = (intptr_t)lv_event_get_user_data(e);
    int new_idx = (int)ui::radar::rangeIndex() + dir;
    if (new_idx < 0) {
        new_idx = ui::radar::kRangePresetCount - 1;
    } else if (new_idx >= ui::radar::kRangePresetCount) {
        new_idx = 0;
    }
    ui::radar::setRangeIndex(new_idx);
    update_range_label();
}

static lv_obj_t* create_range_row(lv_obj_t * parent) {
    lv_obj_t * row = lv_obj_create(parent);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, LV_PCT(100), 75);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(row, get_lv_color(getCatppuccinFlavor(s_theme_flavor).mantle), 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 10, 0);

    lv_obj_t * lbl = lv_label_create(row);
    lv_label_set_text(lbl, "Radar Radius");
    lv_obj_set_style_text_color(lbl, get_lv_color(getCatppuccinFlavor(s_theme_flavor).text), 0);
    lv_obj_set_width(lbl, LV_PCT(100));

    lv_obj_t * controls = lv_obj_create(row);
    lv_obj_set_size(controls, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(controls, 0, 0);
    lv_obj_set_style_border_width(controls, 0, 0);
    lv_obj_set_style_pad_all(controls, 0, 0);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * btn_minus = lv_btn_create(controls);
    lv_obj_set_size(btn_minus, kArrowBtnW, kArrowBtnH);
    lv_obj_set_ext_click_area(btn_minus, kArrowBtnExtClickPx);
    lv_obj_add_event_cb(btn_minus, range_btn_event_cb, LV_EVENT_CLICKED, (void*)(intptr_t)-1);
    lv_obj_set_style_bg_color(btn_minus, get_lv_color(getCatppuccinFlavor(s_theme_flavor).overlay), 0);
    lv_obj_t * lbl_minus = lv_label_create(btn_minus);
    lv_label_set_text(lbl_minus, "<");
    lv_obj_center(lbl_minus);

    range_val_label = lv_label_create(controls);
    update_range_label();
    lv_obj_set_style_text_color(range_val_label, get_lv_color(getCatppuccinFlavor(s_theme_flavor).blue), 0);
    lv_obj_set_flex_grow(range_val_label, 1);
    lv_obj_set_style_text_align(range_val_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t * btn_plus = lv_btn_create(controls);
    lv_obj_set_size(btn_plus, kArrowBtnW, kArrowBtnH);
    lv_obj_set_ext_click_area(btn_plus, kArrowBtnExtClickPx);
    lv_obj_add_event_cb(btn_plus, range_btn_event_cb, LV_EVENT_CLICKED, (void*)(intptr_t)1);
    lv_obj_set_style_bg_color(btn_plus, get_lv_color(getCatppuccinFlavor(s_theme_flavor).overlay), 0);
    lv_obj_t * lbl_plus = lv_label_create(btn_plus);
    lv_label_set_text(lbl_plus, ">");
    lv_obj_center(lbl_plus);

    return row;
}


static const char* theme_names[] = {"Mocha", "Macchiato", "Frappe", "Latte"};
static lv_obj_t * theme_val_label = nullptr;

static void theme_btn_event_cb(lv_event_t * e) {
    intptr_t dir = (intptr_t)lv_event_get_user_data(e);
    s_theme_flavor += dir;
    if (s_theme_flavor < 1) s_theme_flavor = 4;
    if (s_theme_flavor > 4) s_theme_flavor = 1;
    
    lv_label_set_text(theme_val_label, theme_names[s_theme_flavor - 1]);
    s_prefs.putInt("theme", s_theme_flavor);
    ui::updateThemeColors();
    
    // Changing the theme immediately requires refreshing all colors in the settings menu,
    // which is complex. For now, it will apply when re-opening settings.
}

static lv_obj_t* create_theme_row(lv_obj_t * parent) {
    lv_obj_t * row = lv_obj_create(parent);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, LV_PCT(100), 75);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(row, get_lv_color(getCatppuccinFlavor(s_theme_flavor).mantle), 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 10, 0);

    lv_obj_t * lbl = lv_label_create(row);
    lv_label_set_text(lbl, "Theme Flavor");
    lv_obj_set_style_text_color(lbl, get_lv_color(getCatppuccinFlavor(s_theme_flavor).text), 0);
    lv_obj_set_width(lbl, LV_PCT(100));

    lv_obj_t * controls = lv_obj_create(row);
    lv_obj_set_size(controls, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(controls, 0, 0);
    lv_obj_set_style_border_width(controls, 0, 0);
    lv_obj_set_style_pad_all(controls, 0, 0);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * btn_minus = lv_btn_create(controls);
    lv_obj_set_size(btn_minus, kArrowBtnW, kArrowBtnH);
    lv_obj_set_ext_click_area(btn_minus, kArrowBtnExtClickPx);
    lv_obj_add_event_cb(btn_minus, theme_btn_event_cb, LV_EVENT_CLICKED, (void*)(intptr_t)-1);
    lv_obj_set_style_bg_color(btn_minus, get_lv_color(getCatppuccinFlavor(s_theme_flavor).overlay), 0);
    lv_obj_t * lbl_minus = lv_label_create(btn_minus);
    lv_label_set_text(lbl_minus, "<");
    lv_obj_center(lbl_minus);

    theme_val_label = lv_label_create(controls);
    lv_label_set_text(theme_val_label, theme_names[s_theme_flavor - 1]);
    lv_obj_set_style_text_color(theme_val_label, get_lv_color(getCatppuccinFlavor(s_theme_flavor).blue), 0);
    lv_obj_set_flex_grow(theme_val_label, 1);
    lv_obj_set_style_text_align(theme_val_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t * btn_plus = lv_btn_create(controls);
    lv_obj_set_size(btn_plus, kArrowBtnW, kArrowBtnH);
    lv_obj_set_ext_click_area(btn_plus, kArrowBtnExtClickPx);
    lv_obj_add_event_cb(btn_plus, theme_btn_event_cb, LV_EVENT_CLICKED, (void*)(intptr_t)1);
    lv_obj_set_style_bg_color(btn_plus, get_lv_color(getCatppuccinFlavor(s_theme_flavor).overlay), 0);
    lv_obj_t * lbl_plus = lv_label_create(btn_plus);
    lv_label_set_text(lbl_plus, ">");
    lv_obj_center(lbl_plus);

    return row;
}

static lv_obj_t* create_timezone_row(lv_obj_t * parent) {
    lv_obj_t * row = lv_obj_create(parent);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, LV_PCT(100), 75);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(row, get_lv_color(getCatppuccinFlavor(s_theme_flavor).mantle), 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 10, 0);

    lv_obj_t * lbl = lv_label_create(row);
    lv_label_set_text(lbl, "Timezone");
    lv_obj_set_style_text_color(lbl, get_lv_color(getCatppuccinFlavor(s_theme_flavor).text), 0);
    lv_obj_set_width(lbl, LV_PCT(100));

    lv_obj_t * controls = lv_obj_create(row);
    lv_obj_set_size(controls, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(controls, 0, 0);
    lv_obj_set_style_border_width(controls, 0, 0);
    lv_obj_set_style_pad_all(controls, 0, 0);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * btn_minus = lv_btn_create(controls);
    lv_obj_set_size(btn_minus, kArrowBtnW, kArrowBtnH);
    lv_obj_set_ext_click_area(btn_minus, kArrowBtnExtClickPx);
    lv_obj_add_event_cb(btn_minus, tz_btn_event_cb, LV_EVENT_CLICKED, (void*)(intptr_t)-1);
    lv_obj_set_style_bg_color(btn_minus, get_lv_color(getCatppuccinFlavor(s_theme_flavor).overlay), 0);
    lv_obj_t * lbl_minus = lv_label_create(btn_minus);
    lv_label_set_text(lbl_minus, "<");
    lv_obj_center(lbl_minus);

    tz_val_label = lv_label_create(controls);
    lv_label_set_text(tz_val_label, tz_presets[s_tz_idx].label);
    lv_obj_set_style_text_color(tz_val_label, get_lv_color(getCatppuccinFlavor(s_theme_flavor).blue), 0);
    lv_obj_set_flex_grow(tz_val_label, 1);
    lv_obj_set_style_text_align(tz_val_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t * btn_plus = lv_btn_create(controls);
    lv_obj_set_size(btn_plus, kArrowBtnW, kArrowBtnH);
    lv_obj_set_ext_click_area(btn_plus, kArrowBtnExtClickPx);
    lv_obj_add_event_cb(btn_plus, tz_btn_event_cb, LV_EVENT_CLICKED, (void*)(intptr_t)1);
    lv_obj_set_style_bg_color(btn_plus, get_lv_color(getCatppuccinFlavor(s_theme_flavor).overlay), 0);
    lv_obj_t * lbl_plus = lv_label_create(btn_plus);
    lv_label_set_text(lbl_plus, ">");
    lv_obj_center(lbl_plus);

    return row;
}

// Text rows edited with an on-screen keyboard. Lat/lon go to services::location (the radar
// re-centres on the next fetch); Wi-Fi credentials go to the "wifi" prefs read by WifiManager,
// which uses them on its next connect attempt (reboot, or after the connection drops).
enum EditField { kFieldLat, kFieldLon, kFieldSsid1, kFieldPass1, kFieldSsid2, kFieldPass2, kFieldCount };
struct EditFieldInfo {
    const char* title;
    const char* wifi_key;  // nullptr for lat/lon
    bool numeric;
};
static const EditFieldInfo kEditFields[kFieldCount] = {
    {"Latitude", nullptr, true},
    {"Longitude", nullptr, true},
    {"WiFi 1 SSID", "ssid", false},
    {"WiFi 1 Pass", "pass", false},
    {"WiFi 2 SSID", "ssid2", false},
    {"WiFi 2 Pass", "pass2", false},
};
static lv_obj_t * s_field_val_labels[kFieldCount] = {};
static lv_obj_t * s_editor = nullptr;
static lv_obj_t * s_editor_title = nullptr;
static lv_obj_t * s_editor_ta = nullptr;
static int s_edit_field = -1;

static String read_field(int field) {
    if (field == kFieldLat) return String(services::location::lat(), 4);
    if (field == kFieldLon) return String(services::location::lon(), 4);
    Preferences wifi_prefs;
    wifi_prefs.begin("wifi", true);
    // isKey() first: getString() on a missing key logs an NVS error.
    const char* key = kEditFields[field].wifi_key;
    String value = wifi_prefs.isKey(key) ? wifi_prefs.getString(key, "") : String();
    const bool ssid1_saved = wifi_prefs.isKey("ssid") && wifi_prefs.getString("ssid", "").length() > 0;
    wifi_prefs.end();
    // WifiManager falls back to the compiled-in network when no SSID 1 is saved.
    if (!ssid1_saved && field == kFieldSsid1) value = WIFI_SSID;
    if (!ssid1_saved && field == kFieldPass1) value = WIFI_PASSWORD;
    return value;
}

static bool write_field(int field, const char* text) {
    if (field == kFieldLat) {
        return services::location::saveFromStrings(text, String(services::location::lon(), 6).c_str());
    }
    if (field == kFieldLon) {
        return services::location::saveFromStrings(String(services::location::lat(), 6).c_str(), text);
    }
    Preferences wifi_prefs;
    wifi_prefs.begin("wifi", false);
    wifi_prefs.putString(kEditFields[field].wifi_key, text);
    wifi_prefs.end();
    return true;
}

static void update_field_label(int field) {
    if (s_field_val_labels[field] == nullptr) return;
    const String value = read_field(field);
    lv_label_set_text(s_field_val_labels[field], value.length() > 0 ? value.c_str() : "-");
}

static void close_editor() {
    if (s_editor != nullptr) {
        lv_obj_del_async(s_editor);  // called from the keyboard's own event
        s_editor = nullptr;
        s_editor_title = nullptr;
        s_editor_ta = nullptr;
    }
    s_edit_field = -1;
}

static void editor_save() {
    if (s_edit_field < 0 || s_editor_ta == nullptr) return;
    if (!write_field(s_edit_field, lv_textarea_get_text(s_editor_ta))) {
        lv_label_set_text_fmt(s_editor_title, "Invalid %s", kEditFields[s_edit_field].title);
        lv_obj_set_style_text_color(s_editor_title, get_lv_color(getCatppuccinFlavor(s_theme_flavor).red), 0);
        return;
    }
    if (s_edit_field == kFieldLat || s_edit_field == kFieldLon) {
        update_field_label(kFieldLat);
        update_field_label(kFieldLon);
    } else {
        update_field_label(s_edit_field);
    }
    close_editor();
}

// The keyboard's OK / keyboard keys only fire on release (CLICK_TRIG) and did nothing on the
// FNK0104B, so the editor also has real Save / Cancel buttons; all of these end up here.
static void editor_kb_event_cb(lv_event_t * e) {
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CANCEL) {
        close_editor();
    } else if (code == LV_EVENT_READY) {
        editor_save();
    }
}

static void editor_save_btn_cb(lv_event_t * e) { editor_save(); }
static void editor_cancel_btn_cb(lv_event_t * e) { close_editor(); }

static lv_obj_t * create_editor_btn(const char * text, lv_coord_t x_ofs, uint32_t bg, lv_event_cb_t cb) {
    lv_obj_t * btn = lv_btn_create(s_editor);
    lv_obj_set_size(btn, 90, 32);
    lv_obj_align_to(btn, s_editor_ta, LV_ALIGN_OUT_BOTTOM_MID, x_ofs, 10);
    lv_obj_set_ext_click_area(btn, kArrowBtnExtClickPx);
    lv_obj_set_style_bg_color(btn, get_lv_color(bg), 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, get_lv_color(getCatppuccinFlavor(s_theme_flavor).crust), 0);
    lv_obj_center(lbl);
    return btn;
}

static void open_editor(int field) {
    if (s_editor != nullptr) return;
    s_edit_field = field;

    s_editor = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_editor, LV_PCT(100), LV_PCT(100));
    lv_obj_align(s_editor, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_editor, get_lv_color(getCatppuccinFlavor(s_theme_flavor).base), 0);
    lv_obj_set_style_border_width(s_editor, 0, 0);
    lv_obj_set_style_radius(s_editor, 0, 0);
    lv_obj_set_style_pad_all(s_editor, 5, 0);
    lv_obj_clear_flag(s_editor, LV_OBJ_FLAG_SCROLLABLE);

    s_editor_title = lv_label_create(s_editor);
    lv_label_set_text(s_editor_title, kEditFields[field].title);
    lv_obj_set_style_text_color(s_editor_title, get_lv_color(getCatppuccinFlavor(s_theme_flavor).text), 0);
    lv_obj_align(s_editor_title, LV_ALIGN_TOP_MID, 0, 10);

    s_editor_ta = lv_textarea_create(s_editor);
    lv_textarea_set_one_line(s_editor_ta, true);
    lv_textarea_set_max_length(s_editor_ta, 63);
    lv_textarea_set_text(s_editor_ta, read_field(field).c_str());
    lv_obj_set_width(s_editor_ta, LV_PCT(100));
    lv_obj_align(s_editor_ta, LV_ALIGN_TOP_MID, 0, 35);
    lv_obj_add_state(s_editor_ta, LV_STATE_FOCUSED);  // show the cursor

    lv_obj_t * kb = lv_keyboard_create(s_editor);
    lv_keyboard_set_textarea(kb, s_editor_ta);
    if (kEditFields[field].numeric) {
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
    }
    lv_obj_add_event_cb(kb, editor_kb_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(s_editor_ta, editor_kb_event_cb, LV_EVENT_READY, NULL);  // Enter key

    create_editor_btn("Cancel", -55, getCatppuccinFlavor(s_theme_flavor).overlay, editor_cancel_btn_cb);
    create_editor_btn("Save", 55, getCatppuccinFlavor(s_theme_flavor).blue, editor_save_btn_cb);
}

static void edit_row_event_cb(lv_event_t * e) {
    open_editor((int)(intptr_t)lv_event_get_user_data(e));
}

static void create_edit_row(lv_obj_t * parent, int field) {
    lv_obj_t * row = lv_obj_create(parent);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, LV_PCT(100), 40);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(row, get_lv_color(getCatppuccinFlavor(s_theme_flavor).mantle), 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 10, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, edit_row_event_cb, LV_EVENT_CLICKED, (void*)(intptr_t)field);

    lv_obj_t * lbl = lv_label_create(row);
    lv_label_set_text(lbl, kEditFields[field].title);
    lv_obj_set_style_text_color(lbl, get_lv_color(getCatppuccinFlavor(s_theme_flavor).text), 0);

    lv_obj_t * val = lv_label_create(row);
    lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
    lv_obj_set_width(val, LV_PCT(50));
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(val, get_lv_color(getCatppuccinFlavor(s_theme_flavor).blue), 0);
    s_field_val_labels[field] = val;
    update_field_label(field);
}

static void build_ui() {
    settings_screen = lv_obj_create(NULL);
    if (!settings_screen) return;
    
    lv_obj_set_style_bg_color(settings_screen, get_lv_color(getCatppuccinFlavor(s_theme_flavor).base), 0);
    
    lv_obj_t * title = lv_label_create(settings_screen);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_color(title, get_lv_color(getCatppuccinFlavor(s_theme_flavor).text), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);
    
    lv_obj_t * wifi_label = lv_label_create(settings_screen);
    lv_label_set_text(wifi_label, LV_SYMBOL_WIFI);
    
    lv_color_t wifi_color;
    if (WiFi.status() == WL_CONNECTED) {
        wifi_color = get_lv_color(getCatppuccinFlavor(s_theme_flavor).green);
    } else if (WiFi.getMode() == WIFI_AP) {
        wifi_color = get_lv_color(getCatppuccinFlavor(s_theme_flavor).mauve);
    } else {
        wifi_color = get_lv_color(getCatppuccinFlavor(s_theme_flavor).red);
    }
    lv_obj_set_style_text_color(wifi_label, wifi_color, 0);
    lv_obj_align(wifi_label, LV_ALIGN_TOP_RIGHT, -10, 15);
    lv_obj_add_flag(wifi_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(wifi_label, 15);
    lv_obj_add_event_cb(wifi_label, wifi_icon_click_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t * list = lv_obj_create(settings_screen);
    lv_obj_set_size(list, LV_PCT(95), LV_PCT(70));
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(list, get_lv_color(getCatppuccinFlavor(s_theme_flavor).base), 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 5, 0);
    lv_obj_set_style_pad_row(list, 10, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    

    // Settings List
    create_toggle_row(list, "Large Airports", s_show_airports, airports_switch_event_cb);
    create_toggle_row(list, "Medium Airports", s_show_medium_airports, medium_airports_switch_event_cb);
    create_toggle_row(list, "Metric Units", !ui::radar::useMiles(), units_switch_event_cb);
    create_toggle_row(list, "Ground Aircraft", s_show_ground_aircraft, ground_aircraft_switch_event_cb);
    create_toggle_row(list, "Radar Sweep", s_show_radar_sweep, radar_sweep_switch_event_cb);
    create_toggle_row(list, "Auto-Dim Night", s_auto_dimming, auto_dimming_switch_event_cb);
    
    create_slider_row(list, "Max Alt", 0, 50000, s_max_altitude, "ft", max_altitude_slider_event_cb);
    create_slider_row(list, "Max Planes", 1, services::adsb::kMaxAircraft, s_max_aircraft, "", max_aircraft_slider_event_cb);
    create_slider_row(list, "Sweep Speed", 1000, 15000, s_sweep_speed, "ms", sweep_speed_slider_event_cb);
    create_timezone_row(list);
    create_screensaver_row(list);
    create_range_row(list);
    create_theme_row(list);
    for (int field = 0; field < kFieldCount; ++field) {
        create_edit_row(list, field);
    }

    // Close button
    lv_obj_t * close_btn = lv_btn_create(settings_screen);
    lv_obj_set_size(close_btn, 100, 30);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_event_cb(close_btn, close_btn_event_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(close_btn, get_lv_color(getCatppuccinFlavor(s_theme_flavor).blue), 0);
    
    lv_obj_t * close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, "Close");
    lv_obj_set_style_text_color(close_label, get_lv_color(getCatppuccinFlavor(s_theme_flavor).crust), 0);
    lv_obj_center(close_label);
}

void setup() {
    s_prefs.begin("settings", false);
    s_theme_flavor = s_prefs.getInt("theme", CATPPUCCIN_MOCHA);
    if (s_theme_flavor < 1 || s_theme_flavor > 4) {
        s_theme_flavor = CATPPUCCIN_MOCHA;
    }
    s_max_aircraft = s_prefs.getInt("max_ac", services::adsb::kMaxAircraft);
    if (s_max_aircraft < 1) s_max_aircraft = 1;
    if (s_max_aircraft > services::adsb::kMaxAircraft) s_max_aircraft = services::adsb::kMaxAircraft;
    // These were saved on change but never read back, so they reset on reboot.
    s_show_airports = s_prefs.getBool("airports", s_show_airports);
    s_show_medium_airports = s_prefs.getBool("med_airports", s_show_medium_airports);
    s_auto_dimming = s_prefs.getBool("auto_dim", s_auto_dimming);
    const String tz = s_prefs.getString("tz", "");
    for (int i = 0; i < num_tz_presets; ++i) {
        if (tz == tz_presets[i].value) {
            s_tz_idx = i;
            break;
        }
    }
    const int ss_min = s_prefs.getInt("ss_min", config::kScreensaverDefaultMin);
    for (int i = 0; i < num_ss_presets; ++i) {
        if (ss_min == ss_presets_min[i]) {
            s_ss_idx = i;
            break;
        }
    }

    lv_init();
    
    // Allocate draw buffer (reduce to 5 lines to save heap for SSL)
    const size_t buf_size = tft.width() * 5;
    buf1 = (lv_color_t*)malloc(buf_size * sizeof(lv_color_t));
    lv_disp_draw_buf_init(&draw_buf, buf1, nullptr, buf_size);

    // Initialize the display
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = tft.width();
    disp_drv.ver_res = tft.height();
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // Initialize the (dummy) input device driver
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);
    
    last_tick_millis = millis();
}

static lv_obj_t* s_default_screen = nullptr;

void loop() {
    if (s_visible) {
        lv_timer_handler();
        
        if (s_pending_hide) {
            s_pending_hide = false;
            hide();
        }
    }
}

void show() {
    s_visible = true;
    if (s_default_screen == nullptr) {
        s_default_screen = lv_scr_act();
    }
    build_ui();
    if (!settings_screen) {
        // OOM: cannot open settings screen right now
        return;
    }
    lv_scr_load(settings_screen);
    s_visible = true;
    s_pending_hide = false;
    s_show_ms = millis();
}

void hide() {
    s_visible = false;
    // Switch back to the default empty screen BEFORE deleting the settings screen
    if (s_default_screen) {
        lv_scr_load(s_default_screen);
    }
    lv_obj_del(settings_screen);  // also deletes an open editor
    settings_screen = nullptr;
    s_editor = nullptr;
    s_editor_title = nullptr;
    s_editor_ta = nullptr;
    s_edit_field = -1;
    for (int field = 0; field < kFieldCount; ++field) {
        s_field_val_labels[field] = nullptr;
    }
    
    // We clear the screen completely so LovyanGFX can draw the radar over it again
    const CatppuccinColors& flavor = getCatppuccinFlavor(s_theme_flavor);
    uint32_t bg_color = flavor.base;
    tft.fillScreen(tft.color565((bg_color >> 16) & 0xFF, (bg_color >> 8) & 0xFF, bg_color & 0xFF));
    ui::radarDisplayRefreshAircraft(); // Force redraw of the bottom details panel
}

bool isVisible() { return s_visible; }
bool isAirportsEnabled() { return s_show_airports; }
bool isMediumAirportsEnabled() { return s_show_medium_airports; }
bool isGroundAircraftEnabled() { return s_show_ground_aircraft; }
bool isRadarSweepEnabled() { return s_show_radar_sweep; }
bool isAutoDimmingEnabled() { return s_auto_dimming; }
int getMaxAltitudeFilter() { return s_max_altitude; }
int getSweepRotationSpeedMs() { return s_sweep_speed; }
int getMaxAircraftLimit() { return s_max_aircraft; }
unsigned long getScreensaverTimeoutMs() { return ss_presets_min[s_ss_idx] * 60000UL; }

bool isTimezoneChanged() { return s_tz_changed; }
void clearTimezoneChanged() { s_tz_changed = false; }
const char* getTimezoneStr() { return tz_presets[s_tz_idx].value; }

int getThemeFlavor() { return s_theme_flavor; }
void setThemeFlavor(int flavor) {
    if (flavor >= 1 && flavor <= 4) {
        if (s_theme_flavor != flavor) {
            s_theme_flavor = flavor;
            s_prefs.putInt("theme", s_theme_flavor);
            ui::updateThemeColors();
        }
    }
}

} // namespace ui::settings
