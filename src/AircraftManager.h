#pragma once

#include <map>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

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

    // display-side copy (read by main loop)
    std::map<String, TrackedAircraft> trackedAircraft;

    struct CachedRoute {
        String route;
        unsigned long fetchedAt;
    };
    std::map<String, CachedRoute> routeCache;

    // network task writes here, main loop swaps in
    SemaphoreHandle_t dataMutex = nullptr;
    std::map<String, TrackedAircraft> stagedAircraft;
    std::map<String, CachedRoute> stagedRoutes;
    volatile bool newDataReady = false;

    bool displayInfoText = true;
    bool displayTriangles = true;
    enum Units { IMPERIAL, METRIC, AVIATION };
    Units units = IMPERIAL;
    bool useAltitudeScaling = true;
    bool displayRangeLabels = true;
    bool displayPingEffect = true;
    float screenRotation = 0.0f;

    unsigned long fetchInterval = 0;
    String openskyId;
    String openskySecret;

    // stats (since last reboot)
    struct Stats {
        int totalPlanesTracked = 0;
        int militaryCount = 0;
        String lastMilitaryCallsign;
        int emergencyCount = 0;
        String lastEmergencyCallsign;
        String lastEmergencySquawk;
        float closestProximity = 999999.0f;
        String closestPair1;
        String closestPair2;
        int peakPlanes = 0;
        unsigned long peakTime = 0;
    } stats;

    ConfigurationWebServer& configServer;
    OpenSkyAuthTokenHandler& authHandler;
    HttpRequestManager& http;
    LGFX& tft;

    void DrawRadarCircles(LGFX_Sprite& backbuffer) const;
    std::pair<int, int> ProjectCoordinateToScreen(float predLat, float predLon) const;
    void DrawAircraftInfo(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked) const;
    void DrawAircraftTriangle(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked, uint32_t color, float scaleOverride = 0.0f) const;
    void DrawSquawkAlert(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked) const;
    uint32_t GetProximityColor(const TrackedAircraft& tracked) const;
    bool IsMilitary(const TrackedAircraft& tracked) const;
    bool IsHelicopter(const TrackedAircraft& tracked) const;
    float DistanceBetweenAircraft(const TrackedAircraft& a, const TrackedAircraft& b) const;

    static void NetworkTaskFunc(void* param);

public:
    AircraftManager(ConfigurationWebServer& config, OpenSkyAuthTokenHandler& auth, HttpRequestManager& httpManager, LGFX& tftGfx)
        : configServer(config), authHandler(auth), http(httpManager), tft(tftGfx)
    {
    }
    ~AircraftManager() = default;

    void Initialise();
    void Update();
    void Draw(LGFX_Sprite& backbuffer, float sweepAngle);
    const Stats& GetStats() const { return stats; }
};