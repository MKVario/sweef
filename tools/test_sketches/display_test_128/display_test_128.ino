// Sweef – Test fuer das 1,5" OLED 128x128 (SH1107, I2C)
// Anschluss wie das alte Display: VCC=3V3, GND, SCL=D5, SDA=D4
// Taste 1 (D0): naechste Einstellung probieren
// Taste 2 (D1): Helligkeit wechseln
//
// Es gibt drei uebliche Varianten dieses Displays. Richtig ist die,
// bei der der Rahmen auf allen vier Seiten vollstaendig zu sehen ist.

#include <Wire.h>
#include <U8g2lib.h>

const int PIN_T1 = D0;
const int PIN_T2 = D1;

U8G2_SH1107_128X128_F_HW_I2C          v0(U8G2_R0, U8X8_PIN_NONE);
U8G2_SH1107_SEEED_128X128_F_HW_I2C    v1(U8G2_R0, U8X8_PIN_NONE);
U8G2_SH1107_PIMORONI_128X128_F_HW_I2C v2(U8G2_R0, U8X8_PIN_NONE);
U8G2* anz[3] = { &v0, &v1, &v2 };
const char* name[3] = { "Standard", "Seeed", "Pimoroni" };

int variante = 0;
int hell = 2;
const uint8_t HELL[4] = { 0x10, 0x40, 0x80, 0xFF };
uint8_t adresse = 0;

uint8_t sucheAdresse() {
  const uint8_t liste[2] = { 0x3C, 0x3D };
  for (uint8_t a : liste) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) return a;
  }
  return 0;
}

void zeichnen() {
  U8G2* u = anz[variante];
  u->clearBuffer();
  u->drawFrame(0, 0, 128, 128);              // Rahmen: muss ganz sichtbar sein
  u->drawFrame(2, 2, 124, 124);
  u->setFont(u8g2_font_logisoso32_tn);
  u->drawStr(18, 52, "+1.8");
  u->setFont(u8g2_font_6x12_tf);
  u->drawStr(84, 52, "m/s");
  u->drawHLine(6, 60, 116);
  u->setFont(u8g2_font_7x14B_tf);
  u->drawStr(8, 78, "Hoehe   124 m");
  u->drawStr(8, 94, "Zeit    0:42");
  u->setFont(u8g2_font_5x8_tf);
  char z[40];
  snprintf(z, sizeof(z), "%s  0x%02X  Hell %d/4", name[variante], adresse, hell + 1);
  u->drawStr(8, 112, z);
  u->drawStr(8, 121, "T1 Variante  T2 Hell");
  u->sendBuffer();
}

void starten() {
  U8G2* u = anz[variante];
  if (adresse) u->setI2CAddress(adresse * 2);
  u->begin();
  u->setContrast(HELL[hell]);
  zeichnen();
}

void setup() {
  pinMode(PIN_T1, INPUT_PULLUP);
  pinMode(PIN_T2, INPUT_PULLUP);
  Serial.begin(115200);
  Wire.begin();
  delay(100);
  adresse = sucheAdresse();
  Serial.print("Display-Adresse: 0x");
  Serial.println(adresse, HEX);
  if (!adresse) Serial.println("Kein Display gefunden! Kabel pruefen.");
  starten();
}

void loop() {
  if (digitalRead(PIN_T1) == LOW) {
    variante = (variante + 1) % 3;
    Serial.print("Variante: "); Serial.println(name[variante]);
    starten();
    while (digitalRead(PIN_T1) == LOW) delay(10);
    delay(50);
  }
  if (digitalRead(PIN_T2) == LOW) {
    hell = (hell + 1) % 4;
    anz[variante]->setContrast(HELL[hell]);
    zeichnen();
    while (digitalRead(PIN_T2) == LOW) delay(10);
    delay(50);
  }
}
