/*
 * FVE_ovladani_podlahovka_ESP32.ino — v1.0.6 (oprava OneWire rušení, noční klid do půlnoci)
 */
#include "variables.h"
#include "wifi_manager.h"
#include "mqtt_handler.h"
#include "relay_control.h"
#include "sensors.h"
#include "sunset.h"
#include "web_setup.h"
#include <ArduinoOTA.h>

Preferences g_prefs;
WiFiClient   g_wifi_client;
PubSubClient g_mqtt(g_wifi_client);
RelayState      g_relay;
MenicData       g_menic;
SunsetData      g_sunset;
Settings        g_settings;
SensorData      g_sensors;
PodlahovkaState g_podlahovka_state = PS_OFF;
bool            g_podlahovka_enabled = false;
bool            g_mqtt_connected = false;
unsigned long   g_last_mqtt_msg_ms = 0;
unsigned long   g_last_wifi_change_ms = 0;
bool            g_wifi_config_mode = false;
unsigned long   g_loop_ms = 0;

static void load_settings() {
    g_prefs.begin("podlahovka2200", false);
    g_settings.mqtt_timeout   = g_prefs.getInt("mqtt_timeout", DEFAULT_MQTT_TIMEOUT);
    g_settings.max_vykon      = g_prefs.getInt("max_vykon", DEFAULT_MAX_VYKON);
    g_settings.vybijeni_bat   = g_prefs.getInt("vybijeni_bat", DEFAULT_VYBIJENI_BAT);
    g_settings.headroom_limit = g_prefs.getInt("headroom", DEFAULT_HEADROOM_LIMIT);
    g_settings.min_pv_voltage = g_prefs.getFloat("min_pv_v", DEFAULT_MIN_PV_VOLTAGE);
    g_settings.sunset_offset  = g_prefs.getInt("sunset_off", DEFAULT_SUNSET_OFFSET);
    g_settings.latitude       = g_prefs.getFloat("lat", DEFAULT_LATITUDE);
    g_settings.longitude      = g_prefs.getFloat("lon", DEFAULT_LONGITUDE);
    g_settings.utc_offset     = g_prefs.getInt("utc", DEFAULT_UTC_OFFSET);
    g_settings.hyst_zapnuto   = g_prefs.getInt("hyst_on", DEFAULT_HYST_ZAPNUTO);
    g_settings.hyst_vypnuto   = g_prefs.getInt("hyst_off", DEFAULT_HYST_VYPNUTO);
    g_settings.manual_override = g_prefs.getInt("man_over", DEFAULT_MANUAL_OVERRIDE);
    String pass = g_prefs.getString("web_pass", DEFAULT_WEB_PASS);
    strncpy(g_settings.web_password, pass.c_str(), 31);
}

void setup() {
    Serial.begin(115200); delay(1000);
    Serial.println(F("\nFVE podlahovka 2200W v1.0.5"));
    load_settings();
    relay_control_init();
    wifi_manager_init();
    if (WiFi.isConnected() && !g_wifi_config_mode) {
        ArduinoOTA.setHostname("ESP32-podlahovka2200");
        ArduinoOTA.onStart([]() { });
        ArduinoOTA.onEnd([]() { });
        ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {});
        ArduinoOTA.onError([](ota_error_t e) {});
        ArduinoOTA.begin();
        sunset_init();
        mqtt_handler_init();
        web_setup_init();
    }
    g_last_mqtt_msg_ms = millis();
    Serial.println(F("Setup done. Releasing Serial for OneWire..."));
    Serial.flush();
    delay(50);
    Serial.end();        // Uvolni GPIO1 (TX) a GPIO3 (RX) — od teď žádné Serial.print()!
    delay(100);          // Nech UART doběhnout
    sensors_init();      // OneWire na GPIO1 — teď už Serial nepřekáží
}

void loop() {
    g_loop_ms = millis();
    wifi_manager_loop();
    if (WiFi.isConnected()) ArduinoOTA.handle();
    if (g_wifi_config_mode) { delay(50); return; }
    sensors_loop();
    sunset_loop();
    web_setup_loop();
    mqtt_handler_loop();
    relay_control_loop();
    delay(10);
}