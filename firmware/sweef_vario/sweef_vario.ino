// Sweef - Soaring-Vario, Version 2 (Teil 10: grosses Display)
// Neu in Teil 10:
// - 1,5" OLED 128 x 128 (SH1107, Variante "Seeed", I2C 0x3C)
//   Anschluss wie bisher: VCC=3V3, GND, SCL=D5, SDA=D4
// - alle Bilder neu fuer 128 x 128: Steigwert gross, Akku in Prozent
// Teil 9:
// - Begruessungstext (aus der App) beim Einschalten, darunter die Geraete-ID
// - Geraetename "Sweef-xxxx" (eigene Kennung, in der App aenderbar)
// - PIN (in der App einstellbar), die App merkt sich das Handy
// - Stumm am Boden: Ton erst nach erkanntem Start (Taste kurz = sofort Ton)
// - Akkuwarnung bei 10 % und 5 %, bei leerem Akku am Boden aus
// - Selbsttest beim Einschalten
// - Nullschieber: leises Ticken bei schwachem Sinken (in der App schaltbar)
// - Display um 180 Grad drehbar (in der App)
// - Vario suchen: lauter Piepton per App, jede Taste beendet ihn
// - PIN vergessen: im Lademodus (aus, Kabel dran) Taste 2 8 s halten
// Teil 8: Akku-Symbol unten auf dem Display (blinkt unter 10 %).
// Ladeanzeige mit Prozent: erscheint, wenn das USB-Kabel eingesteckt wird,
// waehrend das Vario aus ist (oder beim Ausschalten mit Kabel).
// "voll" meldet der Ladechip selbst, das ist zuverlaessig. Die Prozent
// waehrend des Ladens sind eine Schaetzung aus der Spannung.
// Ladestrom jetzt 100 mA statt 50 mA (laedt doppelt so schnell).
// Teil 7: schaltet sich nach 30 Minuten ohne Bewegung am Boden aus
// (mit 30 s Vorwarnung), im Flug laesst es sich nicht ausschalten.
// Teil 6: Flugaufzeichnung, Uebertragung aufs Handy.
// Teil 5: Bluetooth-Einstellungen
// Neu in Teil 5: Einstellungen per Handy ueber Bluetooth (Name "Sweef").
// Die Einstellungen werden im Vario gespeichert. Die Werte unten sind nur
// noch die Grundeinstellung (beim allerersten Start oder nach "Zuruecksetzen").
// Teil 4: Flugzeit, maximale Hoehe, Hoehe ueber Meer.
// Teil 3: wandernder Pfeil hinter der Steigzahl, Zahl dort invertiert.
// -----------------------------------------------------------------
// - Bewegungssensor + Luftdruck ergeben den Steigwert (wie Teil 1)
// - 6 Lautstaerkestufen + stumm, die Stufe wird gespeichert
// - Ein/Aus wie beim Skytraxx: Taste halten, dann mit Taste 2 bestaetigen
// - Aus = Tiefschlaf (nur mit Akku, ohne USB). Mit USB-Kabel geht es nur
//   in den Lademodus (Akku-Anzeige), damit das Hochladen klappt.
//
// Bedienung:
// - Taste 1 kurz:           lauter (bis Stufe 6)
// - Taste 1 Doppelklick:    Flugmessung stoppen
// - Taste 2 kurz:           leiser (bis stumm)
// - Taste 1 2 s halten:     "Ausschalten?" -> mit Taste 2 bestaetigen
// - Taste 2 3 s halten:     Hoehe auf 0
// - beide 5 s halten:       Neustart (alles auf 0, Sensor neu kalibrieren)
// - Einschalten:            Taste 1 2 s halten -> mit Taste 2 bestaetigen
// - Am Boden (Anzeige "Boden"): Taste kurz = Ton schon jetzt an
// - Vario suchen laeuft:    beliebige Taste = Ton aus
// - Lademodus, Taste 2 8 s: PIN loeschen

// Display-Variante: 0 = 1,5" 128x128 (SH1107), 1 = altes 0,96" 128x64 (SSD1306)
// Beim Bauen auf GitHub wird das automatisch fuer beide Varianten gesetzt.
#ifndef DISPLAY_KLEIN
#define DISPLAY_KLEIN 0
#endif
#include <Wire.h>
#include <Adafruit_DPS310.h>
#include <U8g2lib.h>
#include <LSM6DS3.h>
#include <InternalFileSystem.h>
#include <bluefruit.h>
#include <Adafruit_SPIFlash.h>
using namespace Adafruit_LittleFS_Namespace;

#define FW_VERSION "2.10.31"
unsigned long updateModusAb = 0;     // >0: Bootloader fuer Bluetooth-Update starten
// Geschwindigkeit ueber Grund vom Handy-GPS ("gs=28.4" in km/h, jede Sekunde)
volatile float gsKmh = -1;
volatile unsigned long gsZeit = 0;
bool gsDa() { return gsZeit != 0 && millis() - gsZeit < 5000 && gsKmh >= 0; }
// Wind, vom Handy geschaetzt ("wi=25,230": km/h, kommt aus Grad)
volatile int windKmh = -1, windAus = 0;
volatile unsigned long windZeit = 0;
bool windDa() { return windZeit != 0 && millis() - windZeit < 90000UL && windKmh >= 0; }
// Sonnenuntergang (Unix-Zeit) vom Handy ("su=1767288000")
volatile uint32_t sonneUnter = 0;
bool gegenwindAktiv = false;           // Gegenwind-Warnung laeuft
unsigned long gegenwindSeit = 0;
int  restMin = -1;                     // geschaetzte Restlaufzeit in Minuten (-1 = noch unbekannt)

// ---------- Einstellungen (hier kannst du Werte aendern) ----------
float STEIG_EIN    = 0.10;  // m/s, ab hier piept es
float STEIG_AUS    = 0.05;  // m/s, darunter hoert es auf (Hysterese)
float SINK_ALARM   = -2.5;  // m/s, ab hier Sinkton
int   TON_MIN      = 700;   // Hz bei schwachem Steigen
int   TON_MAX      = 2400;  // Hz beim Steigen von TON_SKALA
int   TON_SINK     = 600;   // Hz Sinkalarm
int   TAKT_LANGSAM = 750;   // ms Piep+Pause bei schwachem Steigen
int   TAKT_SCHNELL = 280;   // ms Piep+Pause beim Steigen von TON_SKALA
int   PIEP_ANTEIL  = 50;    // Prozent des Takts, in dem der Ton klingt
float TON_SKALA    = 18.75; // m/s, hier ist der Ton am hoechsten/schnellsten

// Ton am Boden: erst nach erkanntem Start piepen (Ground-Handling)
bool  STUMM_AM_BODEN = true;
// Nullschieber: leises Ticken, wenn es kaum noch sinkt (Thermik/Hang-Kante)
bool  NULLSCHIEBER   = false;
float NULL_AB        = -0.3;   // m/s, ab hier tickt es (bis zum Steigton)
const int NULL_FREQ  = 380;    // Hz, tiefer Ton
// Display um 180 Grad drehen (Vario andersherum am Gurt)
bool  DISPLAY_DREHEN = false;
int   HELLIGKEIT     = 100;  // Display-Helligkeit in % (per Handy einstellbar)
bool  G_AUTO         = true;   // in Spiralen G-Last statt Hoehe anzeigen
float G_WARNUNG      = 0;      // g, ab hier Warnton (0 = aus)
int   GEGENWIND_AB   = 5;      // km/h ueber Grund im Flug: darunter Gegenwind-Warnung (0 = aus)
bool  WIND_ANZEIGEN  = true;   // geschaetzten Wind am Display zeigen
bool  RESTZEIT       = true;   // Restlaufzeit statt Prozent (sobald genug gemessen)
int   SONNE_VORHER   = 30;     // Minuten vor Sonnenuntergang Hinweis (0 = aus)
bool  DOPPELTIPP     = false;  // zweimal aufs Gehaeuse tippen = stumm / Ton an

// Auto-Aus: nach so vielen Minuten ohne Bewegung am Boden ausschalten (0 = nie)
int   AUTO_AUS_MIN       = 30;
int   DISPLAY_AUS_MIN    = 0;      // Display am Boden aus nach ... Minuten ohne Tastendruck (0 = nie)
bool  liveHintergrund    = false;  // App im Hintergrund: Live-Werte seltener senden
bool  displayAus         = false;  // Display gerade zum Stromsparen aus
unsigned long letzteTaste = 0;
unsigned long schnellBis = 0;      // bis dahin schneller Bluetooth-Takt (Fluege uebertragen)

// Flug-Funktionen
float STARTPLATZ_NN      = 0;    // m ueber Meer, wo du die Hoehe nullst.
                                       // 0 = aus Normaldruck schaetzen (ungenau)
float QNH                = 0;    // hPa, Luftdruck auf Meereshoehe (aus dem
                                 // Flugwetter). 0 = nicht benutzen
float FLUG_START_HOEHE   = 5.0;  // m Hoehenaenderung = Flug beginnt
int   FLUG_START_DAUER_S = 10;   // s, so lange muss die Hoehe abweichen
int   LANDUNG_RUHE_S     = 120;  // s ruhig auf gleicher Hoehe = gelandet
int LIVE_MS = 500;                  // so oft gehen Live-Werte ans Handy (ms)
int ZAHL_MS = 500;                  // Steigwert-Zahl auf dem Display: Mittel ueber ... ms
const int   ANZEIGE_MS   = 40;    // neues Bild alle 40 ms (25 Bilder pro Sekunde) im Flug
int anzeigeTakt();                // am Boden seltener (spart Strom)

// Pfeil im Hintergrund der Steigzahl
bool  PFEIL_AN     = true;  // false = kein Pfeil
bool  PFEIL_HOHL   = false; // true = nur Umriss, false = ausgefuellt
const float PFEIL_AB     = 0.2;   // m/s, ab hier (Steigen oder Sinken) erscheint er
float PFEIL_TEMPO  = 25.0;  // Pixel pro Sekunde je 1 m/s
const float PFEIL_MAX    = 120.0;  // hoechstes Tempo in Pixel pro Sekunde

// Lautstaerke: Anteil (in Promille), in dem das Tonsignal "an" ist.
// 500 = am lautesten. Jede Stufe darunter ist etwa halb so laut.
const int   LAUT_STUFEN  = 6;
const int   LAUT_TABELLE[LAUT_STUFEN + 1] = {0, 15, 30, 60, 125, 250, 500};
const int   LAUT_START   = 4;     // Stufe beim allerersten Einschalten

// Bedienung (Zeiten in Millisekunden)
const unsigned long HALTEN_AUS     = 2000;   // Taste 1 halten zum Aus-/Einschalten
const unsigned long DOPPELKLICK_MS = 400;    // max. Abstand fuer Doppelklick
const unsigned long HALTEN_HOEHE   = 3000;   // Taste 2 halten fuer Hoehe 0
const unsigned long HALTEN_NEU     = 5000;   // beide halten fuer Neustart
const unsigned long BESTAETIGEN_MS = 10000;  // Zeit fuer die Bestaetigung

// Akku: falls die angezeigte Spannung vom Multimeter abweicht, hier
// korrigieren: Multimeter-Wert geteilt durch angezeigten Wert (z.B. 4.05/4.00 = 1.0125)
const float AKKU_KORREKTUR = 1.000;

// Filter-Einstellungen (nur aendern, wenn es zu unruhig oder zu traege ist)
const float DRUCK_RAUSCHEN    = 0.02;
const float BESCHL_RAUSCHEN   = 0.10;
const float OHNE_IMU_REAKTION = 2.0;

// ---------- Anschluesse ----------
const int PIN_TON    = D2;
const int PIN_VERST  = D3;
const int PIN_TASTE1 = D0;
const int PIN_TASTE2 = D1;

Adafruit_DPS310 dps;
#if DISPLAY_KLEIN
U8G2_SSD1306_128X64_NONAME_F_HW_I2C anzeige(U8G2_R1, U8X8_PIN_NONE);   // altes 0,96" OLED 128x64, hochkant
#else
U8G2_SH1107_SEEED_128X128_F_HW_I2C anzeige(U8G2_R0, U8X8_PIN_NONE);   // 1,5" OLED 128x128
#endif
LSM6DS3 imu(I2C_MODE, 0x6A);

// ---------- Zustand ----------
bool  sensorOk   = false;
bool  imuOk      = false;
bool  gestartet  = false;
int   lautStufe  = LAUT_START;   // 0 = stumm
float nullDruck  = 0;

void meldung(const char* z1, const char* z2, const char* z3 = "", const char* z4 = "");
// Startplatz-Frage vom Handy: "Startplatz? Name, Hoehe  T1 = Ja, T2 = Nein"
char frageName[28] = "";
char frageRichtung[12] = "";    // gute Startrichtungen, z. B. "S/SW"
int frageHoehe = 0;
unsigned long frageBis = 0;
bool frageAktiv() { return frageBis && (long)(millis() - frageBis) < 0; }
void frageBox();

bool taste1() { return digitalRead(PIN_TASTE1) == LOW; }
bool taste2() { return digitalRead(PIN_TASTE2) == LOW; }

bool usbSteckt() {
  uint8_t sdAn = 0;
  sd_softdevice_is_enabled(&sdAn);
  uint32_t status;
  if (sdAn) sd_power_usbregstatus_get(&status);     // Bluetooth laeuft
  else      status = NRF_POWER->USBREGSTATUS;
  return (status & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
}

// =================================================================
// Akku
// =================================================================
// Auf dem XIAO: Spannungsteiler 1 MOhm / 510 kOhm an P0.31,
// eingeschaltet mit P0.14 = LOW. Ladechip-Ausgang ~CHG an P0.17
// (LOW = laedt). P0.13 = LOW waehlt 100 mA Ladestrom.
const int PIN_LADEN = 23;              // P0.17
const int HELL      = 0xFF;            // normale Display-Helligkeit

float akkuV    = 0;                    // geglaettete Akkuspannung
bool  akkuVoll = false;
int   ladeMax  = 0;                    // beim Laden geht die Anzeige nie zurueck
// Nach dem Abziehen eines vollen Akkus sinkt die Spannung etwas, ohne dass
// Ladung fehlt. Damit die Anzeige nicht sofort auf ~90 % springt, wird der
// Unterschied gemerkt und bis 70 % allmaehlich ausgeblendet.
bool  warVoll     = false;
int   vollAusgleich = 0;               // Prozentpunkte, die anfangs fehlen
int   vollStart     = 0;               // Kurvenwert beim Abziehen
int   vollNachlauf  = 0;               // Sekunden, in denen vollStart noch sinken darf
int   vollGespeichert = -1;            // was in /voll steht (-1 = nichts)
const int VOLL_NACHLAUF = 300;         // 5 Minuten Entspannung nach dem Abziehen

float akkuMessen() {
  long summe = 0;
  for (int i = 0; i < 16; i++) summe += analogRead(PIN_VBAT);
  return summe / 16.0 / 4095.0 * 3.0 * (1510.0 / 510.0) * AKKU_KORREKTUR;
}

void akkuStarten() {
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, LOW);      // Messung an (schuetzt auch den Eingang)
  pinMode(PIN_CHARGING_CURRENT, OUTPUT);
  digitalWrite(PIN_CHARGING_CURRENT, LOW);   // 100 mA Ladestrom
  pinMode(PIN_LADEN, INPUT_PULLUP);
  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  analogSampleTime(40);                // hochohmiger Teiler braucht Zeit
  delay(5);
  akkuV = akkuMessen();
  // "frisch voll" aus dem letzten Schlaf zurueckholen
  Adafruit_LittleFS_Namespace::File f(InternalFS);
  if (f.open("/voll", FILE_O_READ)) {
    uint8_t b[2];
    if (f.read(b, 2) == 2 && b[0] > 70 && b[0] <= 100) {
      vollStart = b[0];
      vollAusgleich = 100 - vollStart;
      vollNachlauf = b[1] ? VOLL_NACHLAUF : 0;   // war noch in der Entspannung?
      vollGespeichert = b[0] * 2 + (b[1] ? 1 : 0);
    }
    f.close();
  }
}

// vor dem Schlafen: Zustand in /voll merken (nur schreiben, wenn sich was aendert)
void vollSichern() {
  if (warVoll && !usbSteckt()) {                  // gerade erst abgezogen
    warVoll = false;
    vollStart = prozentAusSpannung(akkuV);
    vollAusgleich = 100 - vollStart;
    vollNachlauf = VOLL_NACHLAUF;
  }
  if (usbSteckt() && akkuVoll) {                  // schlaeft am Kabel, voll
    vollStart = 99; vollNachlauf = VOLL_NACHLAUF;  // beim Aufwachen neu einpendeln
  } else if (vollAusgleich <= 0 || vollStart <= 70) {
    if (vollGespeichert != -1) InternalFS.remove("/voll");
    vollGespeichert = -1;
    return;
  }
  int code = vollStart * 2 + (vollNachlauf > 0 ? 1 : 0);
  if (code == vollGespeichert) return;
  InternalFS.remove("/voll");
  Adafruit_LittleFS_Namespace::File f(InternalFS);
  if (f.open("/voll", FILE_O_WRITE)) {
    uint8_t b[2] = { (uint8_t)vollStart, (uint8_t)(vollNachlauf > 0 ? 1 : 0) };
    f.write(b, 2);
    f.close();
    vollGespeichert = code;
  }
}

bool laedt() { return digitalRead(PIN_LADEN) == LOW; }

// Spannung -> Prozent. Gemessen am Sweef (Akkutest 19./20.10.2026, ca. 14 mA):
// Prozent = Anteil der restlichen Laufzeit bis zur Abschaltung bei 3,40 V.
int prozentAusSpannung(float v) {
  const float V[] = {3.400, 3.435, 3.472, 3.505, 3.530, 3.549, 3.565, 3.580, 3.593, 3.611, 3.630,
                     3.663, 3.704, 3.750, 3.799, 3.849, 3.902, 3.956, 4.014, 4.076, 4.150};
  if (v <= V[0])  return 0;
  if (v >= V[20]) return 100;
  int i = 0;
  while (v > V[i + 1]) i++;
  return i * 5 + (int)(5.0 * (v - V[i]) / (V[i + 1] - V[i]));
}

// in loop() aufrufen, misst selbst nur einmal pro Sekunde
void akkuAktualisieren() {
  static unsigned long letzte = 0, nichtLadenSeit = 0;
  if (millis() - letzte < 1000) return;
  letzte = millis();
  akkuV += 0.15 * (akkuMessen() - akkuV);

  bool usb = usbSteckt();
  if (usb && !laedt() && akkuV > 4.0) {
    if (nichtLadenSeit == 0) nichtLadenSeit = millis();
    if (millis() - nichtLadenSeit > 5000) akkuVoll = true;   // Ladechip fertig
  } else {
    nichtLadenSeit = 0;
    if (!usb || laedt()) akkuVoll = false;
  }
  if (!usb) ladeMax = 0;
  if (usb && akkuVoll) warVoll = true;           // voll geladen, Kabel steckt
  if (!usb && warVoll) {                          // gerade abgezogen
    warVoll = false;
    vollStart = prozentAusSpannung(akkuV);
    vollAusgleich = 100 - vollStart;
    vollNachlauf = VOLL_NACHLAUF;
  }
  // Die ersten Minuten sinkt die Spannung noch, ohne dass Ladung fehlt:
  // so lange wird der Startwert nachgefuehrt, die Anzeige bleibt bei 100 %.
  if (!usb && vollNachlauf > 0) {
    vollNachlauf--;
    int p = prozentAusSpannung(akkuV);
    if (p < vollStart && p > 70) { vollStart = p; vollAusgleich = 100 - p; }
  }
  if (usb && laedt()) { vollAusgleich = 0; vollNachlauf = 0; }   // neu am Laden
}

int akkuProzent() {
  if (akkuVoll) return 100;
  if (usbSteckt() && laedt()) {
    // beim Laden ist die Spannung etwas hoeher als in Ruhe
    int p = min(99, prozentAusSpannung(akkuV - 0.05));
    if (p > ladeMax) ladeMax = p;
    return ladeMax;
  }
  int p = prozentAusSpannung(akkuV);
  if (vollAusgleich > 0 && !usbSteckt()) {
    if (p <= 70 || vollStart <= 70) { vollAusgleich = 0; return p; }
    // Ausgleich wird bis 70 % linear kleiner
    float anteil = (float)(p - 70) / (vollStart - 70);
    if (anteil > 1) anteil = 1;
    p = min(100, p + (int)(vollAusgleich * anteil + 0.5f));
  }
  return p;
}

bool akkuFehlt() { return akkuV < 2.5; }

// =================================================================
// Kennung, Name, Begruessung, PIN (im internen Speicher)
// =================================================================
char geraeteId[5]   = "0000";  // aus der Chip-Kennung, z.B. "A3F2"
char nameZusatz[16] = "";      // Name = "Sweef-" + Zusatz (leer = ID)
char gruss[100]     = "";      // Begruessung, Zeilen getrennt durch '|'
char pinCode[8]     = "";      // leer = keine PIN
uint32_t schluessel[3] = {0, 0, 0};   // gemerkte Handys

void textLaden(const char* datei, char* ziel, int groesse) {
  ziel[0] = 0;
  Adafruit_LittleFS_Namespace::File f(InternalFS);
  if (f.open(datei, FILE_O_READ)) {
    int n = f.read(ziel, groesse - 1);
    f.close();
    ziel[n > 0 ? n : 0] = 0;
  }
}

void textSpeichern(const char* datei, const char* text) {
  InternalFS.remove(datei);
  if (!text[0]) return;
  Adafruit_LittleFS_Namespace::File f(InternalFS);
  if (f.open(datei, FILE_O_WRITE)) {
    f.write((const uint8_t*)text, strlen(text));
    f.close();
  }
}

void schluesselSpeichern() {
  InternalFS.remove("/schl");
  Adafruit_LittleFS_Namespace::File f(InternalFS);
  if (f.open("/schl", FILE_O_WRITE)) {
    f.write((uint8_t*)schluessel, sizeof(schluessel));
    f.close();
  }
}

void kennungLaden() {
  snprintf(geraeteId, sizeof(geraeteId), "%04X", (unsigned)(NRF_FICR->DEVICEADDR[0] & 0xFFFF));
  textLaden("/name", nameZusatz, sizeof(nameZusatz));
  textLaden("/gruss", gruss, sizeof(gruss));
  textLaden("/pin", pinCode, sizeof(pinCode));
  Adafruit_LittleFS_Namespace::File f(InternalFS);
  if (f.open("/schl", FILE_O_READ)) {
    f.read((uint8_t*)schluessel, sizeof(schluessel));
    f.close();
  }
}

void geraeteName(char* buf, int groesse) {
  snprintf(buf, groesse, "Sweef-%s", nameZusatz[0] ? nameZusatz : geraeteId);
}

bool pinAktiv() { return pinCode[0] != 0; }

void pinLoeschen() {
  pinCode[0] = 0;
  textSpeichern("/pin", "");
  for (int i = 0; i < 3; i++) schluessel[i] = 0;
  schluesselSpeichern();
}

// Test-Steigwert vom Handy ("Ton testen"): ersetzt fuer ein paar
// Sekunden den echten Steigwert bei Ton und Anzeige
float testSteigen = 0;
unsigned long testBis = 0;

// =================================================================
// Lage-Berechnung (Mahony-Filter)
// =================================================================
float q0 = 1, q1 = 0, q2 = 0, q3 = 0;
float iFx = 0, iFy = 0, iFz = 0;
float gyroOffX = 0, gyroOffY = 0, gyroOffZ = 0;

void lageAktualisieren(float gx, float gy, float gz,
                       float ax, float ay, float az, float dt, float kp) {
  const float KI = 0.05;
  float n = sqrtf(ax * ax + ay * ay + az * az);
  if (n > 0.01f) {
    ax /= n; ay /= n; az /= n;
    float vx = 2 * (q1 * q3 - q0 * q2);
    float vy = 2 * (q0 * q1 + q2 * q3);
    float vz = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3;
    float ex = ay * vz - az * vy;
    float ey = az * vx - ax * vz;
    float ez = ax * vy - ay * vx;
    iFx += KI * ex * dt; iFy += KI * ey * dt; iFz += KI * ez * dt;
    gx += kp * ex + iFx;
    gy += kp * ey + iFy;
    gz += kp * ez + iFz;
  }
  float hdt = 0.5f * dt;
  float a = q0, b = q1, c = q2, d = q3;
  q0 += (-b * gx - c * gy - d * gz) * hdt;
  q1 += ( a * gx + c * gz - d * gy) * hdt;
  q2 += ( a * gy - b * gz + d * gx) * hdt;
  q3 += ( a * gz + b * gy - c * gx) * hdt;
  n = sqrtf(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
  q0 /= n; q1 /= n; q2 /= n; q3 /= n;
}

float senkrechteBeschl(float ax, float ay, float az) {
  float vx = 2 * (q1 * q3 - q0 * q2);
  float vy = 2 * (q0 * q1 + q2 * q3);
  float vz = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3;
  return (vx * ax + vy * ay + vz * az - 1.0f) * 9.807f;
}

// =================================================================
// Kalman-Filter: Hoehe, Steigen, Beschleunigungs-Fehler
// =================================================================
float h = 0, v = 0, bias = 0;
float P[3][3];

// Steigwert fuer Ton und Anzeige (waehrend "Ton testen" der Testwert)
float vJetzt() {
  if (testBis != 0 && (long)(millis() - testBis) < 0) return testSteigen;
  testBis = 0;
  return v;
}

// Steigwert fuer die Zahl auf dem Display: ueber ZAHL_MS gemittelt, damit
// sie ruhig bleibt. Ton und Pfeile nutzen weiter den schnellen Wert.
float vAnzeige() {
  static float summe = 0, wert = 0;
  static int anzahl = 0;
  static unsigned long seit = 0;
  float jetzt = vJetzt();
  if (testBis != 0) { summe = 0; anzahl = 0; seit = millis(); return wert = jetzt; }   // Ton-Test: sofort
  summe += jetzt; anzahl++;
  if (millis() - seit >= (unsigned long)ZAHL_MS) {
    wert = summe / anzahl;
    summe = 0; anzahl = 0; seit = millis();
  }
  return wert;
}

void kalmanReset() {
  h = 0; v = 0; bias = 0;
  for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) P[i][j] = 0;
  P[0][0] = 1; P[1][1] = 1; P[2][2] = 0.1;
}

void kalmanVorhersage(float a, float dt, float qBeschl) {
  float acc = a - bias;
  h += v * dt + 0.5f * acc * dt * dt;
  v += acc * dt;
  float F[3][3] = {{1, dt, -0.5f * dt * dt}, {0, 1, -dt}, {0, 0, 1}};
  float FP[3][3], N[3][3];
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) {
      FP[i][j] = 0;
      for (int k = 0; k < 3; k++) FP[i][j] += F[i][k] * P[k][j];
    }
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) {
      N[i][j] = 0;
      for (int k = 0; k < 3; k++) N[i][j] += FP[i][k] * F[j][k];
    }
  float g0 = 0.5f * dt * dt, g1 = dt;
  N[0][0] += qBeschl * g0 * g0;
  N[0][1] += qBeschl * g0 * g1;
  N[1][0] += qBeschl * g0 * g1;
  N[1][1] += qBeschl * g1 * g1;
  N[2][2] += (imuOk ? 1e-4f : 0) * dt;
  for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) P[i][j] = N[i][j];
}

void kalmanKorrektur(float messHoehe, float r) {
  float S = P[0][0] + r;
  float K[3] = {P[0][0] / S, P[1][0] / S, P[2][0] / S};
  float y = messHoehe - h;
  h += K[0] * y;
  v += K[1] * y;
  if (imuOk) bias += K[2] * y;
  float P0[3] = {P[0][0], P[0][1], P[0][2]};
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) P[i][j] -= K[i] * P0[j];
}

float hoeheAusDruck(float p) {
  return 44330.0 * (1.0 - pow(p / nullDruck, 0.1903));
}

// =================================================================
// Bewegungssensor
// =================================================================
unsigned long letzteImuZeit = 0;
unsigned long imuStartMs = 0;
float letzteBeschl = 0;
float gWert    = 1.0;     // G-Last (Betrag aller drei Achsen, geglaettet)
float gMaxFlug = 0;       // hoechste G-Last im aktuellen Flug
extern bool imFlug;

bool imuStarten() {
  imu.settings.gyroRange       = 500;
  imu.settings.gyroSampleRate  = 104;
  imu.settings.accelRange      = 8;     // +-8 g, damit Spiralen nicht abgeschnitten werden
  imu.settings.accelSampleRate = 104;
  imu.settings.accelBandWidth  = 50;
  if (imu.begin() != 0) return false;

  // 1 Sekunde ruhig liegen lassen: Nullpunkt der Drehrate messen
  float sx = 0, sy = 0, sz = 0, ax = 0, ay = 0, az = 0;
  int n = 0;
  unsigned long start = millis();
  while (millis() - start < 1000) {
    sx += imu.readFloatGyroX(); sy += imu.readFloatGyroY(); sz += imu.readFloatGyroZ();
    ax += imu.readFloatAccelX(); ay += imu.readFloatAccelY(); az += imu.readFloatAccelZ();
    n++;
    delay(10);
  }
  if (n == 0) return false;
  gyroOffX = sx / n; gyroOffY = sy / n; gyroOffZ = sz / n;
  ax /= n; ay /= n; az /= n;
  if (sqrtf(ax * ax + ay * ay + az * az) < 0.5f) return false;

  q0 = 1; q1 = q2 = q3 = 0; iFx = iFy = iFz = 0;
  for (int i = 0; i < 300; i++) lageAktualisieren(0, 0, 0, ax, ay, az, 0.01f, 10.0f);
  imuStartMs = millis();
  letzteImuZeit = micros();
  return true;
}

bool imuLesen() {
  uint8_t status = 0;
  imu.readRegister(&status, LSM6DS3_ACC_GYRO_STATUS_REG);
  if ((status & 0x01) == 0) return false;

  uint8_t roh[12];
  imu.readRegisterRegion(roh, LSM6DS3_ACC_GYRO_OUTX_L_G, 12);
  int16_t w[6];
  for (int i = 0; i < 6; i++) w[i] = (int16_t)(roh[2 * i] | (roh[2 * i + 1] << 8));

  const float GRAD = 0.0174533f;
  float gx = (imu.calcGyro(w[0]) - gyroOffX) * GRAD;
  float gy = (imu.calcGyro(w[1]) - gyroOffY) * GRAD;
  float gz = (imu.calcGyro(w[2]) - gyroOffZ) * GRAD;
  float ax = imu.calcAccel(w[3]);
  float ay = imu.calcAccel(w[4]);
  float az = imu.calcAccel(w[5]);

  unsigned long jetzt = micros();
  float dt = (jetzt - letzteImuZeit) / 1e6;
  letzteImuZeit = jetzt;
  if (dt <= 0 || dt > 0.1f) return false;

  float kp = (millis() - imuStartMs < 2000) ? 5.0f : 0.5f;
  lageAktualisieren(gx, gy, gz, ax, ay, az, dt, kp);
  letzteBeschl = senkrechteBeschl(ax, ay, az);

  // G-Meter: Betrag der Beschleunigung, ueber ca. 0,4 s geglaettet
  float betrag = sqrtf(ax * ax + ay * ay + az * az);
  gWert += (betrag - gWert) * min(1.0f, dt / 0.4f);
  if (imFlug && gWert > gMaxFlug) gMaxFlug = gWert;

  if (gestartet) kalmanVorhersage(letzteBeschl, dt, BESCHL_RAUSCHEN);
  return true;
}

// =================================================================
// Messen: Bewegungssensor + Luftdruck (wird staendig aufgerufen)
// =================================================================
unsigned long letzteMessung = 0;

// =================================================================
// Flug-Funktionen: Flugzeit, maximale Hoehe, Hoehe ueber Meer
// =================================================================
bool  imFlug        = false;
bool  flugGewesen   = false;     // es gab schon einen Flug (Zeit anzeigen)
unsigned long flugStartMs = 0;
unsigned long flugDauerMs = 0;
float maxHoehe      = 0;
float bezugHoehe    = 0;         // Hoehe am Boden vor dem Start
float letzterDruck  = 0;
bool  bodenFrei     = false;     // am Boden per Taste schon Ton erlaubt

// Am Boden stumm (bis der Start erkannt wird)?
bool bodenStumm() { return STUMM_AM_BODEN && !imFlug && !bodenFrei; }

// =================================================================
// Flugspeicher (2 MB Zusatzspeicher auf dem XIAO)
// =================================================================
// Jeder Flug beginnt an einer 4-KB-Grenze:
//   32 Byte Kopf, dann pro Sekunde 2 Byte Hoehe (dm + 32768).
// Kopf: 0 "SWF1" | 4 Nr | 8 Startzeit (Unix, 0=unbekannt) | 12 Einschalt-Nr
//       16 Sekunden seit Einschalten | 20 Hoehe ueber Meer am Nullpunkt (m)
//       22 Sekunden vor dem Start | 24 Anzahl gueltig | 28 Anzahl geschrieben
// Noch nicht geschriebene Bytes sind 0xFF (so ist Flash-Speicher geloescht).
Adafruit_FlashTransport_QSPI flashTransport;
Adafruit_SPIFlash flash(&flashTransport);
const SPIFlash_Device_t FLASH_P25Q16H = {
  (1UL << 21), 10000, 0x85, 0x60, 0x15, 55, 0x02,
  false, true, true, true, false, false, false
};
bool flashOk = false;
uint8_t flashKennung[3] = {0, 0, 0};   // nur bei Fehler gelesen (soll 85 60 15)

struct FlugInfo { uint32_t adr, nr, zeit, boot, up, anzahl; int16_t nn; uint16_t vor; };
const int MAX_FLUEGE = 300;
FlugInfo fluege[MAX_FLUEGE];
int anzFluege = 0;
uint32_t logEndeAdr = 0;       // ab hier ist der Speicher frei (geloescht)
uint32_t naechsteNr = 1;
uint32_t bootNr = 0;           // wie oft eingeschaltet (fuer die Uhrzeit)
int64_t  zeitBasis = 0;        // Unix-Zeit minus Sekunden seit Start (vom Handy)

bool     logAktiv = false;
uint32_t logAdr = 0;           // Kopf des laufenden Flugs
uint32_t logGeschrieben = 0;   // geschriebene Werte
uint16_t logVor = 0;
uint16_t logPuffer[32];
int      logPufferAnz = 0;
float    vorRing[64];          // Hoehe der letzten 64 Sekunden (am Boden)
int      vorRingPos = 0, vorRingAnz = 0;

uint32_t jetztUnix() {
  if (zeitBasis == 0) return 0;
  return (uint32_t)(zeitBasis + millis() / 1000);
}
uint32_t aufRunden4k(uint32_t a) { return (a + 4095) & ~4095UL; }
uint16_t hoeheWert(float hm) {
  long dm = lroundf(hm * 10) + 32768;
  return (uint16_t)constrain(dm, 0, 0xFFFE);      // 0xFFFF = leer
}

// Zaehlt Werte bis zum ersten leeren (0xFFFF), fuer unterbrochene Fluege
uint32_t werteZaehlen(uint32_t adr) {
  uint32_t n = 0;
  uint16_t buf[128];
  while (adr + 32 + n * 2 < flash.size()) {
    flash.readBuffer(adr + 32 + n * 2, (uint8_t*)buf, sizeof(buf));
    for (int i = 0; i < 128; i++) {
      if (buf[i] == 0xFFFF) return n + i;
    }
    n += 128;
  }
  return n;
}

// Liest beim Start alle Fluege ein
void flugspeicherEinlesen() {
  anzFluege = 0;
  uint32_t adr = 0;
  naechsteNr = 1;
  while (adr + 4096 <= flash.size()) {
    uint8_t k[32];
    flash.readBuffer(adr, k, 32);
    if (memcmp(k, "SWF1", 4) != 0) break;            // kein Flug mehr
    FlugInfo f;
    f.adr = adr;
    memcpy(&f.nr, k + 4, 4);   memcpy(&f.zeit, k + 8, 4);
    memcpy(&f.boot, k + 12, 4); memcpy(&f.up, k + 16, 4);
    memcpy(&f.nn, k + 20, 2);  memcpy(&f.vor, k + 22, 2);
    uint32_t gueltig, belegt;
    memcpy(&gueltig, k + 24, 4); memcpy(&belegt, k + 28, 4);
    if (belegt == 0xFFFFFFFF) {                      // Flug wurde unterbrochen
      belegt = werteZaehlen(adr);                    // (z.B. Akku leer)
      gueltig = belegt;
      flash.writeBuffer(adr + 24, (uint8_t*)&gueltig, 4);
      flash.writeBuffer(adr + 28, (uint8_t*)&belegt, 4);
    }
    f.anzahl = gueltig;
    if (f.zeit == 0xFFFFFFFF) f.zeit = 0;
    if (anzFluege < MAX_FLUEGE) fluege[anzFluege++] = f;
    if (f.nr >= naechsteNr) naechsteNr = f.nr + 1;
    adr = aufRunden4k(adr + 32 + belegt * 2);
  }
  logEndeAdr = adr;
}

// Einmalig: ganzen Zusatzspeicher loeschen (dauert bis zu 30 s)
void flugspeicherVorbereiten() {
  Adafruit_LittleFS_Namespace::File f(InternalFS);
  if (f.open("/logok", FILE_O_READ)) { f.close(); return; }
  meldung("Speicher", "vorbereiten", "einmalig,", "bis 30 s");
  flash.eraseChip();
  flash.waitUntilReady();
  if (f.open("/logok", FILE_O_WRITE)) { f.write((uint8_t)1); f.close(); }
}

// QSPI-Schnittstelle ganz abschalten und zuruecksetzen (auch wenn sie vom
// Bootloader - z. B. mit USB-Kabel - noch eingeschaltet ist). Danach lassen
// sich Pins und Einstellungen neu setzen und der Treiber startet sauber.
void qspiAus() {
  flashTransport.end();                              // Treiber freigeben (falls an)
  if (NRF_QSPI->ENABLE) {
    NRF_QSPI->TASKS_DEACTIVATE = 1;
    delay(1);
    NRF_QSPI->ENABLE = 0;
  }
  // Baustein kurz stromlos schalten: setzt alle QSPI-Register zurueck
  *(volatile uint32_t*)0x40029FFCul = 0;
  delay(1);
  *(volatile uint32_t*)0x40029FFCul = 1;
}

void flugspeicherStarten() {
  // Einschalt-Zaehler
  Adafruit_LittleFS_Namespace::File f(InternalFS);
  if (f.open("/boot", FILE_O_READ)) { f.read((uint8_t*)&bootNr, 4); f.close(); }
  bootNr++;
  InternalFS.remove("/boot");
  if (f.open("/boot", FILE_O_WRITE)) { f.write((uint8_t*)&bootNr, 4); f.close(); }

  // bis zu 6 Versuche: wecken, Speicher-Reset, erkennen (mit Pausen,
  // damit sich der Speicher nach einem Spannungs-Wackler fangen kann)
  delay(10);
  for (int versuch = 0; versuch < 6 && !flashOk; versuch++) {
    if (versuch > 0) delay(20 * versuch);
    qspiAus();                                       // jedes Mal frisch starten
    flashTransport.begin();
    flashTransport.runCommand(0xFF);                 // evtl. Dauer-Lesemodus beenden
    flashTransport.runCommand(0xAB);                 // aus dem Tiefschlaf wecken
    delay(2);
    flashTransport.runCommand(0x66);                 // Reset freigeben
    flashTransport.runCommand(0x99);                 // Reset
    delay(2);
    flashOk = flash.begin(&FLASH_P25Q16H, 1);
  }
  if (!flashOk) {                                    // Kennung fuer die Fehlersuche
    flashTransport.readCommand(0x9F, flashKennung, 3);
    return;
  }
  flugspeicherVorbereiten();
  flugspeicherEinlesen();
}

void flugspeicherSchlafen() {
  if (flashOk) flashTransport.runCommand(0xB9);      // Speicher in Tiefschlaf
}

void logPufferSchreiben() {
  if (logPufferAnz == 0) return;
  uint32_t a = logAdr + 32 + logGeschrieben * 2;
  if (a + logPufferAnz * 2 < flash.size())
    flash.writeBuffer(a, (uint8_t*)logPuffer, logPufferAnz * 2);
  logGeschrieben += logPufferAnz;
  logPufferAnz = 0;
}

void logWert(float hm) {
  if (logAdr + 32 + (logGeschrieben + logPufferAnz + 1) * 2 >= flash.size()) return;  // voll
  logPuffer[logPufferAnz++] = hoeheWert(hm);
  if (logPufferAnz >= 32) logPufferSchreiben();
}

// Hoechste G-Last je Flug in einer kleinen Datei merken (die letzten ca. 50 Fluege)
uint32_t logNr = 0;
void gMaxMerken(uint32_t nr, float g) {
  static char alt[700], neu[740];
  textLaden("/gmax", alt, sizeof(alt));
  char* p = alt;
  int len = strlen(p);
  while (len > 600) {                                // aelteste Zeilen weglassen
    char* nl = strchr(p, '\n');
    if (!nl) { p += len; len = 0; break; }
    len -= (nl + 1 - p);
    p = nl + 1;
  }
  snprintf(neu, sizeof(neu), "%s%lu:%d\n", p, (unsigned long)nr, (int)lroundf(g * 10));
  textSpeichern("/gmax", neu);
}
// G-Max (x10) eines Flugs aus dem Text holen, -1 = unbekannt
int gMaxSuchen(const char* text, uint32_t nr) {
  int wert = -1;
  const char* p = text;
  while (*p) {
    unsigned long n = strtoul(p, (char**)&p, 10);
    if (*p == ':') { long g = strtol(p + 1, (char**)&p, 10); if (n == nr) wert = (int)g; }
    while (*p && *p != '\n') p++;
    if (*p == '\n') p++;
  }
  return wert;
}

// Flug beginnt: Kopf schreiben, die letzten Sekunden vor dem Start mitnehmen
void logStart(int sekSeitAbheben) {
  int vorSekunden = sekSeitAbheben + 3;              // 3 s vor dem Abheben mit
  if (!flashOk || logAktiv) return;
  if (logEndeAdr + 8192 > flash.size()) return;      // Speicher voll
  logAdr = logEndeAdr;
  logGeschrieben = 0;
  logPufferAnz = 0;
  uint8_t k[24];
  memcpy(k, "SWF1", 4);
  uint32_t nr = naechsteNr, zeit = jetztUnix(), up = millis() / 1000;
  logNr = nr;
  int n = min(vorSekunden, vorRingAnz);
  if (zeit) zeit -= n;
  else      zeit = 0xFFFFFFFF;                       // unbekannt, kann spaeter rein
  up -= n;
  int16_t nn = (int16_t)lroundf(hoeheNN() - h);
  uint16_t vor = max(0, n - sekSeitAbheben);         // Sekunden vor dem Abheben
  memcpy(k + 4, &nr, 4); memcpy(k + 8, &zeit, 4); memcpy(k + 12, &bootNr, 4);
  memcpy(k + 16, &up, 4); memcpy(k + 20, &nn, 2); memcpy(k + 22, &vor, 2);
  flash.writeBuffer(logAdr, k, 24);
  logVor = vor;
  logAktiv = true;
  for (int i = n; i > 0; i--) logWert(vorRing[(vorRingPos - i + 64) % 64]);
}

// Flug endet: Anzahl eintragen, damit er vollstaendig ist
void logEnde(unsigned long dauerMs) {
  if (!logAktiv) return;
  logPufferSchreiben();
  uint32_t gueltig = min((uint32_t)(logVor + dauerMs / 1000), logGeschrieben);
  flash.writeBuffer(logAdr + 24, (uint8_t*)&gueltig, 4);
  flash.writeBuffer(logAdr + 28, (uint8_t*)&logGeschrieben, 4);
  logAktiv = false;
  if (gMaxFlug > 0) gMaxMerken(logNr, gMaxFlug);
  flugspeicherEinlesen();                            // Liste aktualisieren
}

// jede Sekunde aufrufen
void logArbeiten() {
  static unsigned long letzte = 0;
  if (millis() - letzte < 1000) return;
  letzte += 1000;
  if (millis() - letzte > 2000) letzte = millis();   // nach Pausen nicht nachholen
  if (!gestartet) return;
  vorRing[vorRingPos] = h;
  vorRingPos = (vorRingPos + 1) % 64;
  if (vorRingAnz < 64) vorRingAnz++;
  if (logAktiv) logWert(h);
}

void flugReset() {
  imFlug = false;
  bodenFrei = false;
  flugGewesen = false;
  flugDauerMs = 0;
  maxHoehe = 0;
  gMaxFlug = 0;
  bezugHoehe = 0;
}

// wird bei jeder Druckmessung aufgerufen
void flugPruefen() {
  static unsigned long wegSeit = 0;       // seit wann Hoehe deutlich anders
  static unsigned long ruhigSeit = 0;     // seit wann alles ruhig
  static float ruhigHoehe = 0;
  unsigned long jetzt = millis();

  if (!imFlug) {
    // Start erkennen: Hoehe weicht um FLUG_START_HOEHE ab, und zwar
    // ununterbrochen FLUG_START_DAUER_S lang. Kurzes Pusten oder eine
    // Windboee reicht dafuer nicht.
    bool weg = fabs(h - bezugHoehe) >= FLUG_START_HOEHE;
    if (weg) {
      if (wegSeit == 0) wegSeit = jetzt;
    } else {
      wegSeit = 0;
      // am Boden: Bezugshoehe langsam mitziehen (Wetter, Herumlaufen)
      if (fabs(v) < 0.3) bezugHoehe += (h - bezugHoehe) * 0.0005;
    }
    if (wegSeit != 0 && jetzt - wegSeit >= FLUG_START_DAUER_S * 1000UL) {
      imFlug = true;
      flugGewesen = true;
      flugStartMs = wegSeit;                // Zeit ab dem Moment des Abhebens
      flugDauerMs = jetzt - wegSeit;
      maxHoehe = h;
      gMaxFlug = gWert;
      ruhigSeit = 0;
      logStart((jetzt - wegSeit) / 1000);      // Aufzeichnung ab kurz vor dem Abheben
      wegSeit = 0;
      kurzerPiep(1800, 60);                 // kurzer Hinweis: Flug erkannt
    }
    return;
  }

  // im Flug
  flugDauerMs = jetzt - flugStartMs;
  if (h > maxHoehe) maxHoehe = h;

  // Landung erkennen: lange ruhig und Hoehe bleibt gleich
  if (fabs(v) < 0.2 && fabs(h - ruhigHoehe) < 1.0) {
    if (ruhigSeit == 0) { ruhigSeit = jetzt; ruhigHoehe = h; }
    if (jetzt - ruhigSeit >= LANDUNG_RUHE_S * 1000UL) {
      imFlug = false;
      flugDauerMs = ruhigSeit - flugStartMs;  // ruhige Zeit nicht mitzaehlen
      bezugHoehe = h;
      bodenFrei = false;                     // am Boden wieder stumm
      logEnde(flugDauerMs);
    }
  } else {
    ruhigSeit = 0;
    ruhigHoehe = h;
  }
}

// Flugmessung von Hand beenden (Doppelklick Taste 1)
void flugStoppen() {
  if (!imFlug) {                       // laeuft gar nicht: tiefer Doppelpiep
    kurzerPiep(400, 40);
    delay(60);
    kurzerPiep(400, 40);
    return;
  }
  imFlug = false;
  flugDauerMs = millis() - flugStartMs;
  bezugHoehe = h;                      // ab hier neu auf einen Start warten
  bodenFrei = false;
  logEnde(flugDauerMs);
  kurzerPiep(1500, 80);                // absteigende Tonfolge = gestoppt
  delay(40);
  kurzerPiep(1000, 80);
  delay(40);
  kurzerPiep(700, 120);
}

// Hoehe ueber dem Meer
float hoeheNN() {
  if (STARTPLATZ_NN != 0) return STARTPLATZ_NN + h;
  if (letzterDruck <= 0) return 0;
  float meer = (QNH > 800) ? QNH : 1013.25;                        // QNH oder Normaldruck
  return 44330.0 * (1.0 - pow(letzterDruck / meer, 0.1903));
}

void messen() {
  if (imuOk) imuLesen();

  if (sensorOk && dps.pressureAvailable()) {
    sensors_event_t t, p;
    dps.getEvents(&t, &p);
    float druck = p.pressure;
    unsigned long jetzt = micros();
    letzterDruck = druck;

    if (nullDruck == 0) {                      // Start oder Hoehe nullen
      nullDruck = druck;
      float altBias = bias;
      kalmanReset();
      if (gestartet) bias = altBias;
      letzteMessung = jetzt;
      gestartet = true;
      bezugHoehe = 0;
      if (imFlug) maxHoehe = 0;
    } else {
      float dt = (jetzt - letzteMessung) / 1e6;
      letzteMessung = jetzt;
      if (dt > 0 && dt < 0.5) {
        if (!imuOk) kalmanVorhersage(0, dt, OHNE_IMU_REAKTION);
        kalmanKorrektur(hoeheAusDruck(druck), imuOk ? DRUCK_RAUSCHEN : 0.0144f);
        flugPruefen();
      }
    }

    if (Serial) {                             // nur wenn ein PC zuhoert
      Serial.print("Steigen:");
      Serial.print(v, 2);
      Serial.print(",Beschl:");
      Serial.println(letzteBeschl, 2);
    }
  }
}

// =================================================================
// Ton mit Lautstaerke
// Statt tone() wird der Tonbaustein (PWM) direkt benutzt. Die Lautstaerke
// entsteht dadurch, wie lange das Signal pro Schwingung "an" ist.
// =================================================================
int aktFreq = 0;

// Verstaerker nur einschalten, wenn ein Ton kommt; nach 1,5 s Stille wieder aus.
// Spart ca. 4 mA und es rauscht in den Pausen nicht.
const unsigned long VERST_NACHLAUF = 1500;   // ms Stille, bis er ausgeht
const int VERST_ANLAUF = 30;                 // ms, die der Verstaerker zum Starten braucht
bool verstAn = false;
unsigned long verstZuletzt = 0;
void verstaerkerAus() { digitalWrite(PIN_VERST, LOW); verstAn = false; }
void verstaerkerEin() {
  verstZuletzt = millis();
  if (!verstAn) { digitalWrite(PIN_VERST, HIGH); verstAn = true; delay(VERST_ANLAUF); }
}
void verstaerkerPruefen() {
  if (verstAn && aktFreq == 0 && millis() - verstZuletzt > VERST_NACHLAUF) verstaerkerAus();
}

void tonAusgeben(int freq, int stufe) {
  if (freq <= 0 || stufe <= 0) {
    HwPWM3.stop();                        // Ausgang bleibt LOW = Ruhe
    if (verstAn) verstZuletzt = millis(); // Nachlauf ab Ende des Tons
    return;
  }
  verstaerkerEin();
  uint32_t top = 16000000UL / (uint32_t)freq;
  if (top > 32767) top = 32767;
  HwPWM3.setMaxValue(top);
  HwPWM3.writePin(PIN_TON, top * LAUT_TABELLE[stufe] / 1000);
}

void tonEin(int freq) {
  if (freq != aktFreq) {
    tonAusgeben(freq, lautStufe);
    aktFreq = freq;
  }
}

void tonAus() {
  if (aktFreq != 0) {
    tonAusgeben(0, 0);
    aktFreq = 0;
  }
}

enum TonPhase { RUHE, PIEP, PAUSE, SINK };
TonPhase phase = RUHE;
unsigned long phaseEnde = 0;
unsigned long pauseStart = 0;
bool steigtGerade = false;

int tonFreq(float s)  { return TON_MIN + (int)((TON_MAX - TON_MIN) * s / TON_SKALA); }
int taktLaenge(float s) {
  int z = TAKT_LANGSAM - (int)((TAKT_LANGSAM - TAKT_SCHNELL) * s / TON_SKALA);
  return z < TAKT_SCHNELL ? TAKT_SCHNELL : z;
}
void piepStarten(float s, unsigned long jetzt) {
  tonEin(tonFreq(s));
  phase = PIEP;
  phaseEnde = jetzt + taktLaenge(s) * PIEP_ANTEIL / 100;
}

// "Vario suchen": lauter Wechselton, bis die Zeit um ist oder eine Taste kommt
unsigned long suchenBis = 0;
bool suchenLaeuft() { return suchenBis != 0 && (long)(millis() - suchenBis) < 0; }
bool testLaeuft()   { return testBis != 0 && (long)(millis() - testBis) < 0; }

// Ton mit eigener Kennung (negativ), damit tonEin() ihn nicht verwechselt
void tonKennung(int freq, int stufe, int kennung) {
  if (aktFreq != kennung) {
    tonAusgeben(freq, stufe);
    aktFreq = kennung;
  }
}

void tonAktualisieren() {
  unsigned long jetzt = millis();

  if (suchenBis) {                         // Vario suchen hat Vorrang
    if (suchenLaeuft()) {
      unsigned long t = jetzt % 400;
      if (t < 150)                tonKennung(2600, LAUT_STUFEN, -2600);
      else if (t >= 200 && t < 350) tonKennung(2000, LAUT_STUFEN, -2000);
      else                        tonAus();
      phase = RUHE;
      return;
    }
    suchenBis = 0;
    tonAus();
    phase = RUHE;
  }

  // G-Warnung hat Vorrang vor dem Vario-Ton (klingt auch bei "stumm", Stufe 3)
  if (G_WARNUNG > 0 && imuOk && gestartet && gWert >= G_WARNUNG) {
    unsigned long t = jetzt % 600;
    if (t < 100 || (t >= 180 && t < 280)) tonKennung(3200, max(lautStufe, 3), -3200);
    else                                  tonAus();
    phase = RUHE;
    return;
  }

  // Gegenwind: alle 4 s drei fallende Toene (auch wenn stumm), dazwischen normaler Vario-Ton
  if (gegenwindAktiv) {
    unsigned long t = (jetzt - gegenwindSeit) % 4000;
    if (t < 600) {
      int f = (t < 170) ? 1100 : (t >= 200 && t < 370) ? 850 : (t >= 400 && t < 570) ? 620 : 0;
      if (f) tonKennung(f, max(lautStufe, 3), -f);
      else   tonAus();
      phase = RUHE;
      return;
    }
  }

  float v = vJetzt();                      // echter Wert oder Testwert
  if (lautStufe == 0 || !gestartet || (bodenStumm() && !testLaeuft())) {
    tonAus();
    phase = RUHE;
    return;
  }

  if (v >= STEIG_EIN) steigtGerade = true;
  if (v <  STEIG_AUS) steigtGerade = false;

  float s = constrain(v, 0.0, TON_SKALA);

  switch (phase) {
    case RUHE:
    case SINK:
      if (steigtGerade) {
        tonAus();
        piepStarten(s, jetzt);
      } else if (v <= SINK_ALARM) {
        tonEin(TON_SINK);
        phase = SINK;
      } else if (NULLSCHIEBER && v >= NULL_AB) {
        // Nullschieber: kurzes leises Ticken alle 0,7 s
        if (jetzt % 700 < 35) tonKennung(NULL_FREQ, max(1, lautStufe - 2), -1);
        else                  tonAus();
        phase = RUHE;
      } else {
        tonAus();
        phase = RUHE;
      }
      break;

    case PIEP:
      if ((long)(jetzt - phaseEnde) >= 0) {
        tonAus();
        phase = PAUSE;
        pauseStart = jetzt;
      }
      break;

    case PAUSE:
      phaseEnde = pauseStart + taktLaenge(s) * (100 - PIEP_ANTEIL) / 100;
      if ((long)(jetzt - phaseEnde) >= 0) {
        if (steigtGerade) piepStarten(s, jetzt);
        else phase = RUHE;
      }
      break;
  }
}

// Kurzer Quittungston. Bei "stumm" leise (Stufe 1), damit man ihn hoert.
void kurzerPiep(int f, int ms) {
  tonAus();
  tonAusgeben(f, lautStufe > 0 ? lautStufe : 1);
  delay(ms);
  tonAusgeben(0, 0);
  aktFreq = 0;
  phase = RUHE;
}

// =================================================================
// Lautstaerke speichern (im internen Speicher des Controllers)
// =================================================================
unsigned long speichernUm = 0;   // 0 = nichts zu speichern

void lautLaden() {
  Adafruit_LittleFS_Namespace::File f(InternalFS);
  if (f.open("/laut", FILE_O_READ)) {
    int s = f.read();
    f.close();
    if (s >= 0 && s <= LAUT_STUFEN) lautStufe = s;
  }
}

void lautSpeichern() {
  InternalFS.remove("/laut");
  Adafruit_LittleFS_Namespace::File f(InternalFS);
  if (f.open("/laut", FILE_O_WRITE)) {
    f.write((uint8_t)lautStufe);
    f.close();
  }
  speichernUm = 0;
}

// richtung +1 = lauter (Taste 1), -1 = leiser (Taste 2)
void lautAendern(int richtung) {
  if (bodenStumm() && lautStufe > 0) {               // am Boden: erst nur Ton an
    bodenFrei = true;
    kurzerPiep(1200, 80);
    return;
  }
  int neu = constrain(lautStufe + richtung, 0, LAUT_STUFEN);
  if (neu == lautStufe) {                            // schon ganz oben/unten
    kurzerPiep(400, 40);
    delay(60);
    kurzerPiep(400, 40);
    return;
  }
  lautStufe = neu;
  aktFreq = 0;
  if (lautStufe > 0) kurzerPiep(1200, 80);           // Probeton in neuer Stufe
  else               kurzerPiep(600, 80);            // stumm: leiser tiefer Piep
  speichernUm = millis() + 3000;                     // in 3 s speichern
  if (speichernUm == 0) speichernUm = 1;
}

// =================================================================
// Anzeige
// =================================================================
// Wind als Text: "SW 25" (kommt aus Suedwest, 25 km/h); lang: "Wind SW 25"
void windText(char* t, int laenge, bool lang) {
  static const char* R[8] = {"N", "NO", "O", "SO", "S", "SW", "W", "NW"};
  int i = ((windAus % 360 + 360 + 22) % 360) / 45;
  if (windKmh < 3) snprintf(t, laenge, lang ? "Wind still" : "still");
  else snprintf(t, laenge, lang ? "Wind %s %d" : "%s %d", R[i], windKmh);
}

void zahlMitVorzeichen(char* text, int laenge, float wert) {
  int zehntel = (int)round(wert * 10);
  char vz = (zehntel > 0) ? '+' : (zehntel < 0 ? '-' : ' ');
  zehntel = abs(zehntel);
  snprintf(text, laenge, "%c%d.%d", vz, zehntel / 10, zehntel % 10);
}

unsigned long warnBis = 0;     // Akkuwarnung anzeigen bis
char warnText[16] = "";
char warnTitel[12] = "Akku";

#if DISPLAY_KLEIN
// ---------- Anzeige 64 x 128 (altes 0,96" OLED 128x64, SSD1306, hochkant) ----------
const int BR = 64;                     // Breite in Pixel (hochkant)
const int HO = 128;                    // Hoehe in Pixel

// Helligkeit in % -> Kontrastwert des Displays (quadratisch, damit es unten feiner geht)
uint8_t kontrast() {
  int h = constrain(HELLIGKEIT, 5, 100);
  return (uint8_t)max(1, (255 * h * h) / 10000);
}

void displayAusrichten() {
  anzeige.setDisplayRotation(DISPLAY_DREHEN ? U8G2_R3 : U8G2_R1);
  anzeige.setContrast(kontrast());
}

int textMitte(const char* t) { return max(0, (BR - (int)anzeige.getUTF8Width(t)) / 2); }

// 6 Balken unten links, gefuellt bis zur aktuellen Stufe
void lautSymbol() {
  for (int i = 0; i < LAUT_STUFEN; i++) {
    int hoehe = 3 + i;
    int x = 2 + i * 5;
    if (i < lautStufe) anzeige.drawBox(x, HO - hoehe, 3, hoehe);
    else               anzeige.drawPixel(x + 1, HO - 1);
  }
}

// Linie Punkt fuer Punkt, nur sichtbare Punkte (negative Koordinaten sicher)
void linieClip(int ax, int ay, int bx, int by, int xmin, int ymin, int xmax, int ymax) {
  int dx = bx - ax, dy = by - ay;
  int n = max(abs(dx), abs(dy));
  if (n > 400) return;
  for (int i = 0; i <= n; i++) {
    int x = (n == 0) ? ax : ax + (dx * i + (dx >= 0 ? n / 2 : -n / 2)) / n;
    int y = (n == 0) ? ay : ay + (dy * i + (dy >= 0 ? n / 2 : -n / 2)) / n;
    if (x >= xmin && x <= xmax && y >= ymin && y <= ymax) anzeige.drawPixel(x, y);
  }
}
const int PFEIL_ZONE = 46;
void linieZ(int ax, int ay, int bx, int by) { linieClip(ax, ay, bx, by, 0, 0, BR - 1, PFEIL_ZONE - 1); }

// Ein Pfeil (38 Pixel hoch). y = Oberkante, nachOben = Spitze oben
void einPfeil(int y, bool nachOben) {
  int sy = 0, by = 18, ey = 38;
  if (!nachOben) { sy = 38; by = 20; ey = 0; }
  sy += y; by += y; ey += y;
  if (PFEIL_HOHL) {
    linieZ(32, sy, 59, by);
    linieZ(59, by, 43, by);
    linieZ(43, by, 43, ey);
    linieZ(43, ey, 20, ey);
    linieZ(20, ey, 20, by);
    linieZ(20, by, 4, by);
    linieZ(4, by, 32, sy);
  } else {
    anzeige.drawTriangle(32, sy, 4, by, 60, by);
    int oben = min(by, ey), unten = max(by, ey);
    anzeige.drawBox(20, oben, 24, unten - oben + 1);
  }
}

// Wandernde Pfeile im oberen Bereich (0..45)
void pfeileZeichnen() {
  float v = vJetzt();
  static float pos = 0;
  static unsigned long letzte = 0;
  unsigned long jetzt = millis();
  float dt = (jetzt - letzte) / 1000.0;
  letzte = jetzt;
  if (dt > 0.5) dt = 0;

  if (!PFEIL_AN || fabs(v) < PFEIL_AB) return;

  bool hoch = v > 0;
  float tempo = min(PFEIL_MAX, PFEIL_TEMPO * (float)fabs(v));
  const int ABSTAND = 46;
  pos += tempo * dt;
  while (pos >= ABSTAND) pos -= ABSTAND;

  int versatz = (int)pos;
  int start = hoch ? -versatz : versatz - ABSTAND;
  anzeige.setClipWindow(0, 0, BR, PFEIL_ZONE);
  for (int k = 0; k < 3; k++) einPfeil(start + k * ABSTAND + 4, hoch);
  anzeige.setMaxClipWindow();
}

// Kleines Akku-Symbol (12 x 7 Pixel). Laedt: Fuellung laeuft durch. Unter 10 %: blinkt.
void akkuSymbol(int x, int y) {
  int p = akkuProzent();
  bool blink = (millis() / 500) % 2;
  if (!usbSteckt() && p < 10 && blink) return;
  anzeige.drawFrame(x, y, 11, 7);
  anzeige.drawBox(x + 11, y + 2, 1, 3);
  int fuell = (p * 9 + 50) / 100;
  if (usbSteckt() && laedt()) fuell = max(fuell, (int)(millis() / 400) % 10);
  if (fuell > 0) anzeige.drawBox(x + 1, y + 1, fuell, 5);
}

// G-Anzeige: in der Spirale (ueber 1,5 g fuer 2 s) statt der Hoehe,
// zurueck, wenn es 3 s lang unter 1,25 g liegt
bool gModus = false;
void gModusPruefen() {
  static unsigned long seit = 0;
  if (!G_AUTO || !imuOk) { gModus = false; seit = 0; return; }
  bool wechseln = gModus ? (gWert < 1.25f) : (gWert > 1.5f);
  if (!wechseln) { seit = 0; return; }
  if (seit == 0) seit = millis();
  if (millis() - seit >= (gModus ? 3000UL : 2000UL)) { gModus = !gModus; seit = 0; }
}

void bildZeichnen() {
  float v = vAnzeige();
  char text[20];
  anzeige.clearBuffer();

  if (!sensorOk) {
    anzeige.setFont(u8g2_font_6x10_tf);
    anzeige.drawStr(4, 40, "DPS310");
    anzeige.drawStr(4, 52, "fehlt");
    return;
  }

  pfeileZeichnen();

  // Steigwert, "invertiert": wo Pfeil ist, wird die Zahl dunkel
  anzeige.setFontMode(1);
  anzeige.setDrawColor(2);
  zahlMitVorzeichen(text, sizeof(text), v);
  anzeige.setFont(u8g2_font_logisoso18_tn);
  int breite = anzeige.getStrWidth(text);
  anzeige.drawStr((BR - breite) / 2, 26, text);
  anzeige.setFont(u8g2_font_6x10_tf);
  anzeige.drawStr(23, 38, "m/s");
  anzeige.setDrawColor(1);
  anzeige.setFontMode(0);

  anzeige.drawHLine(0, PFEIL_ZONE, BR);

  anzeige.setFont(u8g2_font_6x10_tf);
  if (gModus) {
    anzeige.drawUTF8(2, 58, "G-Last");
    int g10 = (int)lroundf(gWert * 10);
    snprintf(text, sizeof(text), "%d.%d g", g10 / 10, g10 % 10);
  } else {
    anzeige.drawUTF8(2, 58, "Höhe");
    snprintf(text, sizeof(text), "%d m", (int)round(h));
  }
  if (!imuOk) anzeige.drawStr(38, 58, "!IMU");
  anzeige.setFont(u8g2_font_9x15B_tf);
  anzeige.drawStr(2, 74, text);

  // Hoehe ueber Meer, Maximum, Flugzeit (klein)
  anzeige.setFont(u8g2_font_6x10_tf);
  if (WIND_ANZEIGEN && windDa() && (millis() / 3000) % 2) windText(text, sizeof(text), true);
  else if (gsDa()) snprintf(text, sizeof(text), "GS %d km/h", (int)lroundf(gsKmh));
  else        snprintf(text, sizeof(text), "NN  %d m", (int)round(hoeheNN()));
  anzeige.drawStr(2, 89, text);

  if (gModus) {
    int gm = (int)lroundf(gMaxFlug * 10);
    snprintf(text, sizeof(text), "Gmax %d.%d", gm / 10, gm % 10);
  } else if (flugGewesen) snprintf(text, sizeof(text), "Max %d m", (int)round(maxHoehe));
  else                    snprintf(text, sizeof(text), "Max --");
  anzeige.drawStr(2, 101, text);

  if (flugGewesen) {
    unsigned long min = flugDauerMs / 60000UL;
    snprintf(text, sizeof(text), "Zeit %lu:%02lu", min / 60, min % 60);
  } else {
    snprintf(text, sizeof(text), "Zeit -:--");
  }
  anzeige.drawStr(2, 113, text);
  if (imFlug && (millis() / 500) % 2) anzeige.drawDisc(60, 109, 2);   // blinkt im Flug

  // Statuszeile: Lautstaerke, Akku, Bluetooth
  if (lautStufe == 0)     anzeige.drawStr(2, 127, "stumm");
  else if (bodenStumm())  anzeige.drawStr(2, 127, "Boden");
  else                    lautSymbol();
  akkuSymbol(35, 121);
  if (Bluefruit.connected()) anzeige.drawStr(51, 127, "BT");

  // Hinweise ueber allem: Akkuwarnung, Vario suchen
  if (warnBis && (long)(millis() - warnBis) < 0) hinweisBox(warnTitel, warnText);
  else warnBis = 0;
  if (suchenLaeuft() && (millis() / 300) % 2) hinweisBox("Hier!", "Taste=aus");
  if (gegenwindAktiv && (millis() / 700) % 2) {
    snprintf(text, sizeof(text), "%d km/h", (int)lroundf(gsKmh));
    hinweisBox("Gegenwind!", text);
  }
  if (frageAktiv()) frageBox();
}

// Schwarze Schrift in weissem Kasten mitten im Bild
void hinweisBox(const char* z1, const char* z2) {
  anzeige.setDrawColor(1);
  anzeige.drawBox(0, 50, BR, 34);
  anzeige.setDrawColor(0);
  anzeige.setFont(u8g2_font_6x10_tf);
  anzeige.drawUTF8(textMitte(z1), 64, z1);
  anzeige.drawUTF8(textMitte(z2), 78, z2);
  anzeige.setDrawColor(1);
}

// Grosse Ladeanzeige. versatz verschiebt alles ein wenig (schont das Display)
void ladeBild(int versatz) {
  char text[16];
  int p = akkuProzent();
  anzeige.clearBuffer();
  anzeige.setFont(u8g2_font_6x10_tf);
  anzeige.drawUTF8(20 + versatz, 10, "Akku");

  int x = 17 + versatz, y = 20;
  anzeige.drawBox(x + 9, y - 4, 12, 4);
  anzeige.drawFrame(x, y, 30, 60);
  anzeige.drawFrame(x + 1, y + 1, 28, 58);
  int fuell = p * 54 / 100;
  if (usbSteckt() && laedt() && !akkuVoll) {
    int extra = (millis() / 300) % 4 * 3;
    fuell = min(54, fuell + extra);
  }
  if (fuell > 0) anzeige.drawBox(x + 3, y + 57 - fuell, 24, fuell);

  if (akkuFehlt()) {
    anzeige.drawUTF8(2 + versatz, 100, "kein Akku?");
  } else {
    anzeige.setFont(u8g2_font_logisoso18_tn);
    snprintf(text, sizeof(text), "%d", p);
    int b = anzeige.getStrWidth(text);
    int xs = (BR - b - 8) / 2 + versatz;
    anzeige.drawStr(xs, 104, text);
    anzeige.setFont(u8g2_font_6x10_tf);
    anzeige.drawStr(xs + b + 2, 104, "%");
  }

  anzeige.setFont(u8g2_font_6x10_tf);
  if (akkuVoll)       anzeige.drawUTF8(20 + versatz, 116, "voll");
  else if (laedt())   anzeige.drawUTF8(11 + versatz, 116, "lädt...");
  else                anzeige.drawUTF8(2 + versatz, 116, "lädt nicht");
  int mv = (int)(akkuV * 100 + 0.5);
  snprintf(text, sizeof(text), "%d.%02d V", mv / 100, mv % 100);
  anzeige.drawStr(14 + versatz, 127, text);
}

int naechsteZeile = 8;                 // 8 Zeilen zu je 8 Pixel (128 x 64 Display)

void anzeigeWeiter() {
  static unsigned long letztesBild = 0;
  if (displayAus) return;
  if (naechsteZeile >= 8) {
    if (millis() - letztesBild >= (unsigned long)anzeigeTakt()) {
      letztesBild = millis();
      bildZeichnen();
      naechsteZeile = 0;
    }
    return;
  }
  // je Aufruf 1 Zeile senden, damit das Messen nicht gebremst wird
  anzeige.updateDisplayArea(0, naechsteZeile, 16, 1);
  naechsteZeile++;
}

void meldung(const char* z1, const char* z2, const char* z3, const char* z4) {
  anzeige.clearBuffer();
  anzeige.setFont(u8g2_font_6x10_tf);
  anzeige.drawUTF8(2, 40, z1);
  anzeige.drawUTF8(2, 52, z2);
  anzeige.drawUTF8(2, 76, z3);
  anzeige.drawUTF8(2, 88, z4);
  anzeige.sendBuffer();
  naechsteZeile = 8;
}

// Einschaltbild: oben "Sweef", darunter Begruessung und ID
void startBild(const char* unten1, const char* unten2) {
  anzeige.clearBuffer();
  anzeige.setFont(u8g2_font_9x15B_tf);
  anzeige.drawStr(textMitte("Sweef"), 16, "Sweef");
  anzeige.drawHLine(8, 21, 48);
  anzeige.setFont(u8g2_font_6x10_tf);
  char tmp[100];
  strncpy(tmp, gruss, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = 0;
  char* rest = tmp;
  char* z;
  int y = 36, zeilen = 0;
  while (zeilen < 4 && (z = strtok_r(rest, "|", &rest)) != NULL) {
    anzeige.drawUTF8(textMitte(z), y, z);
    y += 11;
    zeilen++;
  }
  char id[12];
  snprintf(id, sizeof(id), "ID %s", geraeteId);
  anzeige.drawStr(textMitte(id), 92, id);
  anzeige.drawUTF8(textMitte(unten1), 112, unten1);
  anzeige.drawUTF8(textMitte(unten2), 124, unten2);
  anzeige.sendBuffer();
  naechsteZeile = 8;
}

void warteMessend(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) messen();
}

// Zeigt, ob alle Teile da sind. Bei Fehler: tiefe Pieps, laenger anzeigen.
void selbsttest() {
  struct { const char* name; bool ok; } t[] = {
    {"Druck", sensorOk}, {"Lage", imuOk}, {"Daten", flashOk}
  };
  bool fehler = false;
  anzeige.clearBuffer();
  anzeige.setFont(u8g2_font_6x10_tf);
  anzeige.drawStr(2, 12, "Selbsttest");
  anzeige.drawHLine(0, 16, BR);
  for (int i = 0; i < 3; i++) {
    int y = 32 + i * 13;
    anzeige.drawStr(2, y, t[i].name);
    const char* e = t[i].ok ? "OK" : "FEHLT";
    anzeige.drawStr(62 - anzeige.getStrWidth(e), y, e);
    if (!t[i].ok) fehler = true;
  }
  char a[12];
  if (akkuFehlt()) snprintf(a, sizeof(a), "--");
  else             snprintf(a, sizeof(a), "%d%%", akkuProzent());
  anzeige.drawStr(2, 71, "Akku");
  anzeige.drawStr(62 - anzeige.getStrWidth(a), 71, a);
  anzeige.drawHLine(0, 80, BR);
  if (fehler) {
    anzeige.drawStr(2, 98, "Fehler!");
    if (!flashOk) {
      char k[12];
      snprintf(k, sizeof(k), "%02X %02X %02X", flashKennung[0], flashKennung[1], flashKennung[2]);
      anzeige.drawStr(2, 112, "Daten-ID");
      anzeige.drawStr(2, 124, k);
    } else {
      anzeige.drawStr(2, 112, "Bitte");
      anzeige.drawUTF8(2, 124, "prüfen");
    }
  } else {
    anzeige.drawStr(2, 104, "alles OK");
  }
  anzeige.sendBuffer();
  naechsteZeile = 8;
  if (fehler) {
    for (int i = 0; i < 3; i++) { kurzerPiep(400, 150); delay(100); }
    warteMessend(4000);
  } else {
    warteMessend(1200);
  }
}

#else  // grosses Display
// ---------- Anzeige 128 x 128 (1,5" OLED, SH1107) ----------
const int BR = 128;                    // Breite in Pixel
const int HO = 128;                    // Hoehe in Pixel

// Helligkeit in % -> Kontrastwert des Displays (quadratisch, damit es unten feiner geht)
uint8_t kontrast() {
  int h = constrain(HELLIGKEIT, 5, 100);
  return (uint8_t)max(1, (255 * h * h) / 10000);
}

void displayAusrichten() {
  anzeige.setDisplayRotation(DISPLAY_DREHEN ? U8G2_R2 : U8G2_R0);
  anzeige.setContrast(kontrast());
}

int textMitte(const char* t) { return (BR - anzeige.getUTF8Width(t)) / 2; }

// Lautstaerke: 6 Balken unten links, gefuellt bis zur aktuellen Stufe
void lautSymbol() {
  for (int i = 0; i < LAUT_STUFEN; i++) {
    int hoehe = 3 + i;
    int x = 2 + i * 6;
    if (i < lautStufe) anzeige.drawBox(x, HO - hoehe, 4, hoehe);
    else               anzeige.drawHLine(x, HO - 1, 4);
  }
}

// Linie Punkt fuer Punkt zeichnen, nur die sichtbaren Punkte.
// (U8g2 rechnet negative Koordinaten sonst als riesige Zahlen und zeichnet
// tausende unsichtbare Punkte.) Die Schleife hat hoechstens so viele
// Schritte wie die Linie lang ist - sie kann nicht haengen bleiben.
void linieClip(int ax, int ay, int bx, int by, int xmin, int ymin, int xmax, int ymax) {
  int dx = bx - ax, dy = by - ay;
  int n = max(abs(dx), abs(dy));
  if (n > 400) return;                    // Sicherheit: so lang ist keine Pfeillinie
  for (int i = 0; i <= n; i++) {
    int x = (n == 0) ? ax : ax + (dx * i + (dx >= 0 ? n / 2 : -n / 2)) / n;
    int y = (n == 0) ? ay : ay + (dy * i + (dy >= 0 ? n / 2 : -n / 2)) / n;
    if (x >= xmin && x <= xmax && y >= ymin && y <= ymax) anzeige.drawPixel(x, y);
  }
}

// Linie im Pfeilbereich (oben), vorher zugeschnitten
void linieZ(int ax, int ay, int bx, int by) { linieClip(ax, ay, bx, by, 0, 0, BR - 1, 60); }

// Ein Pfeil (52 Pixel hoch). y = Oberkante, nachOben = Spitze oben
void einPfeil(int y, bool nachOben) {
  int sy = 0, by = 24, ey = 52;            // Spitze, Kopf-Unterkante, Ende
  if (!nachOben) { sy = 52; by = 28; ey = 0; }
  sy += y; by += y; ey += y;
  const int L = 10, R = 118, SL = 44, SR = 84, M = 64;   // Kopf- und Schaftbreite
  if (PFEIL_HOHL) {                         // Umriss, 1 Pixel stark
    linieZ(M, sy, R, by);
    linieZ(R, by, SR, by);
    linieZ(SR, by, SR, ey);
    linieZ(SR, ey, SL, ey);
    linieZ(SL, ey, SL, by);
    linieZ(SL, by, L, by);
    linieZ(L, by, M, sy);
  } else {
    anzeige.drawTriangle(M, sy, L, by, R, by);
    int oben = min(by, ey), unten = max(by, ey);
    anzeige.drawBox(SL, oben, SR - SL, unten - oben + 1);
  }
}

// Zeichnet die wandernden Pfeile im oberen Bereich (0..60)
const int PFEIL_ZONE = 61;
void pfeileZeichnen() {
  float v = vJetzt();
  static float pos = 0;
  static unsigned long letzte = 0;
  unsigned long jetzt = millis();
  float dt = (jetzt - letzte) / 1000.0;
  letzte = jetzt;
  if (dt > 0.5) dt = 0;

  if (!PFEIL_AN || fabs(v) < PFEIL_AB) return;

  bool hoch = v > 0;
  float tempo = min(PFEIL_MAX * 1.4f, PFEIL_TEMPO * 1.4f * (float)fabs(v));
  const int ABSTAND = 64;                  // Pfeil + Luecke
  pos += tempo * dt;
  while (pos >= ABSTAND) pos -= ABSTAND;

  int versatz = (int)pos;
  int start = hoch ? -versatz : versatz - ABSTAND;
  anzeige.setClipWindow(0, 0, BR, PFEIL_ZONE);
  for (int k = 0; k < 3; k++) einPfeil(start + k * ABSTAND + 4, hoch);
  anzeige.setMaxClipWindow();
}

// Akku-Symbol (16 x 8 Pixel). Laedt: Fuellung laeuft durch. Unter 10 %: blinkt.
void akkuSymbol(int x, int y) {
  int p = akkuProzent();
  bool blink = (millis() / 500) % 2;
  if (!usbSteckt() && p < 10 && blink) return;
  anzeige.drawFrame(x, y, 15, 8);
  anzeige.drawBox(x + 15, y + 2, 2, 4);
  int fuell = (p * 13 + 50) / 100;
  if (usbSteckt() && laedt()) fuell = max(fuell, (int)(millis() / 300) % 14);
  if (fuell > 0) anzeige.drawBox(x + 1, y + 1, fuell, 6);
}

// ---- Steigwert mit schwarzem Rand (fuer Umriss-Pfeile) ----
// Die Zahl wird nur neu "gestempelt", wenn sie sich aendert. Danach wird
// sie jedes Bild per Maske ins Bild gesetzt: das kostet fast keine Zeit,
// damit Ton und Pfeile nicht ins Stocken kommen.
const int PUFFER = 2048;               // 128 x 128 / 8
uint8_t zahlMaske[PUFFER];             // Pixel der Zahl
uint8_t randMaske[PUFFER];             // Zahl + 2 Pixel Rand
char    maskeText[20] = "";
bool    maskeGedreht = false;

void zahlZeichnen(const char* text, int xz, int breite) {
  anzeige.setFont(u8g2_font_logisoso38_tn);
  anzeige.drawStr(xz, 48, text);
  anzeige.setFont(u8g2_font_helvB12_tf);
  anzeige.drawStr(xz + breite + 3, 48, "m/s");
}

void maskeBauen(const char* text, int xz, int breite) {
  uint8_t* buf = anzeige.getBufferPtr();
  static uint8_t sicher[PUFFER];       // static: nicht auf den kleinen Stack
  memcpy(sicher, buf, PUFFER);           // bisheriges Bild (Pfeile) merken
  anzeige.clearBuffer();
  anzeige.setDrawColor(1);
  zahlZeichnen(text, xz, breite);
  memcpy(zahlMaske, buf, PUFFER);
  // Rand: jede Spalte 2 Pixel nach oben/unten, dann 2 Spalten seitlich
  static uint8_t senk[PUFFER];
  for (int p = 0; p < 16; p++) {
    for (int x = 0; x < 128; x++) {
      uint32_t oben  = p > 0  ? zahlMaske[(p - 1) * 128 + x] : 0;
      uint32_t mitte = zahlMaske[p * 128 + x];
      uint32_t unten = p < 15 ? zahlMaske[(p + 1) * 128 + x] : 0;
      uint32_t w = oben | (mitte << 8) | (unten << 16);
      uint8_t r = 0;
      for (int k = -2; k <= 2; k++) r |= (uint8_t)(w >> (8 + k));
      senk[p * 128 + x] = r;
    }
  }
  for (int p = 0; p < 16; p++) {
    for (int x = 0; x < 128; x++) {
      uint8_t r = 0;
      for (int k = -2; k <= 2; k++) {
        int xx = x + k;
        if (xx >= 0 && xx < 128) r |= senk[p * 128 + xx];
      }
      randMaske[p * 128 + x] = r;
    }
  }
  memcpy(buf, sicher, PUFFER);           // Pfeile zurueck
  strncpy(maskeText, text, sizeof(maskeText) - 1);
  maskeGedreht = DISPLAY_DREHEN;
}

void zahlMitRand(const char* text, int xz, int breite) {
  if (strcmp(text, maskeText) != 0 || maskeGedreht != DISPLAY_DREHEN) maskeBauen(text, xz, breite);
  uint8_t* buf = anzeige.getBufferPtr();
  for (int i = 0; i < PUFFER; i++) buf[i] = (buf[i] & ~randMaske[i]) | zahlMaske[i];
}

// G-Anzeige: in der Spirale (ueber 1,5 g fuer 2 s) statt der Hoehe,
// zurueck, wenn es 3 s lang unter 1,25 g liegt
bool gModus = false;
void gModusPruefen() {
  static unsigned long seit = 0;
  if (!G_AUTO || !imuOk) { gModus = false; seit = 0; return; }
  bool wechseln = gModus ? (gWert < 1.25f) : (gWert > 1.5f);
  if (!wechseln) { seit = 0; return; }
  if (seit == 0) seit = millis();
  if (millis() - seit >= (gModus ? 3000UL : 2000UL)) { gModus = !gModus; seit = 0; }
}

void bildZeichnen() {
  float v = vAnzeige();
  char text[20];
  anzeige.clearBuffer();

  if (!sensorOk) {
    anzeige.setFont(u8g2_font_9x15B_tf);
    anzeige.drawStr(textMitte("DPS310"), 56, "DPS310");
    anzeige.drawStr(textMitte("fehlt"), 76, "fehlt");
    return;
  }

  pfeileZeichnen();

  // Steigwert gross ueber den Pfeilen.
  // Gefuellter Pfeil: Zahl "invertiert" (wo Pfeil ist, wird sie dunkel).
  // Umriss-Pfeil: Zahl weiss mit schwarzem Rand, damit sich nichts vermischt.
  zahlMitVorzeichen(text, sizeof(text), v);
  anzeige.setFont(u8g2_font_helvB12_tf);
  int be = anzeige.getStrWidth("m/s");
  anzeige.setFont(u8g2_font_logisoso38_tn);
  int breite = anzeige.getStrWidth(text);
  int xz = (BR - breite - be - 3) / 2;
  anzeige.setFontMode(1);
  if (PFEIL_HOHL) {
    zahlMitRand(text, xz, breite);           // schnell, mit schwarzem Rand
  } else {
    anzeige.setDrawColor(2);
    zahlZeichnen(text, xz, breite);
    anzeige.setDrawColor(1);
  }
  anzeige.setFontMode(0);

  anzeige.drawHLine(0, PFEIL_ZONE + 1, BR);

  if (gModus) {
    // G-Last (gross) statt Hoehe
    anzeige.setFont(u8g2_font_7x14B_tf);
    anzeige.drawUTF8(2, 80, "G-Last");
    int g10 = (int)lroundf(gWert * 10);
    snprintf(text, sizeof(text), "%d.%d", g10 / 10, g10 % 10);
    anzeige.setFont(u8g2_font_logisoso20_tn);
    int bg = anzeige.getStrWidth(text);
    anzeige.drawStr(111 - bg, 87, text);
    anzeige.setFont(u8g2_font_helvB12_tf);
    anzeige.drawStr(115, 87, "g");
  } else {
    // Hoehe ueber Startplatz (gross)
    anzeige.setFont(u8g2_font_7x14B_tf);
    anzeige.drawUTF8(2, 80, "Höhe");
    snprintf(text, sizeof(text), "%d", (int)round(h));
    anzeige.setFont(u8g2_font_logisoso20_tn);
    int bh = anzeige.getStrWidth(text);
    anzeige.drawStr(111 - bh, 87, text);
    anzeige.setFont(u8g2_font_helvB12_tf);
    anzeige.drawStr(114, 87, "m");
  }

  // Hoehe ueber Meer, Maximum, Flugzeit
  anzeige.setFont(u8g2_font_6x12_tf);
  snprintf(text, sizeof(text), "NN %d", (int)round(hoeheNN()));
  anzeige.drawStr(2, 101, text);
  if (gModus) {                              // in der Spirale: hoechste G-Last
    int gm = (int)lroundf(gMaxFlug * 10);
    snprintf(text, sizeof(text), "Gmax %d.%d", gm / 10, gm % 10);
  } else if (flugGewesen) snprintf(text, sizeof(text), "Max %d", (int)round(maxHoehe));
  else             snprintf(text, sizeof(text), "Max --");
  int maxX = 126 - anzeige.getStrWidth(text);
  anzeige.drawStr(maxX, 101, text);
  if (WIND_ANZEIGEN && windDa()) {           // Wind in der Mitte, wenn Platz ist
    char nn[12];
    snprintf(nn, sizeof(nn), "NN %d", (int)round(hoeheNN()));
    int nnEnde = 2 + anzeige.getStrWidth(nn);
    windText(text, sizeof(text), false);
    int bw = anzeige.getStrWidth(text);
    int x = (BR - bw) / 2;
    if (x < nnEnde + 4) x = nnEnde + 4;
    if (x + bw < maxX - 3) anzeige.drawStr(x, 101, text);
  }

  if (flugGewesen) {
    unsigned long min = flugDauerMs / 60000UL;
    snprintf(text, sizeof(text), "Zeit %lu:%02lu", min / 60, min % 60);
  } else {
    snprintf(text, sizeof(text), "Zeit -:--");
  }
  anzeige.drawStr(2, 114, text);
  if (imFlug && (millis() / 500) % 2) anzeige.drawDisc(70, 110, 2);   // blinkt im Flug
  if (!imuOk) anzeige.drawStr(96, 114, "!IMU");
  else if (gsDa()) {                         // Geschwindigkeit ueber Grund (Handy-GPS)
    snprintf(text, sizeof(text), "%d km/h", (int)lroundf(gsKmh));
    anzeige.drawStr(126 - anzeige.getStrWidth(text), 114, text);
  }

  anzeige.drawHLine(0, 117, BR);

  // Statuszeile: Lautstaerke, Akku, Bluetooth
  if (lautStufe == 0)     anzeige.drawStr(2, 127, "stumm");
  else if (bodenStumm())  anzeige.drawStr(2, 127, "Boden");   // Ton erst nach Start
  else                    lautSymbol();
  akkuSymbol(52, 120);
  if (!akkuFehlt()) {
    if (RESTZEIT && restMin >= 0) {
      int st = restMin / 60;
      if (st >= 10) snprintf(text, sizeof(text), "%dh", st);
      else          snprintf(text, sizeof(text), "%d:%02dh", st, restMin % 60);
    } else snprintf(text, sizeof(text), "%d%%", akkuProzent());
    anzeige.drawStr(72, 127, text);
  }
  if (Bluefruit.connected()) anzeige.drawStr(112, 127, "BT");

  // Hinweise ueber allem: Akkuwarnung, Vario suchen
  if (warnBis && (long)(millis() - warnBis) < 0) hinweisBox(warnTitel, warnText);
  else warnBis = 0;
  if (suchenLaeuft() && (millis() / 300) % 2) hinweisBox("Hier!", "Taste = aus");
  if (gegenwindAktiv && (millis() / 700) % 2) {
    snprintf(text, sizeof(text), "%d km/h", (int)lroundf(gsKmh));
    hinweisBox("Gegenwind!", text);
  }
  if (frageAktiv()) frageBox();
}

// Schwarze Schrift in weissem Kasten mitten im Bild
void hinweisBox(const char* z1, const char* z2) {
  anzeige.setDrawColor(1);
  anzeige.drawRBox(6, 40, BR - 12, 48, 4);
  anzeige.setDrawColor(0);
  anzeige.setFont(u8g2_font_9x15B_tf);
  anzeige.drawUTF8(textMitte(z1), 60, z1);
  anzeige.drawUTF8(textMitte(z2), 78, z2);
  anzeige.setDrawColor(1);
}

// Grosse Ladeanzeige. versatz verschiebt alles ein wenig (schont das Display)
void ladeBild(int versatz) {
  char text[16];
  int p = akkuProzent();
  anzeige.clearBuffer();
  anzeige.setFont(u8g2_font_9x15B_tf);
  anzeige.drawUTF8(textMitte("Akku") + versatz, 14, "Akku");

  // Akku stehend: Koerper 36 x 76, Kappe oben (links)
  int x = 12 + versatz, y = 30;
  anzeige.drawBox(x + 11, y - 5, 14, 5);
  anzeige.drawFrame(x, y, 36, 76);
  anzeige.drawFrame(x + 1, y + 1, 34, 74);
  int fuell = p * 70 / 100;
  if (usbSteckt() && laedt() && !akkuVoll) {
    int extra = (millis() / 300) % 4 * 4;     // Balken "waechst" = laedt gerade
    fuell = min(70, fuell + extra);
  }
  if (fuell > 0) anzeige.drawBox(x + 3, y + 73 - fuell, 30, fuell);

  int rx = 58 + versatz;                      // rechte Spalte
  if (akkuFehlt()) {
    anzeige.setFont(u8g2_font_7x14B_tf);
    anzeige.drawUTF8(rx, 64, "kein");
    anzeige.drawUTF8(rx, 80, "Akku?");
  } else {
    anzeige.setFont(u8g2_font_helvB12_tf);
    int bp = anzeige.getStrWidth("%");
    anzeige.setFont(u8g2_font_logisoso32_tn);
    snprintf(text, sizeof(text), "%d", p);
    int b = anzeige.getStrWidth(text);
    int xs = min(rx, 125 + versatz - bp - 2 - b);   // "100%" passt sicher rein
    anzeige.drawStr(xs, 66, text);
    anzeige.setFont(u8g2_font_helvB12_tf);
    anzeige.drawStr(xs + b + 2, 66, "%");
  }
  anzeige.setFont(u8g2_font_7x14B_tf);
  if (akkuVoll)       anzeige.drawUTF8(rx, 88, "voll");
  else if (laedt())   anzeige.drawUTF8(rx, 88, "lädt...");
  else                anzeige.drawUTF8(rx, 88, "lädt nicht");
  anzeige.setFont(u8g2_font_6x12_tf);
  int mv = (int)(akkuV * 100 + 0.5);
  snprintf(text, sizeof(text), "%d.%02d V", mv / 100, mv % 100);
  anzeige.drawStr(rx, 104, text);
}

int naechsteZeile = 16;                // 16 Zeilen zu je 8 Pixel

// Oben (Steigwert + Pfeile, Zeilen 0-7) wird jedes Bild gesendet, der
// untere Teil nur alle 250 ms. So laufen die Pfeile doppelt so fluessig.
int letzteZeile = 16;
void anzeigeWeiter() {
  static unsigned long letztesBild = 0, letztesUnten = 0;
  if (displayAus) return;
  if (naechsteZeile >= letzteZeile) {
    if (millis() - letztesBild >= (unsigned long)anzeigeTakt()) {
      letztesBild = millis();
      bildZeichnen();
      naechsteZeile = 0;
      letzteZeile = 8;
      if (millis() - letztesUnten >= 250) { letzteZeile = 16; letztesUnten = millis(); }
    }
    return;
  }
  // je Aufruf 1 Zeile senden, damit das Messen nicht gebremst wird
  anzeige.updateDisplayArea(0, naechsteZeile, 16, 1);
  naechsteZeile++;
}

void meldung(const char* z1, const char* z2, const char* z3, const char* z4) {
  anzeige.clearBuffer();
  anzeige.setFont(u8g2_font_9x15B_tf);
  anzeige.drawUTF8(textMitte(z1), 34, z1);
  anzeige.drawUTF8(textMitte(z2), 52, z2);
  anzeige.setFont(u8g2_font_7x14B_tf);
  anzeige.drawUTF8(textMitte(z3), 84, z3);
  anzeige.drawUTF8(textMitte(z4), 102, z4);
  anzeige.sendBuffer();
  naechsteZeile = 16;
}

// Einschaltbild: oben "Sweef", darunter Begruessung und ID
void startBild(const char* unten1, const char* unten2) {
  anzeige.clearBuffer();
  anzeige.setFont(u8g2_font_logisoso24_tr);
  anzeige.drawStr(textMitte("Sweef"), 28, "Sweef");
  anzeige.drawHLine(24, 34, 80);
  anzeige.setFont(u8g2_font_7x14_tf);
  char tmp[100];
  strncpy(tmp, gruss, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = 0;
  char* rest = tmp;
  char* z;
  int y = 52, zeilen = 0;
  while (zeilen < 4 && (z = strtok_r(rest, "|", &rest)) != NULL) {
    anzeige.drawUTF8(max(0, textMitte(z)), y, z);
    y += 14;
    zeilen++;
  }
  anzeige.setFont(u8g2_font_6x12_tf);
  char id[12];
  snprintf(id, sizeof(id), "ID %s", geraeteId);
  anzeige.drawStr(textMitte(id), 104, id);
  // Hinweis unten deutlich, in einer Zeile (z. B. "Ruhig halten...")
  char hinweis[40];
  snprintf(hinweis, sizeof(hinweis), "%s%s%s", unten1, (unten1[0] && unten2[0]) ? " " : "", unten2);
  anzeige.drawHLine(0, 109, BR);
  anzeige.setFont(u8g2_font_7x14B_tf);
  anzeige.drawUTF8(max(0, textMitte(hinweis)), 125, hinweis);
  anzeige.sendBuffer();
  naechsteZeile = 16;
}

void warteMessend(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) messen();
}

// Zeigt, ob alle Teile da sind. Bei Fehler: tiefe Pieps, laenger anzeigen.
void selbsttest() {
  struct { const char* name; bool ok; } t[] = {
    {"Druck", sensorOk}, {"Lage", imuOk}, {"Daten", flashOk}
  };
  bool fehler = false;
  anzeige.clearBuffer();
  anzeige.setFont(u8g2_font_9x15B_tf);
  anzeige.drawStr(textMitte("Selbsttest"), 14, "Selbsttest");
  anzeige.drawHLine(0, 19, BR);
  anzeige.setFont(u8g2_font_7x14B_tf);
  for (int i = 0; i < 3; i++) {
    int y = 36 + i * 16;
    anzeige.drawStr(6, y, t[i].name);
    const char* e = t[i].ok ? "OK" : "FEHLT";
    anzeige.drawStr(122 - anzeige.getStrWidth(e), y, e);
    if (!t[i].ok) fehler = true;
  }
  char a[12];
  if (akkuFehlt()) snprintf(a, sizeof(a), "--");
  else             snprintf(a, sizeof(a), "%d%%", akkuProzent());
  anzeige.drawStr(6, 84, "Akku");
  anzeige.drawStr(122 - anzeige.getStrWidth(a), 84, a);
  anzeige.drawHLine(0, 92, BR);
  if (fehler) {
    if (!flashOk) {                        // Kennung des Datenspeichers zeigen
      char k[24];
      snprintf(k, sizeof(k), "Daten-ID %02X %02X %02X", flashKennung[0], flashKennung[1], flashKennung[2]);
      anzeige.drawStr(textMitte("Fehler!"), 108, "Fehler!");
      anzeige.setFont(u8g2_font_6x12_tf);
      anzeige.drawStr(textMitte(k), 124, k);
    } else {
      anzeige.drawUTF8(textMitte("Fehler! Bitte prüfen"), 114, "Fehler! Bitte prüfen");
    }
  } else {
    anzeige.drawStr(textMitte("alles OK"), 114, "alles OK");
  }
  unsigned long t0 = millis();
  anzeige.sendBuffer();
  Serial.print("Selbsttest gesendet in "); Serial.print(millis() - t0);
  Serial.print(" ms  Druck="); Serial.print(sensorOk); Serial.print(" Lage="); Serial.print(imuOk);
  Serial.print(" Daten="); Serial.println(flashOk);
  naechsteZeile = 16;
  if (fehler) {
    for (int i = 0; i < 3; i++) { kurzerPiep(400, 150); delay(100); }
    warteMessend(4000);
  } else {
    warteMessend(2000);
  }
}

#endif  // DISPLAY_KLEIN

// Startplatz-Frage ueber der normalen Anzeige (passt auf beide Displays)
void frageBox() {
  char name[28], hoehe[28];
  strncpy(name, frageName, sizeof(name) - 1); name[sizeof(name) - 1] = 0;
  anzeige.setFont(u8g2_font_6x10_tf);
  while (strlen(name) > 3 && anzeige.getUTF8Width(name) > BR - 4) name[strlen(name) - 1] = 0;   // kuerzen
  snprintf(hoehe, sizeof(hoehe), "%d m", frageHoehe);
  if (frageRichtung[0]) {                      // Hoehe und Startrichtung in einer Zeile, wenn es passt
    char mit[28];
    snprintf(mit, sizeof(mit), "%d m %s", frageHoehe, frageRichtung);
    anzeige.setFont(u8g2_font_6x10_tf);
    if (anzeige.getUTF8Width(mit) <= BR - 4) strncpy(hoehe, mit, sizeof(hoehe) - 1), hoehe[sizeof(hoehe) - 1] = 0;
  }
  anzeige.setDrawColor(1);
  anzeige.drawBox(0, 30, BR, 64);
  anzeige.setDrawColor(0);
  const char* titel = BR >= 100 ? "Startplatz?" : "Startpl.?";
  anzeige.drawUTF8(textMitte(titel), 42, titel);
  anzeige.drawUTF8(textMitte(name), 55, name);
  anzeige.drawUTF8(textMitte(hoehe), 68, hoehe);
  if (BR >= 100) {
    anzeige.drawUTF8(textMitte("T1 = Ja  T2 = Nein"), 86, "T1 = Ja  T2 = Nein");
  } else {
    anzeige.drawUTF8(textMitte("T1 = Ja"), 81, "T1 = Ja");
    anzeige.drawUTF8(textMitte("T2 = Nein"), 92, "T2 = Nein");
  }
  anzeige.setDrawColor(1);
}

// =================================================================
// Ein / Aus
// =================================================================

// Zeigt eine Frage und wartet bis zu 10 s auf Taste 2.
// Misst dabei weiter, damit Hoehe und Steigen stimmen bleiben.
bool bestaetigen(const char* z1, const char* z2) {
  tonAus();
  unsigned long start = millis();
  bool alt2 = taste2();
  bool alt1 = taste1();                     // Taste 1 ist evtl. noch gedrueckt
  int letzteSek = -1;
  while (millis() - start < BESTAETIGEN_MS) {
    messen();
    int rest = (BESTAETIGEN_MS - (millis() - start)) / 1000 + 1;
    if (rest != letzteSek) {
      char zeile[16];
      snprintf(zeile, sizeof(zeile), "T2=ja  %ds", rest);
      meldung(z1, z2, zeile, "T1=nein");
      letzteSek = rest;
    }
    // Taste 2 = ja
    bool t2 = taste2();
    if (t2 && !alt2) {
      delay(30);
      if (taste2()) {
        kurzerPiep(1500, 80);
        while (taste2()) messen();          // warten bis losgelassen
        return true;
      }
    }
    alt2 = t2;
    // Taste 1 (neu gedrueckt) = nein, abbrechen
    bool t1 = taste1();
    if (t1 && !alt1) {
      delay(30);
      if (taste1()) {
        kurzerPiep(600, 120);
        while (taste1()) messen();          // warten bis losgelassen
        return false;
      }
    }
    alt1 = t1;
  }
  kurzerPiep(600, 120);                      // abgebrochen
  return false;
}

void warteTaste1Los() {
  while (taste1()) delay(10);
  delay(50);
}

// Echter Tiefschlaf: verbraucht fast nichts. Aufwachen mit Taste 1.
// Beim Aufwachen startet das Programm ganz neu (setup).
void flugBeendenWennNoetig() {         // beim Ausschalten / Neustart
  if (imFlug) {
    flugDauerMs = millis() - flugStartMs;
    imFlug = false;
    logEnde(flugDauerMs);
  }
}

void tiefschlaf() {
  tonAus();
  flugBeendenWennNoetig();
  if (speichernUm) lautSpeichern();
  einstSichern();
  vollSichern();
  flugspeicherSchlafen();
  verstaerkerAus();                                // Verstaerker aus
  anzeige.setPowerSave(1);                         // Display aus
  if (sensorOk) dps.setMode(DPS310_IDLE);          // Drucksensor schlafen
  digitalWrite(PIN_LSM6DS3TR_C_POWER, LOW);        // Bewegungssensor aus
  warteTaste1Los();                                // sonst sofort wieder wach
  systemOff(PIN_TASTE1, LOW);
}

void selbsttest();
void neustart() {
  tonAus();
  meldung("Neustart", "", "Ruhig", "halten...");
  flugBeendenWennNoetig();
  // alle Teile neu pruefen (auch die, die beim Einschalten gefehlt haben)
  sensorOk = dps.begin_I2C(0x77, &Wire);
  if (sensorOk) {
    dps.configurePressure(DPS310_32HZ, DPS310_8SAMPLES);
    dps.configureTemperature(DPS310_2HZ, DPS310_1SAMPLE);
  }
  imuOk = imuStarten();
  if (!flashOk) flugspeicherStarten();
  nullDruck = 0;
  gestartet = false;
  flugReset();
  kalmanReset();
  selbsttest();
  kurzerPiep(1000, 120);
  delay(80);
  kurzerPiep(1500, 120);
}

// Einschalten: Taste 1 muss gehalten werden, dann bestaetigen.
bool einschaltAbfrage() {
  unsigned long start = millis();
  while (taste1()) {
    if (millis() - start >= HALTEN_AUS - 300) {    // ~0,3 s braucht der Start
      anzeige.setPowerSave(0);
      anzeige.setContrast(kontrast());
      // Frage sofort zeigen (nicht erst nach dem Loslassen).
      // bestaetigen() wertet die noch gehaltene Taste 1 nicht als "nein".
      bool ja = bestaetigen("Ein-", "schalten?");
      warteTaste1Los();                            // sonst gleich "Ausschalten?"
      return ja;
    }
    delay(10);
  }
  return false;                                    // zu kurz gedrueckt
}

// Lademodus: Vario ist "aus", USB-Kabel steckt. Zeigt den Akku an.
// Kabel ab -> Tiefschlaf. Taste 1 halten + bestaetigen -> zurueck (einschalten).
// Taste 2 -> Display fuer 30 s hell (sonst wird es nach 30 s dunkler).
void lademodus() {
  tonAus();
  verstaerkerAus();
  anzeige.setPowerSave(0);
  anzeige.setContrast(kontrast());
  unsigned long hellAb = millis();
  unsigned long t2Seit = 0;
  bool dunkel = false;
  while (true) {
    if (!usbSteckt()) tiefschlaf();                // Kabel ab -> richtig aus
    akkuAktualisieren();
    // PIN vergessen: Taste 2 8 Sekunden halten
    if (taste2() && !taste1()) {
      if (t2Seit == 0) t2Seit = millis();
      if (millis() - t2Seit > 3000) {
        int rest = 8 - (millis() - t2Seit) / 1000;
        if (rest <= 0) {
          pinLoeschen();
          meldung("PIN", "gelöscht", "Handy neu", "verbinden");
          while (taste2()) delay(10);
          delay(2500);
          t2Seit = 0;
        } else {
          char z[16];
          snprintf(z, sizeof(z), "halten %d s", rest);
          meldung("PIN", "löschen?", z, "");
          delay(150);
          continue;
        }
      }
    } else {
      t2Seit = 0;
    }
    if (taste1() || taste2()) {
      hellAb = millis();
      if (dunkel) { anzeige.setContrast(kontrast()); dunkel = false; }
    }
    if (taste1()) {
      if (einschaltAbfrage()) return;
      verstaerkerAus();
      anzeige.setPowerSave(0);
    }
    if (!dunkel && millis() - hellAb > 30000) { anzeige.setContrast(min((uint8_t)10, kontrast())); dunkel = true; }
    int versatz = (int)((millis() / 60000) % 5) - 2;   // jede Minute etwas verschieben
    verstaerkerPruefen();
    ladeBild(versatz);
    anzeige.sendBuffer();
    delay(150);
  }
}

// "Aus" mit USB-Kabel: Lademodus. Der Controller bleibt wach,
// damit das Hochladen weiter funktioniert. Bluetooth geht aus
// (sonst findet das Handy ein Vario, das "aus" ist).
extern uint16_t verbHandle;
void ausMitUsb() {
  tonAus();
  Bluefruit.Advertising.restartOnDisconnect(false);
  Bluefruit.Advertising.stop();
  if (Bluefruit.connected() && verbHandle != BLE_CONN_HANDLE_INVALID) {
    Bluefruit.disconnect(verbHandle);
    delay(200);
  }
  flugBeendenWennNoetig();
  if (speichernUm) lautSpeichern();
  einstSichern();
  lademodus();
  // Einschalten aus dem Lademodus: richtig neu starten, damit wie beim
  // Einschalten mit Akku Startbild und Selbsttest kommen.
  vollSichern();
  qspiAus();                       // Speicher-Schnittstelle sauber aus vor dem Neustart
  delay(50);
  NVIC_SystemReset();
}

void ausschalten() {
  if (usbSteckt()) ausMitUsb();
  else             tiefschlaf();
}

// =================================================================
// Bluetooth: Einstellungen per Handy
// =================================================================
// Das Vario meldet sich als "Sweef". Es gibt drei "Kanaele":
//  - Einstellungen (lesen): Text wie "se=0.10;sk=-2.50;f1=700;..."
//  - Befehl (schreiben):    z.B. "se=0.2" oder "test=2.5" oder "reset"
//  - Live (Meldungen):      Steigen, Hoehe usw. zweimal pro Sekunde
// Kennung (UUID) des Dienstes: 7e5e0001-b5a3-f393-e0a9-e50e24dcca9e
const uint8_t UUID_DIENST[16] = {0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x01,0x00,0x5e,0x7e};
const uint8_t UUID_EINST[16]  = {0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x02,0x00,0x5e,0x7e};
const uint8_t UUID_BEFEHL[16] = {0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x03,0x00,0x5e,0x7e};
const uint8_t UUID_LIVE[16]   = {0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x04,0x00,0x5e,0x7e};
const uint8_t UUID_DATEN[16]  = {0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x05,0x00,0x5e,0x7e};
const uint8_t UUID_INFO[16]   = {0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x06,0x00,0x5e,0x7e};

BLEService        dienst(UUID_DIENST);
BLECharacteristic chrEinst(UUID_EINST);
BLECharacteristic chrBefehl(UUID_BEFEHL);
BLECharacteristic chrLive(UUID_LIVE);
BLECharacteristic chrDaten(UUID_DATEN);    // Fluege uebertragen, Antworten
BLECharacteristic chrInfo(UUID_INFO);      // ID, Name, Begruessung, PIN an/aus

// Freigabe: ohne PIN immer frei, mit PIN erst nach richtiger PIN/Schluessel
bool freigegeben = false;
volatile bool neuVerbunden = false, neuGetrennt = false;
int  falscheVersuche = 0;
unsigned long sperreBis = 0;
bool gesperrt() { return pinAktiv() && !freigegeben; }

// Uebertragung der Fluege (laeuft Stueck fuer Stueck in loop())
int      sendeArt = 0;          // 0 = nichts, 1 = Flugliste, 2 = Flugdaten
int      sendeIdx = 0;          // Liste: naechster Flug / Daten: welcher Flug
uint32_t sendeAb = 0;           // Daten: naechster Wert
bool     loeschenAngefordert = false;

// Liste aller per Handy aenderbaren Einstellungen
// Kuerzel, Typ (f=Kommazahl, i=Ganzzahl, b=an/aus), Variable, min, max
struct Einstellung { const char* kurz; char typ; void* wert; float min, max; };
Einstellung EINST[] = {
  {"se", 'f', &STEIG_EIN,          0.0,   3.0},   // Steigton ab (m/s)
  {"sk", 'f', &SINK_ALARM,       -10.0,  -0.5},   // Sinkalarm ab (m/s)
  {"f1", 'i', &TON_MIN,            300,  3000},   // Tonhoehe schwach (Hz)
  {"f2", 'i', &TON_MAX,            500,  4000},   // Tonhoehe stark (Hz)
  {"fk", 'i', &TON_SINK,           200,  2000},   // Sinkton (Hz)
  {"fs", 'f', &TON_SKALA,          1.0,  30.0},   // Ton-Skala (m/s)
  {"tl", 'i', &TAKT_LANGSAM,       200,  2000},   // Takt schwach (ms)
  {"ts", 'i', &TAKT_SCHNELL,       100,  1500},   // Takt stark (ms)
  {"pa", 'i', &PIEP_ANTEIL,         10,    90},   // Piep-Anteil (%)
  {"nn", 'f', &STARTPLATZ_NN,   -100.0, 5000.0},  // Startplatz ueber Meer (m)
  {"qn", 'f', &QNH,                0.0, 1100.0},  // QNH (hPa), 0 = aus
  {"pf", 'b', &PFEIL_AN,             0,     1},   // Pfeil an/aus
  {"ph", 'b', &PFEIL_HOHL,           0,     1},   // Pfeil hohl
  {"pt", 'f', &PFEIL_TEMPO,        1.0,  80.0},   // Pfeil-Tempo
  {"fh", 'f', &FLUG_START_HOEHE,   1.0,  50.0},   // Flugstart: Hoehe (m)
  {"fd", 'i', &FLUG_START_DAUER_S,   2,    60},   // Flugstart: Dauer (s)
  {"lr", 'i', &LANDUNG_RUHE_S,      20,   900},   // Landung: Ruhezeit (s)
  {"ao", 'i', &AUTO_AUS_MIN,         0,   240},   // Auto-Aus nach Minuten (0 = nie)
  {"sb", 'b', &STUMM_AM_BODEN,       0,     1},   // am Boden stumm
  {"nz", 'b', &NULLSCHIEBER,         0,     1},   // Nullschieber an/aus
  {"nb", 'f', &NULL_AB,           -1.5,   0.0},   // Nullschieber ab (m/s)
  {"dr", 'b', &DISPLAY_DREHEN,       0,     1},   // Display 180 Grad
  {"hl", 'i', &HELLIGKEIT,           5,   100},   // Display-Helligkeit (%)
  {"ga", 'b', &G_AUTO,               0,     1},   // G-Anzeige automatisch
  {"gw", 'f', &G_WARNUNG,          0.0,   8.0},   // G-Warnung ab (g), 0 = aus
  {"la", 'i', &LIVE_MS,            100,  2000},   // Live-Werte ans Handy alle ... ms
  {"za", 'i', &ZAHL_MS,            100,  2000},   // Steigwert-Zahl am Display: Mittel ueber ... ms
  {"ds", 'i', &DISPLAY_AUS_MIN,      0,    60},   // Display am Boden aus nach ... min (0 = nie)
  {"gv", 'i', &GEGENWIND_AB,         0,    30},   // Gegenwind-Warnung unter ... km/h (0 = aus)
  {"wa", 'b', &WIND_ANZEIGEN,        0,     1},   // Wind am Display
  {"rz", 'b', &RESTZEIT,             0,     1},   // Restlaufzeit statt Prozent
  {"sh", 'i', &SONNE_VORHER,         0,   120},   // Sonnenuntergang-Hinweis ... min vorher (0 = aus)
  {"dt", 'b', &DOPPELTIPP,           0,     1},   // Doppeltippen = stumm / Ton an
};
const int ANZ_EINST = sizeof(EINST) / sizeof(EINST[0]);
float standardWerte[ANZ_EINST];    // Grundeinstellung (fuer "reset")

float einstHolen(int i) {
  switch (EINST[i].typ) {
    case 'f': return *(float*)EINST[i].wert;
    case 'i': return *(int*)EINST[i].wert;
    default:  return *(bool*)EINST[i].wert ? 1 : 0;
  }
}

void einstSetzen(int i, float x) {
  x = constrain(x, EINST[i].min, EINST[i].max);
  switch (EINST[i].typ) {
    case 'f': *(float*)EINST[i].wert = x; break;
    case 'i': *(int*)EINST[i].wert = (int)lroundf(x); break;
    default:  *(bool*)EINST[i].wert = (x >= 0.5); break;
  }
  STEIG_AUS = max(0.0f, STEIG_EIN - 0.05f);     // Hysterese mitfuehren
}

// Zahl als Text mit 2 Nachkommastellen (ohne printf-Kommazahlen)
int zahlText(char* t, int laenge, float x) {
  long z = lroundf(x * 100);
  const char* vz = (z < 0) ? "-" : "";
  z = labs(z);
  if (z % 100 == 0) return snprintf(t, laenge, "%s%ld", vz, z / 100);
  return snprintf(t, laenge, "%s%ld.%02ld", vz, z / 100, z % 100);
}

// Alle Einstellungen als Text: "se=0.1;sk=-2.5;..."
int einstText(char* buf, int groesse) {
  int p = 0;
  for (int i = 0; i < ANZ_EINST && p < groesse - 20; i++) {
    p += snprintf(buf + p, groesse - p, "%s%s=", i ? ";" : "", EINST[i].kurz);
    p += zahlText(buf + p, groesse - p, einstHolen(i));
  }
  p += snprintf(buf + p, groesse - p, ";ls=%d", lautStufe);
  return p;
}

unsigned long einstSpeichernUm = 0;

void einstSpeichern() {
  char buf[300];
  int n = einstText(buf, sizeof(buf));
  InternalFS.remove("/einst");
  Adafruit_LittleFS_Namespace::File f(InternalFS);
  if (f.open("/einst", FILE_O_WRITE)) {
    f.write((uint8_t*)buf, n);
    f.close();
  }
  einstSpeichernUm = 0;
}

void einstAnHandy() {             // Einstellungs-Kanal aktualisieren
  char buf[300];
  int n = gesperrt() ? snprintf(buf, sizeof(buf), "lock=1") : einstText(buf, sizeof(buf));
  chrEinst.write(buf, n);
  if (Bluefruit.connected()) chrEinst.notify(buf, n);
}

// Fuehrt Befehle aus: "se=0.2;f1=800" / "test=2.5" / "ls=3" / "reset" / "null"
// mitWirkung=false: nur Werte setzen (beim Laden aus dem Speicher)
void befehlAusfuehren(char* text, bool mitWirkung) {
  bool geaendert = false;
  char* rest = text;
  char* teil;
  while ((teil = strtok_r(rest, ";\n\r", &rest)) != NULL) {
    char* gleich = strchr(teil, '=');
    if (!gleich) {
      if (mitWirkung && strcmp(teil, "reset") == 0) {          // Grundeinstellung
        for (int i = 0; i < ANZ_EINST; i++) einstSetzen(i, standardWerte[i]);
        geaendert = true;
      } else if (mitWirkung && strcmp(teil, "fl_liste") == 0) {  // Flugliste senden
        sendeArt = 1; sendeIdx = 0;
        schnellBis = millis() + 5000;
      } else if (mitWirkung && strcmp(teil, "fl_loeschen") == 0) {  // alle Fluege loeschen
        loeschenAngefordert = true;
      } else if (mitWirkung && strcmp(teil, "null") == 0) {    // Hoehe 0
        nullDruck = 0;
      } else if (mitWirkung && strcmp(teil, "suchen") == 0) {  // Vario suchen
        suchenBis = millis() + 20000;
        if (suchenBis == 0) suchenBis = 1;
      } else if (mitWirkung && strcmp(teil, "dfu") == 0) {     // Firmware-Update per Bluetooth
        if (imFlug) { antwort("U,0"); continue; }
        antwort("U,1");
        updateModusAb = millis() + 400;        // kurz warten, damit die Antwort ankommt
      } else if (mitWirkung && strcmp(teil, "suchen_aus") == 0) {
        if (suchenBis) suchenBis = millis();
      }
      continue;
    }
    *gleich = 0;
    const char* name = teil;
    float wert = atof(gleich + 1);

    if (strcmp(name, "zeit") == 0) {          // Uhrzeit vom Handy
      uint32_t unix = strtoul(gleich + 1, NULL, 10);
      if (unix > 1600000000UL) {
        zeitBasis = (int64_t)unix - millis() / 1000;
        if (logAktiv) {                       // laufender Flug ohne Zeit: nachtragen
          uint32_t z; flash.readBuffer(logAdr + 8, (uint8_t*)&z, 4);
          if (z == 0xFFFFFFFF) {
            uint32_t up; flash.readBuffer(logAdr + 16, (uint8_t*)&up, 4);
            z = (uint32_t)(zeitBasis + up);
            flash.writeBuffer(logAdr + 8, (uint8_t*)&z, 4);
          }
        }
      }
      continue;
    }
    if (strcmp(name, "lb") == 0) {            // App im Hintergrund (nicht gespeichert)
      liveHintergrund = wert > 0;
      continue;
    }
    if (strcmp(name, "fl_hol") == 0) {        // Flugdaten senden: "fl_hol=Nr@ab"
      schnellBis = millis() + 5000;
      if (mitWirkung) {
        uint32_t nr = strtoul(gleich + 1, NULL, 10);
        char* at = strchr(gleich + 1, '@');
        sendeAb = at ? strtoul(at + 1, NULL, 10) : 0;
        sendeIdx = -1;
        for (int i = 0; i < anzFluege; i++) if (fluege[i].nr == nr) sendeIdx = i;
        sendeArt = (sendeIdx >= 0) ? 2 : 0;
        if (sendeIdx < 0) { char t[24]; int n = snprintf(t, sizeof(t), "E,%lu,0", (unsigned long)nr); chrDaten.notify(t, n); }
      }
      continue;
    }
    if (strcmp(name, "test") == 0) {          // Ton testen fuer 4 Sekunden
      if (mitWirkung) {
        testSteigen = constrain(wert, -10.0f, 30.0f);
        testBis = millis() + 4000;
        if (testBis == 0) testBis = 1;
      }
      continue;
    }
    if (strcmp(name, "ls") == 0) {            // Lautstaerke
      lautStufe = constrain((int)wert, 0, LAUT_STUFEN);
      aktFreq = 0;
      if (mitWirkung) speichernUm = millis() + 3000;
      continue;
    }
    for (int i = 0; i < ANZ_EINST; i++) {
      if (strcmp(name, EINST[i].kurz) == 0) {
        einstSetzen(i, wert);
        geaendert = true;
      }
    }
  }
  if (geaendert) displayAusrichten();
  if (geaendert && mitWirkung) {
    einstSpeichernUm = millis() + 1500;       // gleich speichern
    einstAnHandy();
  }
}

void einstLaden() {
  for (int i = 0; i < ANZ_EINST; i++) standardWerte[i] = einstHolen(i);
  Adafruit_LittleFS_Namespace::File f(InternalFS);
  if (f.open("/einst", FILE_O_READ)) {
    char buf[240];
    int n = f.read(buf, sizeof(buf) - 1);
    f.close();
    if (n > 0) {
      buf[n] = 0;
      befehlAusfuehren(buf, false);
    }
  }
  STEIG_AUS = max(0.0f, STEIG_EIN - 0.05f);
}

// Befehle kommen im Bluetooth-Teil an und werden in loop() ausgefuehrt
volatile bool befehlDa = false;
char befehlPuffer[201];

void befehlEmpfangen(uint16_t verbindung, BLECharacteristic* chr, uint8_t* daten, uint16_t laenge) {
  if (laenge > 3 && laenge < 12 && memcmp(daten, "gs=", 3) == 0) {   // GPS-Tempo: sofort merken
    char t[12];
    memcpy(t, daten + 3, laenge - 3);
    t[laenge - 3] = 0;
    gsKmh = atof(t);
    gsZeit = millis();
    if (gsZeit == 0) gsZeit = 1;
    return;
  }
  if (laenge > 3 && laenge < 20 && memcmp(daten, "wi=", 3) == 0) {   // Wind: "wi=25,230"
    char t[20];
    memcpy(t, daten + 3, laenge - 3);
    t[laenge - 3] = 0;
    char* k = strchr(t, ',');
    windKmh = atoi(t);
    windAus = k ? atoi(k + 1) : 0;
    windZeit = millis();
    if (windZeit == 0) windZeit = 1;
    return;
  }
  if (laenge > 3 && laenge < 20 && memcmp(daten, "su=", 3) == 0) {   // Sonnenuntergang (Unix)
    char t[20];
    memcpy(t, daten + 3, laenge - 3);
    t[laenge - 3] = 0;
    sonneUnter = strtoul(t, NULL, 10);
    return;
  }
  if (befehlDa) return;                       // vorheriger noch nicht fertig
  if (laenge > 200) laenge = 200;
  memcpy(befehlPuffer, daten, laenge);
  befehlPuffer[laenge] = 0;
  befehlDa = true;
}

// ---- Info-Kanal: "id=A3F2;nm=Sweef-A3F2;zs=;pin=1;ok=0;fw=2.10;gt=..." ----
void infoAnHandy() {
  char buf[200], name[24];
  geraeteName(name, sizeof(name));
  int n = snprintf(buf, sizeof(buf), "id=%s;nm=%s;zs=%s;pin=%d;ok=%d;fw=" FW_VERSION ";dv=%c",
                   geraeteId, name, nameZusatz, pinAktiv() ? 1 : 0, gesperrt() ? 0 : 1, DISPLAY_KLEIN ? 'k' : 'g');
  if (!gesperrt()) n += snprintf(buf + n, sizeof(buf) - n, ";gt=%s", gruss);
  chrInfo.write(buf, n);
  if (Bluefruit.connected()) chrInfo.notify(buf, n);
}

void antwort(const char* text) {          // kurze Antwort ans Handy
  if (Bluefruit.connected()) chrDaten.notify(text, strlen(text));
}

// Werbung (Name, unter dem das Vario gefunden wird)
void werbungEinrichten() {
  char name[24];
  geraeteName(name, sizeof(name));
  Bluefruit.setName(name);
  Bluefruit.Advertising.clearData();
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addName();
}

uint32_t neuerSchluessel() {
  uint32_t k = 0;
  for (int i = 0; i < 20 && k == 0; i++) {
    if (sd_rand_application_vector_get((uint8_t*)&k, 4) != NRF_SUCCESS) { k = 0; delay(2); }
  }
  if (k == 0) k = (micros() * 2654435761UL) ^ NRF_FICR->DEVICEID[0];
  if (k == 0) k = 1;
  schluessel[2] = schluessel[1];          // die letzten 3 Handys merken
  schluessel[1] = schluessel[0];
  schluessel[0] = k;
  schluesselSpeichern();
  return k;
}

void schluesselSchicken() {
  char t[16];
  snprintf(t, sizeof(t), "K,%08lX", (unsigned long)neuerSchluessel());
  antwort(t);
}

void freigeben() {
  freigegeben = true;
  falscheVersuche = 0;
  antwort("A,1");
  einstAnHandy();
  infoAnHandy();
}

void pinPruefen(const char* code) {
  if (!pinAktiv()) { freigeben(); return; }
  if ((long)(millis() - sperreBis) < 0) { antwort("A,2"); return; }   // kurz gesperrt
  if (strcmp(code, pinCode) == 0) {
    schluesselSchicken();
    freigeben();
  } else {
    falscheVersuche++;
    sperreBis = millis() + (falscheVersuche >= 3 ? 30000 : 2000);
    antwort("A,0");
  }
}

void schluesselPruefen(const char* hex) {
  if (!pinAktiv()) { freigeben(); return; }
  uint32_t k = strtoul(hex, NULL, 16);
  for (int i = 0; i < 3; i++) {
    if (k != 0 && k == schluessel[i]) { freigeben(); return; }
  }
  antwort("A,0");
}

// Neue PIN (4-6 Ziffern) oder leer = PIN aus
void pinSetzen(const char* code) {
  int n = strlen(code);
  bool ok = (n == 0) || (n >= 4 && n <= 6);
  for (int i = 0; i < n; i++) if (code[i] < '0' || code[i] > '9') ok = false;
  if (!ok) { antwort("P,F"); return; }
  strcpy(pinCode, code);
  textSpeichern("/pin", pinCode);
  for (int i = 0; i < 3; i++) schluessel[i] = 0;    // andere Handys muessen neu
  schluesselSpeichern();
  if (pinAktiv()) schluesselSchicken();              // dieses Handy bleibt drin
  antwort(pinAktiv() ? "P,1" : "P,0");
  infoAnHandy();
}

// Name hinter "Sweef-": Buchstaben, Ziffern, - und _, hoechstens 12
void nameSetzen(const char* text) {
  char neu[16];
  int n = 0;
  for (const char* c = text; *c && n < 12; c++) {
    if (isalnum((unsigned char)*c) || *c == '-' || *c == '_') neu[n++] = *c;
  }
  neu[n] = 0;
  strcpy(nameZusatz, neu);
  textSpeichern("/name", nameZusatz);
  werbungEinrichten();                     // gilt ab dem naechsten Verbinden
  infoAnHandy();
  antwort("N,1");
}

// Begruessung: bis 4 Zeilen, getrennt durch '|'
void grussSetzen(const char* text) {
  strncpy(gruss, text, sizeof(gruss) - 1);
  gruss[sizeof(gruss) - 1] = 0;
  textSpeichern("/gruss", gruss);
  infoAnHandy();
  antwort("G,1");
}

// Alles, was vom Handy kommt, landet hier
void befehlVomHandy(char* text) {
  if (strncmp(text, "pin=", 4) == 0)  { pinPruefen(text + 4); return; }
  if (strncmp(text, "auth=", 5) == 0) { schluesselPruefen(text + 5); return; }
  if (gesperrt()) {                        // ohne PIN nur die Uhrzeit
    if (strncmp(text, "zeit=", 5) == 0) befehlAusfuehren(text, true);
    else antwort("A,0");
    return;
  }
  if (strncmp(text, "pin_neu=", 8) == 0) { pinSetzen(text + 8); return; }
  if (strncmp(text, "nm=", 3) == 0)      { nameSetzen(text + 3); return; }
  if (strncmp(text, "gt=", 3) == 0)      { grussSetzen(text + 3); return; }
  if (strncmp(text, "spfrage=", 8) == 0) {             // Startplatz-Frage (nur am Boden)
    if (imFlug) { antwort("S,9"); return; }
    char* trenn = strchr(text + 8, '|');           // "186|Name|S/SW"
    frageHoehe = atoi(text + 8);
    char* trenn2 = trenn ? strchr(trenn + 1, '|') : NULL;
    if (trenn2) *trenn2 = 0;
    strncpy(frageName, trenn ? trenn + 1 : "", sizeof(frageName) - 1);
    frageName[sizeof(frageName) - 1] = 0;
    strncpy(frageRichtung, trenn2 ? trenn2 + 1 : "", sizeof(frageRichtung) - 1);
    frageRichtung[sizeof(frageRichtung) - 1] = 0;
    frageBis = millis() + 30000;
    aktivJetzt();
    kurzerPiep(1200, 70); delay(50); kurzerPiep(1600, 90);
    return;
  }
  befehlAusfuehren(text, true);
}

uint16_t verbHandle = BLE_CONN_HANDLE_INVALID;
void verbundenCb(uint16_t verbindung) { neuVerbunden = true; verbHandle = verbindung; }
// Bluetooth-Takt: im Normalfall 60 ms (spart Strom), zum Uebertragen der Fluege 15 ms
int verbTakt = 0;
void verbindungsTakt(int einheiten) {           // Einheiten zu 1,25 ms
  if (verbTakt == einheiten || verbHandle == BLE_CONN_HANDLE_INVALID) return;
  BLEConnection* c = Bluefruit.Connection(verbHandle);
  if (c && c->requestConnectionParameter(einheiten)) verbTakt = einheiten;
}
void getrenntCb(uint16_t verbindung, uint8_t grund) { neuGetrennt = true; }

void bluetoothStarten() {
  Bluefruit.autoConnLed(false);               // blaue LED nicht blinken lassen
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin();
  Bluefruit.setTxPower(0);
  Bluefruit.Periph.setConnectCallback(verbundenCb);
  Bluefruit.Periph.setDisconnectCallback(getrenntCb);

  dienst.begin();

  chrEinst.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  chrEinst.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  chrEinst.setMaxLen(300);
  chrEinst.begin();

  chrBefehl.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
  chrBefehl.setPermission(SECMODE_NO_ACCESS, SECMODE_OPEN);
  chrBefehl.setMaxLen(200);
  chrBefehl.setWriteCallback(befehlEmpfangen);
  chrBefehl.begin();

  chrLive.setProperties(CHR_PROPS_NOTIFY);
  chrLive.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  chrLive.setMaxLen(150);
  chrLive.begin();

  chrDaten.setProperties(CHR_PROPS_NOTIFY);
  chrDaten.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  chrDaten.setMaxLen(244);
  chrDaten.begin();

  chrInfo.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  chrInfo.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  chrInfo.setMaxLen(200);
  chrInfo.begin();

  einstAnHandy();
  infoAnHandy();

  werbungEinrichten();
  Bluefruit.ScanResponse.addService(dienst);
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(320, 1600);   // 0,2 s schnell, dann 1 s
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
}

// in loop(): Befehle ausfuehren, Live-Werte senden, Speichern
void einstSichern() { if (einstSpeichernUm) einstSpeichern(); }

// Wie viele Bytes passen in eine Bluetooth-Meldung?
int meldungsGroesse() {
  BLEConnection* c = Bluefruit.Connection(Bluefruit.connHandle());
  int mtu = c ? c->getMtu() : 23;
  return constrain(mtu - 3, 20, 244);
}

// Schickt ein Stueck Flugliste oder Flugdaten. true = gesendet
bool flugdatenSenden() {
  if (!Bluefruit.connected()) { sendeArt = 0; return false; }
  char t[80];
  static char gText[700];
  if (sendeArt == 1) {                        // Liste: eine Zeile pro Flug
    if (sendeIdx == 0) textLaden("/gmax", gText, sizeof(gText));
    if (sendeIdx < anzFluege) {
      FlugInfo& f = fluege[sendeIdx];
      int n = snprintf(t, sizeof(t), "L,%lu,%lu,%lu,%lu,%lu,%d,%u,%d",
                       (unsigned long)f.nr, (unsigned long)f.zeit, (unsigned long)f.boot,
                       (unsigned long)f.up, (unsigned long)f.anzahl, f.nn, f.vor,
                       gMaxSuchen(gText, f.nr));
      if (!chrDaten.notify(t, n)) return false;
      sendeIdx++;
    } else {                                  // Ende: Infos zum Vario
      int frei = flashOk ? (int)(1000 - (uint64_t)logEndeAdr * 1000 / flash.size()) : 0;
      int n = snprintf(t, sizeof(t), "LE,%lu,%lu,%d,%d,%d",
                       (unsigned long)bootNr, (unsigned long)(millis() / 1000), frei,
                       anzFluege, logAktiv ? 1 : 0);
      if (!chrDaten.notify(t, n)) return false;
      sendeArt = 0;
    }
    return true;
  }
  if (sendeArt == 2) {                        // Daten: 'D', Nr (2), ab (4), Werte
    FlugInfo& f = fluege[sendeIdx];
    if (sendeAb >= f.anzahl) {
      int n = snprintf(t, sizeof(t), "E,%lu,%lu", (unsigned long)f.nr, (unsigned long)f.anzahl);
      if (!chrDaten.notify(t, n)) return false;
      sendeArt = 0;
      return true;
    }
    uint8_t pak[244];
    int maxWerte = (meldungsGroesse() - 7) / 2;
    int anz = min((uint32_t)maxWerte, f.anzahl - sendeAb);
    pak[0] = 'D';
    uint16_t nr16 = f.nr;
    memcpy(pak + 1, &nr16, 2);
    memcpy(pak + 3, &sendeAb, 4);
    flash.readBuffer(f.adr + 32 + sendeAb * 2, pak + 7, anz * 2);
    if (!chrDaten.notify(pak, 7 + anz * 2)) return false;
    sendeAb += anz;
    return true;
  }
  return false;
}

void flugspeicherLoeschen() {
  loeschenAngefordert = false;
  if (!flashOk || logAktiv) { chrDaten.notify("X,0", 3); return; }   // nicht im Flug
  tonAus();
  meldung("Flüge", "löschen...", "", "");
  uint32_t sektoren = aufRunden4k(logEndeAdr) / 4096;
  if (sektoren > 64) flash.eraseChip();
  else for (uint32_t i = 0; i < sektoren; i++) flash.eraseSector(i);
  flash.waitUntilReady();
  InternalFS.remove("/gmax");
  flugspeicherEinlesen();
  chrDaten.notify("X,1", 3);
}

void bluetoothArbeiten() {
  if (neuGetrennt) {
    neuGetrennt = false;
    freigegeben = false;
    sendeArt = 0;
    einstAnHandy();
    infoAnHandy();
  }
  if (neuVerbunden) {
    neuVerbunden = false;
    verbTakt = 0; liveHintergrund = false;
    freigegeben = !pinAktiv();
    falscheVersuche = 0;
    einstAnHandy();
    infoAnHandy();
  }
  if (Bluefruit.connected() && millis() > 3000)
    verbindungsTakt((sendeArt != 0 || (long)(millis() - schnellBis) < 0) ? 12 : 48);
  if (loeschenAngefordert) flugspeicherLoeschen();
  for (int i = 0; i < 3 && sendeArt != 0; i++) {
    if (!flugdatenSenden()) break;            // Puffer voll: spaeter weiter
  }
  if (befehlDa) {
    char text[201];
    strcpy(text, befehlPuffer);
    befehlDa = false;
    befehlVomHandy(text);
  }
  if (einstSpeichernUm && (long)(millis() - einstSpeichernUm) >= 0) einstSpeichern();

  static unsigned long letzteLive = 0;
  unsigned long liveTakt = liveHintergrund ? max(2000UL, (unsigned long)LIVE_MS) : (unsigned long)LIVE_MS;
  if (Bluefruit.connected() && !gesperrt() && millis() - letzteLive >= liveTakt) {
    letzteLive = millis();
    char buf[150], z1[16], z2[16];
    zahlText(z1, sizeof(z1), vJetzt());
    zahlText(z2, sizeof(z2), hoeheNN());
    unsigned long minuten = flugGewesen ? flugDauerMs / 60000UL : 0;
    int n = snprintf(buf, sizeof(buf), "v=%s;h=%ld;nn=%s;mx=%ld;ls=%d;fl=%d;t=%lu;imu=%d;ak=%d;ld=%d;gb=%d;g=%d;gm=%d;av=%d;us=%d",
                     z1, lroundf(h), z2, lroundf(maxHoehe), lautStufe,
                     imFlug ? 1 : 0, minuten, imuOk ? 1 : 0,
                     akkuProzent(), (usbSteckt() && laedt()) ? 1 : 0, bodenStumm() ? 1 : 0,
                     (int)lroundf(gWert * 10), (int)lroundf(gMaxFlug * 10),
                     (int)lroundf(akkuV * 1000), usbSteckt() ? 1 : 0);   // av = Akkuspannung in mV
    chrLive.notify(buf, n);
  }
}

// =================================================================
// Auto-Aus: schaltet ab, wenn das Vario am Boden lange nicht bewegt wird
// =================================================================
unsigned long letzteAktiv = 0;
float aktivHoehe = 0;

void aktivJetzt() { letzteAktiv = millis(); aktivHoehe = h; }

void autoAusPruefen() {
  static unsigned long letztePruefung = 0;
  if (millis() - letztePruefung < 1000) return;
  letztePruefung = millis();
  // nie im Flug, am USB-Kabel oder wenn das Handy verbunden ist
  if (AUTO_AUS_MIN == 0 || imFlug || usbSteckt() || Bluefruit.connected()) { aktivJetzt(); return; }
  if (fabs(h - aktivHoehe) > 3.0 || fabs(letzteBeschl) > 1.5) aktivJetzt();   // bewegt
  if (millis() - letzteAktiv < AUTO_AUS_MIN * 60000UL) return;

  // 30 Sekunden Vorwarnung, jede Taste bricht ab
  tonAus();
  unsigned long start = millis();
  int letzteSek = -1;
  while (millis() - start < 30000) {
    messen();
    int rest = 30 - (millis() - start) / 1000;
    if (rest != letzteSek) {
      char z[16];
      snprintf(z, sizeof(z), "in %d s", rest);
      meldung("Auto-Aus", z, "Taste", "drücken");
      if (rest % 10 == 0) { kurzerPiep(900, 80); delay(60); kurzerPiep(700, 120); }
      letzteSek = rest;
    }
    if (taste1() || taste2()) {
      while (taste1() || taste2()) messen();   // warten bis losgelassen
      kurzerPiep(1500, 80);
      aktivJetzt();
      return;
    }
  }
  tiefschlaf();
}

// =================================================================
// Akkuwarnung: bei 10 % und 5 % (dann alle 5 Minuten), leer am Boden: aus
// =================================================================
void warnung(const char* text) {
  strncpy(warnTitel, "Akku", sizeof(warnTitel) - 1);
  strncpy(warnText, text, sizeof(warnText) - 1);
  warnBis = millis() + 5000;
  if (warnBis == 0) warnBis = 1;
  for (int i = 0; i < 3; i++) { kurzerPiep(520, 90); delay(70); }
}

void akkuWarnen() {
  static unsigned long letzte = 0, stufe2Um = 0, leerSeit = 0;
  static int warnStufe = 0;              // 0 = nichts, 1 = 10 % gemeldet, 2 = 5 %
  if (millis() - letzte < 2000) return;
  letzte = millis();
  if (usbSteckt() || akkuFehlt() || millis() < 10000) {
    if (usbSteckt()) warnStufe = 0;
    leerSeit = 0;
    return;
  }
  int p = akkuProzent();
  if (p <= 5) {
    if (warnStufe < 2 || (long)(millis() - stufe2Um) >= 0) {
      warnStufe = 2;
      stufe2Um = millis() + 300000UL;    // in 5 Minuten wieder
      warnung("5 %");
    }
  } else if (p <= 10 && warnStufe < 1) {
    warnStufe = 1;
    warnung("10 %");
  }
  // Akku leer und am Boden: sauber ausschalten (im Flug nie)
  if (akkuV < 3.40f && !imFlug) {
    if (leerSeit == 0) leerSeit = millis();
    if (millis() - leerSeit > 30000) {
      meldung("Akku leer", "", "schalte", "aus...");
      delay(2500);
      tiefschlaf();
    }
  } else {
    leerSeit = 0;
  }
}

// =================================================================
// Taster
// =================================================================
void tasterAbfragen() {
  static unsigned long t1Start = 0, t2Start = 0, beideStart = 0;
  static bool t1Erledigt = false, t2Erledigt = false, beideErledigt = false;

  bool t1 = taste1(), t2 = taste2();
  if (t1 || t2) letzteTaste = millis();
  if (displayAus && (t1 || t2)) {              // Display war aus: Taste weckt nur auf
    while (taste1() || taste2()) messen();
    delay(30);
    letzteTaste = millis();
    displaySparen();
    t1Start = t2Start = beideStart = 0;
    return;
  }
  if (suchenLaeuft() && (t1 || t2)) {         // Vario gefunden: Ton aus
    suchenBis = millis();
    tonAktualisieren();
    while (taste1() || taste2()) messen();
    delay(30);
    t1Start = t2Start = beideStart = 0;
    return;
  }
  if (frageAktiv() && (t1 || t2)) {            // Antwort auf die Startplatz-Frage
    while (taste1() || taste2()) messen();
    delay(30);
    frageBis = 0;
    if (t1 && !t2) {
      char b[32];
      snprintf(b, sizeof(b), "nn=%d;null", frageHoehe);
      befehlAusfuehren(b, true);
      antwort("S,1");
      kurzerPiep(1500, 80); delay(60); kurzerPiep(2000, 120);
    } else {
      antwort("S,0");
      kurzerPiep(700, 120);
    }
    t1Start = t2Start = beideStart = 0;
    aktivJetzt();
    return;
  }
  unsigned long jetzt = millis();
  if (t1 || t2) aktivJetzt();

  // beide zusammen
  if (t1 && t2) {
    if (beideStart == 0) beideStart = jetzt;
    t1Erledigt = t2Erledigt = true;           // Einzelfunktionen sperren
    if (!beideErledigt && jetzt - beideStart > HALTEN_NEU) {
      beideErledigt = true;
      neustart();
    }
    return;
  }
  beideStart = 0;
  if (!t1 && !t2) beideErledigt = false;

  // Taste 1
  // Einzelklick wird erst ausgefuehrt, wenn kein zweiter Klick folgt
  static unsigned long klickWartet = 0;    // Zeitpunkt des 1. Klicks, 0 = keiner
  if (t1) {
    if (t1Start == 0) t1Start = jetzt;
    if (!t1Erledigt && jetzt - t1Start > HALTEN_AUS) {
      t1Erledigt = true;
      klickWartet = 0;
      aktivJetzt();
      if (imFlug) {                            // im Flug nicht ausschalten
        meldung("Im Flug", "gesperrt", "Erst 2x T1", "(Flugende)");
        kurzerPiep(400, 60); delay(60); kurzerPiep(400, 60);
        delay(1500);
      } else if (bestaetigen("Aus-", "schalten?")) ausschalten();
    }
  } else {
    if (t1Start != 0 && !t1Erledigt && jetzt - t1Start > 30) {   // kurzer Klick
      if (klickWartet != 0 && jetzt - klickWartet <= DOPPELKLICK_MS) {
        klickWartet = 0;
        flugStoppen();                                 // Doppelklick
      } else {
        klickWartet = jetzt;                           // vielleicht kommt noch einer
      }
    }
    t1Start = 0;
    t1Erledigt = false;
    if (klickWartet != 0 && jetzt - klickWartet > DOPPELKLICK_MS) {
      klickWartet = 0;
      lautAendern(+1);                                 // war nur ein Einzelklick
    }
  }

  // Taste 2
  if (t2) {
    if (t2Start == 0) t2Start = jetzt;
    if (!t2Erledigt && jetzt - t2Start > HALTEN_HOEHE) {
      t2Erledigt = true;
      nullDruck = 0;
      kurzerPiep(1200, 60);
      delay(60);
      kurzerPiep(1200, 60);
    }
  } else {
    if (t2Start != 0 && !t2Erledigt && jetzt - t2Start > 30) lautAendern(-1);
    t2Start = 0;
    t2Erledigt = false;
  }
}

// =================================================================
// Start
// =================================================================
void setup() {
  pinMode(PIN_VERST, OUTPUT);
  digitalWrite(PIN_VERST, LOW);
  pinMode(PIN_TASTE1, INPUT_PULLUP);
  pinMode(PIN_TASTE2, INPUT_PULLUP);
  HwPWM3.setClockDiv(PWM_PRESCALER_PRESCALER_DIV_1);   // 16 MHz
  HwPWM3.addPin(PIN_TON);

  Serial.begin(115200);
  Wire.begin();
  anzeige.setI2CAddress(0x3C * 2);   // neues Display: Adresse fest auf 0x3C
  anzeige.begin();                   // Bus-Takt laesst U8g2 selbst (200 kHz)
  anzeige.setContrast(HELL);         // sonst bleibt der dunkle Startwert (0x2F) stehen
                                     // (nach dem Laden der Einstellungen gilt HELLIGKEIT)

  InternalFS.begin();
  kennungLaden();                               // ID, Name, Begruessung, PIN
  einstLaden();                                 // gespeicherte Handy-Einstellungen
  displayAusrichten();
  lautLaden();                                  // danach, sonst ueberschreibt der
                                                // alte Wert aus den Einstellungen sie
  akkuStarten();

  // Ohne USB (also mit Akku): nur einschalten, wenn Taste 1 gehalten
  // und bestaetigt wird. Sonst sofort wieder schlafen.
  // Nach einem Firmware-Update per Handy direkt eingeschaltet starten
  bool nachUpdate = InternalFS.exists("/nachupdate");
  if (nachUpdate) InternalFS.remove("/nachupdate");

  if (!usbSteckt() && !nachUpdate) {
    anzeige.setPowerSave(1);
    if (!einschaltAbfrage()) tiefschlaf();
  } else if (readResetReason() & POWER_RESETREAS_VBUS_Msk) {
    // war aus und wurde durch Einstecken des Kabels geweckt -> nur laden
    lademodus();
  }

  anzeige.setPowerSave(0);
  if (nachUpdate) {                    // kurz bestaetigen, dass das Update geklappt hat
    meldung("Update", "fertig!", "Version", FW_VERSION);
    kurzerPiep(1500, 80); delay(60); kurzerPiep(2000, 120);
    delay(2500);
  }
  startBild("Ruhig", "halten...");
  Serial.println("Startbild gezeigt");
  unsigned long startBildAb = millis();

  sensorOk = dps.begin_I2C(0x77, &Wire);
  if (sensorOk) {
    dps.configurePressure(DPS310_32HZ, DPS310_8SAMPLES);
    dps.configureTemperature(DPS310_2HZ, DPS310_1SAMPLE);
  }

  imuOk = imuStarten();
  kalmanReset();
  flugspeicherStarten();
  bluetoothStarten();

  // Begruessung mindestens 2,5 s zeigen, dann Selbsttest
  if (millis() - startBildAb < 2500) warteMessend(2500 - (millis() - startBildAb));
  selbsttest();

  kurzerPiep(1000, 120);
  delay(80);
  kurzerPiep(1500, 120);
}

// =================================================================
// Hauptschleife
// =================================================================
void gegenwindPruefen();
void sonnePruefen();
void doppeltippPruefen();
void restzeitMessen();

void loop() {
  messen();
  tonAktualisieren();
  tasterAbfragen();
  tonAktualisieren();
  verstaerkerPruefen();
  gModusPruefen();
  anzeigeWeiter();
  bluetoothArbeiten();
  logArbeiten();
  autoAusPruefen();
  akkuAktualisieren();
  akkuWarnen();
  gegenwindPruefen();
  sonnePruefen();
  doppeltippPruefen();
  restzeitMessen();
  if (speichernUm && (long)(millis() - speichernUm) >= 0) lautSpeichern();
  if (updateModusAb && (long)(millis() - updateModusAb) >= 0) updateModusStarten();
  if (frageBis && (!frageAktiv() || imFlug)) { frageBis = 0; antwort("S,2"); }   // keine Antwort / gestartet
  displaySparen();
  // Kurz schlafen: der Prozessor wartet stromsparend auf den naechsten Messwert
  // (Drucksensor 32/s, Lagesensor 104/s). Bei Ton-Test und Suche nicht.
  if (!suchenLaeuft()) delay(2);
}

int anzeigeTakt() { return imFlug ? ANZEIGE_MS : 100; }   // am Boden 10 Bilder pro Sekunde

// Display am Boden nach DISPLAY_AUS_MIN Minuten ohne Tastendruck aus; im Flug,
// bei Warnungen, Fragen oder Suche immer an.
void displaySparen() {
  bool aus = DISPLAY_AUS_MIN > 0 && !imFlug && !frageAktiv() && !suchenLaeuft() && !usbSteckt() &&
             !(warnBis && (long)(millis() - warnBis) < 0) &&
             millis() - letzteTaste > (unsigned long)DISPLAY_AUS_MIN * 60000UL;
  if (aus && !displayAus) { anzeige.setPowerSave(1); displayAus = true; }
  else if (!aus && displayAus) {
    displayAus = false;
    anzeige.setPowerSave(0);
    anzeige.setContrast(kontrast());
    naechsteZeile = 99;                          // gleich neues Bild
  }
}

// Firmware-Update per Bluetooth: Einstellungen sichern, dann in den Bootloader.
// Die App spielt danach die neue Firmware auf, das Vario startet von selbst neu.
void updateModusStarten() {
  updateModusAb = 0;
  tonAus();
  verstaerkerAus();
  if (einstSpeichernUm) einstSpeichern();
  if (speichernUm) lautSpeichern();
  meldung("Update", "per Handy", "ca. 3 Min.", "warten...");
  textSpeichern("/nachupdate", "1");   // nach dem Update gleich eingeschaltet starten
  delay(300);
  enterOTADfu();                 // startet den Bootloader im Bluetooth-Update-Modus
}

// =================================================================
// Gegenwind: im Flug mit Handy-GPS. Faellt das Tempo ueber Grund 3 s lang
// unter GEGENWIND_AB, warnt das Vario (Ton + Anzeige), bis es wieder
// 3 s lang mindestens 3 km/h darueber liegt.
// =================================================================
void gegenwindPruefen() {
  static unsigned long langsamSeit = 0, schnellSeit = 0;
  if (GEGENWIND_AB <= 0 || !imFlug || !gsDa()) {
    gegenwindAktiv = false;
    langsamSeit = schnellSeit = 0;
    return;
  }
  unsigned long jetzt = millis();
  if (gsKmh < GEGENWIND_AB) {
    schnellSeit = 0;
    if (!langsamSeit) langsamSeit = jetzt | 1;
    if (!gegenwindAktiv && jetzt - langsamSeit > 3000) {
      gegenwindAktiv = true;
      gegenwindSeit = jetzt;
    }
  } else if (gsKmh > GEGENWIND_AB + 3) {
    langsamSeit = 0;
    if (!schnellSeit) schnellSeit = jetzt | 1;
    if (gegenwindAktiv && jetzt - schnellSeit > 3000) gegenwindAktiv = false;
  }
}

// Hinweis-Kasten fuer 8 s mit zwei hellen Toenen
void hinweisZeigen(const char* titel, const char* text) {
  strncpy(warnTitel, titel, sizeof(warnTitel) - 1);
  warnTitel[sizeof(warnTitel) - 1] = 0;
  strncpy(warnText, text, sizeof(warnText) - 1);
  warnText[sizeof(warnText) - 1] = 0;
  warnBis = millis() + 8000;
  if (warnBis == 0) warnBis = 1;
  kurzerPiep(1500, 120); delay(80); kurzerPiep(1900, 160);
}

// Sonnenuntergang: Hinweis SONNE_VORHER Minuten vorher und beim Untergang
void sonnePruefen() {
  static unsigned long letzte = 0;
  static uint32_t fuer = 0;
  static uint8_t stufe = 0;
  if (millis() - letzte < 5000) return;
  letzte = millis();
  if (SONNE_VORHER <= 0 || sonneUnter == 0) return;
  uint32_t jetzt = jetztUnix();
  if (jetzt == 0) return;
  long rest = (long)sonneUnter - (long)jetzt;
  if (fuer != sonneUnter) {                 // neuer Wert vom Handy
    fuer = sonneUnter;
    stufe = (rest <= 0) ? 2 : 0;
  }
  if (stufe == 0 && rest > 0 && rest <= SONNE_VORHER * 60L) {
    stufe = 1;
    char t[16];
    snprintf(t, sizeof(t), "in %ld min", (rest + 59) / 60);
    hinweisZeigen("Sunset", t);
  } else if (stufe == 1 && rest <= 0) {
    stufe = 2;
    hinweisZeigen("Sunset", "jetzt");
  }
}

// Doppeltippen aufs Gehaeuse (Tipp-Erkennung im Bewegungssensor):
// am Boden stumm <-> Ton an; im Flug nur Ton an (nie aus Versehen stumm).
void tippEinrichten() {
  imu.writeRegister(0x58, DOPPELTIPP ? 0x8F : 0x00);   // TAP_CFG: x/y/z, gelatcht
  imu.writeRegister(0x59, 0x08);                       // Schwelle 8 x 0,25 g = 2 g (bei +-8 g)
  imu.writeRegister(0x5A, 0x25);                       // max. 0,6 s zwischen den Tipps, kurz und ruhig
  imu.writeRegister(0x5B, DOPPELTIPP ? 0x80 : 0x00);   // Doppeltipp an
}

void doppeltippPruefen() {
  static unsigned long letzte = 0, eingerichtetFuer = 0;
  static bool warAn = false;
  static int vorher = LAUT_START;
  if (!imuOk) return;
  if (eingerichtetFuer != imuStartMs || warAn != DOPPELTIPP) {
    tippEinrichten();
    eingerichtetFuer = imuStartMs;
    warAn = DOPPELTIPP;
  }
  if (!DOPPELTIPP || millis() - letzte < 40) return;
  letzte = millis();
  uint8_t quelle = 0;
  imu.readRegister(&quelle, 0x1C);                     // TAP_SRC (Lesen loescht)
  if (!(quelle & 0x10)) return;                        // kein Doppeltipp
  letzteTaste = millis();
  aktivJetzt();
  if (lautStufe > 0) {
    if (imFlug) return;                                // im Flug nie stumm schalten
    vorher = lautStufe;
    lautStufe = 0;
    aktFreq = 0;
    kurzerPiep(600, 80);
  } else {
    lautStufe = (vorher > 0) ? vorher : LAUT_START;
    aktFreq = 0;
    kurzerPiep(1200, 80);
  }
  speichernUm = millis() + 3000;
  if (speichernUm == 0) speichernUm = 1;
}

// Restlaufzeit: aus dem Abfall der Prozent seit dem Abziehen (mind. 20 min, 2 %)
void restzeitMessen() {
  static unsigned long letzte = 0, t0 = 0;
  static int p0 = -1;
  if (millis() - letzte < 10000) return;
  letzte = millis();
  if (usbSteckt() || akkuFehlt()) { p0 = -1; restMin = -1; return; }
  int p = akkuProzent();
  if (p0 < 0 || p > p0 + 2) { p0 = p; t0 = millis(); restMin = -1; return; }
  unsigned long dt = millis() - t0;
  if (dt < 20UL * 60000UL || p0 - p < 2) return;
  float proMin = (p0 - p) / (dt / 60000.0f);
  restMin = (int)(p / proMin);
}
