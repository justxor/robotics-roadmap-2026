// 01. Игра «Время реакции» — Arduino UNO / ESP32
// Учит: прерывания, антидребезг, неблокирующие таймеры через millis()/micros().
//
// Подключение (UNO):
//   LED  -> D8 через резистор 220 Ом -> GND
//   Кнопка -> D2 и GND (используем INPUT_PULLUP, D2 = вход прерывания INT0)
// Для ESP32: LED_PIN 2, BTN_PIN 0 (кнопка BOOT) — работает без доп. деталей.

const uint8_t LED_PIN = 8;
const uint8_t BTN_PIN = 2;

enum State { IDLE, WAITING, LIT, DONE };
volatile State state = IDLE;

volatile unsigned long pressMicros = 0;
volatile unsigned long lastIsrMicros = 0;
volatile bool pressed = false;

unsigned long waitUntil = 0;     // когда зажечь LED
unsigned long litAtMicros = 0;   // когда LED зажёгся
unsigned long doneAt = 0;
unsigned long best = 0xFFFFFFFF;

void onButton() {
  unsigned long now = micros();
  if (now - lastIsrMicros < 30000UL) return;  // антидребезг 30 мс
  lastIsrMicros = now;
  pressMicros = now;
  pressed = true;
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BTN_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BTN_PIN), onButton, FALLING);
  randomSeed(analogRead(A0));
  Serial.println(F("Нажми кнопку, чтобы начать раунд."));
}

void loop() {
  unsigned long now = millis();

  switch (state) {
    case IDLE:
      if (pressed) {
        pressed = false;
        waitUntil = now + random(1500, 5000);
        state = WAITING;
        Serial.println(F("Жди света..."));
      }
      break;

    case WAITING:
      if (pressed) {               // фальстарт
        pressed = false;
        Serial.println(F("Фальстарт! Нажми ещё раз для нового раунда."));
        state = IDLE;
      } else if ((long)(now - waitUntil) >= 0) {
        digitalWrite(LED_PIN, HIGH);
        litAtMicros = micros();
        state = LIT;
      }
      break;

    case LIT:
      if (pressed) {
        pressed = false;
        noInterrupts();
        unsigned long reaction = (pressMicros - litAtMicros) / 1000UL;
        interrupts();
        digitalWrite(LED_PIN, LOW);
        if (reaction < best) best = reaction;
        Serial.print(F("Реакция: ")); Serial.print(reaction);
        Serial.print(F(" мс | рекорд: ")); Serial.print(best); Serial.println(F(" мс"));
        doneAt = now;
        state = DONE;
      }
      break;

    case DONE:
      if (now - doneAt > 1000) {   // пауза, чтобы не начать раунд случайно
        pressed = false;
        Serial.println(F("Нажми кнопку для нового раунда."));
        state = IDLE;
      }
      break;
  }
}
