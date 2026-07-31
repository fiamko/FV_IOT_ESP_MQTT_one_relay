/*
 * ============================================================================
 * wifi_manager.cpp — IMPLEMENTACE WIFI PŘIPOJENÍ S DUÁLNÍ SÍTÍ
 *            Podlahovka 2200W
 * ============================================================================
 *
 * Oproti vířivce přidáno: krátký stisk tlačítka → manual override relé.
 */

#include "variables.h"
#include "wifi_manager.h"
#include "relay_control.h"
#include <DNSServer.h>
#include <WebServer.h>

// ============================================================================
// STATICKÉ PROMĚNNÉ
// ============================================================================

DNSServer g_dns_server;
static WebServer* s_config_server = nullptr;

static enum { WIFI_DISCONNECTED, WIFI_CONNECTING, WIFI_CONNECTED } s_wifi_state = WIFI_DISCONNECTED;
static int8_t s_active_network = -1;
static unsigned long s_reconnect_timer = 0;
static unsigned long s_button_press_start = 0;
static bool s_button_was_pressed = false;
static unsigned long s_led_timer = 0;
static bool s_led_state = false;
static bool s_portal_active = false;
static bool s_button_handled = false;  // aby krátký stisk nevolal relay_button_short_press() opakovaně

// ============================================================================
// INTERNÍ FUNKCE
// ============================================================================

static void set_led(bool on) {
    bool level = LED_ACTIVE_HIGH ? on : !on;
    digitalWrite(LED_PIN, level);
}

// LED blink pattern: kratky zablesk (50ms) v intervalu, jinak zhasnuta
#define LED_FLASH_MS 30

static enum { LED_WAIT, LED_FLASH } s_led_phase = LED_WAIT;

static void led_blink() {
    unsigned long now = millis();
    int interval;

    switch (s_wifi_state) {
        case WIFI_CONNECTING: interval = 250;  break;  // rychle blikani
        case WIFI_CONNECTED:  interval = 2000; break;  // klidny interval
        default:              interval = 2000; break;  // odpojeno
    }

    if (g_wifi_config_mode) interval = 150;

    switch (s_led_phase) {
        case LED_WAIT:
            if (now - s_led_timer > interval) {
                s_led_timer = now;
                s_led_phase = LED_FLASH;
                set_led(true);
            }
            break;
        case LED_FLASH:
            if (now - s_led_timer > LED_FLASH_MS) {
                set_led(false);
                s_led_timer = now;
                s_led_phase = LED_WAIT;
            }
            break;
    }
}

static bool connect_wifi(const char* ssid, const char* password) {
    if (strlen(ssid) == 0) return false;

    Serial.print(F("WiFi: připojuji k "));
    Serial.println(ssid);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(F("."));
        led_blink();
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.print(F("WiFi: připojeno! IP: "));
        Serial.println(WiFi.localIP());
        s_wifi_state = WIFI_CONNECTED;
        return true;
    }

    Serial.println(F(" FAIL"));
    return false;
}

static String get_config_html() {
    String html = F(
        "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8' name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>ESP32 Podlahovka 2200W — WiFi nastavení</title>"
        "<style>"
        "body{font-family:Arial;max-width:500px;margin:20px auto;padding:15px;background:#1a1a2e;color:#eee}"
        "h2{color:#e94560;text-align:center}"
        "fieldset{border:1px solid #333;border-radius:8px;padding:15px;margin-top:15px}"
        "legend{color:#e94560;font-weight:bold}"
        "input{width:100%;padding:10px;margin-top:4px;border:1px solid #333;border-radius:5px;"
        "background:#16213e;color:#eee;font-size:16px;box-sizing:border-box}"
        "input[type=submit]{background:#e94560;color:#fff;border:none;padding:12px;margin-top:20px;"
        "font-size:16px;cursor:pointer}"
        "</style></head><body>"
        "<h2>⚙️ ESP32 Podlahovka 2200W</h2>"
        "<p>Nastavení WiFi připojení (primární + záložní).</p>"
        "<form method='POST' action='/save'>"
        "<fieldset><legend>🔵 Primární síť</legend>"
        "<label>SSID:</label><input name='w1s' maxlength='31' required>"
        "<label>Heslo:</label><input name='w1p' type='password' maxlength='63'>"
        "</fieldset>"
        "<fieldset><legend>🟠 Záložní síť</legend>"
        "<label>SSID:</label><input name='w2s' maxlength='31'>"
        "<label>Heslo:</label><input name='w2p' type='password' maxlength='63'>"
        "</fieldset>"
        "<input type='submit' value='💾 Uložit a restartovat'>"
        "</form></body></html>"
    );
    return html;
}

static void handle_config_save() {
    if (!s_config_server) return;

    String w1s = s_config_server->arg("w1s");
    String w1p = s_config_server->arg("w1p");
    String w2s = s_config_server->arg("w2s");
    String w2p = s_config_server->arg("w2p");

    strncpy(g_settings.wifi1_ssid, w1s.c_str(), 31);
    strncpy(g_settings.wifi1_pass, w1p.c_str(), 63);
    strncpy(g_settings.wifi2_ssid, w2s.c_str(), 31);
    strncpy(g_settings.wifi2_pass, w2p.c_str(), 63);

    g_prefs.putString("w1_ssid", w1s);
    g_prefs.putString("w1_pass", w1p);
    g_prefs.putString("w2_ssid", w2s);
    g_prefs.putString("w2_pass", w2p);

    String html = F(
        "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8' name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Uloženo</title>"
        "<style>"
        "body{font-family:Arial;max-width:400px;margin:50px auto;text-align:center;"
        "background:#1a1a2e;color:#eee}"
        "h2{color:#4ecca3}"
        "</style></head><body>"
        "<h2>✅ Uloženo!</h2>"
        "<p>ESP32 se restartuje a připojí k nové síti.</p>"
        "</body></html>"
    );

    s_config_server->send(200, "text/html; charset=utf-8", html);
    delay(2000);
    ESP.restart();
}

static void handle_captive_portal() {
    if (!s_config_server) return;
    s_config_server->send(200, "text/html; charset=utf-8", get_config_html());
}

// ============================================================================
// VEŘEJNÉ FUNKCE
// ============================================================================

void wifi_start_config_portal() {
    Serial.println(F("WiFi: spouštím konfigurační režim..."));
    g_wifi_config_mode = true;

    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP32-podlahovka2200", nullptr);

    Serial.print(F("WiFi AP: ESP32-podlahovka2200, IP: "));
    Serial.println(WiFi.softAPIP());

    g_dns_server.start(53, "*", WiFi.softAPIP());

    if (s_config_server) delete s_config_server;
    s_config_server = new WebServer(80);

    s_config_server->onNotFound(handle_captive_portal);
    s_config_server->on("/save", HTTP_POST, handle_config_save);
    s_config_server->on("/", HTTP_GET, handle_captive_portal);

    s_config_server->begin();
    s_portal_active = true;

    Serial.println(F("WiFi: konfigurační portál spuštěn."));
}

void wifi_manager_init() {
    pinMode(LED_PIN, OUTPUT);
    set_led(false);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // Načtení WiFi údajů z Preferences
    String w1s = g_prefs.getString("w1_ssid", "");
    String w1p = g_prefs.getString("w1_pass", "");
    String w2s = g_prefs.getString("w2_ssid", "");
    String w2p = g_prefs.getString("w2_pass", "");

    strncpy(g_settings.wifi1_ssid, w1s.c_str(), 31);
    strncpy(g_settings.wifi1_pass, w1p.c_str(), 63);
    strncpy(g_settings.wifi2_ssid, w2s.c_str(), 31);
    strncpy(g_settings.wifi2_pass, w2p.c_str(), 63);

    if (strlen(g_settings.wifi1_ssid) == 0) {
        Serial.println(F("WiFi: žádné údaje — spouštím portál."));
        wifi_start_config_portal();
        return;
    }

    s_wifi_state = WIFI_CONNECTING;
    if (connect_wifi(g_settings.wifi1_ssid, g_settings.wifi1_pass)) {
        s_active_network = 0;
    } else if (strlen(g_settings.wifi2_ssid) > 0) {
        Serial.println(F("WiFi: primární selhalo, zkouším záložní..."));
        if (connect_wifi(g_settings.wifi2_ssid, g_settings.wifi2_pass)) {
            s_active_network = 1;
        }
    }

    if (s_wifi_state != WIFI_CONNECTED) {
        Serial.println(F("WiFi: obě sítě selhaly — spouštím portál."));
        wifi_start_config_portal();
    }
}

void wifi_manager_loop() {
    unsigned long now = millis();

    // Obsluha konfiguračního portálu
    if (s_portal_active && s_config_server) {
        g_dns_server.processNextRequest();
        s_config_server->handleClient();
    }

    // Detekce tlačítka
    bool button_now = (digitalRead(BUTTON_PIN) == LOW);

    if (button_now && !s_button_was_pressed) {
        s_button_press_start = now;
        s_button_handled = false;
    } else if (button_now && s_button_was_pressed && !s_button_handled) {
        unsigned long press_duration = now - s_button_press_start;

        if (press_duration > BUTTON_LONG_PRESS_MS && !g_wifi_config_mode) {
            // Dlouhý stisk (5s) → konfigurační portál
            Serial.println(F("WiFi: dlouhý stisk → portál."));
            wifi_start_config_portal();
            s_button_handled = true;
        }
    } else if (!button_now && s_button_was_pressed && !s_button_handled) {
        // Krátký stisk (uvolnění před 5s) → manual override relé
        unsigned long press_duration = now - s_button_press_start;
        if (press_duration > 100 && press_duration < BUTTON_LONG_PRESS_MS) {
            Serial.println(F("Tlačítko: krátký stisk → manual override."));
            relay_button_short_press();
            s_button_handled = true;
        }
    }
    s_button_was_pressed = button_now;

    // Kontrola WiFi výpadku
    if (!g_wifi_config_mode && s_wifi_state == WIFI_CONNECTED) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println(F("WiFi: spojení ztraceno!"));
            s_wifi_state = WIFI_DISCONNECTED;
            s_reconnect_timer = now;
        }
    }

    // Reconnect logika
    if (!g_wifi_config_mode && s_wifi_state == WIFI_DISCONNECTED) {
        if (now - s_reconnect_timer > 10000) {
            s_reconnect_timer = now;
            s_wifi_state = WIFI_CONNECTING;

            if (s_active_network == 0 && strlen(g_settings.wifi2_ssid) > 0) {
                Serial.println(F("WiFi: zkouším záložní síť..."));
                if (connect_wifi(g_settings.wifi2_ssid, g_settings.wifi2_pass)) {
                    s_active_network = 1;
                    return;
                }
            }

            Serial.println(F("WiFi: zkouším primární síť..."));
            if (connect_wifi(g_settings.wifi1_ssid, g_settings.wifi1_pass)) {
                s_active_network = 0;
                return;
            }

            s_wifi_state = WIFI_DISCONNECTED;
            Serial.println(F("WiFi: obě sítě nedostupné."));
        }
    }

    led_blink();
}
