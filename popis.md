# ESP32 — ovládání podlahového topení 2200W (kuchyně)

## Popis zařízení

### Účel

Zařízení slouží k **řízení podlahového topení v kuchyni (2200 W)** jako způsobu vytěžování
přebytků z fotovoltaické elektrárny. Výkonové relé 30A spíná topné těleso podle povelů
z nadřazeného systému (OrangePi PC2) přes MQTT. Zařízení má vlastní inteligenci:
chrání měnič před přetížením, baterii před nadměrným vybíjením, hlídá PV napětí
a počítá západ slunce pro úsporu energie na noc. Tlačítko umožňuje ruční override.

### Hardware

| Součást | Popis |
|---------|-------|
| **Řídicí modul** | ESP32-32E N4 |
| **Deska** | Single-relé modul s palubním spínaným zdrojem |
| **Napájení** | AC 230 V (palubní spínaný zdroj) |
| **Relé** | 1× 30 A (COM/NO/NC), spínané při HIGH |
| **GPIO relé** | GPIO2 |
| **LED** | GPIO4 — svítí při HIGH |
| **Tlačítko** | GPIO0 — stisk = LOW (INPUT_PULLUP), krátký stisk = ruční override, dlouhý stisk (5 s) = WiFi konfigurace |
| **Programování** | 6pin header (GND/TX/RX/3V3/IO0/GND), USB-UART převodník |
| **DS18B20 čidla** | 2× DS18B20 na GPIO1 (TX pin programovacího headeru, Serial se po setupu vypíná), paralelně na jedné OneWire sběrnici |
| **Pull-up** | Jeden 4,7 kΩ rezistor mezi DATA a 3,3 V (pro celou sběrnici) |
| **OTA** | ArduinoOTA, hostname `ESP32-podlahovka2200`, port 3232 |

### Blokové schéma

```
┌──────────────────────────────────────────────────────┐
│              ESP32 Single Relay Board                 │
│                                                      │
│  AC 230V ──► Spínaný zdroj ──► 3.3V ──► ESP32-32E   │
│                                                      │
│  GPIO2  ──► Relé 30A ──► Topné těleso 2200W         │
│  GPIO4  ──► LED                                      │
│  GPIO0  ◄── Tlačítko                                 │
│  GPIO1  ◄── 2× DS18B20 (OneWire) ──► vstup/výstup    │
│                                                      │
│  WiFi ◄──► MQTT broker (192.168.0.191:1883)         │
│  NTP  ◄──► pool.ntp.org (čas pro západ slunce)      │
└──────────────────────────────────────────────────────┘
```

### Komunikace

| Směr | Topic MQTT | Obsah |
|------|-----------|-------|
| OPI → ESP | `fve/spotrebice/podlaha2200/set` | `enabled` (true = zapnout, false = vypnout) |
| OPI → ESP | `menic/1/data` | `output_apparent_power`, `battery_discharge_current`, `pv_input_voltage` |
| ESP → MQTT | `fve/spotrebice/podlaha2200/stav` | `status` (ZAP/OFF), `vystup` (0/1), `duvod` |
| ESP → MQTT | `fve/spotrebice/podlaha2200/status` | `status` (online/offline — Last Will) |
| ESP → MQTT | `fve/spotrebice/podlaha2200/teplota` | `teplota_vstup`, `teplota_vystup` (°C) — každých 5 s |

### Princip činnosti

ESP32 se připojí k domácí WiFi síti a k MQTT brokeru na OrangePi PC2
(192.168.0.191:1883, bez autentizace). Naslouchá na dvou topicích:

1. **`fve/spotrebice/podlaha2200/set`** — řídicí povely z OPI. JSON s klíčem `enabled`
   (true = zapnout ohřev, false = vypnout).

2. **`menic/1/data`** — data z měniče. ESP čte:
   - `output_apparent_power` — pro ochranu proti přetížení měniče
   - `battery_discharge_current` — pro ochranu proti nadměrnému vybíjení baterie
   - `pv_input_voltage` — pro inteligenci PV napětí

#### Stavový automat řízení relé

ESP32 používá stavový automat se čtyřmi stavy:

| Stav | Popis |
|------|-------|
| **PS_OFF** | Relé OFF, čeká na `enabled=true` + příznivé podmínky |
| **PS_ACTIVE** | Relé ON, hlídá override podmínky |
| **PS_MANUAL_ON** | Tlačítkem vynucené ZAPNUTO, ignoruje OPI po dobu `manual_override` |
| **PS_MANUAL_OFF** | Tlačítkem vynucené VYPNUTO, ignoruje OPI po dobu `manual_override` |

**Priority** (od nejvyšší):
1. **Safety — MQTT timeout:** výpadek delší než `mqtt_timeout` → relé okamžitě OFF
2. **Override — výkon měniče:** `output_apparent_power > max_vykon` → relé OFF
3. **Override — baterie:** `battery_discharge_current > vybijeni_bat` → relé OFF
4. **Inteligence — PV napětí:** `pv_input_voltage < min_pv_voltage` → relé OFF
5. **Inteligence — západ slunce:** aktuální čas v sunset okně → relé OFF
6. **MQTT povel:** `enabled=true/false` → normální řízení

**Bezpečnost:** Bez `enabled=true` z OPI ESP nikdy nezapne relé (kromě MANUAL_ON).
Vypnout relé (kvůli ochraně) může vždy.

### Inteligentní funkce

#### Měření teploty vstupu a výstupu kotle (2× DS18B20)

ESP32 čte **dvě teplotní čidla DS18B20** zapojená paralelně na GPIO1 (TX pin, Serial se po setupu vypíná) přes OneWire sběrnici:

- **Čidlo #1 (vstup):** teplota vody na vstupu do podlahového kotle — jak teplá voda do podlahovky vtéká
- **Čidlo #2 (výstup):** teplota vody na výstupu z podlahového kotle — jak moc se voda ochladila po průchodu podlahou

Adresy obou čidel se ukládají do Preferences (`ds18_a1`, `ds18_a2`) — nemusí se hledat při každém startu. Při změně zapojení nebo výměně čidla stačí spustit příkaz `scan` v Serial Monitoru (115200 baud) — ESP prohledá sběrnici, najde obě čidla a uloží jejich nové adresy.

Teploty se měří každých 5 sekund a publikují do MQTT topicu `fve/spotrebice/podlaha2200/teplota` ve formátu:
```json
{"vstup": 42.5, "vystup": 35.1}
```

**Poznámka:** Teploty jsou **pouze informativní** — neovlivňují spínání relé.

**Zobrazení na dashboardu:** Nad ikonou podlahovky 2200W se běžně zobrazují teploty ve formátu `in42.5 | out35.1` (tučně, 80% opacity). Při jakékoli změně stavu ESP (zapnuto/vypnuto/chybový stav) se na 5 sekund zobrazí stavová hláška, poté se zobrazení automaticky vrátí na teploty. Výkon pod ikonou ukazuje skutečný odběr z ESP (2200 W při sepnutém relé, 0 W při vypnutém).

#### Západ slunce (NOAA algoritmus)
ESP32 synchronizuje čas přes NTP (`pool.ntp.org`) každou hodinu. Každých 5 minut
přepočítává dnešní čas západu slunce podle NOAA zjednodušeného algoritmu
(přesnost ±1 minuta). Pokud je aktuální čas v rozmezí "západ − offset" až "západ",
relé se vypne — šetří se energie v baterii pro noční provoz domu.

Parametry: latitude, longitude, UTC offset, sunset_offset — vše nastavitelné přes web.

#### PV napětí
Pokud napětí z panelů (`pv_input_voltage`) klesne pod nastavitelnou mez, relé se vypne.
Tím se zabrání vybíjení baterie při zatažené obloze nebo v pozdních odpoledních hodinách.

#### Hystereze
Samostatné nastavení minimální doby v zapnutém (`hyst_zapnuto`) a vypnutém
(`hyst_vypnuto`) stavu. Zabraňuje rychlému přepínání (kmitání) při kolísání
měřených hodnot kolem prahových mezí.

### Bezpečnostní prvky

- **MQTT Watchdog**: při výpadku MQTT spojení delším než nastavený timeout (výchozí 15 s)
  se relé vypne. Tím se zabrání nekontrolovanému odběru při ztrátě komunikace.
- **Last Will Testament**: při ztrátě MQTT spojení broker automaticky publikuje
  `{"status":"offline"}` na `fve/spotrebice/podlaha2200/status`.
- **Výchozí stav po startu**: relé je vypnuté, dokud nepřijde první MQTT zpráva.
- **Webové heslo**: ukládání nastavení přes web vyžaduje heslo (ověřuje se server-side,
  NENÍ předvyplněno v HTML).
- **Verzování nastavení**: při změně verze firmware se Preferences automaticky vymažou
  — čistý start s výchozími hodnotami.

### Struktura firmware

| Soubor | Účel |
|--------|------|
| `FVE_ovladani_podlahovka_ESP32.ino` | Hlavní soubor — `setup()` a `loop()`, OTA |
| `config.h` | Definice pinů, MQTT topiců, konstant, výchozích hodnot |
| `variables.h` | Globální proměnné a struktury s podrobnými komentáři |
| `wifi_manager.h/.cpp` | WiFi, duální síť, captive portal, detekce tlačítka |
| `mqtt_handler.h/.cpp` | MQTT spojení, JSON parsing, publish/subscribe |
| `relay_control.h/.cpp` | Stavový automat relé, vyhodnocení override podmínek |
| `sensors.h/.cpp` | Čtení 2× DS18B20 (OneWire, GPIO1/TX), ukládání/načítání adres z Preferences |
| `sunset.h/.cpp` | NOAA výpočet západu slunce + NTP synchronizace |
| `web_setup.h/.cpp` | Webová stránka s AJAX pollingem a POST formulářem |
| `test_gpio_ota/test_gpio_ota.ino` | Testovací program pro ověření GPIO pinů |

### Použité knihovny

- `PubSubClient` (MQTT)
- `ArduinoJson` (JSON parsing)
- `ArduinoOTA` (OTA aktualizace)
- `DNSServer` (captive portal)
- `WebServer` (webové rozhraní)
- `WiFi` (ESP32 built-in)
- `Preferences` (trvalé úložiště nastavení)
- `OneWire` + `DallasTemperature` (DS18B20 teplotní čidla)
- `time.h` (NTP synchronizace, standardní C knihovna)

### Technické parametry

| Parametr | Hodnota |
|----------|---------|
| Napájení | AC 230 V, palubní spínaný zdroj |
| Max. spínaný proud | 30 A (AC 250 V) |
| Odběr desky (relé OFF) | ~1 W |
| WiFi | 802.11 b/g/n, 2,4 GHz |
| NTP synchronizace | každou hodinu |
| Přesnost západu slunce | ±1 minuta (NOAA algoritmus) |

---

*Firmware verze 1.0.5 — červenec 2026*
