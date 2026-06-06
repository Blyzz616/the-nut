#include "wifi_manager.h"
#include "secrets.h"
#include <WiFi.h>
#include <WiFiMulti.h>

static WiFiMulti _wifiMulti;
static bool _connected = false;

void wifi_init() {
    WiFi.mode(WIFI_STA);
    for (auto& ap : WIFI_APS) {
        _wifiMulti.addAP(ap.ssid, ap.pass);
    }
    _connected = (_wifiMulti.run(5000) == WL_CONNECTED);
    if (_connected) Serial.printf("[WiFi] Connected: %s\n", WiFi.SSID().c_str());
    else Serial.println("[WiFi] Not connected");
}

bool wifi_connected() { return WiFi.status() == WL_CONNECTED; }
