/*
 * D24A (WHEELTEC, 2x TB6612FNG) - 4 motor + 4 encoder test
 * Board  : ESP32 DevKit (ESP32 klasik), Arduino-ESP32 core 3.x
 * Library: ESP32Encoder (madhephaestus) - via Library Manager
 *
 * Wiring:
 *   D24A        GPIO
 *   STBY        4      (LOW saat boot -> motor aman)
 *   PWMA..PWMD  25, 26, 27, 13
 *   AIN1/AIN2   16 / 17
 *   BIN1/BIN2   18 / 19
 *   CIN1/CIN2   21 / 22
 *   DIN1/DIN2   23 / 2
 *   E1A/E1B     34 / 35   (input-only)
 *   E2A/E2B     36 / 39   (input-only)
 *   E3A/E3B     32 / 5
 *   E4A/E4B     14 / 15
 *   ADC         33        (ADC1, aman dipakai bareng WiFi)
 *   GND         GND       WAJIB common ground
 *
 * ================================================================
 *  !!! PERINGATAN !!!
 *  Encoder di D24A dicatu 5V -> sinyal E1A..E4B kemungkinan 0-5V.
 *  GPIO ESP32 TIDAK toleran 5V (maks 3.3V). Sambung langsung bisa
 *  MERUSAK ESP32 secara permanen.
 *  -> Ukur dulu level sinyal encoder.
 *  -> Kalau ~5V, WAJIB level shifter / pembagi resistor (10k/20k)
 *     di ke-8 jalur encoder. Lihat README bagian 6.3.
 *  -> Jangan sambungkan pin 5V D24A ke GPIO mana pun.
 * ================================================================
 *
 * Encoder di-decode x4 oleh hardware PCNT (tanpa ISR).
 */

#include <ESP32Encoder.h>

struct Motor {
  uint8_t pwm, in1, in2;
};

const Motor M[4] = {
  {25, 16, 17},  // A
  {26, 18, 19},  // B
  {27, 21, 22},  // C
  {13, 23, 2},   // D
};

const uint8_t PIN_STBY = 4;
const uint8_t ENC_A[4] = {34, 36, 32, 14};
const uint8_t ENC_B[4] = {35, 39, 5, 15};
const uint8_t PIN_VBAT = 33;

const uint32_t PWM_FREQ = 20000;  // 20 kHz, tidak terdengar
const uint8_t  PWM_BITS = 8;      // duty 0..255

// Konektor B & D di D24A urutannya xO2..xO1 (kebalik dari A & C)
const int8_t MOTOR_DIR[4] = {1, -1, 1, -1};
const int8_t ENC_DIR[4]   = {1, 1, 1, 1};

ESP32Encoder enc[4];

void setMotor(uint8_t i, int speed) {
  speed = constrain(speed, -255, 255) * MOTOR_DIR[i];
  if (speed > 0) {
    digitalWrite(M[i].in1, HIGH);
    digitalWrite(M[i].in2, LOW);
  } else if (speed < 0) {
    digitalWrite(M[i].in1, LOW);
    digitalWrite(M[i].in2, HIGH);
  } else {
    digitalWrite(M[i].in1, LOW);   // coast
    digitalWrite(M[i].in2, LOW);
  }
  ledcWrite(M[i].pwm, abs(speed));
}

void brakeMotor(uint8_t i) {
  digitalWrite(M[i].in1, HIGH);
  digitalWrite(M[i].in2, HIGH);
  ledcWrite(M[i].pwm, 255);
}

void setAll(int a, int b, int c, int d) {
  setMotor(0, a); setMotor(1, b); setMotor(2, c); setMotor(3, d);
}

long readEnc(uint8_t i) {
  return (long)enc[i].getCount() * ENC_DIR[i];
}

float readVbat() {
  // D24A: Vadc = Vin / 11 ; analogReadMilliVolts sudah terkalibrasi eFuse
  return analogReadMilliVolts(PIN_VBAT) * 11.0f / 1000.0f;
}

void setup() {
  Serial.begin(115200);

  // STBY LOW dulu selama inisialisasi
  pinMode(PIN_STBY, OUTPUT);
  digitalWrite(PIN_STBY, LOW);

  for (uint8_t i = 0; i < 4; i++) {
    pinMode(M[i].in1, OUTPUT);
    pinMode(M[i].in2, OUTPUT);
    digitalWrite(M[i].in1, LOW);
    digitalWrite(M[i].in2, LOW);
    ledcAttach(M[i].pwm, PWM_FREQ, PWM_BITS);
    ledcWrite(M[i].pwm, 0);
  }

  // Encoder: pull-up dari sisi encoder / level shifter, bukan internal
  ESP32Encoder::useInternalWeakPullResistors = puType::none;
  for (uint8_t i = 0; i < 4; i++) {
    enc[i].attachFullQuad(ENC_A[i], ENC_B[i]);  // x4 decoding di PCNT
    enc[i].setFilter(250);                      // filter glitch (~3 us)
    enc[i].clearCount();
  }

  analogSetPinAttenuation(PIN_VBAT, ADC_11db);

  digitalWrite(PIN_STBY, HIGH);   // driver aktif
  Serial.println("D24A ESP32 test start");
}

void loop() {
  // Demo: maju pelan -> maju cepat -> stop -> mundur -> brake
  static const int seq[] = {80, 180, 0, -120, 999};   // 999 = brake
  static uint8_t step = 0;
  static uint32_t tStep = 0, tPrint = 0;
  static long last[4] = {0, 0, 0, 0};

  uint32_t now = millis();

  if (now - tStep >= 2000) {
    tStep = now;
    int s = seq[step];
    if (s == 999) {
      for (uint8_t i = 0; i < 4; i++) brakeMotor(i);
    } else {
      setAll(s, s, s, s);
    }
    Serial.printf("cmd = %d\n", s);
    step = (step + 1) % (sizeof(seq) / sizeof(seq[0]));
  }

  if (now - tPrint >= 100) {
    tPrint = now;
    for (uint8_t i = 0; i < 4; i++) {
      long c = readEnc(i);
      Serial.printf("M%c: %ld (%ld)  ", 'A' + i, c, c - last[i]);
      last[i] = c;
    }
    Serial.printf("Vbat=%.2fV\n", readVbat());
  }
}
