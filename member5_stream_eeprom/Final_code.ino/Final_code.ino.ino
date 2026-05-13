/****************************************************
 * Safeband_X - FULL Combined Firmware (ESP32-CAM)
 * Features (per proposal/flowchart):
 *  - Dual trigger: Panic button (3s hold) OR MPU6050 impact interrupt
 *  - Safe cancel window: 10 seconds
 *  - Siren + LED strobe on SOS
 *  - ESP32-CAM MJPEG live stream (/stream)
 *  - JPEG snapshot on trigger (/capture) + optional save to SD
 *  - GPS (Neo-6M) parsing via TinyGPS++ -> Google Maps link
 *  - Blynk IoT event via Blynk.logEvent()
 *  - EEPROM event log: source + timestamp + lat/lon + filename
 *
 * NOTE about pins:
 *  ESP32-CAM SD_MMC uses GPIO 2,4,12,13,14,15 (AI Thinker)
 *  If you enable SD_MMC, avoid using 2/14/15 for I2S/I2C peripherals.
 ****************************************************/

// ---------- FEATURE FLAGS (edit these) ----------
#define ENABLE_WIFI        1
#define ENABLE_BLYNK       1
#define ENABLE_CAMERA      1
#define ENABLE_STREAM      1
#define ENABLE_SNAPSHOT    1
#define ENABLE_SD_SAVE     1     // Save snapshot to SD (uses SD_MMC pins)
#define ENABLE_GPS         1
#define ENABLE_EEPROM_LOG  1
#define ENABLE_MPU6050     0     // Set 1 only if you wire MPU6050 I2C on free pins (see notes)
#define ENABLE_AUDIO_I2S   0     // ESP32-CAM pin-limited; keep 0 unless you redesign pins/hardware

// ---------- Libraries ----------
#include <WiFi.h>
#include <WebServer.h>
#include "esp_camera.h"

#if ENABLE_BLYNK
  #define BLYNK_PRINT Serial
  #include <BlynkSimpleEsp32.h>
#endif

#if ENABLE_GPS
  #include <TinyGPS++.h>
#endif

#if ENABLE_EEPROM_LOG
  #include <EEPROM.h>
#endif

#if ENABLE_SD_SAVE
  #include "FS.h"
  #include "SD_MMC.h"
#endif

#if ENABLE_MPU6050
  #include <Wire.h>
  #include <MPU6050.h>
  MPU6050 mpu;
#endif

// ---------- WiFi / Blynk credentials ----------
#if ENABLE_WIFI
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
#endif

#if ENABLE_BLYNK
char BLYNK_AUTH[] = "YOUR_BLYNK_DEVICE_AUTH_TOKEN";
#define BLYNK_EVENT_CODE  "sos_alert"   // create this event in Blynk Console
#endif

// ---------- Pins (adjust to your final pin map) ----------
#define PANIC_BTN   0      // 3-second hold (input pullup)
#define SAFE_BTN    4      // cancel within 10 seconds (input pullup)
#define SIREN_PIN   33
#define LED_PIN     32

// MPU interrupt pin (if used)
#define MPU_INT_PIN 12

// GPS: by default use UART0 pins (ESP32-CAM U0RXD/U0TXD are GPIO3/GPIO1)
// This may interfere with Serial Monitor; you can minimize Serial prints after boot.
#if ENABLE_GPS
HardwareSerial GPSSerial(1);
TinyGPSPlus gps;
#define GPS_RX_PIN  3   // GPS TX -> ESP32 RX
#define GPS_TX_PIN  1   // GPS RX -> ESP32 TX (optional)
#define GPS_BAUD    9600
#endif

// ---------- Camera model (AI Thinker ESP32-CAM) ----------
#define CAMERA_MODEL_AI_THINKER 1

#if CAMERA_MODEL_AI_THINKER
  // AI Thinker pin map
  #define PWDN_GPIO_NUM     32
  #define RESET_GPIO_NUM    -1
  #define XCLK_GPIO_NUM      0
  #define SIOD_GPIO_NUM     26
  #define SIOC_GPIO_NUM     27

  #define Y9_GPIO_NUM       35
  #define Y8_GPIO_NUM       34
  #define Y7_GPIO_NUM       39
  #define Y6_GPIO_NUM       36
  #define Y5_GPIO_NUM       21
  #define Y4_GPIO_NUM       19
  #define Y3_GPIO_NUM       18
  #define Y2_GPIO_NUM        5
  #define VSYNC_GPIO_NUM    25
  #define HREF_GPIO_NUM     23
  #define PCLK_GPIO_NUM     22
#endif

// ---------- Web server ----------
WebServer server(80);
String deviceIP = "";

// ---------- SOS state ----------
enum TriggerSource : uint8_t { SRC_BUTTON = 0, SRC_IMPACT = 1 };
volatile bool impactFlag = false;
volatile bool sosActive = false;
unsigned long sosStartMs = 0;
TriggerSource lastSource = SRC_BUTTON;

// Panic button hold detection
unsigned long panicPressStart = 0;

// Snapshot info
String lastSnapshotName = "";

// ---------- EEPROM logging layout (per proposal breakdown idea) ----------
#if ENABLE_EEPROM_LOG
// 0-1: uint16 count
// 2.. : entries
struct LogEntry {
  uint8_t source;      // 0=button,1=impact
  uint32_t ts;         // timestamp (epoch if available, else millis/1000)
  float lat;
  float lon;
  char filename[12];   // 8.3 style / short name
};
const int EEPROM_SIZE = 1024;
const int EEPROM_COUNT_ADDR = 0;
const int EEPROM_DATA_ADDR  = 2;
#endif

// ---------- ISR for impact ----------
void IRAM_ATTR impactISR() {
  impactFlag = true;
}

// ---------- Utility: build Maps link ----------
String buildMapsLink(double lat, double lon) {
  String url = "https://maps.google.com/?q=";
  url += String(lat, 6);
  url += ",";
  url += String(lon, 6);
  return url;
}

// ---------- Utility: get timestamp ----------
uint32_t getTimestamp() {
#if ENABLE_GPS
  // If GPS date/time valid, pack a crude timestamp:
  // For simplicity (no timezone), return seconds since boot if no valid date/time.
  if (gps.date.isValid() && gps.time.isValid()) {
    // NOT true Unix epoch conversion (kept simple for Arduino sketch).
    // Packed format: YYYYMMDD as high, HHMMSS as low is common in logs.
    uint32_t d = gps.date.year() * 10000UL + gps.date.month() * 100UL + gps.date.day();
    uint32_t t = gps.time.hour() * 10000UL + gps.time.minute() * 100UL + gps.time.second();
    // combine by hashing into 32-bit (still ordered enough for your log)
    return (d % 100000UL) * 100000UL + (t % 100000UL);
  }
#endif
  return (uint32_t)(millis() / 1000UL);
}

// ---------- EEPROM: write log entry ----------
#if ENABLE_EEPROM_LOG
void eepromLogEvent(TriggerSource src, float lat, float lon, const String& fname) {
  EEPROM.begin(EEPROM_SIZE);

  uint16_t count = 0;
  EEPROM.get(EEPROM_COUNT_ADDR, count);

  int entryAddr = EEPROM_DATA_ADDR + (count * (int)sizeof(LogEntry));
  if (entryAddr + (int)sizeof(LogEntry) > EEPROM_SIZE) {
    // EEPROM full: wrap by resetting count (simple)
    count = 0;
    entryAddr = EEPROM_DATA_ADDR;
  }

  LogEntry e;
  e.source = (uint8_t)src;
  e.ts = getTimestamp();
  e.lat = lat;
  e.lon = lon;

  // store short filename (max 11 chars + null)
  memset(e.filename, 0, sizeof(e.filename));
  String shortName = fname;
  if (shortName.length() > 11) shortName = shortName.substring(shortName.length() - 11);
  shortName.toCharArray(e.filename, sizeof(e.filename));

  EEPROM.put(entryAddr, e);
  count++;
  EEPROM.put(EEPROM_COUNT_ADDR, count);
  EEPROM.commit();
  EEPROM.end();
}
#endif

// ---------- SD: init ----------
#if ENABLE_SD_SAVE
bool initSD() {
  // 1-bit mode can improve flexibility on ESP32-CAM SD pins, but CMD/CLK/D0 still used.
  // SD_MMC.begin("/sdcard", true) is commonly used for 1-bit mode.
  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("SD_MMC mount failed");
    return false;
  }
  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("No SD card");
    return false;
  }
  Serial.println("SD card OK");
  return true;
}
#endif

// ---------- CAMERA: init ----------
#if ENABLE_CAMERA
bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;

  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Frame config (tune as needed)
  config.frame_size   = FRAMESIZE_VGA;
  config.jpeg_quality = 12;
  config.fb_count     = 2;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }
  Serial.println("Camera OK");
  return true;
}
#endif

// ---------- Snapshot capture ----------
#if ENABLE_SNAPSHOT
camera_fb_t* captureFrame() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return nullptr;
  }
  return fb;
}
#endif

#if ENABLE_SD_SAVE && ENABLE_SNAPSHOT
String saveSnapshotToSD(camera_fb_t* fb) {
  // Create a simple filename: PICxxxx.JPG
  static uint16_t picNum = 0;
  picNum++;

  char name[20];
  snprintf(name, sizeof(name), "/PIC%04u.JPG", picNum);

  File file = SD_MMC.open(name, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return "";
  }
  file.write(fb->buf, fb->len);
  file.close();

  Serial.print("Saved snapshot: ");
  Serial.println(name);
  return String(name);
}
#endif

// ---------- MJPEG stream handler ----------
#if ENABLE_STREAM
void handleStream() {
  WiFiClient client = server.client();

  String hdr =
    "HTTP/1.1 200 OK\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
  client.print(hdr);

  while (client.connected()) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) break;

    client.printf("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", fb->len);
    client.write(fb->buf, fb->len);
    client.print("\r\n");

    esp_camera_fb_return(fb);

    if (!client.connected()) break;
    delay(50); // throttle
  }
}
#endif

// ---------- Single JPEG capture handler ----------
void handleCapture() {
#if ENABLE_SNAPSHOT
  camera_fb_t* fb = captureFrame();
  if (!fb) {
    server.send(500, "text/plain", "capture failed");
    return;
  }

  server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
  server.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);

  esp_camera_fb_return(fb);
#else
  server.send(404, "text/plain", "snapshot disabled");
#endif
}

// ---------- SOS: siren + LED control ----------
void sirenLedOn() {
  digitalWrite(SIREN_PIN, HIGH);
  digitalWrite(LED_PIN, HIGH);
}

void sirenLedOff() {
  digitalWrite(SIREN_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
}

// Simple strobe pattern while SOS active
void sirenLedTask(void* pv) {
  for (;;) {
    if (sosActive) {
      digitalWrite(SIREN_PIN, HIGH);
      digitalWrite(LED_PIN, HIGH);
      vTaskDelay(500 / portTICK_PERIOD_MS);
      digitalWrite(SIREN_PIN, LOW);
      digitalWrite(LED_PIN, LOW);
      vTaskDelay(100 / portTICK_PERIOD_MS);
    } else {
      sirenLedOff();
      vTaskDelay(200 / portTICK_PERIOD_MS);
    }
  }
}

// ---------- SOS trigger dispatcher ----------
void triggerSOS(TriggerSource src) {
  if (sosActive) return;

  sosActive = true;
  sosStartMs = millis();
  lastSource = src;

  Serial.println("\n===== SOS ACTIVATED =====");
  Serial.print("Source: ");
  Serial.println(src == SRC_BUTTON ? "BUTTON" : "IMPACT");

  // 1) Turn on siren/LED immediately
  sirenLedOn();

  // 2) Snapshot
  double lat = 0, lon = 0;
#if ENABLE_GPS
  if (gps.location.isValid()) {
    lat = gps.location.lat();
    lon = gps.location.lng();
  }
#endif

#if ENABLE_SNAPSHOT
  camera_fb_t* fb = captureFrame();
  if (fb) {
#if ENABLE_SD_SAVE
    String fname = saveSnapshotToSD(fb);
    if (fname.length() > 0) lastSnapshotName = fname;
#endif
    esp_camera_fb_return(fb);
  }
#endif

  // 3) Build message (Maps + Stream URL)
  String maps = (lat != 0 || lon != 0) ? buildMapsLink(lat, lon) : String("GPS not fixed");
  String streamUrl = (deviceIP.length() > 0) ? ("http://" + deviceIP + "/stream") : String("(no IP)");

#if ENABLE_BLYNK
  String msg = "Safeband_X SOS | ";
  msg += (src == SRC_BUTTON ? "BUTTON" : "IMPACT");
  msg += " | ";
  msg += maps;
  msg += " | ";
  msg += streamUrl;
  Blynk.logEvent(BLYNK_EVENT_CODE, msg);
#endif

#if ENABLE_EEPROM_LOG
  eepromLogEvent(src, (float)lat, (float)lon, lastSnapshotName);
#endif
}

// ---------- Panic button 3s-hold (polled) ----------
void panicTask(void* pv) {
  for (;;) {
    if (digitalRead(PANIC_BTN) == LOW) {
      if (panicPressStart == 0) panicPressStart = millis();
      if ((millis() - panicPressStart) >= 3000) {
        triggerSOS(SRC_BUTTON);
        panicPressStart = 0;
      }
    } else {
      panicPressStart = 0;
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

// ---------- Impact trigger handling ----------
void impactTask(void* pv) {
  for (;;) {
    if (impactFlag) {
      impactFlag = false;
      triggerSOS(SRC_IMPACT);
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

// ---------- Safe cancel window (10s) ----------
void safeCancelTask(void* pv) {
  for (;;) {
    if (sosActive) {
      if ((millis() - sosStartMs) <= 10000UL) {
        if (digitalRead(SAFE_BTN) == LOW) {
          sosActive = false;
          sirenLedOff();
          Serial.println("❌ SOS CANCELLED (SAFE)");

#if ENABLE_BLYNK
          Blynk.logEvent("safe_status", "Safeband_X: FALSE ALARM - USER SAFE");
#endif
        }
      }
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// ---------- GPS reader ----------
#if ENABLE_GPS
void gpsTask(void* pv) {
  for (;;) {
    while (GPSSerial.available()) {
      gps.encode(GPSSerial.read());
    }
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}
#endif

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  Serial.println("\nSafeband_X booting...");

  pinMode(PANIC_BTN, INPUT_PULLUP);
  pinMode(SAFE_BTN, INPUT_PULLUP);
  pinMode(SIREN_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  sirenLedOff();

  // Impact interrupt pin
  pinMode(MPU_INT_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(MPU_INT_PIN), impactISR, RISING);

#if ENABLE_SD_SAVE
  initSD();
#endif

#if ENABLE_CAMERA
  initCamera();
#endif

#if ENABLE_WIFI
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  deviceIP = WiFi.localIP().toString();
  Serial.print("IP: "); Serial.println(deviceIP);
#endif

#if ENABLE_BLYNK
  Blynk.begin(BLYNK_AUTH, WIFI_SSID, WIFI_PASS);
#endif

#if ENABLE_GPS
  // Use UART0 pins by default (3/1) - adjust if you remap to different pins
  GPSSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
#endif

  // Web routes
  server.on("/", HTTP_GET,  {
    server.send(200, "text/plain", "Safeband_X running. Use /stream or /capture");
  });

#if ENABLE_STREAM
  server.on("/stream", HTTP_GET, handleStream);
#endif

  server.on("/capture", HTTP_GET, handleCapture);
  server.begin();
  Serial.println("HTTP server started");

  // Tasks
  xTaskCreatePinnedToCore(sirenLedTask, "SirenLED", 2048, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(panicTask,    "Panic",    2048, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(impactTask,   "Impact",   2048, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(safeCancelTask,"Safe",    2048, NULL, 1, NULL, 1);

#if ENABLE_GPS
  xTaskCreatePinnedToCore(gpsTask, "GPS", 4096, NULL, 1, NULL, 0);
#endif

  Serial.println("Safeband_X READY ✅");
}

// ---------- Loop ----------
void loop() {
#if ENABLE_BLYNK
  Blynk.run();
#endif
  server.handleClient();

  // After cancel window ends, SOS continues until you stop it manually (extend as needed)
  // You can add an auto-timeout here if required.
}