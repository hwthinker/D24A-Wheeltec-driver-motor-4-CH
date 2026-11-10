/*
 * D24A (WHEELTEC, 2x TB6612FNG) - 4 motor + 4 encoder test
 * Board  : Arduino Mega 2560
 *
 * Wiring (cek ulang label silkscreen di board D24A kamu):
 *   D24A        Mega        Keterangan
 *   PWMA        D6  (OC4A)  PWM motor A
 *   AIN1/AIN2   D30 / D31   arah motor A
 *   PWMB        D7  (OC4B)
 *   BIN1/BIN2   D32 / D33
 *   PWMC        D8  (OC4C)
 *   CIN1/CIN2   D34 / D35
 *   PWMD        D44 (OC5C)
 *   DIN1/DIN2   D36 / D37
 *   STBY        D38         HIGH = aktif (atau jumper cap STBY<->3V3
 *                           yang sebaris di header kiri)
 *   E1A..E4A    D2, D3, D18, D19  (pin interrupt)
 *   E1B..E4B    D22, D23, D24, D25
 *   ADC         A0          tegangan baterai / 11 (header kanan)
 *   GND         GND         WAJIB common ground; header kiri tidak
 *                           punya GND -> ambil dari header kanan/atas
 *
 * Konektor motor 6P: [Out, GND, EnA, EnB, 5V, Out]
 *   A: AO1..AO2   B: BO2..BO1   C: CO1..CO2   D: DO2..DO1
 *
 * Catatan:
 * - PWM dinaikkan ke ~31 kHz (Timer4 & Timer5 prescaler 1) supaya motor
 *   tidak mendengung. analogWrite() tetap 0..255.
 * - Encoder dibaca x2 (CHANGE di fasa A). Kalau arah hitungan kebalik,
 *   balik nilai ENC_DIR[] atau tukar kabel motor.
 */

struct Motor {
  uint8_t pwm, in1, in2;
};

const Motor M[4] = {
  {6, 30, 31},   // A
  {7, 32, 33},   // B
  {8, 34, 35},   // C
  {44, 36, 37},  // D
};

const uint8_t PIN_STBY = 38;
const uint8_t ENC_A[4] = {2, 3, 18, 19};
const uint8_t ENC_B[4] = {22, 23, 24, 25};
const int8_t  ENC_DIR[4] = {1, 1, 1, 1};   // set -1 untuk membalik arah hitung
// Konektor B & D di D24A urutannya xO2..xO1 (kebalik dari A & C),
// jadi default arah motor B & D dibalik supaya "maju" sama semua.
// Sesuaikan lagi dengan cara pasang motor di chassis kamu.
const int8_t  MOTOR_DIR[4] = {1, -1, 1, -1};
const uint8_t PIN_VBAT = A0;

volatile long encCount[4] = {0, 0, 0, 0};

// ---------- Encoder ISR (x2 decoding) ----------
// Saat A berubah: A == B -> satu arah, A != B -> arah sebaliknya
#define ENC_ISR(n)                                              \
  void isrEnc##n() {                                            \
    bool a = digitalRead(ENC_A[n]);                             \
    bool b = digitalRead(ENC_B[n]);                             \
    encCount[n] += (a == b) ? ENC_DIR[n] : -ENC_DIR[n];         \
  }
ENC_ISR(0)
ENC_ISR(1)
ENC_ISR(2)
ENC_ISR(3)

// ---------- Motor control ----------
// speed: -255..255 ; 0 = coast (lepas)
void setMotor(uint8_t i, int speed) {
  speed = constrain(speed, -255, 255) * MOTOR_DIR[i];
  if (speed > 0) {
    digitalWrite(M[i].in1, HIGH);
    digitalWrite(M[i].in2, LOW);
  } else if (speed < 0) {
    digitalWrite(M[i].in1, LOW);
    digitalWrite(M[i].in2, HIGH);
  } else {
    digitalWrite(M[i].in1, LOW);
    digitalWrite(M[i].in2, LOW);
  }
  analogWrite(M[i].pwm, abs(speed));
}

// Short brake: IN1 = IN2 = HIGH
void brakeMotor(uint8_t i) {
  digitalWrite(M[i].in1, HIGH);
  digitalWrite(M[i].in2, HIGH);
  analogWrite(M[i].pwm, 255);
}

void setAll(int a, int b, int c, int d) {
  setMotor(0, a); setMotor(1, b); setMotor(2, c); setMotor(3, d);
}

long readEnc(uint8_t i) {
  noInterrupts();
  long v = encCount[i];
  interrupts();
  return v;
}

float readVbat() {
  // D24A: pembagi 10k + 1k -> Vadc = Vin / 11
  return analogRead(PIN_VBAT) * (5.0 / 1023.0) * 11.0;
}

void setup() {
  Serial.begin(115200);

  for (uint8_t i = 0; i < 4; i++) {
    pinMode(M[i].pwm, OUTPUT);
    pinMode(M[i].in1, OUTPUT);
    pinMode(M[i].in2, OUTPUT);
    pinMode(ENC_A[i], INPUT_PULLUP);
    pinMode(ENC_B[i], INPUT_PULLUP);
  }

  // PWM ~31.4 kHz: phase-correct 8-bit, prescaler 1 (16 MHz / 510)
  TCCR4B = (TCCR4B & 0xF8) | 0x01;   // D6, D7, D8
  TCCR5B = (TCCR5B & 0xF8) | 0x01;   // D44, D45, D46

  attachInterrupt(digitalPinToInterrupt(ENC_A[0]), isrEnc0, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_A[1]), isrEnc1, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_A[2]), isrEnc2, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_A[3]), isrEnc3, CHANGE);

  pinMode(PIN_STBY, OUTPUT);
  digitalWrite(PIN_STBY, HIGH);      // keluar dari standby

  Serial.println(F("D24A test start"));
}

void loop() {
  // Demo: maju pelan -> maju cepat -> stop -> mundur -> brake
  static const int seq[] = {80, 180, 0, -120, 999};   // 999 = brake
  static uint8_t step = 0;
  static unsigned long tStep = 0, tPrint = 0;
  static long last[4] = {0, 0, 0, 0};

  unsigned long now = millis();

  if (now - tStep >= 2000) {
    tStep = now;
    int s = seq[step];
    if (s == 999) {
      for (uint8_t i = 0; i < 4; i++) brakeMotor(i);
    } else {
      setAll(s, s, s, s);
    }
    Serial.print(F("cmd = ")); Serial.println(s);
    step = (step + 1) % (sizeof(seq) / sizeof(seq[0]));
  }

  // Cetak count & kecepatan (count per 100 ms) tiap 100 ms
  if (now - tPrint >= 100) {
    tPrint = now;
    for (uint8_t i = 0; i < 4; i++) {
      long c = readEnc(i);
      Serial.print('M'); Serial.print(char('A' + i));
      Serial.print(F(": ")); Serial.print(c);
      Serial.print(F(" ("));  Serial.print(c - last[i]);
      Serial.print(F(")  "));
      last[i] = c;
    }
    Serial.print(F("Vbat=")); Serial.print(readVbat(), 2);
    Serial.println(F("V"));
  }
}
