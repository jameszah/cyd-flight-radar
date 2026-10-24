#pragma once
#include <ArduinoJson.h>

namespace utils {
namespace adsb {

bool readJsonFloat(const JsonObject& obj, const char* key, float* out);
bool isMilitary(const JsonObject& plane);
float pickNoseHeading(const JsonObject& plane);
float pickTrackHeading(const JsonObject& plane);
float pickGroundSpeed(const JsonObject& plane);
bool isOnGround(const JsonObject& plane);
void copyJsonStringTrimmed(const JsonObject& obj, const char* key, char* out, size_t out_len);
bool isHelicopter(const JsonObject& plane);
/** Common flight-school type (ICAO type code, e.g. "C172", "P28A"). */
bool isTrainerType(const char* type);
void formatAltitudeTag(const JsonObject& plane, char* out, size_t out_len, bool useMiles);

} // namespace adsb
} // namespace utils
