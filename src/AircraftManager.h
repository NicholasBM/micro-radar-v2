#pragma once

#include <map>

#include "models/TrackedAircraft.h"
#include "ConfigurationWebServer.h"
#include "OpenSkyAuthTokenHandler.h"
#include "LGFX.h"


class AircraftManager
{
private:
    double lat = 0.0;
    double lon = 0.0;
    double rad = 0.2;
    std::map<String, TrackedAircraft> trackedAircraft;

    struct CachedRoute {
        String route;
        unsigned long fetchedAt;
    };
    std::map<String, CachedRoute> routeCache;

    bool displayInfoText = true;
    bool displayTriangles = true;
    bool useMetricUnits = false;

    unsigned long fetchInterval = 0;
    unsigned long lastFetch = 999999;

    ConfigurationWebServer& configServer;
    OpenSkyAuthTokenHandler& authHandler;
    HttpRequestManager& http;
    LGFX& tft;

    void DrawRadarCircles(LGFX_Sprite& backbuffer) const;
    std::pair<int, int> ProjectCoordinateToScreen(float predLat, float predLon) const;
    void DrawAircraftInfo(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked) const;
    void DrawAircraftTriangle(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked, uint32_t color) const;
    void DrawSquawkAlert(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked) const;
    uint32_t GetProximityColor(const TrackedAircraft& tracked) const;
    float DistanceBetweenAircraft(const TrackedAircraft& a, const TrackedAircraft& b) const;
    void FetchRoutes();

public:
    AircraftManager(ConfigurationWebServer& config, OpenSkyAuthTokenHandler& auth, HttpRequestManager& httpManager, LGFX& tftGfx)
        : configServer(config), authHandler(auth), http(httpManager), tft(tftGfx)
    {
    }
    ~AircraftManager() = default;

    void Initialise();
    void Update();
    void Draw(LGFX_Sprite& backbuffer);
};