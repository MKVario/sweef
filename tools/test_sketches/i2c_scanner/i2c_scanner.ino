// I2C-Scanner fuer das Soaring-Vario
// Sucht alle Geraete am I2C-Bus (SDA = D4, SCL = D5) und zeigt sie im Seriellen Monitor an.
// Erwartet: 0x3C = OLED-Display, 0x77 = DPS310 Drucksensor

#include <Wire.h>

void setup() {
  // Verstaerker sicher ausschalten (SD = D3 auf Low)
  pinMode(D3, OUTPUT);
  digitalWrite(D3, LOW);

  Serial.begin(115200);
  unsigned long start = millis();
  while (!Serial && millis() - start < 5000) {
    // bis zu 5 Sekunden auf den Seriellen Monitor warten
  }

  Wire.begin();
  Serial.println("I2C-Scanner gestartet");
}

void loop() {
  int gefunden = 0;
  Serial.println("Suche...");

  for (uint8_t adresse = 1; adresse < 127; adresse++) {
    Wire.beginTransmission(adresse);
    if (Wire.endTransmission() == 0) {
      Serial.print("Gefunden: 0x");
      if (adresse < 16) Serial.print("0");
      Serial.print(adresse, HEX);

      if (adresse == 0x3C || adresse == 0x3D) {
        Serial.print("  -> OLED-Display");
      } else if (adresse == 0x77 || adresse == 0x76) {
        Serial.print("  -> DPS310 Drucksensor");
      }
      Serial.println();
      gefunden++;
    }
  }

  if (gefunden == 0) {
    Serial.println("Keine Geraete gefunden - Verkabelung pruefen.");
  }
  Serial.println();
  delay(3000);  // alle 3 Sekunden neu suchen
}
