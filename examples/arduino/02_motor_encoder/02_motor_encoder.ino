// 02. Мотор + энкодер: «ровно один оборот колеса» — ESP32 (Arduino-ESP32 core 3.x)
// Учит: ШИМ через LEDC, драйвер TB6612FNG, чтение квадратурного энкодера в прерывании,
//       первый замкнутый контур (P-регулятор по позиции).
// Схема: schematics/02-motor-driver.md (используется только мотор A и левый энкодер).
//
// Команды в Serial Monitor (115200):
//   r  — повернуть колесо на 1 оборот вперёд
//   b  — на 1 оборот назад
//   s  — тест скоростей (5 уровней ШИМ вперёд/назад, без обратной связи)

const int PWMA = 25, AIN1 = 26, AIN2 = 33, STBY = 27;
const int ENC_A = 34, ENC_B = 35;

// Пример: Pololu 25D 34:1, энкодер 48 CPR (48 — при счёте обоих фронтов ОБОИХ каналов, x4).
// Мы считаем только фронты канала A (x2) -> 24 * 34.014 ≈ 816 тиков на оборот выходного вала.
// ЗАМЕНИ на значение для своего мотора или измерь: обнули счётчик, поверни колесо рукой ровно на 1 оборот.
const long TICKS_PER_REV = 816;

const int PWM_FREQ = 20000;   // 20 кГц — мотор не пищит
const int PWM_RES = 10;       // 0..1023
const int PWM_MAX = (1 << PWM_RES) - 1;

volatile long ticks = 0;

void IRAM_ATTR onEncA() {
  // Направление по состоянию канала B в момент фронта A (x2 декодирование)
  bool a = digitalRead(ENC_A);
  bool b = digitalRead(ENC_B);
  ticks += (a == b) ? -1 : 1;
}

long readTicks() {
  noInterrupts();
  long t = ticks;
  interrupts();
  return t;
}

void setMotor(int cmd) {      // cmd: -PWM_MAX..PWM_MAX
  cmd = constrain(cmd, -PWM_MAX, PWM_MAX);
  digitalWrite(AIN1, cmd > 0);
  digitalWrite(AIN2, cmd < 0);
  ledcWrite(PWMA, abs(cmd));
}

void speedTest() {
  int levels[] = {200, 400, 600, 800, 1023};
  for (int dir = 1; dir >= -1; dir -= 2) {
    for (int lv : levels) {
      long t0 = readTicks();
      setMotor(dir * lv);
      delay(1000);            // в тесте можно — это не цикл управления
      long dt = readTicks() - t0;
      Serial.printf("PWM %5d -> %.1f об/мин\n", dir * lv, dt * 60.0 / TICKS_PER_REV);
    }
  }
  setMotor(0);
}

// P-регулятор позиции с ограничением ШИМ и «мёртвой зоной».
void moveTicks(long delta) {
  const float Kp = 2.0;       // подбери: больше = быстрее, но перерегулирование
  const int MIN_PWM = 180;    // ниже этого мотор не трогается (статическое трение)
  const int TOL = 5;          // допуск, тиков
  long target = readTicks() + delta;
  unsigned long last = 0, start = millis(), settled = 0;

  while (millis() - start < 5000) {
    if (millis() - last < 5) continue;  // цикл 200 Гц
    last = millis();
    long err = target - readTicks();
    if (labs(err) <= TOL) {
      setMotor(0);
      if (settled == 0) settled = millis();
      if (millis() - settled > 200) break;  // стоим в допуске 200 мс
      continue;
    }
    settled = 0;
    int u = (int)(Kp * err);
    if (u > 0 && u < MIN_PWM) u = MIN_PWM;
    if (u < 0 && u > -MIN_PWM) u = -MIN_PWM;
    setMotor(u);
  }
  setMotor(0);
  Serial.printf("Цель %ld, факт %ld, ошибка %ld тиков\n", target, readTicks(), target - readTicks());
}

void setup() {
  Serial.begin(115200);
  pinMode(AIN1, OUTPUT); pinMode(AIN2, OUTPUT); pinMode(STBY, OUTPUT);
  digitalWrite(STBY, HIGH);
  ledcAttach(PWMA, PWM_FREQ, PWM_RES);   // core 3.x API
  pinMode(ENC_A, INPUT); pinMode(ENC_B, INPUT);
  attachInterrupt(digitalPinToInterrupt(ENC_A), onEncA, CHANGE);
  Serial.println("Команды: r = +1 оборот, b = -1 оборот, s = тест скоростей");
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'r') moveTicks(TICKS_PER_REV);
    if (c == 'b') moveTicks(-TICKS_PER_REV);
    if (c == 's') speedTest();
  }
}
