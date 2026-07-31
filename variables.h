/*
 * ============================================================================
 * variables.h — GLOBÁLNÍ PROMĚNNÉ
 *            Podlahovka 2200W — kuchyňské podlahové topení
 * ============================================================================
 *
 * ÚČEL:
 *   Jediné místo pro deklaraci všech globálních proměnných.
 *
 * PRAVIDLA:
 *   - prefix: g_ = globální
 *   - komentář vždy: [KDO ZAPISUJE] [KDO ČTE] [JEDNOTKA] [VÝZNAM]
 *
 * VAZBY:
 *   - config.h: definice pinů a konstant
 *   - Preferences: ukládání/načítání nastavení (namespace "podlahovka2200")
 * ============================================================================
 */

#ifndef VARIABLES_H
#define VARIABLES_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include "config.h"

// ============================================================================
// STAVOVÝ AUTOMAT PODLAHOVKY
// ============================================================================

/*
 * Stavy řízení podlahového topení (jedno relé).
 *
 * PS_OFF          — relé OFF, čeká na enabled=true + příznivé podmínky
 * PS_ACTIVE       — relé ON, hlídá override podmínky
 * PS_MANUAL_ON    — tlačítkem vynucené ZAPNUTO (ignoruje OPI), na dobu manual_override_s
 * PS_MANUAL_OFF   — tlačítkem vynucené VYPNUTO (ignoruje OPI), na dobu manual_override_s
 *
 * Přechody:
 *   OFF → ACTIVE:            enabled=true && !override && !safety
 *   ACTIVE → OFF:            enabled=false
 *   ACTIVE → OFF:            override (výkon/baterie/PV/sunset)
 *   ANY → OFF:               MQTT timeout (safety) — okamžité!
 *   ANY → MANUAL_ON:         krátký stisk tlačítka (je-li OFF nebo ACTIVE)
 *   ANY → MANUAL_OFF:        krátký stisk tlačítka (je-li ACTIVE nebo MANUAL_ON)
 *   MANUAL_ON/OFF → OFF/ACTIVE: timer manual_override_s vypršel → návrat k auto
 */
enum PodlahovkaState {
    PS_OFF,
    PS_ACTIVE,
    PS_MANUAL_ON,
    PS_MANUAL_OFF
};

// ============================================================================
// STRUKTURY
// ============================================================================

/*
 * Stav relé — kdo co požaduje a skutečný stav.
 * Priorita: SAFETY > OVERRIDE > INTELLIGENCE > MQTT_COMMAND
 */
struct RelayState {
    // [ZAPISUJE: relay_control] [ČTE: mqtt_handler, web_setup]
    // Skutečný fyzický stav relé (po vyhodnocení všech override)
    bool actual;

    // [ZAPISUJE: relay_control] [ČTE: web_setup]
    // Důvod poslední změny — pro diagnostiku
    enum Reason {
        NONE,
        MQTT_ON,
        MQTT_OFF,
        OVERRIDE_POWER,
        OVERRIDE_BAT,
        INTELLIGENCE_PV,
        INTELLIGENCE_SUNSET,
        SAFETY_OFF,
        MENIC_STALE,
        MANUAL_BUTTON
    } reason;
};

/*
 * Data z měniče (přijatá z MQTT topicu menic/1/data).
 * Používá se pro bezpečnostní override.
 */
struct MenicData {
    // [ZAPISUJE: mqtt_handler] [ČTE: relay_control, web_setup] [W]
    float output_apparent_power;

    // [ZAPISUJE: mqtt_handler] [ČTE: relay_control, web_setup] [A]
    float battery_discharge_current;

    // [ZAPISUJE: mqtt_handler] [ČTE: relay_control, web_setup] [V]
    float pv_input_voltage;

    // [ZAPISUJE: mqtt_handler] [ČTE: relay_control]
    // Čas posledního příjmu dat z měniče (millis) — pro watchdog
    unsigned long last_update_ms;
};

/*
 * Sunset data — ze sunset.cpp
 */
struct SunsetData {
    // [ZAPISUJE: sunset] [ČTE: relay_control]
    int sunset_hour;          // hodina západu slunce (0-23)
    int sunset_minute;        // minuta západu slunce (0-59)

    // [ZAPISUJE: sunset] [ČTE: relay_control, web_setup]
    bool time_valid;          // true = NTP čas je synchronizován

    // [ZAPISUJE: sunset] [ČTE: relay_control]
    bool is_sunset_window;    // true = aktuálně v okně před západem (do sunset_offset_min minut)
};

/*
 * Data z DS18B20 teplotních čidel (OneWire na GPIO1/TX, Serial.end() v setupu).
 * Dvě čidla: vstup do kotle a výstup z kotle.
 */
struct SensorData {
    // [ZAPISUJE: sensors] [ČTE: mqtt_handler, web_setup] [°C]
    float teplota_vstup;
    float teplota_vystup;

    // [ZAPISUJE: sensors] [ČTE: web_setup]
    bool teplota_error;    // true = čidla neodpovídají / chyba čtení
};

extern SensorData g_sensors;

/*
 * Uživatelské nastavení — ukládáno do Preferences (přežije restart).
 */
struct Settings {
    // === SAFETY ===
    // [ZAPISUJE: web_setup] [ČTE: mqtt_handler] [s]
    int mqtt_timeout;

    // === OVERRIDE ===
    // [ZAPISUJE: web_setup] [ČTE: relay_control] [W]
    int max_vykon;

    // [ZAPISUJE: web_setup] [ČTE: relay_control] [A]
    int vybijeni_bat;

    // [ZAPISUJE: web_setup] [ČTE: relay_control] [V]
    float min_pv_voltage;

    // === INTELLIGENCE ===
    // [ZAPISUJE: web_setup] [ČTE: sunset, relay_control] [min]
    int sunset_offset;

    // [ZAPISUJE: web_setup] [ČTE: sunset] [°]
    float latitude;
    float longitude;
    int utc_offset;

    // === HYSTEREZE ===
    // [ZAPISUJE: web_setup] [ČTE: relay_control] [s]
    int hyst_zapnuto;

    // [ZAPISUJE: web_setup] [ČTE: relay_control] [s]
    int hyst_vypnuto;

    // === MANUAL OVERRIDE ===
    // [ZAPISUJE: web_setup] [ČTE: relay_control] [s]
    int manual_override;

    // === WEB ===
    // [ZAPISUJE: web_setup] [ČTE: web_setup]
    char web_password[32];

    // === WIFI ===
    char wifi1_ssid[32];
    char wifi1_pass[64];
    char wifi2_ssid[32];
    char wifi2_pass[64];
};

// ============================================================================
// GLOBÁLNÍ OBJEKTY — [VLASTNÍK]
// ============================================================================

// [VLASTNÍK: FVE_ovladani_podlahovka_ESP32.ino]
extern Preferences g_prefs;

// [VLASTNÍK: WiFi (ESP32)]
extern WiFiClient g_wifi_client;

// [VLASTNÍK: mqtt_handler]
extern PubSubClient g_mqtt;

// [VLASTNÍK: hlavní .ino — inicializace; ČTE: všechny moduly]
extern RelayState  g_relay;
extern MenicData   g_menic;
extern SunsetData  g_sunset;
extern Settings    g_settings;

// ============================================================================
// GLOBÁLNÍ STAVOVÉ PROMĚNNÉ
// ============================================================================

// [ZAPISUJE: relay_control] [ČTE: mqtt_handler, web_setup, relay_control]
extern PodlahovkaState g_podlahovka_state;

// [ZAPISUJE: mqtt_handler] [ČTE: relay_control, web_setup]
extern bool g_podlahovka_enabled;

// [ZAPISUJE: mqtt_handler] [ČTE: relay_control, web_setup]
extern bool g_mqtt_connected;

// [ZAPISUJE: mqtt_handler] [ČTE: relay_control]
extern unsigned long g_last_mqtt_msg_ms;

// [ZAPISUJE: wifi_manager] [ČTE: web_setup]
extern unsigned long g_last_wifi_change_ms;

// [ZAPISUJE: wifi_manager] [ČTE: hlavní .ino, web_setup]
extern bool g_wifi_config_mode;

// [ZAPISUJE: hlavní .ino] [ČTE: všechny moduly]
extern unsigned long g_loop_ms;

#endif // VARIABLES_H
