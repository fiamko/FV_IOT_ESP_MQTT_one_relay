/*
 * ============================================================================
 * web_setup.cpp — STABILNÍ VERZE (bez sensorů)
 * ============================================================================
 */

#include "variables.h"
#include "web_setup.h"
#include "relay_control.h"
#include "sunset.h"
#include <WebServer.h>

static WebServer* s_web_server = nullptr;

static String generate_html(const char* message = nullptr) {
    String h = F(
        "<!DOCTYPE html><html lang='cs'><head>"
        "<meta charset='UTF-8' name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>ESP32 Podlahovka 2200W</title>"
        "<style>"
        "*{box-sizing:border-box;margin:0;padding:0}"
        "body{font-family:'Segoe UI',Arial;background:#0f0f1a;color:#e0e0e0;padding:15px}"
        "h1{color:#e94560;text-align:center;font-size:22px;margin-bottom:15px}"
        "h2{color:#aaa;font-size:14px;margin:15px 0 8px;border-bottom:1px solid #333;padding-bottom:5px}"
        ".card{background:#1a1a2e;border-radius:8px;padding:12px;margin-bottom:12px;border:1px solid #2a2a4a}"
        ".row{display:flex;justify-content:space-between;padding:4px 0;font-size:14px}"
        ".label{color:#888}.value{font-weight:bold}"
        ".on{color:#4ecca3}.off{color:#e94560}.warn{color:#f0a500}"
        "form{margin-top:10px}"
        "input{width:100%;padding:8px;margin:4px 0;border:1px solid #333;border-radius:5px;"
        "background:#16213e;color:#eee;font-size:14px}"
        "input[type=submit]{background:#e94560;color:#fff;border:none;padding:10px;margin-top:12px;"
        "font-size:15px;cursor:pointer;font-weight:bold}"
        ".msg{padding:8px;border-radius:5px;margin:8px 0;font-size:14px;text-align:center}"
        ".msg-ok{background:#1a3a1a;color:#4ecca3}"
        ".msg-err{background:#3a1a1a;color:#e94560}"
        ".version{text-align:center;color:#555;font-size:11px;margin-top:15px}"
        "</style></head><body>"
        "<h1>🔧 ESP32 Podlahovka 2200W</h1>"
    );

    if (message) {
        bool err = (strstr(message, "patn") || strstr(message, "Chyba") || strstr(message, "Zadej"));
        h += F("<div class='msg ");
        h += err ? F("msg-err") : F("msg-ok");
        h += F("'>");
        h += message;
        h += F("</div>");
    }

    h += F("<div class='card'><h2>📡 WiFi / MQTT</h2>"
           "<div class='row'><span class='label'>WiFi:</span><span id='wifiSsid' class='value ");
    h += WiFi.isConnected() ? F("on") : F("off");
    h += F("'>");
    h += WiFi.isConnected() ? WiFi.SSID() : String("odpojeno");
    h += F("</span></div>"
           "<div class='row'><span class='label'>IP:</span><span id='wifiIp' class='value'>");
    h += WiFi.isConnected() ? WiFi.localIP().toString() : String("—");
    h += F("</span></div>"
           "<div class='row'><span class='label'>MQTT:</span><span id='mqttState' class='value ");
    h += g_mqtt_connected ? F("on") : F("off");
    h += F("'>");
    h += g_mqtt_connected ? F("připojeno") : F("odpojeno");
    h += F("</span></div></div>");

    h += F("<div class='card'><h2>⚡ Relé</h2>"
           "<div class='row'><span class='label'>Stav:</span><span id='relayState' class='value ");
    h += g_relay.actual ? F("on") : F("off");
    h += F("'>");
    h += g_relay.actual ? F("ZAPNUTO") : F("VYPNUTO");
    h += F("</span></div>"
           "<div class='row'><span class='label'>Důvod:</span><span id='relayReason' class='value warn'>");
    h += relay_reason_str();
    h += F("</span></div>"
           "<div class='row'><span class='label'>OPI povel:</span><span id='opiCmd' class='value'>");
    h += g_podlahovka_enabled ? F("zapnout") : F("vypnout");
    h += F("</span></div></div>");

    h += F("<div class='card'><h2>🔋 Měnič</h2>"
           "<div class='row'><span class='label'>Výkon:</span><span id='menicPower' class='value'>");
    h += String(g_menic.output_apparent_power, 0) + " W";
    h += F("</span></div>"
           "<div class='row'><span class='label'>Vybíjení bat:</span><span id='menicBat' class='value'>");
    h += String(g_menic.battery_discharge_current, 1) + " A";
    h += F("</span></div>"
           "<div class='row'><span class='label'>PV napětí:</span><span id='menicPv' class='value'>");
    h += String(g_menic.pv_input_voltage, 1) + " V";
    h += F("</span></div></div>");

    h += F("<div class='card'><h2>🌅 Západ slunce</h2>"
           "<div class='row'><span class='label'>Čas:</span><span id='sunsetTime' class='value'>");
    h += g_sunset.time_valid ? sunset_time_str() : String("—:—");
    h += F("</span></div>"
           "<div class='row'><span class='label'>Noční klid:</span><span id='sunsetWin' class='value ");
    h += g_sunset.is_sunset_window ? F("warn") : F("on");
    h += F("'>");
    h += g_sunset.is_sunset_window ? F("AKTIVNÍ") : F("ne");
    h += F("</span></div>"
           "<div class='row'><span class='label'>NTP:</span><span id='ntpState' class='value ");
    h += g_sunset.time_valid ? F("on") : F("off");
    h += F("'>");
    h += g_sunset.time_valid ? F("synchronizován") : F("čekám...");
    h += F("</span></div></div>");

    h += F("<div class='card'><h2>🌡️ Teploty kotle</h2>"
           "<div class='row'><span class='label'>Vstup:</span><span id='tempVstup' class='value'>");
    h += g_sensors.teplota_error ? String(F("chyba")) : String(g_sensors.teplota_vstup, 1) + " °C";
    h += F("</span></div>"
           "<div class='row'><span class='label'>Výstup:</span><span id='tempVystup' class='value'>");
    h += g_sensors.teplota_error ? String(F("chyba")) : String(g_sensors.teplota_vystup, 1) + " °C";
    h += F("</span></div>"
           "<div class='row'><span class='label'>Rozdíl:</span><span id='tempDiff' class='value'>");
    float diff = g_sensors.teplota_vystup - g_sensors.teplota_vstup;
    h += String(diff, 1) + " °C";
    h += F("</span></div></div>");

    h += F("<div class='card'><h2>⚙️ Nastavení</h2>"
           "<form method='POST' action='/save'>"

           "<label>SAFETY — MQTT timeout [s]:</label>"
           "<input name='mqtt_timeout' type='number' min='5' max='300' value='");
    h += String(g_settings.mqtt_timeout);
    h += F("'>"

           "<label>OCHRANA — Max. výkon měniče [W]:</label>"
           "<input name='max_vykon' type='number' min='100' max='20000' value='");
    h += String(g_settings.max_vykon);
    h += F("'>"

           "<label>OCHRANA — Max. vybíjení bat [A]:</label>"
           "<input name='vybijeni_bat' type='number' min='1' max='200' value='");
    h += String(g_settings.vybijeni_bat);
    h += F("'>"

           "<label>INTELLIGENCE — Min. PV napětí [V]:</label>"
           "<input name='min_pv_voltage' type='number' min='0' max='500' step='0.1' value='");
    h += String(g_settings.min_pv_voltage, 1);
    h += F("'>"

           "<label>INTELLIGENCE — Západ offset [min]:</label>"
           "<input name='sunset_offset' type='number' min='0' max='360' value='");
    h += String(g_settings.sunset_offset);
    h += F("'>"

           "<label>GPS — Latitude [°]:</label>"
           "<input name='latitude' type='number' min='-90' max='90' step='0.01' value='");
    h += String(g_settings.latitude, 2);
    h += F("'>"

           "<label>GPS — Longitude [°]:</label>"
           "<input name='longitude' type='number' min='-180' max='180' step='0.01' value='");
    h += String(g_settings.longitude, 2);
    h += F("'>"

           "<label>UTC offset [h]:</label>"
           "<input name='utc_offset' type='number' min='-12' max='14' value='");
    h += String(g_settings.utc_offset);
    h += F("'>"

           "<label>HYSTEREZE — Min. doba ZAPNUTO [s]:</label>"
           "<input name='hyst_zapnuto' type='number' min='1' max='3600' value='");
    h += String(g_settings.hyst_zapnuto);
    h += F("'>"

           "<label>HYSTEREZE — Min. doba VYPNUTO [s]:</label>"
           "<input name='hyst_vypnuto' type='number' min='1' max='3600' value='");
    h += String(g_settings.hyst_vypnuto);
    h += F("'>"

           "<label>MANUÁLNĚ — Doba přepsání [s]:</label>"
           "<input name='manual_override' type='number' min='10' max='3600' value='");
    h += String(g_settings.manual_override);
    h += F("'>"

           "<label>Web heslo (NENI predvyplneno):</label>"
           "<input name='web_password' type='password' maxlength='31' placeholder='Zadej heslo...'>"

           "<input type='submit' value='💾 Uložit nastavení'></form></div>");

    h += F("<div class='version'>FW: 1.0.2 | ESP32-podlahovka2200</div>");
    h += F("<script>"
           "function cof(e,c){e.className='value '+(c?'on':'off')}"
           "function poll(){var x=new XMLHttpRequest();x.open('GET','/api/status',true);"
           "x.onload=function(){if(x.status!=200)return;var d=JSON.parse(x.responseText);"
           "document.getElementById('wifiSsid').textContent=d.wifi_ssid||'odpojeno';"
           "cof(document.getElementById('wifiSsid'),d.wifi);"
           "document.getElementById('wifiIp').textContent=d.wifi_ip||'—';"
           "document.getElementById('mqttState').textContent=d.mqtt?'pripojeno':'odpojeno';"
           "cof(document.getElementById('mqttState'),d.mqtt);"
           "document.getElementById('relayState').textContent=d.relay?'ZAPNUTO':'VYPNUTO';"
           "cof(document.getElementById('relayState'),d.relay);"
           "document.getElementById('relayReason').textContent=d.reason||'—';"
           "document.getElementById('opiCmd').textContent=d.opi_cmd||'—';"
           "document.getElementById('menicPower').textContent=(d.menic_power||0)+' W';"
           "document.getElementById('menicBat').textContent=(d.menic_bat||0).toFixed(1)+' A';"
           "document.getElementById('menicPv').textContent=(d.menic_pv||0).toFixed(1)+' V';"
           "document.getElementById('sunsetTime').textContent=d.sunset_time||'--:--';"
           "document.getElementById('sunsetWin').textContent=d.sunset_window?'AKTIVNI':'ne';"
           "cof(document.getElementById('sunsetWin'),!d.sunset_window);"
            "document.getElementById('ntpState').textContent=d.ntp?'synchronizovan':'cekam...';"
            "if(d.temp_err){"
            "document.getElementById('tempVstup').textContent='chyba';"
            "document.getElementById('tempVystup').textContent='chyba';"
            "document.getElementById('tempDiff').textContent='—';"
            "}else{"
            "document.getElementById('tempVstup').textContent=(d.temp_vstup||0).toFixed(1)+' °C';"
            "document.getElementById('tempVystup').textContent=(d.temp_vystup||0).toFixed(1)+' °C';"
            "document.getElementById('tempDiff').textContent=((d.temp_vystup||0)-(d.temp_vstup||0)).toFixed(1)+' °C';"
            "}"
            "cof(document.getElementById('ntpState'),d.ntp);};x.send();}"
           "poll();setInterval(poll,3000);"
           "</script></body></html>");

    return h;
}

static void handle_api_status() {
    String json = "{";
    json += "\"wifi\":" + String(WiFi.isConnected() ? "true" : "false") + ",";
    json += "\"wifi_ssid\":\"" + String(WiFi.isConnected() ? WiFi.SSID().c_str() : "odpojeno") + "\",";
    json += "\"wifi_ip\":\"" + (WiFi.isConnected() ? WiFi.localIP().toString() : "—") + "\",";
    json += "\"mqtt\":" + String(g_mqtt_connected ? "true" : "false") + ",";
    json += "\"relay\":" + String(g_relay.actual ? "true" : "false") + ",";
    json += "\"reason\":\"" + String(relay_reason_str()) + "\",";
    json += "\"opi_cmd\":\"" + String(g_podlahovka_enabled ? "zapnout" : "vypnout") + "\",";
    json += "\"menic_power\":" + String(g_menic.output_apparent_power, 0) + ",";
    json += "\"menic_bat\":" + String(g_menic.battery_discharge_current, 1) + ",";
    json += "\"menic_pv\":" + String(g_menic.pv_input_voltage, 1) + ",";
    json += "\"sunset_time\":\"" + sunset_time_str() + "\",";
    json += "\"sunset_window\":" + String(g_sunset.is_sunset_window ? "true" : "false") + ",";
    json += "\"ntp\":" + String(g_sunset.time_valid ? "true" : "false") + ",";
    json += "\"temp_vstup\":" + String(g_sensors.teplota_vstup, 1) + ",";
    json += "\"temp_vystup\":" + String(g_sensors.teplota_vystup, 1) + ",";
    json += "\"temp_err\":" + String(g_sensors.teplota_error ? "true" : "false");
    json += "}";
    s_web_server->send(200, "application/json", json);
}

static void handle_root() {
    if (!s_web_server) return;
    s_web_server->send(200, "text/html; charset=utf-8", generate_html());
}

static void handle_save() {
    if (!s_web_server) return;

    String password = s_web_server->arg("web_password");
    if (password.length() == 0) {
        s_web_server->send(200, "text/html; charset=utf-8", generate_html("Zadej heslo!"));
        return;
    }
    if (password != g_settings.web_password) {
        s_web_server->send(200, "text/html; charset=utf-8",
            generate_html("Spatne heslo! Nastaveni nebylo ulozeno."));
        return;
    }

    if (s_web_server->hasArg("mqtt_timeout")) {
        g_settings.mqtt_timeout = s_web_server->arg("mqtt_timeout").toInt();
        g_prefs.putInt("mqtt_timeout", g_settings.mqtt_timeout);
    }
    if (s_web_server->hasArg("max_vykon")) {
        g_settings.max_vykon = s_web_server->arg("max_vykon").toInt();
        g_prefs.putInt("max_vykon", g_settings.max_vykon);
    }
    if (s_web_server->hasArg("vybijeni_bat")) {
        g_settings.vybijeni_bat = s_web_server->arg("vybijeni_bat").toInt();
        g_prefs.putInt("vybijeni_bat", g_settings.vybijeni_bat);
    }
    if (s_web_server->hasArg("min_pv_voltage")) {
        g_settings.min_pv_voltage = s_web_server->arg("min_pv_voltage").toFloat();
        g_prefs.putFloat("min_pv_v", g_settings.min_pv_voltage);
    }
    if (s_web_server->hasArg("sunset_offset")) {
        g_settings.sunset_offset = s_web_server->arg("sunset_offset").toInt();
        g_prefs.putInt("sunset_off", g_settings.sunset_offset);
    }
    if (s_web_server->hasArg("latitude")) {
        g_settings.latitude = s_web_server->arg("latitude").toFloat();
        g_prefs.putFloat("lat", g_settings.latitude);
    }
    if (s_web_server->hasArg("longitude")) {
        g_settings.longitude = s_web_server->arg("longitude").toFloat();
        g_prefs.putFloat("lon", g_settings.longitude);
    }
    if (s_web_server->hasArg("utc_offset")) {
        g_settings.utc_offset = s_web_server->arg("utc_offset").toInt();
        g_prefs.putInt("utc", g_settings.utc_offset);
    }
    if (s_web_server->hasArg("hyst_zapnuto")) {
        g_settings.hyst_zapnuto = s_web_server->arg("hyst_zapnuto").toInt();
        g_prefs.putInt("hyst_on", g_settings.hyst_zapnuto);
    }
    if (s_web_server->hasArg("hyst_vypnuto")) {
        g_settings.hyst_vypnuto = s_web_server->arg("hyst_vypnuto").toInt();
        g_prefs.putInt("hyst_off", g_settings.hyst_vypnuto);
    }
    if (s_web_server->hasArg("manual_override")) {
        g_settings.manual_override = s_web_server->arg("manual_override").toInt();
        g_prefs.putInt("man_over", g_settings.manual_override);
    }
    if (s_web_server->hasArg("web_password") && password.length() > 0) {
        strncpy(g_settings.web_password, password.c_str(), 31);
        g_prefs.putString("web_pass", password);
    }

    s_web_server->send(200, "text/html; charset=utf-8", generate_html("Nastaveni ulozeno!"));
}

void web_setup_init() {
    if (s_web_server) delete s_web_server;
    s_web_server = new WebServer(WEB_PORT);

    s_web_server->on("/", HTTP_GET, handle_root);
    s_web_server->on("/api/status", HTTP_GET, handle_api_status);
    s_web_server->on("/save", HTTP_POST, handle_save);
    s_web_server->on("/save", HTTP_GET, handle_root);

    s_web_server->onNotFound([]() {
        if (s_web_server) s_web_server->send(404, "text/plain", "404");
    });

    s_web_server->begin();
}

void web_setup_loop() {
    if (s_web_server) s_web_server->handleClient();
}
