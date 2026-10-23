// 07. Прошивка «мост» для дифференциального робота: ESP32 + TB6612FNG + 2 мотора с энкодерами
//
// Зачем: это «нижний уровень» настоящего ROS 2-робота (schematics/05-ros2-rover-architecture.md).
// ESP32 держит PID скорости каждого колеса в реальном времени, а компьютер (ноутбук / Raspberry Pi)
// по USB-UART только говорит «какую скорость хочу» и читает энкодеры.
//
// Протокол — текстовый, в стиле ros_arduino_bridge (его понимает hardware interface
// diffdrive_arduino из серии Articulated Robotics). Каждая команда заканчивается '\r' или '\n':
//   e                 -> "<left_ticks> <right_ticks>"   абсолютные счётчики энкодеров
//   r                 -> "OK"                           сброс счётчиков
//   m <L> <R>         -> "OK"   целевая скорость колёс, тиков за кадр PID (кадр = 1/PID_RATE с)
//   o <L> <R>         -> "OK"   «сырой» ШИМ -1023..1023 без PID (для проверки моторов)
//   u <Kp>:<Kd>:<Ki>:<Ko> -> "OK" новые коэффициенты PID (как в ros_arduino_bridge)
//   b                 -> "<baud>"
//   v                 -> "<battery_mV>"                 напряжение батареи через делитель
// Если команды «m» не было AUTO_STOP_MS миллисекунд — моторы останавливаются (защита при обрыве связи).
//
// Проверка без ROS: examples/python/diffdrive_teleop.py (клавиатура -> робот, одометрия в терминале).
// Схема подключения: schematics/02-motor-driver.md

#include <Arduino.h>

// ---------- пины (как в schematics/02-motor-driver.md) ----------
const int PWMA = 25, AIN1 = 26, AIN2 = 33;   // левый мотор
const int PWMB = 14, BIN1 = 12, BIN2 = 13;   // правый мотор
const int STBY = 27;
const int ENC_L_A = 34, ENC_L_B = 35;
const int ENC_R_A = 36, ENC_R_B = 39;
const int VBAT_PIN = 32;                     // делитель 20k/10k от батареи (schematics/01-basics.md)

// ---------- настройки ----------
const long BAUD = 57600;
const int PWM_FREQ = 20000, PWM_RES = 10, PWM_MAX = 1023;
const int PID_RATE = 30;                     // Гц — как в ros_arduino_bridge
const unsigned long PID_INTERVAL_US = 1000000UL / PID_RATE;
const unsigned long AUTO_STOP_MS = 2000;
const bool INVERT_LEFT = false;              // поменяй, если колесо крутится не туда
const bool INVERT_RIGHT = true;              // правый мотор обычно стоит зеркально

// ---------- энкодеры ----------
volatile long encL = 0, encR = 0;

void IRAM_ATTR isrL() { encL += (digitalRead(ENC_L_A) == digitalRead(ENC_L_B)) ? -1 : 1; }
void IRAM_ATTR isrR() { encR += (digitalRead(ENC_R_A) == digitalRead(ENC_R_B)) ? -1 : 1; }

long readEnc(bool left) {
  noInterrupts();
  long v = left ? encL : encR;
  interrupts();
  long sign = left ? (INVERT_LEFT ? -1 : 1) : (INVERT_RIGHT ? -1 : 1);
  return v * sign;
}

void resetEnc() { noInterrupts(); encL = encR = 0; interrupts(); }

// ---------- моторы ----------
void setMotor(int pwmPin, int in1, int in2, int cmd, bool invert) {
  cmd = constrain(cmd, -PWM_MAX, PWM_MAX);
  if (invert) cmd = -cmd;
  digitalWrite(in1, cmd > 0);
  digitalWrite(in2, cmd < 0);
  ledcWrite(pwmPin, abs(cmd));
}

void setMotors(int left, int right) {
  setMotor(PWMA, AIN1, AIN2, left, INVERT_LEFT);
  setMotor(PWMB, BIN1, BIN2, right, INVERT_RIGHT);
}

// ---------- PID скорости (по мотивам ros_arduino_bridge) ----------
struct WheelPID {
  double target = 0;      // тиков за кадр
  long prevEnc = 0;
  int prevInput = 0;
  double iTerm = 0;
  long output = 0;
};

WheelPID pidL, pidR;
// Ko — делитель: output = (Kp*e - Kd*dInput + iTerm) / Ko. Целочисленные коэффициенты удобно слать по UART.
int Kp = 200, Kd = 100, Ki = 0, Ko = 50;   // стартовые значения для 10-бит ШИМ; подстрой командой u
bool moving = false;
bool pidEnabled = true;

void resetPID(WheelPID &p, long enc) {
  p.target = 0; p.prevEnc = enc; p.prevInput = 0; p.iTerm = 0; p.output = 0;
}

void doPID(WheelPID &p, long enc) {
  int input = enc - p.prevEnc;                 // фактическая скорость, тиков за кадр
  long error = (long)p.target - input;
  // D считаем по входу, а не по ошибке — нет «пинка» при смене цели
  long out = (Kp * error - Kd * (input - p.prevInput) + p.iTerm) / Ko;
  out += p.output;                             // скоростная (инкрементальная) форма
  if (out >= PWM_MAX) out = PWM_MAX;
  else if (out <= -PWM_MAX) out = -PWM_MAX;
  else p.iTerm += Ki * error;                  // anti-windup: интегрируем только вне насыщения
  p.output = out;
  p.prevEnc = enc;
  p.prevInput = input;
}

void updatePID() {
  long l = readEnc(true), r = readEnc(false);
  if (!moving) {
    if (pidL.prevInput != 0 || pidR.prevInput != 0) { resetPID(pidL, l); resetPID(pidR, r); }
    pidL.prevEnc = l; pidR.prevEnc = r;
    return;
  }
  doPID(pidL, l);
  doPID(pidR, r);
  setMotors(pidL.output, pidR.output);
}

// ---------- разбор команд ----------
char buf[48];
uint8_t len = 0;
unsigned long lastMotorCmd = 0;

int batteryMilliVolts() {
  // analogReadMilliVolts учитывает калибровку АЦП ESP32; ×3 — коэффициент делителя 20k/10k
  return analogReadMilliVolts(VBAT_PIN) * 3;
}

void runCommand(char *line) {
  char cmd = line[0];
  char *args = line + 1;
  long a = 0, b = 0;

  switch (cmd) {
    case 'e':
      Serial.print(readEnc(true)); Serial.print(' '); Serial.println(readEnc(false));
      break;
    case 'r':
      resetEnc(); resetPID(pidL, 0); resetPID(pidR, 0);
      Serial.println("OK");
      break;
    case 'm':
      sscanf(args, "%ld %ld", &a, &b);
      lastMotorCmd = millis();
      if (a == 0 && b == 0) { setMotors(0, 0); resetPID(pidL, readEnc(true)); resetPID(pidR, readEnc(false)); moving = false; }
      else moving = true;
      pidL.target = a; pidR.target = b;
      pidEnabled = true;
      Serial.println("OK");
      break;
    case 'o':
      sscanf(args, "%ld %ld", &a, &b);
      lastMotorCmd = millis();
      moving = false; pidEnabled = false;
      setMotors(a, b);
      Serial.println("OK");
      break;
    case 'u': {
      int p, d, i, o;
      if (sscanf(args, " %d:%d:%d:%d", &p, &d, &i, &o) == 4 && o != 0) {
        Kp = p; Kd = d; Ki = i; Ko = o;
        Serial.println("OK");
      } else Serial.println("Invalid PID");
      break;
    }
    case 'b': Serial.println(BAUD); break;
    case 'v': Serial.println(batteryMilliVolts()); break;
    default: Serial.println("Invalid Command");
  }
}

void readSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (len > 0) { buf[len] = 0; runCommand(buf); len = 0; }
    } else if (len < sizeof(buf) - 1) {
      buf[len++] = c;
    }
  }
}

// ---------- setup / loop ----------
unsigned long nextPID = 0;

void setup() {
  Serial.begin(BAUD);
  pinMode(AIN1, OUTPUT); pinMode(AIN2, OUTPUT);
  pinMode(BIN1, OUTPUT); pinMode(BIN2, OUTPUT);
  pinMode(STBY, OUTPUT); digitalWrite(STBY, HIGH);
  ledcAttach(PWMA, PWM_FREQ, PWM_RES);
  ledcAttach(PWMB, PWM_FREQ, PWM_RES);
  setMotors(0, 0);

  pinMode(ENC_L_A, INPUT); pinMode(ENC_L_B, INPUT);
  pinMode(ENC_R_A, INPUT); pinMode(ENC_R_B, INPUT);
  attachInterrupt(digitalPinToInterrupt(ENC_L_A), isrL, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_R_A), isrR, CHANGE);

  nextPID = micros();
}

void loop() {
  readSerial();

  if ((long)(micros() - nextPID) >= 0) {
    nextPID += PID_INTERVAL_US;
    if (pidEnabled) updatePID();
  }

  if (lastMotorCmd && millis() - lastMotorCmd > AUTO_STOP_MS) {   // связь пропала — стоп
    setMotors(0, 0);
    moving = false;
    pidL.target = pidR.target = 0;
    lastMotorCmd = 0;
  }
}
