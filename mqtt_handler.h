/*
 * ============================================================================
 * mqtt_handler.h — ROZHRANÍ PRO MQTT KOMUNIKACI
 * ============================================================================
 */
#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

void mqtt_handler_init();
void mqtt_handler_loop();
void mqtt_publish_status();
void mqtt_publish(const char* topic, const char* message);

#endif
