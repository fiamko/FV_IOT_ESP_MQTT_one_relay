# Návod k použití — ESP32 ovládání podlahového topení 2200W (kuchyně)

## První spuštění

### 1. Nahrání firmwaru

1. Připoj USB-UART převodník k 6pin programovacímu headeru:
   - GND → GND
   - TX (převodník) → RX (deska)
   - RX (převodník) → TX (deska)
   - 3.3V (převodník) → 3V3 (deska) — **dej pozor, ať je převodník v režimu 3,3 V!**
2. Připoj **IO0** na **GND**
3. Zapni napájení nebo stiskni EN tlačítko
4. V Arduino IDE: vyber desku **ESP32 Dev Module**, port převodníku
5. Otevři `FVE_ovladani_podlahovka_ESP32.ino` a nahraj
6. Po nahrání **odpoj IO0 od GND** a resetuj (tlačítko EN)

> 💡 **První nahrání testovacím programem:** Pro ověření GPIO doporučujeme nejprve
> nahrát `test_gpio_ota/test_gpio_ota.ino`. Ověříš tím funkčnost relé, LED a tlačítka
> a zároveň získáš OTA přístup pro další nahrávání bez drátů.
> **Relé potřebuje 230V napájení desky pro cvaknutí!**

### 2. Nastavení WiFi

**První spuštění (tovární nastavení):**
- ESP32 nemá uložené WiFi údaje → automaticky spustí **vlastní WiFi síť**
- Na mobilu/notebooku najdi síť **`ESP32-podlahovka2200`** (bez hesla)
- Otevři libovolnou webovou stránku — objeví se konfigurační formulář
- Zadej:
  - **Primární síť:** SSID a heslo (povinné)
  - **Záložní síť:** SSID a heslo (volitelné — např. hotspot z mobilu)
- Klikni na **Uložit a restartovat**
- ESP32 se restartuje a připojí k zadané síti

**Pozdější změna WiFi:**
- Drž tlačítko na desce **5 sekund** — LED začne rychle blikat
- ESP32 spustí konfigurační WiFi `ESP32-podlahovka2200`
- Postupuj stejně jako při prvním spuštění

**Duální síť:**
- ESP32 se vždy pokusí připojit k **primární** síti
- Pokud primární není dostupná, automaticky přepne na **záložní**
- Při obnovení primární sítě se k ní vrátí

### 3. Webové rozhraní

Po připojení k domácí síti zjisti IP adresu ESP32:
- V Arduino IDE: `Nástroje → Port` — ESP32 se zobrazí jako `ESP32-podlahovka2200 at x.x.x.x`
- Nebo v routeru podle DHCP tabulky

Do prohlížeče zadej `http://[IP-adresa]` — zobrazí se stránka se stavem a nastavením.

Stránka používá AJAX — **data se obnovují automaticky bez překreslování stránky**,
takže můžeš v klidu vyplňovat formulář a hodnoty se ti nesmažou.

---

## Webová stránka — přehled

### Stavové informace (čtení — obnovují se automaticky)

| Sekce | Zobrazuje |
|-------|-----------|
| **WiFi / MQTT** | Název připojené sítě, IP adresa, stav MQTT |
| **Relé** | Stav relé (ZAPNUTO/VYPNUTO), důvod poslední změny, stav automatu, OPI povel |
| **Měnič** | Aktuální výkon měniče (W), vybíjecí proud baterie (A), PV napětí (V) |
| **Teploty** | Teplota vstupu a výstupu kotle (°C) — na dashboardu jako `in42.5 \| out35.1`, při změně stavu se na 5s ukáže stavová hláška |
| **Západ slunce** | Dnešní čas západu, zda je aktivní sunset okno, stav NTP synchronizace |

### Nastavení (zápis — vyžaduje heslo)

| Parametr | Výchozí | Rozsah | Význam |
|----------|---------|--------|--------|
| **MQTT timeout** | 15 s | 5–300 | Doba bez MQTT zprávy, po které se relé bezpečnostně vypne |
| **Max. výkon měniče** | 3000 W | 100–20000 | Při překročení relé vypne (ochrana měniče) |
| **Max. vybíjení bat** | 20 A | 1–200 | Při překročení relé vypne (ochrana baterie) |
| **Min. PV napětí** | 120,0 V | 0–500 | Pod touto hodnotou relé vypne (šetří baterii pro noc) |
| **Západ offset** | 120 min | 0–360 | Kolik minut před západem slunce vypnout |
| **Latitude** | 49,50° | −90–90 | Zeměpisná šířka pro výpočet západu |
| **Longitude** | 16,50° | −180–180 | Zeměpisná délka pro výpočet západu |
| **UTC offset** | 1 h | −12–14 | Časová zóna (1=CET, 2=CEST) |
| **Hystereze ZAP** | 20 s | 1–3600 | Minimální doba v zapnutém stavu |
| **Hystereze VYP** | 20 s | 1–3600 | Minimální doba ve vypnutém stavu |
| **Doba override** | 120 s | 10–3600 | Jak dlouho drží ruční přepnutí tlačítkem |
| **Web heslo** | `2330` | max 31 znaků | Heslo pro ukládání změn (NENÍ předvyplněno!) |

> ⚠️ **Heslo se NEPŘEDVYPLŇUJE** — musíš ho zadat ručně při každém ukládání.
> Není vidět v kódu stránky ani ve zdrojáku HTML.

---

## Tlačítko — ruční override

**Krátký stisk** (méně než 5 sekund):
- Přepne stav relé na opačný **na dobu nastavenou v "Doba override"** (výchozí 2 minuty)
- Po vypršení času se vrátí k automatickému řízení podle OPI
- Dalším krátkým stiskem lze manuální override předčasně zrušit
- Na webu OPI se zobrazí důvod *"Manually tlacitko"*

**Dlouhý stisk** (5+ sekund):
- Spustí WiFi konfigurační režim (captive portal)

---

## OTA aktualizace (bezdrátové nahrávání)

Po prvním úspěšném nahrání přes USB-UART můžeš další verze nahrávat bezdrátově:

1. V Arduino IDE: `Nástroje → Port` → vyber **`ESP32-podlahovka2200 at x.x.x.x`** (síťový port)
2. Otevři soubor a klikni na **Nahrát**
3. Po nahrání se ESP32 automaticky restartuje

> 💡 Pokud se OTA port nezobrazuje: zkontroluj, že ESP32 i počítač jsou ve stejné síti.

---

## Signalizace LED

LED na desce signalizuje stav **krátkým zábleskem** (30 ms):

| Interval | Význam |
|----------|--------|
| Každých ~250 ms | Připojování k WiFi |
| Každé ~2 s | Připojeno, vše OK |
| Každé ~3 s | WiFi odpojeno, pokus o reconnect |
| Velmi rychlé | Konfigurační režim (vlastní AP aktivní) |

---

## Inteligentní funkce

### Západ slunce
ESP32 si přes NTP (`pool.ntp.org`) synchronizuje přesný čas a každých 5 minut počítá
dnešní čas západu slunce podle NOAA algoritmu (přesnost ±1 minuta). Pokud je aktuální čas
v okně "západ − offset", relé se vypne (nebo nezapne) — šetří energii v baterii na noc.

### PV napětí
Pokud napětí z fotovoltaických panelů klesne pod nastavenou mez, relé se vypne. Tím se
zabrání vybíjení baterie při zatažené obloze.

### Měření teplot (DS18B20)
ESP32 čte dvě teplotní čidla DS18B20 zapojená na GPIO1 (TX pin, Serial se po setupu vypíná):
- **Vstup kotle** — teplota vody vtékající do podlahového topení
- **Výstup kotle** — teplota vody vracející se z podlahového topení

Pro diagnostiku čidel použij **Serial Monitor** (115200 baud) a příkaz `scan` — ESP prohledá OneWire sběrnici a vypíše adresy všech nalezených čidel. Adresy se automaticky uloží.

---

## Co dělat když...

### ... se relé nezapíná i když OPI dává povel?

Na webové stránce zkontroluj **Důvod** v sekci Relé:
- **Pretizeny menic** — výkon měniče překročil limit
- **Zatez baterie** — vybíjecí proud baterie překročil limit
- **Nizke PV napeti** — napětí panelů kleslo pod nastavenou mez
- **Zapad slunce** — aktuální čas je v sunset okně
- **Ztrata spojeni** — MQTT timeout — zkontroluj MQTT broker
- **Manually tlacitko** — ruční override tlačítkem

### ... teploty ukazují nesmyslné hodnoty nebo chybu?

1. Zkontroluj zapojení: obě čidla musí mít VCC→3.3V, GND→GND, DATA→GPIO1 (TX pin)
2. Zkontroluj pull-up: mezi DATA a 3.3V musí být JEDEN rezistor 4.7 kΩ (ne dva!)
3. V Serial Monitoru (115200 baud) zadej `scan` — ESP prohledá sběrnici
   - Pokud nenajde žádné čidlo: zkontroluj napájení a pull-up
   - Pokud najde jen jedno: druhé čidlo je odpojené nebo vadné
4. Zkontroluj, že čidla nejsou prohozená (vstup vs. výstup)

### ... se webová stránka nenačítá?

1. Ověř IP adresu ESP32 (v Arduino IDE nebo v routeru)
2. Zkontroluj, že jsi ve stejné síti jako ESP32
3. Zkus `http://ESP32-podlahovka2200.local`

### ... potřebuji resetovat do továrního nastavení?

Při změně verze firmware se nastavení **automaticky vymaže**. Pro ruční reset:
1. Připoj USB-UART a otevři Serial Monitor (115200 baud)
2. Nahraj prázdný sketch:
   ```cpp
   #include <Preferences.h>
   void setup() {
       Preferences p;
       p.begin("podlahovka2200", false);
       p.clear();
       p.end();
   }
   void loop() {}
   ```
3. Pak nahraj znovu firmware

---

## Montáž

### Umístění
- **Deska ESP32:** do instalační krabice u rozvaděče podlahového topení (suché místo!)
- **Napájení desky:** AC 230 V — doporučeno připojit před jakékoli spínací prvky,
  ať ESP běží nepřetržitě
- **Relé:** přerušuje přívod k topnému tělesu podlahovky — **sériově**
  s původním SSR spínačem. Původní analogové řízení lze ponechat jako záložní.

---

*Firmware verze 1.0.5 — červenec 2026*
