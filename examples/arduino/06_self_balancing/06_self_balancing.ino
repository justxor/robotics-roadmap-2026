// 06. Самобалансирующий робот — ESP32 + MPU-6050 + TB6612FNG
// Без внешних библиотек. Схемы: schematics/02-motor-driver.md, schematics/04-self-balancing.md
//
// Перед запуском:
//   1. Залей 03_imu_complementary, найди угол, при котором робот стоит в равновесии -> TARGET_ANGLE.
//   2. Проверь знак: наклони робота вперёд — колёса должны крутиться ВПЕРЁД (догонять падение).
//      Если наоборот — поставь MOTOR_DIR = -1.
//   3. Настраивай через Serial: "p 40", "i 200", "d 1.5", "t -1.2" (целевой угол).
// Безопасность: при наклоне > 35° моторы отключаются.

#include <Wire.h>

// --- моторы ---
const int PWMA = 25, AIN1 = 26, AIN2 = 33;
const int PWMB = 14, BIN1 = 12, BIN2 = 13;
const int STBY = 27;
const int PWM_MAX = 1023;
int MOTOR_DIR = 1;

// --- IMU ---
const uint8_t MPU = 0x68;
const float ALPHA = 0.98f;
float gyroBias = 0;
float angle = 0;

// --- PID ---
float Kp = 40.0f, Ki = 200.0f, Kd = 1.5f;
float TARGET_ANGLE = 0.0f;
float integral = 0;
const float FALL_ANGLE = 35.0f;
const int DEADBAND = 60;            // компенсация трения покоя моторов

const unsigned long DT_US = 5000;   // 200 Гц
unsigned long lastUs = 0;

void writeReg(uint8_t r, uint8_t v) { Wire.beginTransmission(MPU); Wire.write(r); Wire.write(v); Wire.endTransmission(); }

void readIMU(float &accAngle, float &gyroRate) {
  Wire.beginTransmission(MPU); Wire.write(0x3B); Wire.endTransmission(false);
  Wire.requestFrom(MPU, (uint8_t)14);
  int16_t ax = Wire.read() << 8 | Wire.read();
  int16_t ay = Wire.read() << 8 | Wire.read(); (void)ay;
  int16_t az = Wire.read() << 8 | Wire.read();
  Wire.read(); Wire.read();
  int16_t gx = Wire.read() << 8 | Wire.read(); (void)gx;
  int16_t gy = Wire.read() << 8 | Wire.read();
  Wire.read(); Wire.read();
  accAngle = atan2((float)ax, (float)az) * 180.0f / PI;
  gyroRate = -(gy - gyroBias) / 131.0f;
}

void motor(int pwm, int in1, int in2, int s) {
  s = constrain(s, -PWM_MAX, PWM_MAX);
  digitalWrite(in1, s > 0); digitalWrite(in2, s < 0);
  ledcWrite(pwm, abs(s));
}

void drive(int s) {
  s *= MOTOR_DIR;
  if (s > 0) s += DEADBAND; else if (s < 0) s -= DEADBAND;
  motor(PWMA, AIN1, AIN2, s);
  motor(PWMB, BIN1, BIN2, s);
}

void handleSerial() {
  if (!Serial.available()) return;
  char c = Serial.read(); float v = Serial.parseFloat();
  if (c == 'p') Kp = v; else if (c == 'i') { Ki = v; integral = 0; }
  else if (c == 'd') Kd = v; else if (c == 't') TARGET_ANGLE = v; else return;
  Serial.printf("Kp=%.2f Ki=%.2f Kd=%.3f target=%.2f\n", Kp, Ki, Kd, TARGET_ANGLE);
}

void setup() {
  Serial.begin(115200);
  pinMode(AIN1, OUTPUT); pinMode(AIN2, OUTPUT); pinMode(BIN1, OUTPUT); pinMode(BIN2, OUTPUT);
  pinMode(STBY, OUTPUT); digitalWrite(STBY, HIGH);
  ledcAttach(PWMA, 20000, 10);
  ledcAttach(PWMB, 20000, 10);
  drive(0);

  Wire.begin(); Wire.setClock(400000);
  writeReg(0x6B, 0x00); writeReg(0x1A, 0x03); writeReg(0x1B, 0x00); writeReg(0x1C, 0x00);

  Serial.println("Калибровка гироскопа — не двигай робота...");
  long sum = 0;
  for (int i = 0; i < 500; i++) {
    Wire.beginTransmission(MPU); Wire.write(0x45); Wire.endTransmission(false);
    Wire.requestFrom(MPU, (uint8_t)2);
    sum += (int16_t)(Wire.read() << 8 | Wire.read());
    delay(2);
  }
  gyroBias = sum / 500.0f;
  float a, g; readIMU(a, g); angle = a;
  lastUs = micros();
  Serial.println("Готово");
}

void loop() {
  handleSerial();
  unsigned long now = micros();
  if (now - lastUs < DT_US) return;
  float dt = (now - lastUs) * 1e-6f;
  lastUs = now;

  float accAngle, gyroRate;
  readIMU(accAngle, gyroRate);
  angle = ALPHA * (angle + gyroRate * dt) + (1 - ALPHA) * accAngle;

  if (fabs(angle - TARGET_ANGLE) > FALL_ANGLE) {   // упал — выключаемся
    drive(0); integral = 0;
    return;
  }

  float error = angle - TARGET_ANGLE;
  integral = constrain(integral + error * dt, -5.0f, 5.0f);
  // D-член берём прямо из гироскопа: это производная угла без шума дифференцирования
  float u = Kp * error + Ki * integral + Kd * gyroRate;
  drive((int)u);

  static uint8_t k = 0;
  if (++k >= 20) { k = 0; Serial.printf("angle:%.2f u:%.0f\n", angle, u); }
}
