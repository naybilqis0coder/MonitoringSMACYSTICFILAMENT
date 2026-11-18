/*
  Monitoring Extruder - ESP32-S3 N16R8
  Mengirim: suhu (MAX6675 + thermocouple K), RPM extruder, RPM puller

  Output data (format JSON, 1 baris, tiap 1 detik):
    {"temp":187.5,"rpm_ext":45.2,"rpm_pull":14.1}

  Dikirim lewat 3 jalur sekaligus:
    1) MQTT  -> dibaca dashboard di GitHub Pages (mode "MQTT")
    2) HTTP  -> http://<ip-esp32>/data   (mode "HTTP", jaringan lokal)
    3) Serial USB -> mode "Serial" (Web Serial, Chrome/Edge)

  Library yang perlu di-install (Library Manager): "PubSubClient" by Nick O'Leary
  Board: ESP32S3 Dev Module
    - PSRAM: "OPI PSRAM"  (N16R8 = 16MB flash, 8MB octal PSRAM)
    - Flash Size: 16MB
    - USB CDC On Boot: Enabled jika pakai port USB native (bukan port UART)

  ====== I/O MAPPING (PERKIRAAN - sesuaikan nanti) ======
  Pin yang SENGAJA dihindari di N16R8:
    GPIO26-32  : flash          GPIO35-37 : octal PSRAM
    GPIO19/20  : USB            GPIO0/3/45/46 : strapping
*/

#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <SPI.h>

// ---------- WiFi & MQTT ----------
const char* WIFI_SSID = "NAMA_WIFI";
const char* WIFI_PASS = "PASSWORD_WIFI";

const char* MQTT_HOST  = "broker.hivemq.com";   // broker publik (port 1883 untuk ESP32)
const uint16_t MQTT_PORT = 1883;
// Harus SAMA dengan "Topic" di Pengaturan dashboard. Ganti bagian unik!
const char* MQTT_TOPIC = "extruder-esp32s3/GANTI-NAMA-UNIK/data";

// ---------- I/O MAPPING (perkiraan) ----------
#define PIN_TC_SCK   12   // MAX6675 SCK
#define PIN_TC_CS    10   // MAX6675 CS
#define PIN_TC_SO    13   // MAX6675 SO (MISO)
#define PIN_RPM_EXT   4   // sensor/encoder extruder (pulsa)
#define PIN_RPM_PULL  5   // sensor/encoder puller   (pulsa)

// ---------- Parameter RPM ----------
// Jumlah pulsa per 1 putaran poros. Sesuaikan dgn sensor (proximity 1 target = 1,
// encoder 100 PPR = 100, dst). Jika ada gearbox & sensor di poros motor,
// kalikan/bagi sesuai rasio gear.
const float PPR_EXT  = 1.0f;
const float PPR_PULL = 1.0f;
const uint32_t DEBOUNCE_US = 200;      // abaikan pulsa lebih cepat dari ini
const uint32_t SAMPLE_MS   = 1000;     // interval hitung & kirim

// ---------- Internal ----------
WebServer server(80);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

volatile uint32_t cntExt = 0, cntPull = 0;
volatile uint32_t lastExtUs = 0, lastPullUs = 0;

void IRAM_ATTR isrExt() {
  uint32_t now = micros();
  if (now - lastExtUs > DEBOUNCE_US) { cntExt++; lastExtUs = now; }
}
void IRAM_ATTR isrPull() {
  uint32_t now = micros();
  if (now - lastPullUs > DEBOUNCE_US) { cntPull++; lastPullUs = now; }
}

float readThermocouple() {            // MAX6675, return NAN jika sensor putus
  digitalWrite(PIN_TC_CS, LOW);
  delayMicroseconds(10);
  uint16_t v = SPI.transfer16(0x0000);
  digitalWrite(PIN_TC_CS, HIGH);
  if (v & 0x0004) return NAN;         // bit D2 = thermocouple open
  return (v >> 3) * 0.25f;
}

char json[96];
void buildJson(float t, float e, float p) {
  if (isnan(t)) snprintf(json, sizeof(json), "{\"temp\":null,\"rpm_ext\":%.1f,\"rpm_pull\":%.1f}", e, p);
  else          snprintf(json, sizeof(json), "{\"temp\":%.1f,\"rpm_ext\":%.1f,\"rpm_pull\":%.1f}", t, e, p);
}

void handleData() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("// WiFi");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) { delay(300); Serial.print("."); }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) { Serial.print("// IP: "); Serial.println(WiFi.localIP()); }
}

void ensureMqtt() {
  static uint32_t lastTry = 0;
  if (mqtt.connected() || WiFi.status() != WL_CONNECTED) return;
  if (millis() - lastTry < 5000) return;
  lastTry = millis();
  String id = "esp32s3-extruder-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (mqtt.connect(id.c_str())) Serial.println("// MQTT connected");
  else Serial.println("// MQTT gagal, coba lagi...");
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_TC_CS, OUTPUT);
  digitalWrite(PIN_TC_CS, HIGH);
  SPI.begin(PIN_TC_SCK, PIN_TC_SO, -1, PIN_TC_CS);
  SPI.setFrequency(1000000);
  SPI.setDataMode(SPI_MODE0);

  pinMode(PIN_RPM_EXT,  INPUT_PULLUP);
  pinMode(PIN_RPM_PULL, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_RPM_EXT),  isrExt,  FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_RPM_PULL), isrPull, FALLING);

  connectWifi();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  server.on("/data", handleData);
  server.on("/", []() { server.send(200, "text/plain", "ESP32-S3 extruder: buka /data"); });
  server.begin();

  buildJson(NAN, 0, 0);
}

void loop() {
  server.handleClient();
  ensureMqtt();
  mqtt.loop();

  static uint32_t lastMs = millis();
  uint32_t now = millis();
  if (now - lastMs >= SAMPLE_MS) {
    float dt = (now - lastMs) / 1000.0f;
    lastMs = now;

    noInterrupts();
    uint32_t ce = cntExt, cp = cntPull;
    cntExt = 0; cntPull = 0;
    interrupts();

    float rpmExt  = (ce / PPR_EXT)  / dt * 60.0f;
    float rpmPull = (cp / PPR_PULL) / dt * 60.0f;
    float temp    = readThermocouple();

    buildJson(temp, rpmExt, rpmPull);
    Serial.println(json);                          // jalur Serial (Web Serial)
    if (mqtt.connected()) mqtt.publish(MQTT_TOPIC, json);   // jalur MQTT
    // jalur HTTP: dibaca client lewat /data (variabel json)
  }

  if (WiFi.status() != WL_CONNECTED) {            // reconnect WiFi sederhana
    static uint32_t lastW = 0;
    if (millis() - lastW > 10000) { lastW = millis(); WiFi.reconnect(); }
  }
}
