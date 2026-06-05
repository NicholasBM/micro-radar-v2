#include "models/Aircraft.h"

namespace JsonParser {
    template<>
    Aircraft Parse<Aircraft>(const JsonVariant& state) {
        Aircraft a;

        a.icao24 = state[0].isNull() ? "" : state[0].as<String>();
        a.callsign = state[1].isNull() ? "" : state[1].as<String>();
        a.originCountry = state[2].isNull() ? "" : state[2].as<String>();
        a.longitude = state[5].isNull() ? 0.0f : state[5].as<float>();
        a.latitude = state[6].isNull() ? 0.0f : state[6].as<float>();
        a.baroAltitude = state[7].isNull() ? 0.0f : state[7].as<float>();
        a.onGround = state[8].as<bool>();
        a.velocity = state[9].isNull() ? 0.0f : state[9].as<float>();
        a.trueTrack = state[10].isNull() ? 0.0f : state[10].as<float>();
        a.verticalRate = state[11].isNull() ? 0.0f : state[11].as<float>();
        a.geoAltitude = state[13].isNull() ? 0.0f : state[13].as<float>();
        a.positionSource = state[16].isNull() ? 0 : state[16].as<int>();

        return a;
    }

}