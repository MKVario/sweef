// Sweef - Klopf-Test, Version 2 (Beispiel, noch nicht im Vario-Programm)
// -----------------------------------------------------------------
// Bedienung nur durch Klopfen:
// - Ein/Aus:      5x gleichmaessig klopfen      x . x . x . x . x
// - Lautstaerke:  2 Doppelklopfer hintereinander   xx . xx
//                 (naechste Stufe 1 -> 2 -> ... -> 6 -> 1)
// Das Display zeigt das zuletzt erkannte Muster, z.B. "xx xx" oder "x x x",
// und die Abstaende stehen im Seriellen Monitor. So kannst du einstellen.
//
// Neu in Version 2:
// - Ein Klopfer wird nicht mehr doppelt gezaehlt (Nachschwingen wird
//   ausgefiltert). Das war der Grund, warum 3x klopfen schon "5" ergab.
// - Es zaehlt jetzt der Rhythmus, nicht nur die Anzahl. Dadurch loest
//   Gerumpel viel seltener etwas aus.

#include <Wire.h>
#include <U8g2lib.h>
#include <LSM6DS3.h>

// ---------- Einstellungen ----------
const uint8_t KLOPF_SCHWELLE = 8;   // Empfindlichkeit 1..31 (x 62,5 mg)
                                    // kleiner = empfindlicher
const int  TOTZEIT_MS     = 145;    // so lange nach einem Klopfer nichts zaehlen
                                    // (filtert Nachschwingen)
const int  KURZ_MS        = 300;    // Abstand kuerzer = Doppelklopfer
const int  KLOPF_PAUSE_MS = 1000;   // laengere Pause = Muster ist zu Ende
const int  EIN_WARTEN_MS  = 5000;   // nach dem Aufwachen so lange auf Klopfer warten
const int  LAUT_STUFEN    = 6;
const int  LAUT_TABELLE[LAUT_STUFEN + 1] = {0, 15, 30, 60, 125, 250, 500};

// ---------- Anschluesse ----------
const int PIN_TON   = D2;
const int PIN_VERST = D3;
const int PIN_KLOPF = PIN_LSM6DS3TR_C_INT1;   // Meldeleitung vom Bewegungssensor

U8G2_SSD1306_128X64_NONAME_F_HW_I2C anzeige(U8G2_R1, U8X8_PIN_NONE);
LSM6DS3 imu(I2C_MODE, 0x6A);

int  lautStufe = 4;
bool istAn     = false;
char muster[16] = "-";      // zuletzt erkanntes Muster fuer die Anzeige

bool usbSteckt() {
  return (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
}

// ---------- Ton ----------
void ton(int freq, int ms) {
  uint32_t top = 16000000UL / (uint32_t)freq;
  HwPWM3.setMaxValue(top);
  HwPWM3.writePin(PIN_TON, top * LAUT_TABELLE[lautStufe] / 1000);
  delay(ms);
  HwPWM3.stop();
}

void tonfolge(int f1, int f2, int f3) {
  ton(f1, 90); delay(30); ton(f2, 90); delay(30); ton(f3, 140);
}

// ---------- Bewegungssensor: Klopf-Erkennung ----------
bool klopfenEinrichten() {
  imu.settings.gyroEnabled     = 0;
  imu.settings.accelRange      = 2;
  imu.settings.accelSampleRate = 416;
  if (imu.begin() != 0) return false;

  imu.writeRegister(LSM6DS3_ACC_GYRO_CTRL1_XL,    0x60); // 416 Hz, 2 g
  imu.writeRegister(LSM6DS3_ACC_GYRO_TAP_CFG1,    0x8F); // Klopfen X/Y/Z, Meldung halten
  imu.writeRegister(LSM6DS3_ACC_GYRO_TAP_THS_6D,  KLOPF_SCHWELLE & 0x1F);
  imu.writeRegister(LSM6DS3_ACC_GYRO_INT_DUR2,    0x0F); // laengste Stoss- und Ruhezeit
  imu.writeRegister(LSM6DS3_ACC_GYRO_WAKE_UP_THS, 0x00); // nur Einzel-Klopfer
  imu.writeRegister(LSM6DS3_ACC_GYRO_MD1_CFG,     0x40); // Einzel-Klopfer auf INT1
  pinMode(PIN_KLOPF, INPUT);
  uint8_t dummy;
  imu.readRegister(&dummy, LSM6DS3_ACC_GYRO_TAP_SRC);    // alte Meldung loeschen
  return true;
}

// true = neuer, echter Klopfer (Nachschwinger werden ignoriert)
bool klopfer() {
  static unsigned long letzterEcht = 0;
  if (digitalRead(PIN_KLOPF) == LOW) return false;
  uint8_t quelle = 0;
  imu.readRegister(&quelle, LSM6DS3_ACC_GYRO_TAP_SRC);   // liest und loescht
  if ((quelle & 0x20) == 0) return false;                // kein Einzel-Klopfer
  if (millis() - letzterEcht < TOTZEIT_MS) return false; // Nachschwingen
  letzterEcht = millis();
  return true;
}

// ---------- Muster auswerten ----------
const int NICHTS = 0, EIN_AUS = 1, LAUTER = 2;   // moegliche Aktionen

// zeiten: Zeitpunkte der Klopfer. ersterAbstandEgal = beim Aufwachen
// ist der Zeitpunkt des ersten Klopfers nicht genau bekannt.
int auswerten(unsigned long* zeiten, int n, bool ersterAbstandEgal) {
  // Muster fuer Anzeige und Seriellen Monitor bauen
  int p = 0;
  Serial.print("Muster: ");
  for (int i = 0; i < n && p < 14; i++) {
    if (i > 0) {
      unsigned long abstand = zeiten[i] - zeiten[i - 1];
      Serial.print(abstand); Serial.print("ms ");
      if (abstand >= KURZ_MS && p < 14) muster[p++] = ' ';
    }
    muster[p++] = 'x';
  }
  muster[p] = 0;
  Serial.print(" -> \""); Serial.print(muster); Serial.println("\"");

  bool kurz[10];
  for (int i = 1; i < n && i < 10; i++) kurz[i] = (zeiten[i] - zeiten[i - 1]) < (unsigned long)KURZ_MS;

  // Lautstaerke: xx xx  -> kurz, lang, kurz
  if (n == 4 && kurz[1] && !kurz[2] && kurz[3]) return LAUTER;

  // Ein/Aus: 5 gleichmaessige Klopfer -> alle Abstaende lang
  if (n == 5) {
    bool gleichmaessig = true;
    for (int i = 1; i < 5; i++) {
      if (i == 1 && ersterAbstandEgal) continue;
      if (kurz[i]) gleichmaessig = false;
    }
    if (gleichmaessig) return EIN_AUS;
  }
  return NICHTS;
}

// Sammelt Klopfer. Liefert eine Aktion, sobald ein Muster zu Ende ist.
int klopfMuster() {
  static unsigned long zeiten[10];
  static int anzahl = 0;
  if (klopfer()) {
    if (anzahl < 10) zeiten[anzahl++] = millis();
    if (istAn) ton(2500, 8);                 // kurzes "Tick"
  }
  if (anzahl > 0 && millis() - zeiten[anzahl - 1] > KLOPF_PAUSE_MS) {
    int a = auswerten(zeiten, anzahl, false);
    anzahl = 0;
    if (istAn) zeigen();
    return a;
  }
  return NICHTS;
}

// ---------- Anzeige ----------
void zeigen() {
  anzeige.clearBuffer();
  anzeige.setFont(u8g2_font_6x10_tf);
  anzeige.drawStr(2, 12, "Klopf-Test");
  anzeige.drawHLine(0, 16, 64);
  anzeige.drawStr(2, 32, "Muster:");
  anzeige.setFont(u8g2_font_9x15B_tf);
  anzeige.drawStr(2, 52, muster);
  anzeige.setFont(u8g2_font_6x10_tf);
  anzeige.drawStr(2, 76, "Lautstaerke");
  for (int i = 0; i < LAUT_STUFEN; i++) {
    int h = 4 + i * 2;
    if (i < lautStufe) anzeige.drawBox(2 + i * 9, 96 - h, 6, h);
    else               anzeige.drawFrame(2 + i * 9, 96 - h, 6, h);
  }
  anzeige.drawStr(2, 112, "xx xx=laut");
  anzeige.drawStr(2, 124, "5x = aus");
  anzeige.sendBuffer();
}

// ---------- Ein / Aus ----------
void einschalten() {
  istAn = true;
  digitalWrite(PIN_VERST, HIGH);
  anzeige.setPowerSave(0);
  tonfolge(800, 1200, 1800);                 // aufsteigend = an
  zeigen();
}

void tiefschlaf() {
  digitalWrite(PIN_VERST, LOW);
  anzeige.setPowerSave(1);
  delay(500);                                // Nachschwingen abwarten
  uint8_t dummy;
  imu.readRegister(&dummy, LSM6DS3_ACC_GYRO_TAP_SRC);
  systemOff(PIN_KLOPF, HIGH);                // Klopfer weckt wieder auf
}

void ausschalten() {
  tonfolge(1800, 1200, 800);                 // absteigend = aus
  istAn = false;
  if (!usbSteckt()) tiefschlaf();
  digitalWrite(PIN_VERST, LOW);              // mit USB: nur dunkel
  anzeige.setPowerSave(1);
}

// Nach dem Aufwachen: Der weckende Klopfer zaehlt als erster.
bool aufwachKlopfen() {
  unsigned long zeiten[10];
  int anzahl = 1;
  zeiten[0] = millis();
  unsigned long start = millis();
  while (millis() - start < EIN_WARTEN_MS) {
    if (klopfer() && anzahl < 10) zeiten[anzahl++] = millis();
    if (millis() - zeiten[anzahl - 1] > KLOPF_PAUSE_MS) break;   // Muster zu Ende
    delay(2);
  }
  return auswerten(zeiten, anzahl, true) == EIN_AUS;
}

// ---------- Start ----------
void setup() {
  pinMode(PIN_VERST, OUTPUT);
  digitalWrite(PIN_VERST, LOW);
  HwPWM3.setClockDiv(PWM_PRESCALER_PRESCALER_DIV_1);
  HwPWM3.addPin(PIN_TON);
  Serial.begin(115200);

  bool vomKlopfenGeweckt = (NRF_POWER->RESETREAS & POWER_RESETREAS_OFF_Msk) != 0;
  NRF_POWER->RESETREAS = 0xFFFFFFFF;

  Wire.begin();
  anzeige.begin();
  anzeige.setPowerSave(1);

  if (!klopfenEinrichten()) {
    anzeige.setPowerSave(0);
    anzeige.clearBuffer();
    anzeige.setFont(u8g2_font_6x10_tf);
    anzeige.drawStr(2, 60, "IMU fehlt");
    anzeige.sendBuffer();
    while (true) delay(1000);
  }

  if (usbSteckt()) { einschalten(); return; }                 // am Rechner: gleich an
  if (vomKlopfenGeweckt && aufwachKlopfen()) { einschalten(); return; }
  tiefschlaf();
}

// ---------- Hauptschleife ----------
void loop() {
  int a = klopfMuster();
  if (a == NICHTS) return;

  if (!istAn) {                              // nur mit USB moeglich
    if (a == EIN_AUS) einschalten();
    return;
  }
  if (a == EIN_AUS) {
    ausschalten();
  } else if (a == LAUTER) {
    lautStufe++;
    if (lautStufe > LAUT_STUFEN) lautStufe = 1;
    ton(1200, 100);
    zeigen();
  }
}
