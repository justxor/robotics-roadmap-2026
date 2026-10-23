// 04. ESP32: веб-страница с живыми данными датчика и управлением сервоприводом
// Учит: Wi-Fi, HTTP-сервер, JSON, ШИМ 50 Гц для серво без сторонних библиотек.
// Arduino-ESP32 core 3.x.
//
// Подключение:
//   Серво SG90/MG90S: сигнал -> GPIO 18, + -> 5V (отдельный источник, если серво мощное), GND -> GND
//   Датчик: потенциометр/фоторезистор (делитель) -> GPIO 34 (АЦП)
// Открой Serial Monitor, узнай IP и зайди с телефона в той же сети.

#include <WiFi.h>
#include <WebServer.h>

const char* WIFI_SSID = "YOUR_WIFI";
const char* WIFI_PASS = "YOUR_PASSWORD";

const int SERVO_PIN = 18;
const int SENSOR_PIN = 34;

WebServer server(80);
int servoAngle = 90;

// 50 Гц, 16 бит: период 20 мс = 65536 отсчётов. Импульс 0.5–2.5 мс = 0–180°.
void servoWrite(int angle) {
  angle = constrain(angle, 0, 180);
  float pulseMs = 0.5f + angle / 180.0f * 2.0f;
  uint32_t duty = (uint32_t)(pulseMs / 20.0f * 65535.0f);
  ledcWrite(SERVO_PIN, duty);
  servoAngle = angle;
}

const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name=viewport content="width=device-width,initial-scale=1">
<title>ESP32 Robot</title>
<style>body{font-family:sans-serif;max-width:420px;margin:2em auto;padding:0 1em}
button{font-size:1.4em;padding:.4em 1em;margin:.2em}#v{font-size:2.5em}</style></head>
<body><h2>ESP32 Robot</h2>
<p>Датчик: <span id=v>—</span></p>
<p>Серво: <b id=a>90</b>°</p>
<input type=range min=0 max=180 value=90 id=s style="width:100%">
<p><button onclick="go(0)">0°</button><button onclick="go(90)">90°</button><button onclick="go(180)">180°</button></p>
<script>
const go=a=>fetch('/servo?angle='+a).then(r=>r.json()).then(d=>{a_.textContent=d.angle;s.value=d.angle});
const a_=document.getElementById('a');
s.oninput=e=>go(e.target.value);
setInterval(()=>fetch('/data').then(r=>r.json()).then(d=>{v.textContent=d.sensor}),200);
</script></body></html>
)HTML";

void handleRoot() { server.send_P(200, "text/html", PAGE); }

void handleData() {
  int raw = analogRead(SENSOR_PIN);
  server.send(200, "application/json", "{\"sensor\":" + String(raw) + ",\"angle\":" + String(servoAngle) + "}");
}

void handleServo() {
  if (server.hasArg("angle")) servoWrite(server.arg("angle").toInt());
  server.send(200, "application/json", "{\"angle\":" + String(servoAngle) + "}");
}

void setup() {
  Serial.begin(115200);
  ledcAttach(SERVO_PIN, 50, 16);
  servoWrite(90);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Подключаюсь к Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print("."); }
  Serial.print("\nОткрой в браузере: http://");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/servo", handleServo);
  server.begin();
}

void loop() {
  server.handleClient();
}
