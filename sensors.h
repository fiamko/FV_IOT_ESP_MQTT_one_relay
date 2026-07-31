/*
 * ============================================================================
 * sensors.h — ROZHRANÍ PRO ČTENÍ DS18B20 TEPLOTNÍCH ČIDEL
 *            Podlahovka 2200W
 * ============================================================================
 */
#ifndef SENSORS_H
#define SENSORS_H

void sensors_init();
void sensors_loop();
void sensors_scan();  // Serial diagnostika — skenování OneWire sběrnice

#endif
