/*
 * ============================================================================
 * sensors.cpp — IMPLEMENTACE DUÁLNÍHO DS18B20 ČTENÍ (vstup/výstup kotle)
 *            Podlahovka 2200W
 * ============================================================================
 *
 * POUŽÍVÁ raw OneWire scan (nespoléhá na DallasTemperature::getDeviceCount(),
 * která na ESP32 nespolehlivě vrací 0).
 *
 * Dvě čidla na GPIO1 (TX pin — Serial se předtím vypne přes Serial.end()), rozlišená adresou:
 *   - "vstup"  — první nalezené čidlo (uloženo v Preferences jako "ds18_a1")
 *   - "vystup" — druhé nalezené čidlo (uloženo v Preferences jako "ds18_a2")
 */

#include "variables.h"
#include "sensors.h"
#include <OneWire.h>
#include <DallasTemperature.h>

// ============================================================================
// HARDWARE OBJEKTY
// ============================================================================

OneWire g_onewire(ONEWIRE_PIN);
DallasTemperature g_dallas(&g_onewire);

// ============================================================================
// STATICKÉ PROMĚNNÉ
// ============================================================================

static DeviceAddress s_addr1, s_addr2;  // adresy dvou čidel
static bool s_valid1 = false;           // čidlo 1 nalezeno
static bool s_valid2 = false;           // čidlo 2 nalezeno
static unsigned long s_last_read = 0;
static int s_active_count = 0;          // kolik čidel celkem funguje

// ============================================================================
// INTERNÍ: Uložení/načtení adres z Preferences
// ============================================================================

static void save_address(const char* key, const DeviceAddress& addr) {
    g_prefs.putBytes(key, addr, 8);
}

static bool load_address(const char* key, DeviceAddress& addr) {
    size_t len = g_prefs.getBytes(key, addr, 8);
    if (len != 8) return false;

    bool all_zero = true;
    for (int i = 0; i < 8; i++) {
        if (addr[i] != 0) { all_zero = false; break; }
    }
    return !all_zero;
}

// ============================================================================
// INTERNÍ: Validace a čtení teploty z jednoho čidla
// ============================================================================

static float read_temp(const DeviceAddress& addr, bool valid) {
    if (!valid) return -127.0;

    float t = g_dallas.getTempC(addr);
    if (t == DEVICE_DISCONNECTED_C || t < -55.0 || t > 125.0) {
        return -127.0;  // signál chyby
    }
    return t;
}

// ============================================================================
// INTERNÍ: Sken sběrnice — zjistí kolik čidel a jejich adresy
// ============================================================================

static int scan_bus(DeviceAddress* out1, DeviceAddress* out2) {
    uint8_t addr[8];
    int count = 0;

    g_onewire.reset_search();
    delay(10);

    while (g_onewire.search(addr) && count < 2) {
        if (out1 && count == 0) memcpy(out1, addr, 8);
        if (out2 && count == 1) memcpy(out2, addr, 8);
        count++;
    }

    return count;
}

// ============================================================================
// VEŘEJNÉ FUNKCE
// ============================================================================

void sensors_init() {
    g_sensors.teplota_vstup = 0;
    g_sensors.teplota_vystup = 0;
    g_sensors.teplota_error = true;  // pesimisticky — dokud se neprokáže opak
    s_active_count = 0;
    s_valid1 = false;
    s_valid2 = false;

    g_dallas.begin();

    bool loaded1 = load_address("ds18_a1", s_addr1);
    bool loaded2 = load_address("ds18_a2", s_addr2);

    if (loaded1 && loaded2 && g_dallas.validAddress(s_addr1) &&
                               g_dallas.validAddress(s_addr2)) {
        g_onewire.reset_search();
        DeviceAddress found1, found2;
        int found = scan_bus(&found1, &found2);

        if (found >= 2) {
            s_valid1 = s_valid2 = true;
            s_active_count = 2;
            g_sensors.teplota_error = false;
        }
    } else {
        int found = scan_bus(&s_addr1, &s_addr2);

        if (found >= 2) {
            save_address("ds18_a1", s_addr1);
            save_address("ds18_a2", s_addr2);
            s_valid1 = s_valid2 = true;
            s_active_count = 2;
            g_sensors.teplota_error = false;
        } else if (found == 1) {
            save_address("ds18_a1", s_addr1);
            s_valid1 = true;
            s_active_count = 1;
        }
    }

    // První čtení — DallasTemperature potřebuje requestTemperatures() před getTempC()
    if (s_active_count > 0) {
        g_dallas.requestTemperatures();
    }
}

void sensors_loop() {
    unsigned long now = millis();

    if (s_active_count == 0) return;

    if (now - s_last_read >= TEMP_READ_MS) {
        s_last_read = now;

        g_dallas.requestTemperatures();  // spustí měření na všech čidlech

        float t1 = read_temp(s_addr1, s_valid1);
        float t2 = read_temp(s_addr2, s_valid2);

        bool err = false;
        if (t1 <= -127.0) { err = true; } else { g_sensors.teplota_vstup = t1; }
        if (t2 <= -127.0) { err = true; } else { g_sensors.teplota_vystup = t2; }
        g_sensors.teplota_error = err;

        if (!err) {
            Serial.print(F("DS18B20: vstup="));
            Serial.print(g_sensors.teplota_vstup, 1);
            Serial.print(F("°C, vystup="));
            Serial.print(g_sensors.teplota_vystup, 1);
            Serial.println(F("°C"));
        }
    }
}

void sensors_scan() {
    Serial.println(F("--- DS18B20 RAW ONEWIRE SKEN ---"));

    DeviceAddress a1, a2;
    int count = scan_bus(&a1, &a2);

    for (int i = 0; i < count && i < 2; i++) {
        DeviceAddress* a = (i == 0) ? &a1 : &a2;
        Serial.print(F("  Cidlo #")); Serial.print(i + 1); Serial.print(F(": "));
        for (int j = 0; j < 8; j++) {
            if ((*a)[j] < 0x10) Serial.print("0");
            Serial.print((*a)[j], HEX);
            if (j < 7) Serial.print(":");
        }
        Serial.println();
    }

    Serial.print(F("Celkem nalezeno: ")); Serial.print(count); Serial.println(F(" cidel."));

    if (count == 0) {
        Serial.println(F("Zkontroluj:"));
        Serial.println(F("  1. Napajeni 3.3V mezi VCC a GND"));
        Serial.println(F("  2. Pull-up 4.7kΩ mezi DATA a 3.3V"));
        Serial.print(F("  3. DATA na GPIO")); Serial.print(ONEWIRE_PIN);
        Serial.println(F(" (TX pin - po Serial.end() volny pro OneWire)"));
    } else {
        // Ulož nalezené adresy a reinicializuj
        if (count >= 1) { memcpy(s_addr1, a1, 8); save_address("ds18_a1", a1); s_valid1 = true; }
        if (count >= 2) { memcpy(s_addr2, a2, 8); save_address("ds18_a2", a2); s_valid2 = true; }
        s_active_count = count;
        g_sensors.teplota_error = (count < 2);
        g_dallas.begin();
        Serial.println(F("Cidla ulozena do Preferences. Restart neni nutny."));
    }

    Serial.println(F("--- KONEC SKENU ---"));
}
