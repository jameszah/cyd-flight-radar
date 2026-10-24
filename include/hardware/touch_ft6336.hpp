#pragma once

// Minimal FT6336 touch driver for LovyanGFX (Freenove FNK0104S).
// lgfx::Touch_FT5x06 never worked on the FNK0104S: it only counts the chip as present when
// register 0xA8 (vendor ID) reads non-zero and probes 1 ms after reset, and it gates reads on
// the INT pin. This one waits after reset, skips the ID check and polls TD_STATUS, the way
// Freenove's FT6336U library does.

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <Arduino.h>

class Touch_FT6336 : public lgfx::ITouch {
public:
  Touch_FT6336() {
    _cfg.i2c_addr = 0x38;
    _cfg.freq = 400000;
  }

  bool init() override {
    if (_cfg.pin_rst >= 0) {
      lgfx::pinMode(_cfg.pin_rst, lgfx::pin_mode_t::output);
      lgfx::gpio_lo(_cfg.pin_rst);
      lgfx::delay(10);
      lgfx::gpio_hi(_cfg.pin_rst);
      lgfx::delay(300);  // FT6336 needs ~300 ms after reset before it answers on I2C
    }
    if (_cfg.pin_int >= 0) {
      lgfx::pinMode(_cfg.pin_int, lgfx::pin_mode_t::input_pullup);
    }
    if (!lgfx::i2c::init(_cfg.i2c_port, _cfg.pin_sda, _cfg.pin_scl).has_value()) {
      return false;
    }
    uint8_t reg = 0xA3;  // chip ID A3, ..., firmware A6, ..., vendor A8
    _ids_ok = lgfx::i2c::transactionWriteRead(_cfg.i2c_port, _cfg.i2c_addr, &reg, 1, _ids,
                                              sizeof(_ids), _cfg.freq).has_value();
    return true;  // keep touch enabled even if the probe failed; reads retry every poll
  }

  void wakeup() override {}
  void sleep() override {}

  uint_fast8_t getTouchRaw(lgfx::touch_point_t* tp, uint_fast8_t count) override {
    if (count == 0) return 0;
    uint8_t reg = 0x02;  // TD_STATUS, then P1_XH, P1_XL, P1_YH, P1_YL
    uint8_t d[5];
    const bool ok = lgfx::i2c::transactionWriteRead(_cfg.i2c_port, _cfg.i2c_addr, &reg, 1, d,
                                                    sizeof(d), _cfg.freq).has_value();
#ifdef TOUCH_DEBUG
    debugReport(ok);
#endif
    if (!ok) return 0;
    const uint8_t points = d[0] & 0x0F;
    if (points == 0 || points > 2) return 0;
    tp[0].x = static_cast<int16_t>(((d[1] & 0x0F) << 8) | d[2]);
    tp[0].y = static_cast<int16_t>(((d[3] & 0x0F) << 8) | d[4]);
    tp[0].size = 1;
    tp[0].id = d[3] >> 4;
    return 1;
  }

private:
  uint8_t _ids[6] = {0};
  bool _ids_ok = false;

#ifdef TOUCH_DEBUG
  // Every 5 s: probe result and whether polling reads work (USB serial isn't up at init).
  void debugReport(bool read_ok) {
    static uint32_t s_last_ms = 0;
    static uint32_t s_fails = 0;
    if (!read_ok) ++s_fails;
    const uint32_t now = millis();
    if (now - s_last_ms < 5000) return;
    s_last_ms = now;
    Serial.printf("ft6336: probe %s chip=0x%02x fw=0x%02x vendor=0x%02x, read fails=%lu\n",
                  _ids_ok ? "ok" : "FAILED", _ids[0], _ids[3], _ids[5],
                  static_cast<unsigned long>(s_fails));
  }
#endif
};
