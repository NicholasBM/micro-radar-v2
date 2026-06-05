#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

#include "JsonParser.h"

// Maps to OpenSky /states/all response
// Field indices match the documented state vector array order
struct Aircraft {
    String icao24;          // [0]  unique ICAO 24-bit transponder address
    String callsign;        // [1]  flight callsign, may be empty
    String originCountry;   // [2]  country inferred from ICAO address
    float  longitude;       // [5]  WGS-84 longitude in decimal degrees
    float  latitude;        // [6]  WGS-84 latitude in decimal degrees
    float  baroAltitude;    // [7]  barometric altitude in metres
    bool   onGround;        // [8]  true if surface position report
    float  velocity;        // [9]  ground speed in m/s
    float  trueTrack;       // [10] heading in degrees clockwise from north
    float  verticalRate;    // [11] climb/descent rate in m/s (positive = climbing)
    float  geoAltitude;     // [13] geometric altitude in metres
    int    positionSource;  // [16] 0=ADS-B, 1=ASTERIX, 2=MLAT, 3=FLARM

    String toString() const {
        String s = "";
        s += "ICAO24: " + icao24 + "\n";
        s += "Callsign: " + callsign + "\n";
        s += "Country: " + originCountry + "\n";
        s += "Lat: " + String(latitude, 6) + "\n";
        s += "Lon: " + String(longitude, 6) + "\n";
        s += "Altitude: " + String(baroAltitude) + "m\n";
        s += "Heading: " + String(trueTrack) + "°\n";
        s += "Speed: " + String(velocity) + "m/s\n";
        s += "Vertical Rate: " + String(verticalRate) + "m/s\n";
        s += "On Ground: " + String(onGround ? "yes" : "no") + "\n";
        return s;
    }
};

namespace JsonParser {

    template<>
    Aircraft Parse<Aircraft>(const JsonVariant& state);

}