#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "config.h"
#if defined(ST7796_DRIVER) && defined(FT6336_TOUCH)
#include "hardware/touch_ft6336.hpp"
#endif

#ifdef RGB_PANEL
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

// Freenove FNK0115Q (5.0" IPS 800x480): 16-bit RGB parallel panel (no controller to set up,
// framebuffer in PSRAM), GT911 touch on I2C. Pins and timings from Freenove's TFT_Display.h /
// TFT_Display.cpp (Arduino_GFX) and Touch.h.
class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_RGB _bus;
  lgfx::Panel_RGB _panel;
  lgfx::Light_PWM _light;
  lgfx::Touch_GT911 _touch;

public:
  LGFX() {
    {
      auto cfg = _panel.config();
      cfg.memory_width = TFT_WIDTH;
      cfg.memory_height = TFT_HEIGHT;
      cfg.panel_width = TFT_WIDTH;
      cfg.panel_height = TFT_HEIGHT;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      _panel.config(cfg);
    }
    {
      auto cfg = _panel.config_detail();
      cfg.use_psram = 1;
      _panel.config_detail(cfg);
    }
    {
      auto cfg = _bus.config();
      cfg.panel = &_panel;
      // RGB565 bit order: d0-d4 blue, d5-d10 green, d11-d15 red.
      cfg.pin_d0 = GPIO_NUM_8;    // B0
      cfg.pin_d1 = GPIO_NUM_3;    // B1
      cfg.pin_d2 = GPIO_NUM_46;   // B2
      cfg.pin_d3 = GPIO_NUM_9;    // B3
      cfg.pin_d4 = GPIO_NUM_1;    // B4
      cfg.pin_d5 = GPIO_NUM_5;    // G0
      cfg.pin_d6 = GPIO_NUM_6;    // G1
      cfg.pin_d7 = GPIO_NUM_7;    // G2
      cfg.pin_d8 = GPIO_NUM_15;   // G3
      cfg.pin_d9 = GPIO_NUM_16;   // G4
      cfg.pin_d10 = GPIO_NUM_4;   // G5
      cfg.pin_d11 = GPIO_NUM_45;  // R0
      cfg.pin_d12 = GPIO_NUM_48;  // R1
      cfg.pin_d13 = GPIO_NUM_47;  // R2
      cfg.pin_d14 = GPIO_NUM_21;  // R3
      cfg.pin_d15 = GPIO_NUM_14;  // R4
      cfg.pin_henable = GPIO_NUM_40;  // DE
      cfg.pin_vsync = GPIO_NUM_41;
      cfg.pin_hsync = GPIO_NUM_39;
      cfg.pin_pclk = GPIO_NUM_42;
      cfg.freq_write = 13000000;
      cfg.hsync_polarity = 0;
      cfg.hsync_front_porch = 4;
      cfg.hsync_pulse_width = 4;
      cfg.hsync_back_porch = 8;
      cfg.vsync_polarity = 0;
      cfg.vsync_front_porch = 4;
      cfg.vsync_pulse_width = 4;
      cfg.vsync_back_porch = 8;
      cfg.pclk_active_neg = 1;  // not read by LovyanGFX's Bus_RGB (see pclk_idle_high)
      cfg.de_idle_high = 0;
      // Bus_RGB sets the PCLK output edge (lcd_ck_out_edge) from this, so it plays the role of
      // Freenove's PCLK_ACTIVE_NEG = 1. At 0 the panel latched data on the wrong edge:
      // colour fringes on text and broken-up rings on hardware.
      cfg.pclk_idle_high = 1;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = TFT_BL;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    {
      auto cfg = _touch.config();
      // Raw GT911 coordinates already match the screen (Freenove maps them 1:1).
      cfg.x_min = 0;
      cfg.x_max = TFT_WIDTH - 1;
      cfg.y_min = 0;
      cfg.y_max = TFT_HEIGHT - 1;
      cfg.pin_int = TOUCH_INT;
      cfg.pin_rst = TOUCH_RST;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.i2c_port = 0;
      cfg.i2c_addr = 0x5D;  // the driver also tries 0x14
      cfg.pin_sda = TOUCH_SDA;
      cfg.pin_scl = TOUCH_SCL;
      cfg.freq = 400000;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};
#else  // SPI panels

class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_SPI _bus;
#ifdef ILI9341_DRIVER
  lgfx::Panel_ILI9341 _panel;
#ifdef FT6336_TOUCH
  lgfx::Touch_FT5x06 _touch;  // FT6336 (Freenove FNK0104B)
#else
  lgfx::Touch_XPT2046 _touch;
#endif
#elif defined(ST7789_DRIVER)
  lgfx::Panel_ST7789 _panel;  // ESP32-2432S028 "CYD2USB" (micro-USB + USB-C version)
  lgfx::Touch_XPT2046 _touch;
#elif defined(ST7796_DRIVER)
  lgfx::Panel_ST7796 _panel;
#ifdef FT6336_TOUCH
  Touch_FT6336 _touch;  // Freenove FNK0104S (Touch_FT5x06 never detected it)
#elif defined(XPT2046_SHARED_CS)
  lgfx::Touch_XPT2046 _touch;  // Freenove FNK0103N: on the LCD's SPI bus
#endif
#else
  lgfx::Panel_GC9A01 _panel;
#endif
  lgfx::Light_PWM _light;

public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
#ifdef TFT_SPI_HZ
      cfg.freq_write = TFT_SPI_HZ;
#else
      cfg.freq_write = 40000000;
#endif
      cfg.pin_sclk = TFT_SCLK;
      cfg.pin_mosi = TFT_MOSI;
      cfg.pin_miso = TFT_MISO;
      cfg.pin_dc = TFT_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = TFT_CS;
      cfg.pin_rst = -1;
      cfg.panel_width = TFT_WIDTH;
      cfg.panel_height = TFT_HEIGHT;
#ifdef ILI9341_DRIVER
#ifdef TFT_INVERT
      cfg.invert = true;
#else
      cfg.invert = false;
#endif
      cfg.rgb_order = false;
#elif defined(ST7789_DRIVER)
#ifdef TFT_INVERT
      cfg.invert = true;
#else
      cfg.invert = false;
#endif
      cfg.rgb_order = false;
#elif defined(ST7796_DRIVER)
#ifdef TFT_INVERT
      cfg.invert = true;  // FNK0104S
#else
      cfg.invert = false;
#endif
      cfg.rgb_order = false;
#endif
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = TFT_BL;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
#if defined(FT6336_TOUCH)
    {
      auto cfg = _touch.config();
#ifdef BOARD_FNK0104S
      // FNK0104S touch runs the same way as the LCD (checked on hardware).
      // (Touch_FT6336 ignores pin_int and polls.)
      cfg.x_min = 0;
      cfg.x_max = TFT_WIDTH - 1;
      cfg.y_min = 0;
      cfg.y_max = TFT_HEIGHT - 1;
#else
      // FNK0104B touch panel is mounted 180 deg from the LCD, so both axes run backwards.
      cfg.x_min = TFT_WIDTH - 1;
      cfg.x_max = 0;
      cfg.y_min = TFT_HEIGHT - 1;
      cfg.y_max = 0;
#endif
      cfg.pin_int = TOUCH_INT;
      cfg.pin_rst = TOUCH_RST;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.i2c_port = 0;
      cfg.i2c_addr = 0x38;
      cfg.pin_sda = TOUCH_SDA;
      cfg.pin_scl = TOUCH_SCL;
      cfg.freq = 400000;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
#elif defined(ILI9341_DRIVER) || defined(ST7789_DRIVER)
    {
      auto cfg = _touch.config();
      // Typical CYD XPT2046 raw AD calibration limits
#ifdef ST7789_DRIVER
      // CYD2USB: both axes run the other way from the ILI9341 CYD (checked on hardware).
      cfg.x_min      = 3900;
      cfg.x_max      = 300;
      cfg.y_min      = 200;
      cfg.y_max      = 3700;
#else
      cfg.x_min      = 300;
      cfg.x_max      = 3900;
      cfg.y_min      = 3700;
      cfg.y_max      = 200;
#endif
      cfg.pin_int    = XPT2046_IRQ;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.spi_host = SPI3_HOST;
      cfg.freq = 2500000;
      cfg.pin_sclk = XPT2046_CLK;
      cfg.pin_mosi = XPT2046_MOSI;
      cfg.pin_miso = XPT2046_MISO;
      cfg.pin_cs   = XPT2046_CS;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
#elif defined(ST7796_DRIVER) && defined(XPT2046_SHARED_CS)
    {
      auto cfg = _touch.config();
      // Raw AD limits; check with TOUCH_DEBUG on serial and adjust if taps land off target.
      cfg.x_min      = 300;
      cfg.x_max      = 3900;
      cfg.y_min      = 3900;
      cfg.y_max      = 300;
      cfg.pin_int    = -1;    // no IRQ line in Freenove's setup: poll pressure instead
      cfg.bus_shared = true;  // same SPI bus as the LCD
      cfg.offset_rotation = 0;
      cfg.spi_host = SPI2_HOST;
      cfg.freq = 1000000;
      cfg.pin_sclk = TFT_SCLK;
      cfg.pin_mosi = TFT_MOSI;
      cfg.pin_miso = TFT_MISO;
      cfg.pin_cs   = XPT2046_SHARED_CS;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
#endif
    setPanel(&_panel);
  }
};
#endif  // RGB_PANEL
