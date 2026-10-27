#include "utils_adsb.h"
#include <cstring>
#include <cmath>
#include <cstdio>

namespace utils {
namespace adsb {

bool readJsonFloat(const JsonObject& obj, const char* key, float* out) {
  if (obj[key].is<float>() || obj[key].is<double>() || obj[key].is<int>()) {
    *out = obj[key].as<float>();
    return true;
  }
  return false;
}

// dbFlags is a bitfield (1 = military, 2 = interesting, 4 = PIA, 8 = LADD), not a bool.
bool isMilitary(const JsonObject& plane) {
  return plane["dbFlags"].is<int>() && (plane["dbFlags"].as<int>() & 1) != 0;
}

float pickNoseHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "true_heading", &v)) return v;
  if (readJsonFloat(plane, "mag_heading", &v)) return v;
  if (readJsonFloat(plane, "track", &v)) return v;
  if (readJsonFloat(plane, "dir", &v)) return v;
  return 0.0f;
}

float pickTrackHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "track", &v)) return v;
  if (readJsonFloat(plane, "true_heading", &v)) return v;
  if (readJsonFloat(plane, "mag_heading", &v)) return v;
  if (readJsonFloat(plane, "dir", &v)) return v;
  return 0.0f;
}

float pickGroundSpeed(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "gs", &v)) return v;
  if (readJsonFloat(plane, "tas", &v)) return v;
  if (readJsonFloat(plane, "ias", &v)) return v;
  return 0.0f;
}

bool isOnGround(const JsonObject& plane) {
  if (!plane["alt_baro"].is<const char*>()) {
    return false;
  }
  return strcmp(plane["alt_baro"].as<const char*>(), "ground") == 0;
}

void copyJsonStringTrimmed(const JsonObject& obj, const char* key, char* out, size_t out_len) {
  out[0] = '\0';
  if (out_len == 0 || !obj[key].is<const char*>()) {
    return;
  }
  const char* s = obj[key].as<const char*>();
  size_t n = strnlen(s, out_len - 1);
  while (n > 0 && s[n - 1] == ' ') {
    --n;
  }
  memcpy(out, s, n);
  out[n] = '\0';
}

bool isHelicopter(const JsonObject& plane) {
  if (plane["category"].is<const char*>()) {
    const char* cat = plane["category"].as<const char*>();
    if (strcmp(cat, "A7") == 0) {
      return true;
    }
  }
  if (plane["desc"].is<const char*>()) {
    const char* desc = plane["desc"].as<const char*>();
    if (strstr(desc, "HELICOPTER") != nullptr || strstr(desc, "ROTORCRAFT") != nullptr || strstr(desc, "Helicopter") != nullptr || strstr(desc, "Rotorcraft") != nullptr) {
      return true;
    }
  }
  return false;
}

bool isTrainerType(const char* type) {
  static const char* const kTrainerTypes[] = {
      "C150", "C152", "C162", "C172",          // Cessna
      "P28A", "P28R", "PA38",                  // Piper Cherokee/Warrior/Archer, Arrow, Tomahawk
      "DV20", "DA40", "DA42",                  // Diamond
      "SR20",                                  // Cirrus
  };
  if (type == nullptr || type[0] == '\0') {
    return false;
  }
  for (const char* t : kTrainerTypes) {
    if (strcmp(type, t) == 0) {
      return true;
    }
  }
  return false;
}

namespace {

// Entries ending in '*' match as a prefix ("B74*" = every 747), others exactly.
bool matchesTypeList(const char* type, const char* const* list, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    const char* t = list[i];
    const size_t n = strlen(t);
    if (n > 0 && t[n - 1] == '*') {
      if (strncmp(type, t, n - 1) == 0) return true;
    } else if (strcmp(type, t) == 0) {
      return true;
    }
  }
  return false;
}

template <size_t N>
bool matchesTypeList(const char* type, const char* const (&list)[N]) {
  return matchesTypeList(type, list, N);
}

const char* const kFighterTypes[] = {
    "F14", "F15", "F16", "F18", "F18H", "F18S", "F22", "F35", "F4", "F5", "A10", "T38",
    "T45", "HAWK", "EUFI", "RFAL", "GRIF", "TOR", "M346", "L39", "MG29", "SU27", "SU30",
};
const char* const kHeavyQuadTypes[] = {
    "B74*", "A34*", "A38*", "C17", "C5M", "A400", "K35R", "K35E", "E3TF", "E3CF", "E6",
    "B52", "IL76", "IL96", "A124", "A225", "BLCF",
};
const char* const kHeavyTypes[] = {
    "B77*", "B78*", "B76*", "A33*", "A35*", "A30*", "A310", "MD11", "DC10", "KC10",
    "IL86", "A3ST",
};
const char* const kBizjetTypes[] = {
    "C25*", "C50*", "C51*", "C52*", "C55*", "C56*", "C65*", "C68*", "C70*", "C75*", "LJ*",
    "GLF*", "GLEX", "GL5T", "GL6T", "GL7T", "GL8T", "CL30", "CL35", "CL60", "G150", "G200",
    "G280", "F2TH", "FA50", "FA6X", "FA7X", "FA8X", "FA10", "FA20", "E50P", "E55P", "E545",
    "E550", "HDJT", "PC24", "H25*", "BE40", "PRM1", "SF50", "EA50", "ASTR", "WW24",
};
const char* const kTurbopropTypes[] = {
    "DH8*", "AT4*", "AT7*", "SF34", "SB20", "B190", "BE20", "BE30", "B350", "BE9*", "E120",
    "E110", "D328", "JS31", "JS32", "JS41", "SW4", "PC12", "PC6T", "C208", "DHC6", "DHC7",
    "C130", "C30J", "P3", "C27J", "CN35", "C295", "L410", "TBM*", "KODI", "P180", "AN2*",
    "M600", "PAY*", "MU2", "AC9*",
};
// Piston singles/twins without a category: Cessna 1xx/2xx (Citations and the Caravan are
// caught above), Pipers, Bonanzas, Cirrus, Mooney, Diamond, RVs.
const char* const kLightGaTypes[] = {
    "C1*", "C2*", "C3*", "C4*", "P28*", "PA*", "BE3*", "BE5*", "BE6*", "BE76", "BE95", "SR2*",
    "M20*", "DA*", "RV*", "AA5", "GLAS*", "CH7*", "AC11", "C77R", "BL8", "DHC2",
};

}  // namespace

services::adsb::AircraftClass classifyAircraft(const char* type, const char* category) {
  using services::adsb::AircraftClass;
  if (type == nullptr) type = "";
  if (category == nullptr) category = "";
  if (type[0] != '\0') {
    if (isTrainerType(type)) return AircraftClass::kTrainer;
    if (matchesTypeList(type, kFighterTypes)) return AircraftClass::kFighter;
    if (matchesTypeList(type, kHeavyQuadTypes)) return AircraftClass::kHeavyQuad;
    if (matchesTypeList(type, kHeavyTypes)) return AircraftClass::kHeavy;
    if (matchesTypeList(type, kBizjetTypes)) return AircraftClass::kBizjet;
    if (matchesTypeList(type, kTurbopropTypes)) return AircraftClass::kTurboprop;
    if (matchesTypeList(type, kLightGaTypes)) return AircraftClass::kLightGa;
  }
  if (strcmp(category, "A5") == 0) return AircraftClass::kHeavy;
  if (strcmp(category, "A6") == 0) return AircraftClass::kFighter;
  if (strcmp(category, "A1") == 0 || strcmp(category, "B1") == 0 ||
      strcmp(category, "B4") == 0) {
    return AircraftClass::kLightGa;
  }
  return AircraftClass::kNarrowbody;
}

bool isEmergency(const JsonObject& plane) {
  if (plane["squawk"].is<const char*>()) {
    const char* sq = plane["squawk"].as<const char*>();
    if (strcmp(sq, "7500") == 0 || strcmp(sq, "7600") == 0 || strcmp(sq, "7700") == 0) {
      return true;
    }
  }
  if (plane["emergency"].is<const char*>()) {
    const char* em = plane["emergency"].as<const char*>();
    return em[0] != '\0' && strcmp(em, "none") != 0 && strcmp(em, "lifeguard") != 0 &&
           strcmp(em, "minfuel") != 0;
  }
  return false;
}

void formatAltitudeTag(const JsonObject& plane, char* out, size_t out_len, bool useMiles) {
  out[0] = '\0';
  if (out_len == 0) {
    return;
  }

  if (plane["alt_baro"].is<const char*>()) {
    const char* s = plane["alt_baro"].as<const char*>();
    if (strcmp(s, "ground") == 0) {
      strncpy(out, "GND", out_len - 1);
      out[out_len - 1] = '\0';
      return;
    }
  }

  float alt = 0.0f;
  if (readJsonFloat(plane, "alt_baro", &alt) ||
      readJsonFloat(plane, "alt_geom", &alt)) {
    if (!useMiles) {
      snprintf(out, out_len, "%d m", static_cast<int>(lroundf(alt * 0.3048f)));
    } else {
      snprintf(out, out_len, "%d ft", static_cast<int>(lroundf(alt)));
    }
    return;
  }
}

} // namespace adsb
} // namespace utils
