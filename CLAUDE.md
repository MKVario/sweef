# Sweef – Soaring-Vario (Projektgedächtnis für Claude Code)

Stand: 12.11.2026 · Firmware **2.10.41** · App **1.0.33** · Release `v2.10.41-app33`

## Wer, was, wofür
- **Mathias** (GitHub `MKVario`): Elektrotechniker, Servicetechniker. Löten ist seine Stärke. **Er programmiert nicht selbst.** Code, Builds und Releases macht Claude.
- **Immer auf Deutsch antworten**, kurz und konkret. Downloads immer mit **vollständigem Link/Pfad**.
- **Sweef** ist ein kleines Soaring-Vario am Tragegurt fürs Küstensoaren (Sand, Salzluft, max. ~200 m über Start). Einsatz: **Wilderness/Garden Route, Südafrika, Januar 2027**, mit **5 Geräten** für die Reisegruppe. Nur Mathias hat Android (OnePlus 12), die anderen iPhone → Web-App im Browser **Bluefy**.
- Das Vario funktioniert **voll ohne Handy**. Die App ist Zugabe (Einstellungen, Flugbuch, GPS-Tempo, Wind, Updates).

## Arbeitsweise (bewährt, bitte beibehalten)
- **Firmware-Dateien fortlaufend nummeriert** ausliefern: `sweef_vario_v2_10_N/sweef_vario_v2_10_N.ino` und dazu `sweef_vario_v2_10_N_klein/…_klein.ino` (= gleiche Quelle mit `#define DISPLAY_KLEIN 1` vor dem ersten `#ifndef DISPLAY_KLEIN`). N = letzte Stelle von `FW_VERSION`.
- **App-Versionen fortlaufend:** `sweef_app_v1_0_N.apk`. Direkter Link: `https://github.com/MKVario/sweef/releases/download/<tag>/sweef_app_v1_0_N.apk`.
- **Keine unnötigen App-Updates:** Die App-Nummer zählt nur Commits, die App-Dateien ändern (siehe Build).
- Bei jeder Firmware-Änderung `FW_VERSION` hochzählen. Bei Web-App-Änderungen `VERSION` in `sw.js` hochzählen (Cache, aktuell `sweef-26`).
- Commits/Push auf `main` = Veröffentlichung. Vor dem Push mit `node --check` die Skripte aus `index.html` prüfen.
- Bei Konstruktionen (Gehäuse, Mechanik) immer auch **STEP-Dateien** erzeugen.

## Repo-Aufbau
```
firmware/sweef_vario/sweef_vario.ino   eine Quelle für beide Displays (DISPLAY_KLEIN 0/1)
index.html, sw.js, manifest.webmanifest Web-App (GitHub Pages) – läuft auch in der Android-App
android-app/                            Kotlin + WebView (de.sweef.vario), Brücke SweefNative
.github/workflows/                      firmware.yml, android.yml, release.yml
hardware/                               Platine v5 (Eagle .brd/.sch, Gerber, BOM, CPL), Gehäuse, Schaltpläne
docs/                                   Messdaten (CSV), Diagramme, alter Wissensstand
tools/test_sketches/                    kleine Arduino-Testprogramme
```

## Build & Release (GitHub Actions)
- `release.yml` („Veroeffentlichen“): bei Push auf `main`, **nur** wenn `firmware/**`, `android-app/**`, `index.html` oder Workflows geändert sind. Baut Firmware (groß+klein, `.hex` + DFU-`.zip`) und APK, legt Release `v<FW>-app<N>` an.
- Firmware: `arduino-cli`, FQBN `Seeeduino:nrf52:xiaonRF52840Sense`, Libs: Adafruit DPS310, U8g2, Seeed Arduino LSM6DS3, Adafruit SPIFlash; Flag `-DDISPLAY_KLEIN=0/1`.
- App-Nummer: `APP_NR = git rev-list --count HEAD -- index.html android-app manifest.webmanifest icon-192.png icon-512.png` (android.yml mit `fetch-depth: 0`); `build.gradle.kts` liest `APP_NR`, release.yml liest die Nummer aus dem APK-Namen.
- Signatur: Keystore + Secret `SWEEF_KEYSTORE_PW` liegen als GitHub-Secrets. Den Keystore **nie** ins Repo legen.
- Update aufs Vario: per Bluetooth aus der App (Befehl `dfu` → Adafruit-Bootloader; MTU 23, PRN 4). Auf Platine v5 bleibt das Display beim Update dunkel (Bootloader setzt D7/D6 auf Low) – normal.

## Hardware
### Controller: Seeed XIAO nRF52840 Sense
- QSPI-Flash: D24 SCK (P0.21), D25 CS (P0.25), D26 IO0 (P0.20), D27 IO1 (P0.24), D28 (P0.22), D29 (P0.23).
- D23 = P0.17 CHG (Ladestatus), D22 = P0.13 HICHG (LOW = 100 mA Ladestrom).
- BAT+/BAT− nur als Pads auf der Unterseite. 5V-Pin = nur VBUS durchgereicht (kein Boost).
- Lader im XIAO: 100 mA, Ladeschluss ~4,18 V. Bei eingeschaltetem Vario meldet er nie „fertig“ (Systemlast ~10 mA hält ihn über der Abschaltschwelle).

### Eigene Platine v5 (34,2 × 46,2 mm, Lasche unten 25 × 8 mm, 2 Lagen, ENIG; 5 Stück bei JLCPCB, Standard-PCBA Rückseite)
| Pin | Funktion |
|---|---|
| D0 / D1 | Taster T1 / T2 |
| D2 | TON (PWM → Filter → PAM8302A) |
| D3 | VERST_EIN (PAM SD, R33 100k Pulldown) |
| D4 / D5 | I²C SDA / SCL (R1/R2 2,2k) |
| D6 | OLED_RES (R21 100k Pulldown) |
| D7 | BOOST_EIN (TPS61040 EN, R24 100k Pulldown) |
| D8 | KOMPASS_INT |
| D9 / D10 | Testpunkte |

- I²C: Display SH1107 0x3C, DPS310 0x77, LIS2MDL 0x1E (WHO_AM_I 0x4F = 0x40), **INA226 0x40** (ID-Reg 0xFE = 0x5449 → daran erkennt die Firmware die Platine: `platine = platineErkennen()`).
- Strommessung: INA226 mit Shunt **R10 0,1 Ω** in der Akku-Plusleitung. Shunt-LSB 2,5 µV → **0,025 mA**. Entladen = positiv, Laden = negativ.
- Display-Versorgung: TPS61040 → **13 V (VPP)**, Teiler R22 1 MΩ / R23 105 kΩ (12,97 V). **Nachgerüstet: 10 pF C0G parallel zu R22** (fehlte im Entwurf → Display pulsierte, ~15 V). In der nächsten Platinenversion einen eigenen Lötplatz dafür vorsehen.
- Displayglas **GoldenMorning GME128128-02** (1,5", SH1107, 128×128, 24-pol. FPC 0,5 mm, Kontakte auf der Vorderseite). Stecker **XUNPU FPC-05FB-24PH20** (LCSC C2856831). VPP laut Datenblatt 12,5–13,5 V.
- Vorderseite Lasche (selbst gelötet): C20, C21 4,7 µF 0603; C22 4,7 µF 25 V 0805; R20 910 kΩ (IREF); R21 100 kΩ.
- Testpunkte (Rückseite, Lasche unten, links→rechts): TP1 D9, TP2 D10, TP3 5V, TP4 INA-Alert, TP5 3,3 V, TP6 13 V, TP7 GND.
- Rückseite, Lasche unten: links J1 Akku (oben +, unten −), rechts J3 Taster (oben T1, Mitte T2, unten GND), oben rechts J5 Lautsprecher. XIAO-USB-C zeigt nach rechts über den Rand. BAT+/BAT− des XIAO werden von vorne durch zwei Durchkontaktierungen gelötet.
- Bekannte Fehler/Lehren v5: 0402-Footprint-Padabstand zu groß (JLC Option D) → Generator korrigieren (Padmitten ~±0,5 mm); Cff an R22 fehlte; Bauteilnamen fehlen im Bestückungsdruck; Lasche evtl. unnötig (v6 ohne Lasche denkbar).
- Die Eagle-Generator-Skripte (Python) sind **nicht** erhalten; die `.brd` in `hardware/platine_v5/` ist die Quelle.

### Prototyp/Testbrett (läuft parallel)
- Breakouts: DPS310, SH1107-Modul 128×128 (eigene Versorgung), PAM8302 an Lautsprecher LSF-13 (8 Ω). Variante `klein` = altes 128×64-Display.
- Mobiles Altgerät: 300-mAh-Akku. Problem Ruhestrom war die falsche Akkugröße (Einstellung `ka`) + PAM an 3,3 V.

### Akku
- Platine v5: **500 mAh LiPo 503035** (Sunkoo). Gemessen: **550 mAh** nutzbar bis 3,40 V.
- Tabelle `V500` (0–100 % in 5-%-Schritten): 3,400 3,480 3,518 3,573 3,636 3,705 3,752 3,779 3,797 3,817 3,832 3,853 3,892 3,935 3,967 3,980 3,988 3,996 4,010 4,046 4,123. Abweichung zur echten Restladung max. 6 Prozentpunkte. Neue Messung (7./8.11.) ergab fast dieselbe Kurve: 3,418 3,478 3,518 3,569 3,621 3,682 3,734 3,769 3,791 3,809 3,822 3,833 3,865 3,922 3,965 3,986 3,993 4,000 4,014 4,065 4,144.
- Einstellung `ka` (Akkugröße mAh): < 400 → Tabelle V300, sonst V500.

## Messwerte Platine v5 (Firmware 2.10.40/41)
| Zustand | Strom |
|---|---|
| Aus (Tiefschlaf) | **25 µA** (INA226 im Power-Down; vorher 0,6 mA) |
| Ein, Display aus | ~5 mA |
| Ein, Display niedrigste Helligkeit | ~25 mA |
| Ein, Display volle Helligkeit | ~43 mA |
| Piepen Stufe 6 (während Ton) | ~400 mA (klingt ohne Gehäuse kratzig) |
| Laden | netto ~90 mA in den Akku (CC), danach CV bei 4,17–4,18 V |
- Laufzeit 500er bei niedrigster Helligkeit: **20,5 h** (Mittel 26,7 mA). Laden 50 → 100 %: ~3 h.
- Display braucht selbst bei Kontrast 1 ~20 mA aus dem Akku → Grundlast der Ansteuerung (Takt/Vorladung/VCOMH).

## Firmware – wichtige Stellen
- `FW_VERSION`, `DISPLAY_KLEIN`, Pin-Defines oben in der Datei.
- `platineErkennen()`, `anzeigeStrom(bool)` (Boost D7 + PowerSave), `displayTakt()` mit `TAKT_VARIANTEN[8][3]` (D5/D9/DB; Variante 0 = Datenblatt 0x80/0x1D/0x35), Einstellung `dk`.
- Display-Init Platine: D7 HIGH → Reset-Puls D6 → `initDisplay` → `0xAD 0x80` (interne Ladepumpe aus) → `displayTakt()` → clear → PowerSave 0. Aus: Display aus, dann VPP aus.
- `kontrast()`: Helligkeit % quadratisch auf 1…255.
- INA226: `inaSchreiben/inaLesen/inaSchlafen` (0x4120 = Power-Down), `stromMessen()` 1×/s Einzelmessung (Config 0x4527, 16× Mittelung), `verbrauchMah` ab Abziehen des USB-Kabels.
- `tiefschlaf()`: Flash DPD, Verstärker aus, Display aus, INA aus, D6 LOW, DPS idle, IMU aus, `systemOff(PIN_TASTE1, LOW)`.
- Flash-Start: `qspiAus()` + `speicherPerHand()` (Bit-Bang-Reset 0xFF,0x66,0x99,0xAB) vor `flash.begin` – behebt „Daten fehlt“ nach USB-Reset. Selbsttest zeigt dann „OK*“ wenn gerettet.
- `selbsttest()` (beide Tasten 5 s): Druck, Lage, Daten, auf Platine zusätzlich Kompass und Strom, Akku.
- Lautstärke: `LAUT_TABELLE {0,15,30,60,125,250,500}` = PWM-Tastgrad in ‰ (Stufe 6 = 50 %).
- Einstellungen: Tabelle `EINST[]` (Kürzel, Typ, Zeiger, min, max), Text max. ~300 Zeichen (`chrEinst.setMaxLen(300)`, knapp!).
- BLE-Dienst `7e5e0001-…`: Einstellungen `…0002`, Befehle `…0003`, Live `…0004` (max. 150 Zeichen), Daten/Flüge `…0005`, Info `…0006`. Nur eine Verbindung.
- Live-String u. a. `ak` (%), `av` (mV), `ld` (lädt), `us` (USB), auf Platine `ma` (0,1 mA), `mh` (0,1 mAh). Info-String u. a. `tr=grund,sek,anzahl`, `aw=n`.
- Befehle: `null`, `reset`, `test=…`, `ls=…`, `suchen`, `fl_liste`, `fl_hol=…`, `fl_loeschen`, `zeit=…`, `gs=…`, `wi=…` (Wind), `su=…` (Sonnenuntergang), `dfu`, `spfrage=…`, `pin=…`, `nm=…`, `gt=…`, `last=N` (Belastungstest).
- Bedienung: T1 kurz lauter, T2 kurz leiser, T1 2 s → Aus (mit T2 bestätigen), T2 3 s Höhe 0, beide 5 s Neustart + Selbsttest, T1 Doppelklick Flugmessung stoppen, Doppeltippen (IMU) entstummen.

## App (index.html + Android)
- Web-App läuft auch im Android-WebView. Android-Dienst hält BLE im Vordergrund, schickt GPS-Tempo (`gs`), Wind (`wi`, Kreisfit aus GPS, `Wind.kt`) und Sonnenuntergang (`su`).
- Einstellungen: Array `FELDER` in index.html (`[kürzel, gruppe, titel, einheit, min, max, schritt, typ, hilfe]`).
- Akku-Protokoll: 1 Punkt/Minute, CSV `Datum;Uhrzeit;Minuten;Akku_%;Spannung_V;Laedt;USB;Strom_mA;Verbraucht_mAh`. Schalter „Strom anzeigen“ (nur App, `localStorage sweefStrom`).
- Windows: Wenn Windows das Sweef gekoppelt hat, bekommt Web Bluetooth keine Live-Daten → Gerät in Windows entfernen. Laptop-Deckel: „Nichts unternehmen“.

## Offene Aufgaben (Reihenfolge)
1. **Display-Takt-Test auswerten:** Mathias misst Stufen 0–7 (`dk`) – Strom + Bild ok? Beste Stufe fest einbauen, Regler wieder entfernen.
2. **Akkuanzeige 100 % / „voll“** per INA226: Akkustrom einige Minuten < ~3 mA beim Laden → 100 % (zeigt bisher 99 %).
3. Lautsprecher im Gehäuse testen; falls Stufe 6 weiter kratzt → höchste Stufe auf der Platine in der Firmware begrenzen.
4. Tests nach Zusammenbau: 5 Geräte nebeneinander 24 h, Fahrtwind-Test (Membranloch), Dichtigkeit, Hitze/Sonne, BLE-Reichweite, Fall, erster Flug im Vergleich zum Prototyp.
5. Inbetriebnahme der restlichen Platinen (Checkliste: https://claude.ai/artifact/FqaXRYFFArvTSSRZs2M4Eq).
6. Später: Kurzanleitung für die Freunde, Platine v6 (Cff-Platz, 0402-Footprint, Bestückungsdruck, evtl. ohne Lasche), Profil-Export, Firmware-Watchdog.
- Geparkt: Sweef XL (GPS, FANET, Luftraum) als eigenes Projekt.
