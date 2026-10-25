#pragma once
#include <time.h>

namespace utils {
namespace time {

// Local time without Arduino's getLocalTime(&tm, 0): with a 0 ms timeout it returns
// false without reading the clock whenever millis() ticks over before its first loop
// check, so the clock blinked to "--:--" for a frame now and then.
inline bool localTimeNow(struct tm* info) {
  const time_t now = ::time(nullptr);
  localtime_r(&now, info);
  return info->tm_year > (2016 - 1900);
}

} // namespace time
} // namespace utils
