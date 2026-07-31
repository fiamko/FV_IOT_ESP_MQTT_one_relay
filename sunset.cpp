/*
 * ============================================================================
 * sunset.cpp — IMPLEMENTACE VÝPOČTU ZÁPADU SLUNCE + NTP SYNCHRONIZACE
 * ============================================================================
 *
 * POUŽÍVÁ zjednodušený NOAA algoritmus pro výpočet času západu slunce.
 * Přesnost ±1-2 minuty — dostačující pro solární aplikace.
 *
 * Reference: https://edwilliams.org/sunrise_sunset_algorithm.htm
 *
 * VAZBY:
 *   - Čte: g_settings (latitude, longitude, utc_offset, sunset_offset)
 *   - Píše: g_sunset (sunset_hour, sunset_minute, time_valid, is_sunset_window)
 *   - Používá: standardní ESP32 time.h (configTime, getLocalTime)
 * ============================================================================
 */

#include "variables.h"
#include "sunset.h"
#include <time.h>

// ============================================================================
// STATICKÉ PROMĚNNÉ
// ============================================================================

static unsigned long s_last_ntp_sync = 0;
static unsigned long s_last_sunset_calc = 0;
static bool s_ntp_configured = false;

// ============================================================================
// INTERNÍ: NOAA výpočet západu slunce
// ============================================================================

/*
 * Převod stupňů na radiány.
 */
static float deg_to_rad(float deg) {
    return deg * PI / 180.0;
}

/*
 * Převod radiánů na stupně.
 */
static float rad_to_deg(float rad) {
    return rad * 180.0 / PI;
}

/*
 * Vypočítá čas západu slunce pro dnešní den.
 *
 * @param year, month, day — aktuální datum
 * @param lat — zeměpisná šířka [°]
 * @param lon — zeměpisná délka [°]
 * @param utc_offset — časová zóna [h] (1=CET, 2=CEST)
 * @param sunset_h, sunset_m — výstup: hodina a minuta západu
 *
 * Používá zenit 90°50' (oficiální západ slunce s atmosférickou refrakcí).
 */
static void calc_sunset(int year, int month, int day,
                        float lat, float lon, int utc_offset,
                        int &sunset_h, int &sunset_m)
{
    // Den v roce (1. ledna = 1)
    int N1 = floor(275.0 * month / 9.0);
    int N2 = floor((month + 9.0) / 12.0);
    int N3 = (1 + floor((year - 4.0 * floor(year / 4.0) + 2.0) / 3.0));
    int day_of_year = N1 - (N2 * N3) + day - 30;

    // Zenit — oficiální západ slunce (90°50')
    const float zenith = 90.833;

    // Převod zeměpisné délky na hodiny
    float lng_hour = lon / 15.0;

    // Přibližný čas západu
    float t = day_of_year + ((18.0 - lng_hour) / 24.0);

    // Střední anomálie Slunce
    float M = (0.9856 * t) - 3.289;

    // Pravá délka Slunce
    float L = M + (1.916 * sin(deg_to_rad(M)))
              + (0.020 * sin(deg_to_rad(2.0 * M))) + 282.634;
    // Normalizace na 0-360
    while (L < 0)   L += 360.0;
    while (L >= 360) L -= 360.0;

    // Rektascenze Slunce
    float RA = rad_to_deg(atan(0.91764 * tan(deg_to_rad(L))));
    // Normalizace
    while (RA < 0)   RA += 360.0;
    while (RA >= 360) RA -= 360.0;

    // Kvadrant rektascenze
    int L_quad = (int)(floor(L / 90.0)) * 90;
    int RA_quad = (int)(floor(RA / 90.0)) * 90;
    RA = RA + (L_quad - RA_quad);
    RA /= 15.0;  // převod na hodiny

    // Deklinace Slunce
    float sin_dec = 0.39782 * sin(deg_to_rad(L));
    float cos_dec = cos(asin(sin_dec));

    // Hodinový úhel (cosH)
    float cosH = (cos(deg_to_rad(zenith)) - (sin_dec * sin(deg_to_rad(lat))))
               / (cos_dec * cos(deg_to_rad(lat)));

    // Kontrola — polární den/noc
    if (cosH > 1.0) {
        // Slunce nezapadá (polární den)
        sunset_h = 23;
        sunset_m = 59;
        return;
    }
    if (cosH < -1.0) {
        // Slunce nevychází (polární noc)
        sunset_h = 0;
        sunset_m = 0;
        return;
    }

    // Čas západu v hodinách (UTC)
    float H = rad_to_deg(acos(cosH)) / 15.0;
    float T = H + RA - (0.06571 * t) - 6.622;

    // UTC → lokální čas
    T += utc_offset;

    // Normalizace na 0-24
    while (T < 0)  T += 24.0;
    while (T >= 24) T -= 24.0;

    sunset_h = (int)T;
    sunset_m = (int)((T - sunset_h) * 60.0);
}

// ============================================================================
// INTERNÍ: NTP synchronizace
// ============================================================================

static void ntp_sync() {
    if (!s_ntp_configured) {
        configTime(g_settings.utc_offset * 3600, 0, NTP_SERVER, "time.nist.gov");
        s_ntp_configured = true;
        Serial.println(F("NTP: konfigurace provedena."));
    }

    // Počkáme max. 3 sekundy na synchronizaci
    struct tm timeinfo;
    int retry = 0;
    while (!getLocalTime(&timeinfo) && retry < 10) {
        delay(300);
        retry++;
    }

    if (retry < 10) {
        g_sunset.time_valid = true;
    } else {
        g_sunset.time_valid = false;
        Serial.println(F("NTP: selhala synchronizace."));
    }
}

// ============================================================================
// VEŘEJNÉ FUNKCE
// ============================================================================

void sunset_init() {
    g_sunset.sunset_hour = 20;
    g_sunset.sunset_minute = 0;
    g_sunset.time_valid = false;
    g_sunset.is_sunset_window = false;
    s_ntp_configured = false;

    Serial.println(F("Sunset: inicializován (čekám na NTP)."));
}

void sunset_loop() {
    unsigned long now = millis();

    // NTP synchronizace — první pokus hned, pak každou hodinu
    if (!g_sunset.time_valid || (now - s_last_ntp_sync > NTP_SYNC_MS)) {
        if (WiFi.isConnected()) {
            ntp_sync();
            s_last_ntp_sync = now;
        }
    }

    // Výpočet západu slunce — jednou za 5 minut a při změně nastavení
    if (g_sunset.time_valid &&
        (s_last_sunset_calc == 0 || (now - s_last_sunset_calc > 300000UL)))
    {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            int sh, sm;
            calc_sunset(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                        g_settings.latitude, g_settings.longitude,
                        g_settings.utc_offset,
                        sh, sm);

            if (sh != g_sunset.sunset_hour || sm != g_sunset.sunset_minute) {
                g_sunset.sunset_hour = sh;
                g_sunset.sunset_minute = sm;
                Serial.print(F("Sunset: dnes v "));
                Serial.print(sh);
                Serial.print(F(":"));
                if (sm < 10) Serial.print('0');
                Serial.println(sm);
            }

            // Vyhodnocení sunset okna
            int now_minutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
            int sunset_minutes = sh * 60 + sm;
            int threshold_minutes = sunset_minutes - g_settings.sunset_offset;

            g_sunset.is_sunset_window = (now_minutes >= threshold_minutes &&
                                         now_minutes < sunset_minutes);

            s_last_sunset_calc = now;
        }
    }
}

String sunset_time_str() {
    if (!g_sunset.time_valid) return F("--:--");

    String s;
    if (g_sunset.sunset_hour < 10) s += "0";
    s += String(g_sunset.sunset_hour);
    s += ":";
    if (g_sunset.sunset_minute < 10) s += "0";
    s += String(g_sunset.sunset_minute);
    return s;
}
