// Tontest fuer das Soaring-Vario
// Verstaerker bleibt dauerhaft an. Spielt nacheinander 800 Hz, 1200 Hz und 1800 Hz
// fuer je 1 Sekunde, dann 1 Sekunde Pause.

const int PIN_TON   = D2;  // Tonsignal zum Verstaerker (A+)
const int PIN_VERST = D3;  // Verstaerker ein/aus (SD)

void setup() {
  pinMode(PIN_VERST, OUTPUT);
  digitalWrite(PIN_VERST, HIGH);  // Verstaerker einschalten
  delay(300);                     // Zeit zum Aufwachen
}

void loop() {
  tone(PIN_TON, 800);
  delay(1000);
  tone(PIN_TON, 1200);
  delay(1000);
  tone(PIN_TON, 1800);
  delay(1000);
  noTone(PIN_TON);
  delay(1000);
}
