#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "config.h"
#if defined(ST7796_DRIVER) && defined(FT6336_TOUCH)
#include "hardware/touch_ft6336.hpp"
#endif

class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_SPI _bus;
#ifdef ILI9341_DRIVER
  lgfx::Panel_ILI9341 _panel;
#ifdef FT6336_TOUCH
  lgfx::Touch_FT5x06 _touch;  // FT6336 (Freenove FNK0104B)
#else
  lgfx::Touch_XPT2046 _touch;
#endif
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
#elif defined(ILI9341_DRIVER)
    {
      auto cfg = _touch.config();
      // Typical CYD XPT2046 raw AD calibration limits
      cfg.x_min      = 300;
      cfg.x_max      = 3900;
      cfg.y_min      = 3700;
      cfg.y_max      = 200;
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
