#include "ConfigurationWebServer.h"

#include <ESPmDNS.h>

void ConfigurationWebServer::Initialise() {
    MDNS.begin("microradar"); // hostname resolution for ip

    // Serve the config page
    server.on("/", HTTP_GET,
        [&](AsyncWebServerRequest* request) {
            Serial.println("[GET] Handling request to config web server...");

            prefs.begin("config", true);  // true = read only
            String latitude = prefs.getString("latitude", "");
            String longitude = prefs.getString("longitude", "");
            String radius = prefs.getString("radius", "1.0");
            String openskyClientId = prefs.getString("opensky-id", "");
            String openskyClientSecret = prefs.getString("opensky-secret", "");
            prefs.end();

            String html = "<html><body>";
            html += "<h1>Configure Micro Radar</h1>";
            html += "<form action='/save' method='POST'>";
            html += "<label>Latitude: <input name='latitude' value='" + latitude + "'></label>";
            html += "<label>Longitude: <input name='longitude' value='" + longitude + "'></label><br>";
            html += "<label>Radius (in °): <input name='radius' value='" + radius + "'></label><br>";
            html += "<label>OpenSky Client ID: <input name='opensky-id' value='" + openskyClientId + "'></label><br>";
            html += "<label>OpenSky Client Secret: <input name='opensky-secret' value='" + openskyClientSecret + "'></label><br>";
            html += "<input type='submit' value='Save'>";
            html += "</form></body></html>";

            request->send(200, "text/html", html);
        }
    );

    // Handle form submission
    server.on("/save", HTTP_POST,
        [&](AsyncWebServerRequest* request) {
            Serial.println("[POST] Handling form submission to config web server...");

            prefs.begin("config", false);
            if (request->hasParam("latitude", true))
                prefs.putString("latitude", request->getParam("latitude", true)->value());
            if (request->hasParam("longitude", true))
                prefs.putString("longitude", request->getParam("longitude", true)->value());
            if (request->hasParam("radius", true))
                prefs.putString("radius", request->getParam("radius", true)->value());
            if (request->hasParam("opensky-id", true))
                prefs.putString("opensky-id", request->getParam("opensky-id", true)->value());
            if (request->hasParam("opensky-secret", true))
                prefs.putString("opensky-secret", request->getParam("opensky-secret", true)->value());
            prefs.end();

            request->send(200, "text/html", "<h1>Preferences saved - restarting...</h1>");

            delay(1000);
            ESP.restart();
        }
    );

    server.begin();
}

String ConfigurationWebServer::GetStoredString(const char* key)
{
    prefs.begin("config", true);
    String value = prefs.getString(key, "");
    prefs.end();
    return value;
}
