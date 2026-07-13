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
    const String units = configServer.GetStoredString("units");
    const String altSize = configServer.GetStoredString("altsize");
    if (!renderText.isEmpty()) displayInfoText = renderText == "true" ? true : false;
    if (!renderTris.isEmpty()) displayTriangles = renderTris == "true" ? true : false;
    const String rangeLabels = configServer.GetStoredString("rangelabels");
    if (!units.isEmpty()) useMetricUnits = units == "metric";
    if (!altSize.isEmpty()) useAltitudeScaling = altSize == "true";
    const String rotation = configServer.GetStoredString("rotation");
    if (!rangeLabels.isEmpty()) displayRangeLabels = rangeLabels == "true";
    if (!rotation.isEmpty()) screenRotation = rotation.toFloat();

    // calculate how often we can call OpenSky API before being rate limited
    constexpr int MS_PER_DAY = 24 * 60 * 60 * 1000;
    constexpr int ANONYMOUS_TOKENS_PER_DAY = 400;
    constexpr int AUTHED_TOKENS_PER_DAY = 4000;
    constexpr int TOKEN_BUFFER = 3;
    int dailyRequestBudget = ANONYMOUS_TOKENS_PER_DAY - TOKEN_BUFFER;

    const String token = authHandler.GetValidToken(configServer.GetStoredString("opensky-id"), configServer.GetStoredString("opensky-secret"));
    if (!token.isEmpty())
        dailyRequestBudget = AUTHED_TOKENS_PER_DAY - TOKEN_BUFFER;

    fetchInterval = MS_PER_DAY / dailyRequestBudget;

    // start background network task
    dataMutex = xSemaphoreCreateMutex();
    xTaskCreate(NetworkTaskFunc, "network", 10240, this, 1, nullptr);
}

void AircraftManager::Update()
{
    // swap in new data from background task if available
    if (newDataReady && xSemaphoreTake(dataMutex, 0) == pdTRUE) {
        trackedAircraft = stagedAircraft;
        routeCache = stagedRoutes;
        newDataReady = false;
        xSemaphoreGive(dataMutex);
    }

    // tick all aircraft for position interpolation
    for (auto& [icao, tracked] : trackedAircraft) {
        tracked.Tick();
    }
}

void AircraftManager::NetworkTaskFunc(void* param)
{
    AircraftManager* self = static_cast<AircraftManager*>(param);

    // local working copies
    std::map<String, TrackedAircraft> localAircraft;
    std::map<String, CachedRoute> localRoutes;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(self->fetchInterval));

        // auth
        const String token = self->authHandler.GetValidToken(
            self->configServer.GetStoredString("opensky-id"),
            self->configServer.GetStoredString("opensky-secret")
        );

        std::vector<std::pair<String, String>> headers = {};
        if (!token.isEmpty()) headers.push_back({ "Authorization", "Bearer " + token });

        // fetch aircraft
        HttpResult result = self->http.Get(
            "https://opensky-network.org/api/states/all",
            {
              {"lamin", String(self->lat - self->rad)},
              {"lamax", String(self->lat + self->rad)},
              {"lomin", String(self->lon - self->rad)},
              {"lomax", String(self->lon + self->rad)}
            },
            headers
        );

        if (!result.success) continue;

        JsonDocument doc;
        deserializeJson(doc, result.response);
        auto aircraft = JsonParser::ParseArray<Aircraft>(doc["states"]);
        unsigned long now = millis();

        // update local tracked aircraft
        for (auto& ac : aircraft) {
            auto it = localAircraft.find(ac.icao24);
            if (it == localAircraft.end())
                localAircraft.emplace(ac.icao24, TrackedAircraft{ ac, now });
            else
                it->second.Update(ac, now);
        }

        // remove disappeared planes
        for (auto it = localAircraft.begin(); it != localAircraft.end(); ) {
            bool present = std::any_of(aircraft.begin(), aircraft.end(),
                [&](const Aircraft& ac) { return ac.icao24 == it->first; });
            if (!present)
                it = localAircraft.erase(it);
            else
                ++it;
        }

        // fetch one route per plane that needs it
        constexpr unsigned long RETRY_INTERVAL = 300000;
        for (auto& [icao, tracked] : localAircraft) {
            auto it = localRoutes.find(icao);
            if (it != localRoutes.end()) {
                if (it->second.route.length() > 0) continue;
                if (now - it->second.fetchedAt < RETRY_INTERVAL) continue;
            }

            String callsign = tracked.state.callsign;
            callsign.trim();
            if (callsign.isEmpty()) {
                localRoutes[icao] = { "", now };
                continue;
            }

            String prefix = callsign.substring(0, 2);
            String url = "https://vrs-standing-data.adsb.lol/routes/" + prefix + "/" + callsign + ".json";

            HttpResult routeResult = self->http.Get(url, {}, {});

            if (routeResult.success && routeResult.statusCode == 200) {
                JsonDocument rdoc;
                deserializeJson(rdoc, routeResult.response);
                String iata = rdoc["_airport_codes_iata"] | "";
                if (iata.length() > 0) {
                    JsonArray airports = rdoc["_airports"];
                    if (airports.size() >= 2) {
                        float originLat = airports[0]["lat"] | 0.0f;
                        float originLon = airports[0]["lon"] | 0.0f;
                        float destLat = airports[airports.size() - 1]["lat"] | 0.0f;
                        float destLon = airports[airports.size() - 1]["lon"] | 0.0f;

                        float headingRad = radians(tracked.state.trueTrack);
                        float hdgX = sin(headingRad);
                        float hdgY = cos(headingRad);

                        float toOriginX = originLon - tracked.state.longitude;
                        float toOriginY = originLat - tracked.state.latitude;
                        float toDestX = destLon - tracked.state.longitude;
                        float toDestY = destLat - tracked.state.latitude;

                        float dotOrigin = hdgX * toOriginX + hdgY * toOriginY;
                        float dotDest = hdgX * toDestX + hdgY * toDestY;

                        int sep = iata.indexOf('-');
                        String from = iata.substring(0, sep);
                        String to = iata.substring(sep + 1);

                        if (dotOrigin > dotDest)
                            localRoutes[icao] = { to + ">" + from, now };
                        else
                            localRoutes[icao] = { from + ">" + to, now };
                    } else {
                        iata.replace("-", ">");
                        localRoutes[icao] = { iata, now };
                    }
                } else {
                    localRoutes[icao] = { "", now };
                }
            } else {
                localRoutes[icao] = { "", now };
            }
        }

        // stage results for main loop to pick up
        xSemaphoreTake(self->dataMutex, portMAX_DELAY);
        self->stagedAircraft = localAircraft;
        self->stagedRoutes = localRoutes;
        self->newDataReady = true;
        xSemaphoreGive(self->dataMutex);
    }
}

void AircraftManager::Draw(LGFX_Sprite& backbuffer, float sweepAngle)
{
    DrawRadarCircles(backbuffer);

    for (auto& [icao, tracked] : trackedAircraft) {
        if (tracked.state.onGround) continue;

        tracked.Tick();
        auto [predLat, predLon] = tracked.GetDisplayPosition();
        auto [x, y] = ProjectCoordinateToScreen(predLat, predLon);

        uint32_t color = IsMilitary(tracked)
            ? lgfx::color888(0, 100, 255)
            : GetProximityColor(tracked);

        // pulse ring: expands and fades after sweep hits
        float pdx = x - (SCREEN_SIZE_DIV_2 - 1);
        float pdy = y - (SCREEN_SIZE_DIV_2 - 1);
        float planeAngle = atan2(pdy, pdx);
        if (planeAngle < 0) planeAngle += 2.0f * PI;

        float angleDiff = (sweepAngle + 0.5f) - planeAngle;
        if (angleDiff < 0) angleDiff += 2.0f * PI;
        if (angleDiff > 2.0f * PI) angleDiff -= 2.0f * PI;

        if (angleDiff < 1.2f) {
            float t = angleDiff / 1.2f;
            int radius = 4 + (int)(t * 14);
            uint8_t alpha = (uint8_t)((1.0f - t) * 255);
            backbuffer.drawCircle(x, y, radius, lgfx::color888(0, alpha, 0));
            backbuffer.drawCircle(x, y, radius - 1, lgfx::color888(0, alpha / 2, 0));

            if (angleDiff < 0.2f) {
                DrawAircraftTriangle(backbuffer, x, y, tracked, lgfx::color888(255, 255, 255), 1.3f);
            }
        }

        if (displayInfoText)
            DrawAircraftInfo(backbuffer, x, y, tracked);

        if (tracked.state.category == 7) {
            float altFactor = 1.0f;
            if (useAltitudeScaling) {
                altFactor = 1.6f - (tracked.state.baroAltitude / 12000.0f) * 0.9f;
                altFactor = max(0.7f, min(1.6f, altFactor));
            }
            int r = (int)(5.0f * altFactor);
            backbuffer.drawCircle(x, y, r, color);
            backbuffer.drawCircle(x, y, r - 1, color);
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

    // compass labels
    float angle = radians(screenRotation);
    float cosA = cos(angle);
    float sinA = sin(angle);

    struct Compass { const char* label; float dx; float dy; };
    Compass points[] = {
        { "N",  0.0f, -1.0f },
        { "S",  0.0f,  1.0f },
        { "E",  1.0f,  0.0f },
        { "W", -1.0f,  0.0f },
    };

    backbuffer.setTextSize(1);
    backbuffer.setTextColor(lgfx::color888(0, 150, 0));
    for (auto& p : points) {
        float rx = p.dx * cosA - p.dy * sinA;
        float ry = p.dx * sinA + p.dy * cosA;
        int px = CENTRE + (int)(rx * (OUTER - 8));
        int py = CENTRE + (int)(ry * (OUTER - 8));
        backbuffer.drawCentreString(p.label, px, py - tft.fontHeight() / 2, 1);
    }

    if (displayRangeLabels) {
        float fullDistKm = rad * 111.32f;
        backbuffer.setTextSize(1);
        backbuffer.setTextColor(lgfx::color888(0, 64, 0));

        float angle = radians(screenRotation);
        float cosA = cos(angle);
        float sinA = sin(angle);

        for (int i = 1; i <= 2; i++) {
            float distKm = fullDistKm * i / 3.0f;
            String label;
            if (useMetricUnits)
                label = String((int)distKm) + " km";
            else
                label = String((int)(distKm * 0.6214f)) + " mi";

            int ringR = OUTER * i / 3;
            float lx = 2.0f;
            float ly = (float)ringR;
            float rx = lx * cosA - ly * sinA;
            float ry = lx * sinA + ly * cosA;
            backbuffer.drawString(label, CENTRE + (int)rx, CENTRE + (int)ry - tft.fontHeight());
        }
    }
}

std::pair<int, int> AircraftManager::ProjectCoordinateToScreen(float predLat, float predLon) const
{
    const float lonScale = cos(radians(lat));
    const float dLon = (predLon - lon) * lonScale;
    const float dLat = predLat - lat;

    float normX = dLon / rad;
    float normY = -dLat / rad;

    if (screenRotation != 0.0f) {
        float angle = radians(screenRotation);
        float cosA = cos(angle);
        float sinA = sin(angle);
        float rx = normX * cosA - normY * sinA;
        float ry = normX * sinA + normY * cosA;
        normX = rx;
        normY = ry;
    }

    const int x = static_cast<int>((normX + 1.0f) * 0.5f * SCREEN_SIZE);
    const int y = static_cast<int>((normY + 1.0f) * 0.5f * SCREEN_SIZE);

    return { x, y };
}

void AircraftManager::DrawAircraftInfo(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked) const
{
    const int lineHeight = tft.fontHeight() + 1;

    backbuffer.setTextSize(1);
    backbuffer.setTextColor(lgfx::color888(0, 200, 0));
    backbuffer.drawString(tracked.state.callsign, x + 5, y + 5);

    backbuffer.setTextColor(lgfx::color888(0, 128, 0));
    int line = 1;
    auto it = routeCache.find(tracked.state.icao24);
    if (it != routeCache.end() && it->second.route.length() > 0) {
        backbuffer.drawString(it->second.route, x + 5, y + 5 + lineHeight * line);
        line++;
    }

    int speed, alt;
    if (useMetricUnits) {
        speed = (int)(tracked.state.velocity * 3.6f);
        alt = max(0, (int)tracked.state.baroAltitude);
        backbuffer.drawString(String(speed) + " km/h", x + 5, y + 5 + lineHeight * line);
        line++;
        backbuffer.drawString(String(alt) + " m", x + 5, y + 5 + lineHeight * line);
    } else {
        speed = (int)(tracked.state.velocity * 2.237f);
        alt = max(0, (int)(tracked.state.baroAltitude * 3.281f));
        backbuffer.drawString(String(speed) + " mph", x + 5, y + 5 + lineHeight * line);
        line++;
        backbuffer.drawString(String(alt) + " ft", x + 5, y + 5 + lineHeight * line);
    }
}

void AircraftManager::DrawAircraftTriangle(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked, uint32_t color, float scaleOverride) const
{
    float altFactor = 1.0f;
    if (useAltitudeScaling) {
        altFactor = 1.6f - (tracked.state.baroAltitude / 12000.0f) * 0.9f;
        altFactor = max(0.7f, min(1.6f, altFactor));
    }
    if (scaleOverride > 0.0f) altFactor *= scaleOverride;

    const float angle = radians(tracked.state.trueTrack);
    const float cosA = std::cos(angle);
    const float sinA = std::sin(angle);

    struct Pt { float lx, ly; };
    const Pt nose  = {  0.0f, -5.0f * altFactor };
    const Pt wingL = { -4.0f * altFactor,  2.0f * altFactor };
    const Pt wingR = {  4.0f * altFactor,  2.0f * altFactor };
    const Pt tail  = {  0.0f, -1.0f * altFactor };

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

bool AircraftManager::IsMilitary(const TrackedAircraft& tracked) const
{
    String callsign = tracked.state.callsign;
    callsign.trim();
    if (callsign.isEmpty()) return false;

    static const char* prefixes[] = {
        "RRR",   // RAF (UK)
        "RFR",   // French Air Force
        "GAF",   // German Air Force
        "IAM",   // Italian Air Force
        "AME",   // Spanish Air Force
        "BAF",   // Belgian Air Force
        "NAF",   // Netherlands Air Force
        "DAF",   // Danish Air Force
        "NAF",   // Norwegian Air Force
        "SVF",   // Swedish Air Force
        "FAF",   // Finnish Air Force
        "PLF",   // Polish Air Force
        "HUF",   // Hungarian Air Force
        "CFR",   // Canadian Forces
        "RCH",   // US Air Mobility Command
        "EVIL",  // US Air Force callsign
        "DUKE",  // US Air Force
        "REACH", // US Air Mobility Command
        "NATO",  // NATO
        "ASCOT", // RAF transport
    };

    for (const char* prefix : prefixes) {
        if (callsign.startsWith(prefix))
            return true;
    }

    return false;
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


