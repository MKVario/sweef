// Sweef – Helligkeitstest fuer das 1,5" OLED (SH1107, Variante Seeed)
// Anschluss wie gehabt: VCC=3V3, GND, SCL=D5, SDA=D4
// Taste 1 (D0): naechste Stufe  ·  Taste 2 (D1): Bild wechseln
//
// Stufen (alle mit Kontrast 255 = Maximum der normalen Einstellung):
//  1 Normal        = so wie in Sweef 2_10
//  2 Vorladen      = Pixel werden laenger vorgeladen
//  3 VCOMH         = hoehere Ansteuerspannung
//  4 Beides
//  5 Beides + Takt = zusaetzlich schnellerer Bildtakt

#include <Wire.h>
#include <U8g2lib.h>

const int PIN_T1 = D0;
const int PIN_T2 = D1;
U8G2_SH1107_SEEED_128X128_F_HW_I2C anz(U8G2_R0, U8X8_PIN_NONE);

struct Stufe { const char* name; uint8_t d9, db, d5; };
const Stufe STUFE[5] = {
  {"1 Normal",       0x22, 0x35, 0x50},
  {"2 Vorladen",     0x2F, 0x35, 0x50},   // Vorladen 15 statt 2 Takte
  {"3 VCOMH",        0x22, 0x3F, 0x50},   // VCOMH 0,83 statt 0,77 x VREF
  {"4 Beides",       0x2F, 0x3F, 0x50},
  {"5 Beides+Takt",  0x2F, 0x3F, 0xF0},
};
int stufe = 0, bild = 0;

uint8_t adr = 0x3C;

// Befehl direkt per I2C an den Display-Chip (ohne Umweg ueber U8g2)
void befehl(uint8_t c, uint8_t wert) {
  Wire.beginTransmission(adr);
  Wire.write(0x00);          // 0x00 = es folgen Befehle
  Wire.write(c);
  Wire.write(wert);
  Wire.endTransmission();
}

void anwenden() {
  const Stufe& s = STUFE[stufe];
  anz.setContrast(255);
  befehl(0xD9, s.d9);        // Ent-/Vorladezeit
  befehl(0xDB, s.db);        // VCOMH
  befehl(0xD5, s.d5);        // Takt
}

void zeichnen() {
  anz.clearBuffer();
  if (bild == 0) {                       // Vario-Bild
    anz.setFont(u8g2_font_logisoso38_tn);
    anz.drawStr(14, 50, "+1.8");
    anz.setFont(u8g2_font_7x14B_tf);
    anz.drawStr(98, 50, "m/s");
    anz.drawHLine(0, 62, 128);
    anz.drawUTF8(2, 82, "Höhe");
    anz.setFont(u8g2_font_logisoso20_tn);
    anz.drawStr(70, 88, "124");
  } else {                                // Flaechen: weiss / Streifen
    anz.drawBox(0, 0, 128, 44);
    for (int x = 0; x < 128; x += 4) anz.drawVLine(x, 46, 44);
  }
  anz.setDrawColor(1);
  anz.drawBox(0, 100, 128, 28);
  anz.setDrawColor(0);
  anz.setFont(u8g2_font_9x15B_tf);
  anz.drawStr(4, 119, STUFE[stufe].name);
  anz.setDrawColor(1);
  anz.sendBuffer();
}

void setup() {
  pinMode(PIN_T1, INPUT_PULLUP);
  pinMode(PIN_T2, INPUT_PULLUP);
  Serial.begin(115200);
  Wire.begin();
  delay(200);
  adr = 0;                               // Display suchen (wie im ersten Test)
  for (uint8_t a = 0x3C; a <= 0x3D; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { adr = a; break; }
  }
  Serial.print("Display-Adresse: 0x"); Serial.println(adr, HEX);
  if (!adr) adr = 0x3C;
  anz.setI2CAddress(adr * 2);
  anz.begin();
  anz.setContrast(255);
  zeichnen();                            // Stufe 1 = so wie nach dem Start
}

void loop() {
  if (digitalRead(PIN_T1) == LOW) {
    stufe = (stufe + 1) % 5;
    anwenden(); zeichnen();
    Serial.print("Stufe "); Serial.println(STUFE[stufe].name);
    while (digitalRead(PIN_T1) == LOW) delay(10);
    delay(50);
  }
  if (digitalRead(PIN_T2) == LOW) {
    bild = 1 - bild; zeichnen();
    while (digitalRead(PIN_T2) == LOW) delay(10);
    delay(50);
  }
}
