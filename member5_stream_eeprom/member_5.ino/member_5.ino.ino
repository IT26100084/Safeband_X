/************************************************************
 *  Safeband_X – Member 5
 *  MJPEG Live Stream + JPEG Snapshot + EEPROM Logging
 ************************************************************/

#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>

/* ------------------ WIFI ------------------ */
const char* ssid     = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";

/* ------------------ SERVER ---------------- */
WebServer server(80);

/* ------------------ EEPROM ---------------- */
#define EEPROM_SIZE      512
#define LOG_COUNT_ADDR   0
#define LOG_START_ADDR   2
#define LOG_ENTRY_SIZE   25   // EXACT as proposal

/* ------------------ SOS ------------------- */
volatile bool sos_active = false;

/* ------------------ SNAPSHOT -------------- */
camera_fb_t* snapshot_fb = NULL;

/* ------------------ TRIGGER SOURCE -------- */
enum TriggerSource {
  BUTTON = 0,
  IMPACT = 1
};

/* ------------ EEPROM STRUCTURE ------------ */
struct LogEntry {
  uint8_t trigger;       // 1 byte
  uint32_t timestamp;    // 4 bytes
  float latitude;        // 4 bytes
  float longitude;       // 4 bytes
  char filename[12];     // 12 bytes (8.3)
};

/* ============ CAMERA INIT ================= */
void initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = 5;
  config.pin_d1       = 18;
  config.pin_d2       = 19;
  config.pin_d3       = 21;
  config.pin_d4       = 36;
  config.pin_d5       = 39;
  config.pin_d6       = 34;
  config.pin_d7       = 35;
  config.pin_xclk     = 0;
  config.pin_pclk     = 22;
  config.pin_vsync    = 25;
  config.pin_href     = 23;
  config.pin_sccb_sda = 26;
  config.pin_sccb_scl = 27;
  config.pin_pwdn     = 32;
  config.pin_reset    = -1;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  config.frame_size   = FRAMESIZE_VGA;
  config.jpeg_quality = 12;
  config.fb_count     = 2;

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("❌ Camera Init Failed");
    while (1);
  }
}

/* ============ MJPEG STREAM ================= */
void handleStream() {
  WiFiClient client = server.client();
  String boundary = "frame";

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: multipart/x-mixed-replace; boundary=" + boundary);
  client.println();

  while (client.connected()) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) continue;

    client.println("--" + boundary);
    client.println("Content-Type: image/jpeg");
    client.println("Content-Length: " + String(fb->len));
    client.println();
    client.write(fb->buf, fb->len);
    client.println();

    esp_camera_fb_return(fb);
    delay(80);   // ~12 FPS
  }
}

/* ============ SNAPSHOT ===================== */
void captureSnapshot() {
  if (snapshot_fb != NULL) return;

  snapshot_fb = esp_camera_fb_get();
  if (snapshot_fb) {
    Serial.println("📷 Snapshot captured");
  }
}

/* ============ EEPROM LOG WRITE ============= */
void logEvent(uint8_t src,
              uint32_t timestamp,
              float lat,
              float lon,
              const char* filename) {

  EEPROM.begin(EEPROM_SIZE);

  uint16_t count =
    EEPROM.read(LOG_COUNT_ADDR) |
    (EEPROM.read(LOG_COUNT_ADDR + 1) << 8);

  int addr = LOG_START_ADDR + (count * LOG_ENTRY_SIZE);

  LogEntry entry;
  entry.trigger = src;
  entry.timestamp = timestamp;
  entry.latitude = lat;
  entry.longitude = lon;
  strncpy(entry.filename, filename, 12);

  EEPROM.put(addr, entry);

  count++;
  EEPROM.write(LOG_COUNT_ADDR, count & 0xFF);
  EEPROM.write(LOG_COUNT_ADDR + 1, (count >> 8) & 0xFF);

  EEPROM.commit();
  Serial.println("✅ EEPROM event logged");
}

/* ============ EEPROM READ ================== */
void readLog() {
  EEPROM.begin(EEPROM_SIZE);

  uint16_t count =
    EEPROM.read(LOG_COUNT_ADDR) |
    (EEPROM.read(LOG_COUNT_ADDR + 1) << 8);

  Serial.println("===== EEPROM EVENT LOG =====");

  for (int i = 0; i < count; i++) {
    LogEntry e;
    EEPROM.get(LOG_START_ADDR + i * LOG_ENTRY_SIZE, e);

    Serial.print("#"); Serial.print(i + 1);
    Serial.print(" | ");
    Serial.print(e.trigger == BUTTON ? "BUTTON" : "IMPACT");
    Serial.print(" | Time: "); Serial.print(e.timestamp);
    Serial.print(" | Lat: "); Serial.print(e.latitude, 6);
    Serial.print(" | Lon: "); Serial.print(e.longitude, 6);
    Serial.print(" | File: "); Serial.println(e.filename);
  }
}

/* ============ WEB SERVER =================== */
void startWebServer() {
  server.on("/stream", HTTP_GET, handleStream);
  server.begin();
  Serial.println("🌐 Web server started");
}

/* ================= SETUP =================== */
void setup() {
  Serial.begin(115200);
  delay(1500);

  EEPROM.begin(EEPROM_SIZE);

  WiFi.begin(ssid, password);
  Serial.print("📡 Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n✅ WiFi Connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  initCamera();
  startWebServer();
}

/* ================= LOOP ==================== */
void loop() {
  server.handleClient();

  /* DEMO TRIGGER */
  if (!sos_active && Serial.available()) {
    char c = Serial.read();
    if (c == 's') {
      sos_active = true;

      captureSnapshot();

      logEvent(
        BUTTON,
        millis() / 1000,
        6.9271,   // sample GPS
        79.8612,
        "SOS001.AVI"
      );

      Serial.println("🚨 SOS TRIGGERED");
    }

    if (c == 'r') {
      readLog();
    }
  }
}