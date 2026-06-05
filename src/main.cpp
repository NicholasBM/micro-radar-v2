#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <cmath>

#include "LGFX.h"
#include "WiFiManagerHelpers.h"
#include "ConfigurationWebServer.h"
#include "HttpRequestManager.h"
#include "models/Aircraft.h"

#define SCREEN_SIZE 240

LGFX tft;
LGFX_Sprite backbuffer(&tft);

WiFiManager wm;
ConfigurationWebServer configServer;
HttpRequestManager http;

void setup()
{
  Serial.begin(115200);
  // delay(1000); // avoids immediate serial output being cut off - uncomment if needed

  // initialise LGFX + screen
  tft.init();
  tft.invertDisplay(true);
  pinMode(3, OUTPUT);
  digitalWrite(3, HIGH);

  backbuffer.createSprite(SCREEN_SIZE, SCREEN_SIZE);

  // establish WiFi connection
  tft.fillScreen(lgfx::color888(0, 0, 0));
  tft.setTextColor(lgfx::color888(0, 255, 0));
  tft.drawCentreString("Connecting to WiFi...", SCREEN_SIZE / 2, SCREEN_SIZE / 2);

  WiFiManagerHelpers::ConfigureWiFiManager(wm);
  wm.autoConnect(WiFiManagerHelpers::WiFiManagerName);

  // begin background server for configuration
  configServer.Initialise();

  // test request
  String response = http.Get(
    "https://opensky-network.org/api/states/all",
    {
      {"lomin", String(-0.320663 - 1.0)},
      {"lomax", String(-0.320663 + 1.0)},
      {"lamin", String(51.454863 - 1.0)},
      {"lamax", String(51.454863 + 1.0)}
    }
  );
  Serial.println(response);

  JsonDocument doc;
  deserializeJson(doc, response);
  auto aircraft = JsonParser::ParseArray<Aircraft>(doc["states"].as<JsonArray>());

  for (auto& ac : aircraft) {
    Serial.println(ac.toString());
  }
}

void loop()
{
  // TEMP: draw example graphics
  backbuffer.fillScreen(lgfx::color888(0, 0, 0)); // clear buffer

  backbuffer.fillCircle(120, 120, 110, lgfx::color888(0, 255, 0));
  backbuffer.fillCircle(120, 120, 60, lgfx::color888(255, 0, 0));
  backbuffer.fillCircle(120, 120, 30, lgfx::color888(0, 0, 255));

  backbuffer.setTextColor(lgfx::color888(255, 255, 255));
  float freq = millis() / 1000.0f,
    mag = 40.0f;
  backbuffer.drawCentreString(
    "Hello, world!",
    SCREEN_SIZE / 2 + (std::sin(freq) * mag),
    SCREEN_SIZE / 2 + (std::cos(freq) * mag)
  );

  backbuffer.pushSprite(0, 0); // present
}