/*
 * ============================================================================
 * relay_control.h — ROZHRANÍ PRO ŘÍZENÍ RELÉ
 * ============================================================================
 */
#ifndef RELAY_CONTROL_H
#define RELAY_CONTROL_H

void relay_control_init();
void relay_control_loop();
void relay_emergency_off();
void relay_button_short_press();
const char* relay_reason_str();

#endif
