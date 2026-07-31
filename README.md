# ESP32 — ovládání podlahového topení 2200W

Řízení podlahového topení (2200 W) jako vytěžování přebytků FVE.
Jedno výkonové relé 30A spínané podle povelů z nadřazeného systému přes MQTT
s vlastní inteligencí (PV napětí, západ slunce) a ochranami.

## Hardware
| Součást | Popis |
|---------|-------|
| **Modul** | ESP32-32E N4 |
| **Relé** | 1× 30 A (GPIO2, active HIGH) |
| **Čidla** | 2× DS18B20 (vstup/výstup kotle, GPIO1/TX) |
| **LED** | GPIO4 (active HIGH) |
| **Tlačítko** | GPIO0 (krátký stisk = ruční přepsání, dlouhý 5s = WiFi) |

## Funkce
- **WiFi** s duální sítí (primární + záložní), captive portal
- **MQTT** — povely ON/OFF, stav relé, teploty
- **Ochrany** — měnič (výkonový limit), baterie (vybíjecí proud)
- **Inteligence** — PV napětí (minimum pro sepnutí), západ slunce (NOAA, šetří baterii na noc)
- **Senzory** — 2× DS18B20 (teplota vstupu a výstupu kotle)
- **Webové rozhraní** — AJAX, nastavení parametrů, diagnostika
- **OTA** — bezdrátové nahrávání firmwaru

## MQTT topicy
| Topic | Směr | Obsah |
|-------|------|-------|
| `fve/spotrebice/podlaha2200/set` | OPI → ESP | `{"enabled":true/false}` |
| `fve/spotrebice/podlaha2200/stav` | ESP → OPI | `{"status":"ZAP"/"OFF","vystup":0/1,"duvod":"..."}` |
| `fve/spotrebice/podlaha2200/status` | ESP → MQTT | `{"status":"online"/"offline"}` (Last Will) |
| `fve/spotrebice/podlaha2200/teplota` | ESP → MQTT | `{"vstup":42.5,"vystup":35.1}` (°C) |
| `menic/1/data` | OPI → MQTT | data z měniče pro ochrany a inteligenci |

## Priorita řízení relé
1. **SAFETY** — výpadek MQTT → okamžité OFF
2. **OVERRIDE výkon** — překročen limit měniče → OFF
3. **OVERRIDE baterie** — překročen vybíjecí proud → OFF
4. **INTELLIGENCE PV** — napětí panelů pod minimem → OFF
5. **INTELLIGENCE Sunset** — okno před západem slunce → OFF ("Noční klid")
6. **MQTT příkaz** — normální režim podle `enabled`

## Soubory
| Soubor | Účel |
|--------|------|
| `FVE_ovladani_podlahovka_ESP32.ino` | Hlavní soubor, setup/loop, OTA |
| `config.h` | Piny, MQTT topicy, konstanty |
| `variables.h` | Globální proměnné a struktury |
| `wifi_manager.cpp/h` | WiFi, duální síť, captive portal |
| `mqtt_handler.cpp/h` | MQTT, JSON, publish/subscribe |
| `relay_control.cpp/h` | Stavový automat, vyhodnocení podmínek |
| `sensors.cpp/h` | 2× DS18B20 (OneWire) |
| `sunset.cpp/h` | NOAA výpočet západu slunce + NTP |
| `web_setup.cpp/h` | Webové rozhraní s AJAX pollingem |

## Knihovny (Arduino IDE)
`PubSubClient` `ArduinoJson` `DallasTemperature` `OneWire` `ArduinoOTA` `DNSServer` `WebServer` `WiFi` `Preferences` `time.h`

## Použití
Volné dílo — dělej si s tím co chceš.
