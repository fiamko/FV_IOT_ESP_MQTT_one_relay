/*
 * ============================================================================
 * config.h — DEFINICE PINŮ, KONSTANT A VÝCHOZÍCH HODNOT
 *            Podlahovka 2200W — kuchyňské podlahové topení
 * ============================================================================
 *
 * ÚČEL:
 *   Centrální soubor všech hardwarových definic a konstant.
 *
 * VAZBY:
 *   - MQTT broker: 192.168.0.191:1883 (bez autentizace)
 *   - OPI dashboard: fve/spotrebice/podlaha2200/*
 *   - měnič data: menic/1/data (pro override ochranu)
 * ============================================================================
 */

#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// VERZE FIRMWARU
// ============================================================================
#define FIRMWARE_VERSION "1.0.5"

// ============================================================================
// GPIO PINY — OVĚŘENO TESTOVACÍM PROGRAMEM 20.7.2026
// ============================================================================

#define BUTTON_PIN      0     // ✅ OVĚŘENO: Tlačítko — stisk = LOW (INPUT_PULLUP)
#define LED_PIN         4     // ✅ OVĚŘENO: Stavová LED — active HIGH

#define RELAY_PIN       2     // ✅ OVĚŘENO: Relé 30A — active HIGH

// ============================================================================
// ONEWIRE — DS18B20 teplotní čidla na GPIO1 (TX pin) — Serial se po setupu vypne
// ============================================================================
#define ONEWIRE_PIN     1     // GPIO1 = TX pin — po Serial.end() volný pro OneWire
#define TEMP_READ_MS    5000  // [ms] interval čtení teplot (5s)

// ============================================================================
// AKTIVNÍ ÚROVNĚ VÝSTUPŮ — VŠE OVĚŘENO 20.7.2026
// ============================================================================
#define RELAY_ACTIVE_HIGH  true    // ✅ OVĚŘENO
#define LED_ACTIVE_HIGH    true    // ✅ OVĚŘENO

// ============================================================================
// MQTT
// ============================================================================
#define MQTT_SERVER           "192.168.0.191"
#define MQTT_PORT             1883
#define MQTT_CLIENT_ID        "ESP32_podlahovka2200"

// Topicy — Sdílí s OPI dashboard (FV_elektrarna_FIAM_oprava_od_DeepSeek-Coder)
// fve/spotrebice/podlaha2200/set   — již existuje! OPI → ESP: povel ON/OFF
// fve/spotrebice/podlaha2200/stav  — NOVÝ: ESP → MQTT: stav relé
// fve/spotrebice/podlaha2200/status— NOVÝ: ESP → MQTT: online/offline (Last Will)
#define MQTT_TOPIC_CMD      "fve/spotrebice/podlaha2200/set"    // OPI → ESP
#define MQTT_TOPIC_STAV     "fve/spotrebice/podlaha2200/stav"   // ESP → MQTT: stav relé
#define MQTT_TOPIC_STATUS   "fve/spotrebice/podlaha2200/status" // ESP → MQTT: LWT
#define MQTT_TOPIC_MENIC    "menic/1/data"                      // OPI → MQTT: data z měniče
#define MQTT_TOPIC_TEPLOTA  "fve/spotrebice/podlaha2200/teplota" // ESP → MQTT: teploty vstup/vystup

// JSON klíče — příchozí (z OPI)
#define JSON_ENABLED        "enabled"

// JSON klíče — příchozí (z měniče)
#define JSON_VYKON              "output_apparent_power"       // zdánlivý výkon [W]
#define JSON_BAT_VYBIJENI       "battery_discharge_current"   // vybíjecí proud bat [A]
#define JSON_PV_VOLTAGE         "pv_input_voltage"            // napětí z panelů [V]

// JSON klíče — odchozí (stav — podlaha2200/stav)
#define JSON_STATUS         "status"    // "ZAP" / "OFF"
#define JSON_VYSTUP         "vystup"    // 0/1
#define JSON_DUVOD          "duvod"     // textový důvod: "Zapnuto rizenim", "Ztrata spojeni", ...

// ============================================================================
// VÝCHOZÍ HODNOTY NASTAVENÍ (lze změnit přes webovou stránku)
// ============================================================================

// SAFETY — MQTT watchdog
#define DEFAULT_MQTT_TIMEOUT     15      // [s]  výpadek MQTT → vypnout relé
#define MENIC_TIMEOUT_S          30      // [s]  výpadek dat z měniče → vypnout relé

// OVERRIDE — ochrana měniče
#define DEFAULT_MAX_VYKON        3000    // [W]  max. výkon měniče před vypnutím

// OVERRIDE — ochrana baterie
#define DEFAULT_VYBIJENI_BAT     20      // [A]  max. vybíjecí proud baterie

// INTELLIGENCE — PV napětí
#define DEFAULT_MIN_PV_VOLTAGE   120.0   // [V]  min. napětí panelů (pod touto mezí → vypnout)

// INTELLIGENCE — západ slunce
#define DEFAULT_SUNSET_OFFSET    120     // [min] kolik minut před západem vypnout (2h)

// GPS — pro výpočet západu slunce
#define DEFAULT_LATITUDE         49.5    // [°N] ČR
#define DEFAULT_LONGITUDE        16.5    // [°E] ČR
#define DEFAULT_UTC_OFFSET       1       // [h]  CET (zimní čas), +2 pro letní

// HYSTEREZE — minimální doba ve stavu
#define DEFAULT_HYST_ZAPNUTO     20      // [s]  min. doba v zapnutém stavu
#define DEFAULT_HYST_VYPNUTO     20      // [s]  min. doba ve vypnutém stavu

// MANUAL OVERRIDE — tlačítko
#define DEFAULT_MANUAL_OVERRIDE  120     // [s]  doba ručního přepsání (2 min)

// ============================================================================
// WEBOVÉ NASTAVENÍ
// ============================================================================
#define WEB_PORT          80
#define WEB_USERNAME      "admin"
#define DEFAULT_WEB_PASS   "zmenit"   // heslo pro web

// ============================================================================
// ČASOVÁNÍ (ms)
// ============================================================================
#define MQTT_RECONNECT_MS       5000    // interval pokusů o reconnect MQTT
#define STATUS_PUBLISH_MS       5000    // interval publikování stavu do MQTT (5s)
#define BUTTON_LONG_PRESS_MS    5000    // doba pro spuštění WiFi portálu tlačítkem
#define OVERRIDE_RECHECK_MS     4000    // čekání po vypnutí relé při override, než se zkontroluje stav

// ============================================================================
// NTP — synchronizace času (pro sunset výpočet)
// ============================================================================
#define NTP_SERVER          "pool.ntp.org"
#define NTP_SYNC_MS         3600000  // [ms] 1 hodina — jak často synchronizovat čas

#endif // CONFIG_H
