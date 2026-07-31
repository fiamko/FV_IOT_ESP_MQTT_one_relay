/*
 * ============================================================================
 * relay_control.cpp — IMPLEMENTACE ŘÍZENÍ RELÉ (STAVOVÝ AUTOMAT)
 * ============================================================================
 *
 * ÚČEL:
 *   Řízení jednoho relé podlahového topení podle stavového automatu.
 *
 *   STAVY (PodlahovkaState):
 *     PS_OFF         — relé OFF, čeká na enabled=true + podmínky
 *     PS_ACTIVE      — relé ON, hlídá override
 *     PS_MANUAL_ON   — tlačítkem vynucené ON (ignoruje OPI)
 *     PS_MANUAL_OFF  — tlačítkem vynucené OFF (ignoruje OPI)
 *
 *   PRIORITY:
 *     SAFETY (MQTT timeout) > OVERRIDE_POWER > OVERRIDE_BAT >
 *     INTELLIGENCE_PV > INTELLIGENCE_SUNSET > MQTT_COMMAND
 *
 *   BEZPEČNOST:
 *     - Bez enabled=true NIKDY nezapínat relé (kromě MANUAL_ON)
 *     - Vypnout relé lze vždy
 *     - MQTT watchdog: při výpadku delším než mqtt_timeout → okamžité OFF
 *
 * VAZBY:
 *   - Čte:  g_podlahovka_enabled, g_podlahovka_state, g_menic, g_sunset,
 *           g_settings, g_last_mqtt_msg_ms
 *   - Píše: g_relay.actual, g_relay.reason, g_podlahovka_state
 *   - Ovládá: GPIO RELAY_PIN
 * ============================================================================
 */

#include "variables.h"
#include "relay_control.h"

// ============================================================================
// STATICKÉ PROMĚNNÉ
// ============================================================================

static unsigned long s_state_timer = 0;       // časovač přechodů
static bool s_safety_active = false;           // safety-off aktivní
static bool s_menic_stale_active = false;      // menic data timeout aktivní
static unsigned long s_manual_timer = 0;       // časovač manuálního override
static PodlahovkaState s_prev_auto_state = PS_OFF;  // stav před manuálním override
static unsigned long s_last_change_ms = 0;     // čas poslední změny relé (pro hysterezi)

// ============================================================================
// INTERNÍ FUNKCE
// ============================================================================

/*
 * Fyzicky nastaví relé (pouze při změně).
 */
static void set_relay(bool on) {
    if (g_relay.actual == on) return;

    bool level = RELAY_ACTIVE_HIGH ? on : !on;
    digitalWrite(RELAY_PIN, level);
    g_relay.actual = on;
    s_last_change_ms = millis();

    Serial.print(F("Relé → "));
    Serial.println(on ? F("ON") : F("OFF"));
}

/*
 * Vyhodnotí všechny override/intelligence podmínky.
 * @param now aktuální čas (millis)
 * @return důvod override, nebo NONE pokud vše OK
 */
static RelayState::Reason check_override(unsigned long now) {
    // OVERRIDE — výkon měniče (jen pokud máme data)
    if (g_menic.last_update_ms > 0) {
        if (g_menic.output_apparent_power > g_settings.max_vykon) {
            return RelayState::OVERRIDE_POWER;
        }
        if (g_menic.battery_discharge_current > g_settings.vybijeni_bat) {
            return RelayState::OVERRIDE_BAT;
        }
        // INTELLIGENCE — PV napětí
        if (g_menic.pv_input_voltage > 0 &&
            g_menic.pv_input_voltage < g_settings.min_pv_voltage) {
            return RelayState::INTELLIGENCE_PV;
        }
    }

    // INTELLIGENCE — západ slunce
    if (g_sunset.time_valid && g_sunset.is_sunset_window) {
        return RelayState::INTELLIGENCE_SUNSET;
    }

    return RelayState::NONE;
}

/*
 * Vyhodnotí MQTT watchdog.
 */
static bool is_mqtt_timeout(unsigned long now) {
    unsigned long elapsed = now - g_last_mqtt_msg_ms;
    return elapsed > (g_settings.mqtt_timeout * 1000UL);
}

static bool is_menic_timeout(unsigned long now) {
    if (g_menic.last_update_ms == 0) return false;  // ještě nepřišla žádná data
    unsigned long elapsed = now - g_menic.last_update_ms;
    return elapsed > (MENIC_TIMEOUT_S * 1000UL);
}

/*
 * Zjistí, zda uplynula minimální doba v aktuálním stavu (hystereze).
 */
static bool hystereze_ok(unsigned long now) {
    if (g_relay.actual) {
        // relé je zapnuté — hlídáme hyst_zapnuto
        return (now - s_last_change_ms) >= (g_settings.hyst_zapnuto * 1000UL);
    } else {
        // relé je vypnuté — hlídáme hyst_vypnuto
        return (now - s_last_change_ms) >= (g_settings.hyst_vypnuto * 1000UL);
    }
}

// ============================================================================
// VEŘEJNÉ FUNKCE
// ============================================================================

void relay_control_init() {
    pinMode(RELAY_PIN, OUTPUT);

    // Výchozí stav: relé vypnuté (bezpečnost)
    digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? LOW : HIGH);
    g_relay.actual = false;
    g_relay.reason = RelayState::NONE;

    g_podlahovka_state = PS_OFF;
    g_podlahovka_enabled = false;
    s_safety_active = false;
    s_last_change_ms = millis();

    Serial.println(F("Relé: inicializováno (OFF)."));
}

void relay_control_loop() {
    unsigned long now = millis();
    RelayState::Reason override_reason = check_override(now);
    bool override = (override_reason != RelayState::NONE);

    // =====================================================================
    // PRIORITA 1: SAFETY — MQTT timeout / Menic data timeout → okamžité vypnutí
    // =====================================================================
    if (is_mqtt_timeout(now)) {
        if (!s_safety_active) {
            Serial.println(F("Relé: SAFETY OFF — MQTT timeout!"));
            g_relay.reason = RelayState::SAFETY_OFF;
            relay_emergency_off();
            s_safety_active = true;
        }
        return;
    }
    // Menic timeout DOCASNE VYPNUTY — ruseni v datech zpusobuje falesne vypnuti
    // if (is_menic_timeout(now)) { ... }

    // Reset safety příznaků
    if (s_safety_active) {
        s_safety_active = false;
        Serial.println(F("Relé: SAFETY zrušeno — MQTT obnoveno."));
    }
    if (s_menic_stale_active) {
        s_menic_stale_active = false;
        g_relay.reason = RelayState::NONE;
        Serial.println(F("Relé: SAFETY zrušeno — data menice obnovena."));
    }

    // =====================================================================
    // STAVOVÝ AUTOMAT
    // =====================================================================

    switch (g_podlahovka_state) {

        // -----------------------------------------------------------------
        case PS_OFF:
            if (g_podlahovka_enabled && !override && hystereze_ok(now)) {
                set_relay(true);
                g_podlahovka_state = PS_ACTIVE;
                g_relay.reason = RelayState::MQTT_ON;
                Serial.println(F("Relé: ACTIVE — zapnuto řízením."));
            } else if (g_podlahovka_enabled && override) {
                g_relay.reason = override_reason;
            } else if (g_podlahovka_enabled && !hystereze_ok(now)) {
                g_relay.reason = RelayState::MQTT_ON;  // čeká na hysterezi
            } else {
                g_relay.reason = RelayState::MQTT_OFF;
            }
            break;

        // -----------------------------------------------------------------
        case PS_ACTIVE:
            // Hlídáme override a OPI povel
            if (!g_podlahovka_enabled && hystereze_ok(now)) {
                set_relay(false);
                g_podlahovka_state = PS_OFF;
                g_relay.reason = RelayState::MQTT_OFF;
                Serial.println(F("Relé: OFF — vypnuto řízením."));
            } else if (override) {
                set_relay(false);
                g_podlahovka_state = PS_OFF;
                g_relay.reason = override_reason;
                Serial.print(F("Relé: OFF — override: "));
                Serial.println(relay_reason_str());
            }
            break;

        // -----------------------------------------------------------------
        case PS_MANUAL_ON:
            // Časový limit manuálního override — návrat k auto režimu
            if (now - s_manual_timer >= (g_settings.manual_override * 1000UL)) {
                set_relay(false);
                g_podlahovka_state = s_prev_auto_state;
                g_relay.reason = RelayState::MQTT_OFF;
                Serial.println(F("Relé: MANUAL_ON vypršel → návrat k auto."));
            } else if (override && hystereze_ok(now)) {
                // I v manuálním režimu respektujeme override
                set_relay(false);
                g_podlahovka_state = PS_MANUAL_OFF;
                g_relay.reason = override_reason;
                Serial.println(F("Relé: MANUAL_ON přerušen override → MANUAL_OFF."));
            }
            break;

        // -----------------------------------------------------------------
        case PS_MANUAL_OFF:
            // Časový limit manuálního override — návrat k auto režimu
            if (now - s_manual_timer >= (g_settings.manual_override * 1000UL)) {
                g_podlahovka_state = s_prev_auto_state;
                Serial.println(F("Relé: MANUAL_OFF vypršel → návrat k auto."));
            }
            break;
    }
}

void relay_emergency_off() {
    digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? LOW : HIGH);
    g_relay.actual = false;
    g_podlahovka_state = PS_OFF;
    g_relay.reason = RelayState::SAFETY_OFF;
    s_safety_active = true;
}

/*
 * Tlačítko — krátký stisk: přepne manuální override.
 * Voláno z wifi_manager.cpp.
 */
void relay_button_short_press() {
    unsigned long now = millis();

    switch (g_podlahovka_state) {
        case PS_OFF:
        case PS_ACTIVE:
            // Uložíme původní auto stav a přepneme
            s_prev_auto_state = g_podlahovka_state;
            s_manual_timer = now;

            if (g_relay.actual) {
                // Bylo zapnuto → manuálně vypnout
                set_relay(false);
                g_podlahovka_state = PS_MANUAL_OFF;
                g_relay.reason = RelayState::MANUAL_BUTTON;
                Serial.println(F("Relé: MANUAL_OFF — tlačítkem."));
            } else {
                // Bylo vypnuto → manuálně zapnout
                set_relay(true);
                g_podlahovka_state = PS_MANUAL_ON;
                g_relay.reason = RelayState::MANUAL_BUTTON;
                Serial.println(F("Relé: MANUAL_ON — tlačítkem."));
            }
            break;

        case PS_MANUAL_ON:
            // Zrušit manuální override
            set_relay(false);
            g_podlahovka_state = s_prev_auto_state;
            g_relay.reason = RelayState::MQTT_OFF;
            Serial.println(F("Relé: MANUAL_ON zrušen tlačítkem."));
            break;

        case PS_MANUAL_OFF:
            // Zrušit manuální override
            g_podlahovka_state = s_prev_auto_state;
            Serial.println(F("Relé: MANUAL_OFF zrušen tlačítkem."));
            break;
    }
}

const char* relay_reason_str() {
    switch (g_relay.reason) {
        case RelayState::MQTT_ON:            return "Zapnuto rizenim";
        case RelayState::MQTT_OFF:           return "Vypnuto rizenim";
        case RelayState::OVERRIDE_POWER:     return "Pretizeny menic";
        case RelayState::OVERRIDE_BAT:       return "Zatez baterie";
        case RelayState::INTELLIGENCE_PV:    return "Nizke PV napeti";
        case RelayState::INTELLIGENCE_SUNSET:return "Zapad slunce";
        case RelayState::SAFETY_OFF:         return "Ztrata spojeni";
        case RelayState::MENIC_STALE:        return "Vypadek dat menice";
        case RelayState::MANUAL_BUTTON:      return "Manually tlacitko";
        default:                             return "Inicializace...";
    }
}
