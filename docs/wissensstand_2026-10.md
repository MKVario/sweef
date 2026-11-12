# Sweef – Wissensstand aus dem Soaring-Vario-Projekt

Stand: 19. Oktober 2026 · Firmware 2.10.24 · Sweef-App 1.0.17 · Platine v5 (vor dem Routen)

Dieses Dokument fasst zusammen, was im Projekt „Vario“ (Sweef Soaring-Vario) entstanden ist und gelernt wurde. Es dient als Startpunkt für das große Streckenflugvario („Sweef XL“).

---

## 1. Worum es geht

- **Sweef**: kleines Soaring-Vario am Tragegurt, fürs Küstensoaren (Sand, Salzluft), geflogen u. a. in Wilderness / Garden Route (Südafrika). Max. ca. 200 m über Start.
- Funktioniert **voll ohne Handy**. Das Handy (Android-App) ist Zugabe: Einstellungen, Flugbuch, GPS-Tempo, Flugweg, Updates.
- 5 Geräte für die Reisegruppe (Januar, Südafrika). Nur Mathias hat Android, die anderen iPhone → für sie Web-App im Browser **Bluefy**.
- Mathias: Elektrotechniker (Löten = Spezialgebiet), programmiert nicht selbst, kein Oszilloskop. 3D-Druck: Bambu Lab H2C, Konstruktion in Fusion 360.

### Arbeitsweise (bewährt)
- Antworten auf Deutsch, Downloads immer mit **vollständigem Link/Pfad**.
- Firmware-Dateien fortlaufend nummeriert: `sweef_vario_v2_10_N` (Ordner und Datei gleich), dazu `…_klein` für das kleine Display.
- App-Versionen fortlaufend: `sweef_app_v1_0_N.apk`.
- Code liegt im GitHub-Repo **MKVario/sweef** (öffentlich), baut automatisch, Releases unter „releases/latest“.

---

## 2. Hardware

### Prototyp (läuft)
- **Controller:** Seeed XIAO nRF52840 Sense (BLE, Lader, IMU, Mikrofon).
- **Drucksensor:** DPS310.
- **Display:** 1,5" OLED SH1107 128×128 (I²C 0x3C), altes kleines Display 128×64 (Firmware-Variante `klein`).
- **Ton:** Lautsprecher LSF-13 (13 mm, 8 Ω) an PAM8302 (Klasse D). Verstärker wird nur beim Piepen eingeschaltet (gegen Rauschen, spart Strom). Piezo war zu leise.
- **Akku:** 300 mAh LiPo (33 × 19,5 × 6 mm).
- **Taster:** 2 Edelstahl-Einbautaster (wasserdicht), extern an Lötpads.

### Eigene Platine v5 (Stand: vor dem Routen in Fusion)
- Größe 34,2 × 46,2 mm, unten Lasche (x 4,6–29,6, y 0–8), **keine Befestigungslöcher** mehr (Platine wird geklemmt).
- 2 Lagen, GND-Flächen beidseitig, Sperrfläche unter der XIAO-Antenne.
- **Vorne:** Displayglas (33,9 × 37,3), auf der Lasche nur 5 flache Display-Teile (C20–C22, R20, R21).
- **Hinten:** XIAO links (USB-C nach links), rechts DPS310 (unten), Kompass LIS2MDL, I²C-Pull-ups, 13-V-Wandler TPS61040 fürs Display; oben unter dem Akku Verstärker PAM8302A, Tonfilter, INA226 (Strommessung), Testpunkte, Lautsprecher-Pads oben links.
- **Lasche hinten:** Display-Stecker TE 2-1734839-4 (24 Pol, 0,5 mm, Kontakte oben, 0,3-mm-FPC) in der Mitte, Taster-Pads links, Akku-Pads rechts.
- **Displayglas GoldenMorning GME128128-02:** Flachbandkabel 20,0 mm ab Glaskante, 12,5 mm breit, Kontakte 3,0 mm, Versteifung 4,5 mm / 0,30 mm. **Kontakte auf der Vorderseite** (Sichtseite). Kabel geht um die Laschenkante nach hinten in den Stecker; Pin 1 von vorne gesehen links. Ca. 4 mm Überlänge als Schlaufe im Tasterbereich.
- **Bestellung geplant bei JLCPCB:** 2 Lagen, FR-4, 1,6 mm, **ENIG** (wegen 0,5-mm-Stecker und LGA-Sensoren). JLC bestückt nur die **Rückseite**; XIAO, Display-Stecker und die 5 Teile vorne lötet Mathias selbst. TE-Stecker evtl. nicht bei JLC → Mouser/Digikey.
- Stückliste (41 Teile) und Generator-Skripte liegen im Soaring-Projekt.

### Gehäuse (Fusion, 3D-Druck)
- Ca. 70 mm hoch, oben rund mit Lautsprecherkammer, weiß mit schwarzem Zebramuster, Taster Schwarz/Türkis.
- Abdichtung: Stufenfalz mit TPU-95A-Dichtung, Deckel drückt direkt aufs TPU.
- DPS310-Belüftung: ca. 10-mm-Loch mit 20-mm-PTFE-Membran, keine eigene Kammer (Membranloch jetzt rechts unten, wegen Platine v5).

---

## 3. Firmware (Arduino, Seeeduino nRF52 Core)

- Eine Quelle für beide Displays: `DISPLAY_KLEIN` 0/1, Version in `FW_VERSION`.
- **Funktionen:** Steigwert aus Druck + Beschleunigung, Ton mit vielen Parametern, Nullschieber, Sinkalarm, G-Warnung und G-Anzeige, wandernder Pfeil, Flugerkennung (Start/Landung), Flugaufzeichnung (1 Wert/s, laufend in den Flash → bei leerem Akku nicht verloren), Höhe/Höhe über Meer/QNH, Stumm am Boden, Auto-Aus, Akkuwarnung 10 %/5 %, PIN, Gerätename, Begrüßung, Vario suchen, Selbsttest, Lademodus mit Anzeige, Helligkeit, Display drehen, GPS-Tempo vom Handy anzeigen, gemittelte Steigwert-Zahl, **Startplatz-Frage** am Display (T1 = Ja / T2 = Nein, mit Startrichtung).
- **Bedienung:** T1 kurz lauter, T2 kurz leiser, T1 2 s → Aus (mit T2 bestätigen), T2 3 s Höhe 0, beide 5 s Neustart, T1 Doppelklick Flugmessung stoppen.
- **Einstellungen per Bluetooth (Kürzel):** se, sk, f1, f2, fk, fs, tl, ts, pa, nn, qn, pt, fh, fd, lr, ao, nb, hl, gw, la, za, ls (+ Schalter nz, ga, pf, ph, dr, sb).
- **BLE-Dienst** `7e5e0001-…`: Einstellungen `…0002`, Befehle `…0003`, Live `…0004`, Daten/Flüge `…0005`, Info `…0006`.
- **Befehle:** `null`, `reset`, `test=…`, `ls=…`, `suchen`, `fl_liste`, `fl_hol=…`, `fl_loeschen`, `zeit=…`, `gs=…` (Tempo), `dfu` (Update-Modus), `spfrage=Höhe|Name|Richtung`, `pin=…`, `nm=…`, `gt=…`.
- **Update per Bluetooth (DFU):** Befehl `dfu` → Adafruit-Bootloader. Danach startet das Vario selbst wieder eingeschaltet (`/nachupdate`) und zeigt „Update fertig! Version …“.

---

## 4. App

### Android-App (de.sweef.vario)
- Kotlin + WebView (die Web-Oberfläche läuft im App-Rahmen), Brücke `SweefNative`.
- Bluetooth hält die App selbst (Vordergrund-Dienst) → läuft auch bei ausgeschaltetem Bildschirm, verbindet automatisch neu.
- **Funktionen:**
  - GPS-Tempo ans Vario (1 Hz)
  - Flugbuch mit Höhenkurve, CSV-Export, Sicherung
  - **Flugweg** per Handy-GPS (14 Tage Puffer), Karte (OpenStreetMap), Export **KML (Google Earth, 3D)** und **IGC** (ohne Signatur)
  - **Startplätze:** eigene + ParaglidingEarth-Liste (offline, Suche, Entfernung, Startrichtung als Windrose), Startplatz-Frage am Vario
  - **Kompass** zum Messen der Startrichtung (mit Missweisungs-Korrektur)
  - **„Wo ist mein Vario?“** (Ort beim Verbindungsabriss, Karte, Hinführen per Google Maps)
  - Firmware-Update per Bluetooth und App-Selbst-Update
  - Profile, Design „Abendsonne Wilderness“ (Nachtblau/Amber), helle Farben als Schalter
- **Web-App** (GitHub Pages) für iPhone über Bluefy: gleiche Oberfläche, aber **kein GPS im Hintergrund** → kein Tempo/Flugweg, keine Firmware-Updates (die macht Mathias mit seinem Android-Handy).

### Bau und Veröffentlichung
- GitHub Actions: `firmware.yml` (arduino-cli, groß/klein), `android.yml` (Gradle, signiert mit Keystore + Secret `SWEEF_KEYSTORE_PW`), `release.yml` (bei jedem Push auf main → Release mit APK, Firmware-ZIP/HEX, version.txt).

---

## 5. Gelernt (Stolpersteine)

- **DFU mit dem Adafruit-Bootloader:** hing bei 1 % → braucht MTU 23 (kein MTU-Request) und kleine PRN (4). Nach dem Update startete das Vario „aus“ → Merker-Datei `/nachupdate`.
- **Arduino-Prototypen-Generator:** globale Variablen mit Funktionen vor den Includes → Fehler „BLECharacteristic not declared“.
- **Display-Flachbandkabel:** Kontaktseite vor dem Platinenentwurf **am Datenblatt prüfen** (Seitenansicht „CONTACT SIDE“). Beim Umklappen um eine Kante zeigen die Kontakte nach außen → Lötfeld/Stecker-Bauform muss passen.
- **ZIF-Stecker** brauchen mit Befestigungslaschen ca. 17,4 mm Breite → Platz neben Schrauben einplanen.
- **Handy-GPS-Höhe** ist nur ±10–30 m genau → Startplatzhöhe besser aus Datenbank/eigenen Startplätzen, Barometer fürs Relative.
- **iPhone/Web Bluetooth:** nur mit Bluefy, kein Hintergrund-GPS.
- **ParaglidingEarth-Höhen** sind von der Gemeinschaft eingetragen → beim ersten Besuch prüfen.

---

## 6. Offen im Soaring-Projekt

- Platine v5 routen (Fusion-Autorouter), Prüfung, Gerber/BOM/CPL, JLC-Bestellung.
- Akku-Laufzeittest auswerten.
- Kurzanleitung für die Freunde (vor der Reise).
- Angeboten: Windanzeige mit Ampel (Open-Meteo + Startrichtung).

---

## 7. Ideen für das Streckenflugvario (Sweef XL)

- **Eigenes GPS** (kein Handy nötig): Flugweg, IGC, Tempo, Wind.
- **FANET** (LoRa 868 MHz): z. B. Seeed **Wio-SX1262 für XIAO** (bis 22 dBm, ca. 10 €), Positionen der Gruppe sehen, Rückholen erleichtern. Offene Umsetzung: **GXAirCom** (FANET+). Antenne (ca. 8,6 cm) und Platz einplanen; 868-MHz-Zulassung je Land prüfen (Südafrika).
- **Luftraum** mit Warnung, **Live-Tracking**.
- Größeres Display, Kompass-Ansicht (neigt sich mit), Windschätzung aus GPS beim Kreisen.
- **INA226**: Akku in Prozent oder Restflugzeit.
- Wind-Vorhersage (Open-Meteo) mit Ampel passend zur Startrichtung.
- Übernehmen aus Sweef: Startplatz-Frage, „Wo ist mein Vario?“, Bluetooth-Updates, Profile, KML/IGC, ParaglidingEarth-Startplätze, Kompass, Design.
- Vergleich am Markt: XC Tracer Mini V (46 g, ohne Display, ca. 590 $), SkyDrop (Display + GPS, ca. 395 $), Skybean Strato (Farbdisplay, 180 g, ca. 545 $), meist mit XCTrack.
