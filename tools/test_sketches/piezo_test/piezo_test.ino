// Sweef Piezo-Test: Frequenz-Durchlauf, um die Resonanz der Piezoscheibe zu finden
// Ton an D2 (wie im Vario), Verstaerker-Freigabe an D3.
// Laeuft von 300 Hz bis 8000 Hz in 100-Hz-Schritten, je 0,4 s.
// Serieller Monitor (115200 Baud) zeigt die aktuelle Frequenz.
//   Taste 1 (D0): Anhalten / Weiterlaufen  -> bei der lautesten Stelle druecken
//   Taste 2 (D1): Von vorne anfangen

#include <Adafruit_TinyUSB.h>   // noetig fuer Serial beim XIAO nRF52840

const int PIN_TON    = D2;
const int PIN_VERST  = D3;
const int PIN_TASTE1 = D0;
const int PIN_TASTE2 = D1;

const int F_START = 300, F_ENDE = 8000, F_SCHRITT = 100, DAUER_MS = 400;

int  freq = F_START;
bool angehalten = false;
unsigned long letzterSchritt = 0;

void tonSetzen(int f) {
  uint32_t top = 16000000UL / (uint32_t)f;
  if (top > 32767) top = 32767;
  HwPWM3.setMaxValue(top);
  HwPWM3.writePin(PIN_TON, top / 2);        // 50 % = volle Lautstaerke
  Serial.print("Frequenz: ");
  Serial.print(f);
  Serial.println(angehalten ? " Hz  (ANGEHALTEN)" : " Hz");
}

bool gedrueckt(int pin) {
  if (digitalRead(pin) == LOW) {
    delay(30);
    if (digitalRead(pin) == LOW) {
      while (digitalRead(pin) == LOW) delay(5);
      return true;
    }
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_VERST, OUTPUT);
  digitalWrite(PIN_VERST, HIGH);            // Verstaerker an
  pinMode(PIN_TASTE1, INPUT_PULLUP);
  pinMode(PIN_TASTE2, INPUT_PULLUP);
  HwPWM3.setClockDiv(PWM_PRESCALER_PRESCALER_DIV_1);   // 16 MHz
  HwPWM3.addPin(PIN_TON);
  delay(1500);
  Serial.println("Piezo-Test startet");
  tonSetzen(freq);
  letzterSchritt = millis();
}

void loop() {
  if (gedrueckt(PIN_TASTE1)) { angehalten = !angehalten; tonSetzen(freq); }
  if (gedrueckt(PIN_TASTE2)) { freq = F_START; angehalten = false; tonSetzen(freq); letzterSchritt = millis(); }

  if (!angehalten && millis() - letzterSchritt >= DAUER_MS) {
    letzterSchritt = millis();
    freq += F_SCHRITT;
    if (freq > F_ENDE) freq = F_START;
    tonSetzen(freq);
  }
}
