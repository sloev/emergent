#include "net/wifi_ap.h"

#include <Preferences.h>
#include <WiFi.h>

namespace {
constexpr const char* kPrefsNamespace = "emergent";
Preferences prefs;
String g_ssid;
String g_password;
}  // namespace

namespace wifi_ap {

void begin(const BoardConfig& board) {
    prefs.begin(kPrefsNamespace, /*readOnly=*/false);
    g_ssid = prefs.getString("ap_ssid", board.ap_ssid);
    g_password = prefs.getString("ap_pass", board.ap_password);
    prefs.end();

    WiFi.mode(WIFI_AP);
    if (g_password.length() >= 8) {
        WiFi.softAP(g_ssid.c_str(), g_password.c_str());
    } else {
        WiFi.softAP(g_ssid.c_str());  // open network
    }

    Serial.print("[wifi] AP \"");
    Serial.print(g_ssid);
    Serial.print("\" up, IP ");
    Serial.println(WiFi.softAPIP());
}

const char* ssid() { return g_ssid.c_str(); }

void save_credentials(const char* ssid, const char* password) {
    prefs.begin(kPrefsNamespace, /*readOnly=*/false);
    prefs.putString("ap_ssid", ssid);
    prefs.putString("ap_pass", password);
    prefs.end();
}

}  // namespace wifi_ap
