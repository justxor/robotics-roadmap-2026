// 03. MPU-6050 + комплементарный фильтр — ESP32 / Arduino UNO
// Без внешних библиотек: читаем регистры по I2C напрямую (полезно уметь читать даташит!).
// Схема: SDA -> GPIO21 (UNO: A4), SCL -> GPIO22 (UNO: A5), VCC -> 3V3, GND -> GND, AD0 -> GND.
//
// Открой Serial Plotter (115200): три линии — угол по акселерометру (шумный),
// угол интегрированием гироскопа (уплывает), отфильтрованный угол (то, что нужно).

#include <Wire.h>

const uint8_t MPU = 0x68;
const float ALPHA = 0.98f;           // доверие гироскопу
const unsigned long DT_US = 5000;    // 200 Гц

float gyroBiasY = 0;
float angleGyro = 0, angleFused = 0;
unsigned long lastUs = 0;

void writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU);
  Wire.write(reg); Wire.write(val);
  Wire.endTransmission();
}

void readRaw(int16_t &ax, int16_t &ay, int16_t &az, int16_t &gx, int16_t &gy, int16_t &gz) {
  Wire.beginTransmission(MPU);
  Wire.write(0x3B);                  // ACCEL_XOUT_H
  Wire.endTransmission(false);
  Wire.requestFrom(MPU, (uint8_t)14);
  ax = Wire.read() << 8 | Wire.read();
  ay = Wire.read() << 8 | Wire.read();
  az = Wire.read() << 8 | Wire.read();
  Wire.read(); Wire.read();          // температура — пропускаем
  gx = Wire.read() << 8 | Wire.read();
  gy = Wire.read() << 8 | Wire.read();
  gz = Wire.read() << 8 | Wire.read();
}

float accelPitchDeg(int16_t ax, int16_t az) {
  return atan2((float)ax, (float)az) * 180.0f / PI;
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  Wire.setClock(400000);
  writeReg(0x6B, 0x00);   // PWR_MGMT_1: разбудить
  writeReg(0x1A, 0x03);   // CONFIG: DLPF ~44 Гц
  writeReg(0x1B, 0x00);   // GYRO_CONFIG: ±250 °/с -> 131 LSB/(°/с)
  writeReg(0x1C, 0x00);   // ACCEL_CONFIG: ±2g

  // Калибровка гироскопа: держи датчик неподвижно ~1 с
  long sum = 0;
  int16_t ax, ay, az, gx, gy, gz;
  for (int i = 0; i < 500; i++) { readRaw(ax, ay, az, gx, gy, gz); sum += gy; delay(2); }
  gyroBiasY = sum / 500.0f;

  readRaw(ax, ay, az, gx, gy, gz);
  angleGyro = angleFused = accelPitchDeg(ax, az);
  lastUs = micros();
}

void loop() {
  unsigned long now = micros();
  if (now - lastUs < DT_US) return;  // фиксированная частота без delay()
  float dt = (now - lastUs) * 1e-6f;
  lastUs = now;

  int16_t ax, ay, az, gx, gy, gz;
  readRaw(ax, ay, az, gx, gy, gz);

  float accAngle = accelPitchDeg(ax, az);
  // Знак зависит от ориентации платы: если углы расходятся в разные стороны — поставь минус.
  float gyroRate = -(gy - gyroBiasY) / 131.0f;   // °/с

  angleGyro += gyroRate * dt;                                   // уплывает
  angleFused = ALPHA * (angleFused + gyroRate * dt) + (1 - ALPHA) * accAngle;

  static uint8_t div = 0;
  if (++div >= 4) {                  // печатаем 50 раз в секунду
    div = 0;
    Serial.print("acc:");   Serial.print(accAngle);
    Serial.print(" gyro:"); Serial.print(angleGyro);
    Serial.print(" fused:"); Serial.println(angleFused);
  }
}
