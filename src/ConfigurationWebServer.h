#pragma once

#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <functional>

class ConfigurationWebServer {
private:
    AsyncWebServer server;
    Preferences prefs;

public:
    ConfigurationWebServer() : server(80), prefs() {}
    ConfigurationWebServer(int port) : server(port), prefs() {}

    std::function<String(const String&)> statsProvider = nullptr;

    void Initialise();
    [[nodiscard]] const String GetStoredString(const char* key);
};