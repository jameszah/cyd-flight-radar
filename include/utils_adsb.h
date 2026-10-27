#pragma once
#include <ArduinoJson.h>
#include "services/adsb_client.h"

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
/** Icon class from the ICAO type code ("t") and ADS-B emitter category ("A1".."C3"). */
services::adsb::AircraftClass classifyAircraft(const char* type, const char* category);
/** Squawk 7500/7600/7700, or adsb.fi "emergency" set to anything but none/lifeguard/minfuel. */
bool isEmergency(const JsonObject& plane);
void formatAltitudeTag(const JsonObject& plane, char* out, size_t out_len, bool useMiles);

} // namespace adsb
} // namespace utils
