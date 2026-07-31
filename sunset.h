/*
 * ============================================================================
 * sunset.h — ROZHRANÍ PRO VÝPOČET ZÁPADU SLUNCE (NOAA algoritmus)
 * ============================================================================
 */

#ifndef SUNSET_H
#define SUNSET_H

#include <Arduino.h>

/*
 * Inicializace NTP a výpočtu západu slunce.
 * Zavolat v setup() po připojení WiFi.
 */
void sunset_init();

/*
 * Periodická aktualizace — volá se v loop().
 * - Synchronizuje NTP čas (každou hodinu)
 * - Přepočítává sunset
 * - Vyhodnocuje, zda jsme v sunset okně
 */
void sunset_loop();

/*
 * Vrátí aktuální formátovaný čas jako "HH:MM".
 */
String sunset_time_str();

#endif // SUNSET_H
