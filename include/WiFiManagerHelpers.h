#include <WiFiManager.h>

namespace WiFiManagerHelpers
{
    constexpr const char* WiFiManagerName = "MicroRadar-Setup";

    static void ConfigureWiFiManager(WiFiManager& wm)
    {
        wm.setTitle("Micro Radar - Setup WiFi");
        wm.setCustomHeadElement("<style>body{background:#111;color:#00ff00;font-family:monospace;} div:has(> a){background:#00ff00;} a:hover{color:#111;}</style>");
    }
}