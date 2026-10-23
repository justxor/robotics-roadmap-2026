// 05. Робот, едущий по линии, на PID — ESP32 + TB6612FNG + Pololu QTR-8RC
// Библиотека: "QTRSensors" by Pololu (Library Manager).
// Схемы: schematics/02-motor-driver.md, schematics/03-line-follower.md
//
// Порядок работы:
//   1. Включи робота, поставь датчики над линией. 5 секунд калибровки — водить робота
//      рукой поперёк линии туда-сюда (встроенный светодиод горит).
//   2. Поставь на трассу. Робот поедет через 2 секунды.
//   3. Коэффициенты можно менять «на лету» через Serial: "p 0.08", "d 0.6", "i 0", "v 500".

#include <QTRSensors.h>

// --- моторы (TB6612FNG) ---
const int PWMA = 25, AIN1 = 26, AIN2 = 33;   // левый
const int PWMB = 14, BIN1 = 12, BIN2 = 13;   // правый
const int STBY = 27;
const int PWM_FREQ = 20000, PWM_RES = 10, PWM_MAX = 1023;

// --- датчики ---
const uint8_t SENSOR_COUNT = 8;
const uint8_t SENSOR_PINS[SENSOR_COUNT] = {15, 2, 4, 16, 17, 5, 18, 19};
const uint8_t EMITTER_PIN = 23;
QTRSensors qtr;
uint16_t values[SENSOR_COUNT];

// --- PID ---
float Kp = 0.08f;   // начни с Kd = Ki = 0 и подбирай Kp
float Ki = 0.0f;
float Kd = 0.6f;
int baseSpeed = 450;      // из 1023
const uint16_t CENTER = (SENSOR_COUNT - 1) * 1000 / 2;  // 3500

float integral = 0;
int lastError = 0;
unsigned long lastLoop = 0;
const unsigned long LOOP_MS = 2;   // 500 Гц

void motor(int pwmPin, int in1, int in2, int speed) {
  speed = constrain(speed, -PWM_MAX, PWM_MAX);
  digitalWrite(in1, speed >= 0);
  digitalWrite(in2, speed < 0);
  ledcWrite(pwmPin, abs(speed));
}

void drive(int left, int right) {
  motor(PWMA, AIN1, AIN2, left);
  motor(PWMB, BIN1, BIN2, right);
}

void handleSerial() {
  if (!Serial.available()) return;
  char c = Serial.read();
  float v = Serial.parseFloat();
  switch (c) {
    case 'p': Kp = v; break;
    case 'i': Ki = v; integral = 0; break;
    case 'd': Kd = v; break;
    case 'v': baseSpeed = (int)v; break;
    default: return;
  }
  Serial.printf("Kp=%.3f Ki=%.4f Kd=%.3f base=%d\n", Kp, Ki, Kd, baseSpeed);
}

void setup() {
  Serial.begin(115200);
  pinMode(AIN1, OUTPUT); pinMode(AIN2, OUTPUT);
  pinMode(BIN1, OUTPUT); pinMode(BIN2, OUTPUT);
  pinMode(STBY, OUTPUT); digitalWrite(STBY, HIGH);
  ledcAttach(PWMA, PWM_FREQ, PWM_RES);
  ledcAttach(PWMB, PWM_FREQ, PWM_RES);
  drive(0, 0);

  qtr.setTypeRC();
  qtr.setSensorPins(SENSOR_PINS, SENSOR_COUNT);
  qtr.setEmitterPin(EMITTER_PIN);

  Serial.println("Калибровка 5 с: води датчиками поперёк линии");
  unsigned long t0 = millis();
  while (millis() - t0 < 5000) qtr.calibrate();
  Serial.println("Готово. Старт через 2 с");
  delay(2000);
  lastLoop = millis();
}

void loop() {
  handleSerial();
  unsigned long now = millis();
  if (now - lastLoop < LOOP_MS) return;
  float dt = (now - lastLoop) / 1000.0f;
  lastLoop = now;

  uint16_t position = qtr.readLineBlack(values);  // 0..7000 (для белой линии на чёрном — readLineWhite)
  int error = (int)position - CENTER;

  integral += error * dt;
  integral = constrain(integral, -2000, 2000);     // anti-windup
  float derivative = (error - lastError) / dt;
  lastError = error;

  float u = Kp * error + Ki * integral + Kd * derivative * 0.01f;  // 0.01 — масштаб, чтобы Kd был «человеческим»

  int left = baseSpeed + (int)u;
  int right = baseSpeed - (int)u;
  // если линия «потеряна» (крайние значения), крутимся в сторону, где её видели последний раз
  if (position == 0 || position == (SENSOR_COUNT - 1) * 1000) {
    left = (error > 0) ? baseSpeed : -baseSpeed / 2;
    right = (error > 0) ? -baseSpeed / 2 : baseSpeed;
  }
  drive(left, right);
}
