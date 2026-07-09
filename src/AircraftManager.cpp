#include "AircraftManager.h"

#include <cmath>
#include <ArduinoJson.h>

constexpr int SCREEN_SIZE = 240;
constexpr int SCREEN_SIZE_DIV_2 = (SCREEN_SIZE / 2);

void AircraftManager::Initialise()
{
    // get centre point + radius
    lat = configServer.GetStoredString("latitude").toDouble();
    lon = configServer.GetStoredString("longitude").toDouble();
    rad = configServer.GetStoredString("radius").toDouble();

    // configuration
    const String renderText = configServer.GetStoredString("infotext");
    const String renderTris = configServer.GetStoredString("triangle");
    if (!renderText.isEmpty()) displayInfoText = renderText == "true" ? true : false;
    if (!renderTris.isEmpty()) displayTriangles = renderTris == "true" ? true : false;

    // calculate how often we can call OpenSky API before being rate limited
    constexpr int MS_PER_DAY = 24 * 60 * 60 * 1000;
    constexpr int ANONYMOUS_TOKENS_PER_DAY = 400;
    constexpr int AUTHED_TOKENS_PER_DAY = 4000;
    constexpr int TOKEN_BUFFER = 3;
    int dailyRequestBudget = ANONYMOUS_TOKENS_PER_DAY - TOKEN_BUFFER; // non-authed tokens minus buffer

    const String token = authHandler.GetValidToken(configServer.GetStoredString("opensky-id"), configServer.GetStoredString("opensky-secret"));
    if (!token.isEmpty())
        dailyRequestBudget = AUTHED_TOKENS_PER_DAY - TOKEN_BUFFER; // authed tokens minus buffer

    fetchInterval = MS_PER_DAY / dailyRequestBudget;
}

void AircraftManager::Update()
{
    unsigned long now = millis();

    // fetch cycle
    if (now - lastFetch >= fetchInterval) {
        lastFetch = now;

        // auth
        const String token = authHandler.GetValidToken(
            configServer.GetStoredString("opensky-id"),
            configServer.GetStoredString("opensky-secret")
        );

        std::vector<std::pair<String, String>> headers = {};
        if (!token.isEmpty()) headers.push_back({ "Authorization", "Bearer " + token });

        // request
        HttpResult result = http.Get(
            "https://opensky-network.org/api/states/all",
            {
              {"lamin", String(lat - rad)},
              {"lamax", String(lat + rad)},
              {"lomin", String(lon - rad)},
              {"lomax", String(lon + rad)}
            },
            headers
        );

        // If request failed, skip this update
        if (!result.success) {
            Serial.print("[WARN] OpenSky API request failed: ");
            Serial.println(result.errorMessage);
            return;
        }

        // track
        JsonDocument doc;
        deserializeJson(doc, result.response);
        auto aircraft = JsonParser::ParseArray<Aircraft>(doc["states"]);
        now = millis(); // override with post-parse timestamp

        for (auto& ac : aircraft) {
            auto it = trackedAircraft.find(ac.icao24);
            if (it == trackedAircraft.end())
                trackedAircraft.emplace(ac.icao24, TrackedAircraft{ ac, now });
            else
                it->second.Update(ac, now);
        }

        // remove any planes that disappeared from the feed
        for (auto it = trackedAircraft.begin(); it != trackedAircraft.end(); ) {
            bool aircraftPresent = std::any_of(aircraft.begin(), aircraft.end(), [&](const Aircraft& ac) { return ac.icao24 == it->first; });
            if (!aircraftPresent)
                it = trackedAircraft.erase(it);
            else
                ++it;
        }

    }

    FetchRoutes();
}

void AircraftManager::Draw(LGFX_Sprite& backbuffer)
{
    DrawRadarCircles(backbuffer);

    for (auto& [icao, tracked] : trackedAircraft) {
        if (tracked.state.onGround) continue;

        tracked.Tick();
        auto [predLat, predLon] = tracked.GetDisplayPosition();
        auto [x, y] = ProjectCoordinateToScreen(predLat, predLon);

        uint32_t color = GetProximityColor(tracked);

        DrawTrail(backbuffer, tracked, color);

        if (displayInfoText)
            DrawAircraftInfo(backbuffer, x, y, tracked);

        if (tracked.state.category == 7) {
            backbuffer.drawCircle(x, y, 5, color);
            backbuffer.drawCircle(x, y, 4, color);
        } else if (displayTriangles) {
            DrawAircraftTriangle(backbuffer, x, y, tracked, color);
        } else {
            backbuffer.fillCircle(x, y, 3, color);
        }

        DrawSquawkAlert(backbuffer, x, y, tracked);
    }
}

void AircraftManager::DrawRadarCircles(LGFX_Sprite& backbuffer) const
{
    constexpr int CENTRE = SCREEN_SIZE_DIV_2 - 1;
    constexpr int OUTER = SCREEN_SIZE_DIV_2 - 1;

    backbuffer.drawCircle(CENTRE, CENTRE, OUTER, lgfx::color888(0, 200, 0));
    backbuffer.drawCircle(CENTRE, CENTRE, (OUTER / 3) * 2, lgfx::color888(0, 64, 0));
    backbuffer.drawCircle(CENTRE, CENTRE, OUTER / 3, lgfx::color888(0, 32, 0));
}

std::pair<int, int> AircraftManager::ProjectCoordinateToScreen(float predLat, float predLon) const
{
    const float dLon = predLon - lon;
    const float dLat = predLat - lat;

    const float normLon = (dLon + rad) / (2.0f * rad);
    const float normLat = (dLat + rad) / (2.0f * rad);

    const int x = static_cast<int>(normLon * SCREEN_SIZE);
    const int y = static_cast<int>(SCREEN_SIZE - (normLat * SCREEN_SIZE));

    return { x, y };
}

void AircraftManager::DrawAircraftInfo(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked) const
{
    const int lineHeight = tft.fontHeight() + 1;

    backbuffer.setTextSize(1);
    backbuffer.setTextColor(lgfx::color888(0, 128, 0));
    backbuffer.drawString(tracked.state.callsign, x + 5, y + 5);

    int line = 1;
    auto it = routeCache.find(tracked.state.icao24);
    if (it != routeCache.end() && it->second.length() > 0) {
        backbuffer.drawString(it->second, x + 5, y + 5 + lineHeight * line);
        line++;
    }

    backbuffer.drawString(String((int)(tracked.state.velocity * 2.237f)) + "mph", x + 5, y + 5 + lineHeight * line);
    line++;
    backbuffer.drawString(String((int)(tracked.state.baroAltitude * 3.281f)) + "ft", x + 5, y + 5 + lineHeight * line);
}

void AircraftManager::DrawAircraftTriangle(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked, uint32_t color) const
{
    const float angle = radians(tracked.state.trueTrack);
    const float cosA = std::cos(angle);
    const float sinA = std::sin(angle);

    struct Pt { float lx, ly; };
    const Pt nose  = {  0.0f, -5.0f };
    const Pt wingL = { -4.0f,  2.0f };
    const Pt wingR = {  4.0f,  2.0f };
    const Pt tail  = {  0.0f, -1.0f };

    auto rotate = [&](const Pt& p) -> std::pair<float, float> {
        return { x + p.lx * cosA - p.ly * sinA,
                 y + p.lx * sinA + p.ly * cosA };
    };

    auto [nx, ny] = rotate(nose);
    auto [wlx, wly] = rotate(wingL);
    auto [wrx, wry] = rotate(wingR);
    auto [tx, ty] = rotate(tail);

    backbuffer.fillTriangle(nx, ny, wlx, wly, tx, ty, color);
    backbuffer.fillTriangle(nx, ny, wrx, wry, tx, ty, color);
}

float AircraftManager::DistanceBetweenAircraft(const TrackedAircraft& a, const TrackedAircraft& b) const
{
    auto [latA, lonA] = a.GetDisplayPosition();
    auto [latB, lonB] = b.GetDisplayPosition();

    const float dLat = (latB - latA) * 111320.0f;
    const float dLon = (lonB - lonA) * 111320.0f * cos(radians((latA + latB) / 2.0f));
    const float dAlt = a.state.baroAltitude - b.state.baroAltitude;

    return sqrt(dLat * dLat + dLon * dLon + dAlt * dAlt);
}

void AircraftManager::DrawTrail(LGFX_Sprite& backbuffer, const TrackedAircraft& tracked, uint32_t color) const
{
    int count = tracked.trail.size();
    for (int i = 0; i < count; i++) {
        auto [sx, sy] = ProjectCoordinateToScreen(tracked.trail[i].first, tracked.trail[i].second);
        float fade = (float)(i + 1) / (float)(count + 1);
        uint8_t r = ((color >> 16) & 0xFF) * fade * 0.5f;
        uint8_t g = ((color >> 8) & 0xFF) * fade * 0.5f;
        uint8_t b = (color & 0xFF) * fade * 0.5f;
        backbuffer.fillCircle(sx, sy, 1, lgfx::color888(r, g, b));
    }
}

void AircraftManager::DrawSquawkAlert(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked) const
{
    if (tracked.state.squawk == "7700" || tracked.state.squawk == "7600" || tracked.state.squawk == "7500") {
        bool blink = (millis() / 500) % 2 == 0;
        if (blink) {
            backbuffer.drawCircle(x, y, 10, lgfx::color888(255, 0, 0));
            backbuffer.drawCircle(x, y, 11, lgfx::color888(255, 0, 0));
        }

        const char* label = "EMER";
        if (tracked.state.squawk == "7600") label = "NOCOMM";
        if (tracked.state.squawk == "7500") label = "HIJACK";

        backbuffer.setTextSize(1);
        backbuffer.setTextColor(lgfx::color888(255, 0, 0));
        backbuffer.drawString(label, x - 12, y - 16);
    }
}

uint32_t AircraftManager::GetProximityColor(const TrackedAircraft& tracked) const
{
    constexpr float CLOSE_THRESHOLD = 2000.0f;   // 2km — red
    constexpr float MEDIUM_THRESHOLD = 5000.0f;  // 5km — yellow

    float minDist = 999999.0f;

    for (const auto& [icao, other] : trackedAircraft) {
        if (other.state.icao24 == tracked.state.icao24) continue;
        if (other.state.onGround) continue;

        float dist = DistanceBetweenAircraft(tracked, other);
        if (dist < minDist) minDist = dist;
    }

    if (minDist < CLOSE_THRESHOLD)
        return lgfx::color888(255, 0, 0);      // red
    if (minDist < MEDIUM_THRESHOLD)
        return lgfx::color888(255, 200, 0);    // yellow/amber
    return lgfx::color888(0, 255, 0);          // green (default)
}

void AircraftManager::FetchRoutes()
{
    for (auto& [icao, tracked] : trackedAircraft) {
        if (routeCache.count(icao)) continue;

        String callsign = tracked.state.callsign;
        callsign.trim();
        if (callsign.isEmpty()) {
            routeCache[icao] = "";
            continue;
        }

        String prefix = callsign.substring(0, 2);
        String url = "https://vrs-standing-data.adsb.lol/routes/" + prefix + "/" + callsign + ".json";

        HttpResult result = http.Get(url, {}, {});

        if (result.success && result.statusCode == 200) {
            JsonDocument doc;
            deserializeJson(doc, result.response);
            String iata = doc["_airport_codes_iata"] | "";
            if (iata.length() > 0) {
                iata.replace("-", ">");
                routeCache[icao] = iata;
            } else {
                routeCache[icao] = "";
            }
        } else {
            routeCache[icao] = "";
        }
    }
}

