/*
 * ============================================================================
 * mqtt_handler.cpp — IMPLEMENTACE MQTT KOMUNIKACE
 * ============================================================================
 *
 * VAZBY:
 *   - Subscribe: fve/spotrebice/podlaha2200/set (povel ON/OFF z OPI)
 *                menic/1/data (data z měniče — výkon, baterie, PV)
 *   - Publish:   fve/spotrebice/podlaha2200/stav (status relé, periodicky)
 *                fve/spotrebice/podlaha2200/status (online/offline, Last Will)
 * ============================================================================
 */

#include "variables.h"
#include "mqtt_handler.h"
#include "relay_control.h"
#include <ArduinoJson.h>

// ============================================================================
// STATICKÉ PROMĚNNÉ
// ============================================================================

static unsigned long s_last_reconnect_attempt = 0;
static unsigned long s_last_status_publish = 0;
static StaticJsonDocument<1024> s_json_doc;

// ============================================================================
// INTERNÍ: MQTT CALLBACK
// ============================================================================

static void mqtt_callback(char* topic, byte* payload, unsigned int length) {
    char msg[1024];
    unsigned int len = length < 1023 ? length : 1023;
    memcpy(msg, payload, len);
    msg[len] = '\0';

    Serial.print(F("MQTT ← ["));
    Serial.print(topic);
    Serial.print(F("]: "));
    Serial.println(msg);

    // Reset watchdog — přišla zpráva, spojení žije
    g_last_mqtt_msg_ms = millis();

    DeserializationError err = deserializeJson(s_json_doc, msg);
    if (err) {
        Serial.print(F("MQTT: JSON chyba: "));
        Serial.println(err.c_str());
        return;
    }

    // Topic: fve/spotrebice/podlaha2200/set — povel ON/OFF z OPI
    if (strcmp(topic, MQTT_TOPIC_CMD) == 0) {
        if (s_json_doc.containsKey(JSON_ENABLED)) {
            g_podlahovka_enabled = s_json_doc[JSON_ENABLED].as<bool>();
            Serial.print(F("MQTT: podlahovka2200 enabled = "));
            Serial.println(g_podlahovka_enabled ? F("true (ZAP)") : F("false (OFF)"));
        } else {
            Serial.println(F("MQTT: varování — klíč 'enabled' nenalezen!"));
        }
    }

    // Topic: menic/1/data — data z měniče
    if (strcmp(topic, MQTT_TOPIC_MENIC) == 0) {
        if (s_json_doc.containsKey(JSON_VYKON)) {
            g_menic.output_apparent_power = s_json_doc[JSON_VYKON].as<float>();
        }
        if (s_json_doc.containsKey(JSON_BAT_VYBIJENI)) {
            g_menic.battery_discharge_current = s_json_doc[JSON_BAT_VYBIJENI].as<float>();
        }
        if (s_json_doc.containsKey(JSON_PV_VOLTAGE)) {
            g_menic.pv_input_voltage = s_json_doc[JSON_PV_VOLTAGE].as<float>();
        }
        g_menic.last_update_ms = millis();

        Serial.print(F("MQTT: menic vykon="));
        Serial.print(g_menic.output_apparent_power);
        Serial.print(F("W, bat="));
        Serial.print(g_menic.battery_discharge_current);
        Serial.print(F("A, PV="));
        Serial.print(g_menic.pv_input_voltage);
        Serial.println(F("V"));
    }
}

// ============================================================================
// INTERNÍ: Připojení k MQTT brokeru
// ============================================================================

static bool mqtt_connect() {
    if (!WiFi.isConnected()) return false;

    Serial.print(F("MQTT: připojuji k "));
    Serial.print(MQTT_SERVER);
    Serial.print(F("... "));

    // Last Will: při ztrátě spojení oznámí výpadek
    String will_msg = F("{\"status\":\"offline\"}");

    if (g_mqtt.connect(MQTT_CLIENT_ID,
                       nullptr, nullptr,
                       MQTT_TOPIC_STATUS, 0, true,
                       will_msg.c_str())) {

        Serial.println(F("OK"));

        // Subscribe na řídicí povely z OPI
        g_mqtt.subscribe(MQTT_TOPIC_CMD);
        // Subscribe na data z měniče
        g_mqtt.subscribe(MQTT_TOPIC_MENIC);

        Serial.print(F("MQTT: subscribed "));
        Serial.print(MQTT_TOPIC_CMD);
        Serial.print(F(", "));
        Serial.println(MQTT_TOPIC_MENIC);

        // Oznámení že jsme online
        String online_msg = F("{\"status\":\"online\"}");
        g_mqtt.publish(MQTT_TOPIC_STATUS, online_msg.c_str(), true);

        g_mqtt_connected = true;
        return true;
    }

    Serial.print(F("SELHALO ("));
    Serial.print(g_mqtt.state());
    Serial.println(F(")"));
    g_mqtt_connected = false;
    return false;
}

// ============================================================================
// VEŘEJNÉ FUNKCE
// ============================================================================

void mqtt_handler_init() {
    g_mqtt.setClient(g_wifi_client);
    g_mqtt.setServer(MQTT_SERVER, MQTT_PORT);
    g_mqtt.setCallback(mqtt_callback);
    g_mqtt.setBufferSize(1024);

    Serial.println(F("MQTT: handler inicializován."));
}

void mqtt_handler_loop() {
    unsigned long now = millis();

    if (!WiFi.isConnected()) {
        g_mqtt_connected = false;
        return;
    }

    // Reconnect
    if (!g_mqtt.connected()) {
        g_mqtt_connected = false;
        if (now - s_last_reconnect_attempt > MQTT_RECONNECT_MS) {
            s_last_reconnect_attempt = now;
            mqtt_connect();
        }
        return;
    }

    // Zpracování příchozích zpráv
    g_mqtt.loop();

    // Periodické publikování stavu (každých STATUS_PUBLISH_MS)
    if (now - s_last_status_publish > STATUS_PUBLISH_MS) {
        s_last_status_publish = now;
        mqtt_publish_status();
    }
}

void mqtt_publish_status() {
    if (!g_mqtt.connected()) return;

    s_json_doc.clear();
    s_json_doc[JSON_STATUS] = g_relay.actual ? "ZAP" : "OFF";
    s_json_doc[JSON_VYSTUP] = g_relay.actual ? 1 : 0;
    s_json_doc[JSON_DUVOD]  = relay_reason_str();

    String msg;
    serializeJson(s_json_doc, msg);

    mqtt_publish(MQTT_TOPIC_STAV, msg.c_str());

    s_json_doc.clear();
    s_json_doc["vstup"]  = g_sensors.teplota_vstup;
    s_json_doc["vystup"] = g_sensors.teplota_vystup;
    String tempMsg;
    serializeJson(s_json_doc, tempMsg);
    mqtt_publish(MQTT_TOPIC_TEPLOTA, tempMsg.c_str());

    Serial.print(F("MQTT → ["));
    Serial.print(MQTT_TOPIC_STAV);
    Serial.print(F("]: "));
    Serial.println(msg);
}

void mqtt_publish(const char* topic, const char* message) {
    if (!g_mqtt.connected()) return;
    g_mqtt.publish(topic, message);
}
