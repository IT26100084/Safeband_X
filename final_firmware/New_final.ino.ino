*
 * ================================================================
 *  Safeband_X — ESP32 DEV BOARD FIRMWARE (no camera)
 *  Group WD025 | IT1040 Fundamentals of Computing | SLIIT 2026
 * ================================================================
 *
 *  This is a full logic port of the original ESP32-CAM firmware to a
 *  plain ESP32 dev board (e.g. ESP32-WROOM-32 DevKitC / NodeMCU-32S).
 *
 *  WHY THIS EXISTS:
 *  The ESP32-CAM has FIXED camera pins (GPIO0, 5, 18, 19, 21-27, 32-36),
 *  which is why the original firmware had two unavoidable wiring
 *  conflicts (Panic button vs MPU INT on GPIO12, Cancel button vs
 *  camera XCLK on GPIO0). A plain dev board has no reserved camera
 *  pins, so every peripheral below gets its own dedicated GPIO and
 *  there are NO pin conflicts.
 *
 *  TRADE-OFF (hardware limitation, not a logic omission):
 *  No camera and no SD card on this build. All camera-dependent
 *  features (JPEG snapshot, MJPEG stream, video/image upload) and
 *  all SD-dependent features (audio-to-file recording) have been
 *  removed because there's no hardware to drive them. Every other
 *  feature is preserved and fully wired up: impact detection,
 *  panic-button hold, 10s cancel window, OLED UI, EEPROM event log
 *  (in internal flash, not SD), Blynk alerts, GPS, buzzer/LED
 *  strobe, and live INMP441 sound-level monitoring for a
 *  shout/clap-triggered SOS (no recording — just detection, since
 *  there's nowhere to save audio without an SD card).
 *
 *  ── UPDATED PIN MAP (ESP32 dev board, zero conflicts) ──────────
 *    MPU6050   → SDA=21, SCL=22 (shared I2C bus with OLED), INT=27
 *    OLED SSD1306 → SDA=21, SCL=22 (same I2C bus), VCC=3.3V
 *    INMP441   → WS=32, SCK(BCLK)=33, SD(DATA)=34, L/R=GND
 *    GPS Neo-6M→ TX→GPIO16 (RX2), RX→GPIO17 (TX2)
 *    Panic btn → GPIO13 (other leg GND)
 *    Cancel/Safe btn → GPIO14 (other leg GND)
 *    Buzzer    → GPIO25 via 2N2222 transistor + 1kΩ resistor
 *    RGB LED R → GPIO2  via 470Ω resistor
 *    Power     → 3.7V LiPo + TP4056 → 5V/VIN pin
 *
 *  Libraries (install via Library Manager):
 *    - MPU6050           by Electronic Cats
 *    - TinyGPSPlus       by Mikal Hart
 *    - Blynk             by Volodymyr Shymanskyy
 *    - Adafruit SSD1306  by Adafruit
 *    - Adafruit GFX      by Adafruit
 * ================================================================
 */

// ── YOUR CREDENTIALS ─────────────────────────────────────────
#define WIFI_SSID    "Dialog 4G"
#define WIFI_PASS    "01R5LNTJ9LR"

#define BLYNK_TEMPLATE_ID   "TMPL6i0K_89cw"
#define BLYNK_TEMPLATE_NAME  "Safeband X"
#define BLYNK_AUTH_TOKEN    "WTIZGnWAn_wH5MDnlvd-lJA9icgUckRc"


// ── BLYNK VIRTUAL PINS ───────────────────────────────────────
#define VPIN_NOTIFY  V0
#define VPIN_STATUS  V2
#define VPIN_TRIGGER V3
#define VPIN_LOCATION V4
#define VPIN_SOUND V7

// ── SETTINGS ─────────────────────────────────────────────────
#define PANIC_HOLD_MS          3000
#define CANCEL_WINDOW_MS       10000
#define IMPACT_G_THRESHOLD     2.5f
#define RECORD_DURATION_MS     60000
#define AUDIO_SAMPLE_RATE      16000
#define EEPROM_SIZE            512
#define MAX_LOG_ENTRIES        10

#define ENABLE_SOUND_TRIGGER   true    // set false to disable shout/clap SOS
#define SOUND_TRIGGER_LEVEL    200.0f  // RMS threshold, tune to your mic/environment

// ── OLED SETTINGS ────────────────────────────────────────────
#define OLED_ADDRESS    0x3C
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64

// ─────────────────────────────────────────────────────────────

#include "driver/i2s.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <Wire.h>
#include <MPU6050.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <BlynkSimpleEsp32.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

// ── PERIPHERAL PINS (dev board — no conflicts) ────────────────
#define PIN_PANIC       13
#define PIN_SAFE        12
#define PIN_BUZZER      25
#define PIN_LED         2

#define MPU_SDA         21
#define MPU_SCL         22
#define MPU_INT_PIN     27

#define I2S_WS          32
#define I2S_SCK         33
#define I2S_SD          34

// ── OBJECTS ──────────────────────────────────────────────────
MPU6050          mpu;
TinyGPSPlus      gps;
HardwareSerial   gpsSerial(2);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ── EEPROM LOG ───────────────────────────────────────────────
struct EventLog {
  char   source[8];
  char   uptime[16];
  double lat;
  double lng;
};

// ── GLOBALS ──────────────────────────────────────────────────
volatile bool impactFlag    = false;
volatile bool soundFlag     = false;
volatile bool sosActive     = false;
bool          cancelledSOS  = false;
String        triggerSrc    = "";
double        gpsLat        = 0.0;
double        gpsLng        = 0.0;
bool          gpsValid      = false;
int           recordIndex   = 0;
int           battPercent   = 85;   // placeholder — add ADC if needed

volatile float audioLevel = 0;

// ════════════════════════════════════════════════════════════
//  OLED DISPLAY FUNCTIONS
// ════════════════════════════════════════════════════════════

void initOLED() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("[OLED] NOT FOUND — check wiring");
    return;
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  Serial.println("[OLED] OK");

  display.setTextSize(1);
  display.setCursor(20, 10);
  display.println("SAFEBAND_X");
  display.setCursor(10, 25);
  display.println("Group WD025");
  display.setCursor(5, 40);
  display.println("SLIIT 2026");
  display.setCursor(15, 55);
  display.println("Initializing...");
  display.display();
  delay(2000);
}

void showMonitorScreen() {
  display.clearDisplay();

  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(15, 2);
  display.println("SAFEBAND_X");
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 15);
  display.print("STATUS: ");
  display.println(sosActive ? "SOS ACTIVE!" : "ACTIVE");

  display.setCursor(0, 27);
  display.print("GPS: ");
  if (gpsValid) {
    int sats = (int)gps.satellites.value();
    display.print("LOCKED (");
    display.print(sats);
    display.println(" SAT)");
  } else {
    display.println("SEARCHING...");
  }

  display.setCursor(0, 39);
  display.print("WIFI: ");
  display.println(WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED");

  display.setCursor(0, 51);
  display.print("BATT: ");
  display.print(battPercent);
  display.println("%");

  int barWidth = map(battPercent, 0, 100, 0, 40);
  display.drawRect(85, 51, 42, 8, SSD1306_WHITE);
  display.fillRect(86, 52, barWidth, 6, SSD1306_WHITE);

  display.display();
}

void showSOSScreen(const String& source) {
  display.clearDisplay();

  display.fillRect(0, 0, 128, 16, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(2);
  display.setCursor(25, 2);
  display.println("SOS!");
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 20);
  display.print("TRIGGER: ");
  display.println(source);

  display.setCursor(0, 32);
  display.print("LAT: ");
  display.println(gpsLat, 4);

  display.setCursor(0, 42);
  display.print("LNG: ");
  display.println(gpsLng, 4);

  display.setCursor(0, 54);
  display.println("Blynk alert sent!");

  display.display();
}

void showCancelScreen() {
  display.clearDisplay();
  display.setTextSize(1);

  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(15, 2);
  display.println("SAFEBAND_X");
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(10, 20);
  display.println("FALSE ALARM");
  display.setCursor(5, 35);
  display.println("Alert Cancelled");
  display.setCursor(15, 50);
  display.println("User is Safe");
  display.display();
  delay(3000);
}

void showCancelCountdown(int secondsLeft) {
  display.clearDisplay();

  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(15, 2);
  display.println("SAFEBAND_X");
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 16);
  display.println("SOS triggered!");
  display.setCursor(0, 28);
  display.println("Press SAFE to");
  display.setCursor(0, 38);
  display.println("cancel:");

  display.setTextSize(2);
  display.setCursor(50, 48);
  display.print(secondsLeft);
  display.println("s");

  display.display();
}

void showAlertScreen() {
  display.clearDisplay();

  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(15, 2);
  display.println("SAFEBAND_X");
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 16);
  display.println("ALERT ACTIVE");
  display.setCursor(0, 28);
  display.println("Location + status");
  display.setCursor(0, 40);
  display.println("sent to");
  display.setCursor(0, 52);
  display.println("emergency contact");

  display.display();
}

void showError(const String& msg) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("ERROR:");
  display.println(msg);
  display.display();
}

// ════════════════════════════════════════════════════════════
//  MPU6050 INIT (real interrupt — dev board has a free INT pin)
// ════════════════════════════════════════════════════════════
void IRAM_ATTR mpuISR() { impactFlag = true; }

void initMPU() {
  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("[MPU] NOT FOUND");
    showError("MPU6050 missing");
    return;
  }
  mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_8);
  mpu.setDHPFMode(MPU6050_DHPF_5);
  mpu.setMotionDetectionThreshold(125);
  mpu.setMotionDetectionDuration(1);
  mpu.setIntMotionEnabled(true);
  mpu.setIntEnabled(0x40);
  pinMode(MPU_INT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(MPU_INT_PIN), mpuISR, RISING);
  Serial.println("[MPU] OK");
}

// ════════════════════════════════════════════════════════════
//  GPS
// ════════════════════════════════════════════════════════════
void pollGPS(unsigned long ms = 2000) {
  unsigned long t = millis();
  while (millis() - t < ms) {
    while (gpsSerial.available()) gps.encode(gpsSerial.read());
  }
  if (gps.location.isValid()) {
    gpsLat  = gps.location.lat();
    gpsLng  = gps.location.lng();
    gpsValid = true;
    Serial.printf("[GPS] %.6f, %.6f\n", gpsLat, gpsLng);
  }
}

// ── GPS diagnostics ───────────────────────────────────────────
// Type "GPSDEBUG" in the Serial Monitor at any time to print this.
// - charsProcessed == 0        -> no wiring/UART/baud issue upstream,
//                                  nothing is being received AT ALL.
//                                  Check TX/RX crossed correctly and
//                                  that this isn't a PSRAM board
//                                  (WROVER reserves GPIO16/17).
// - charsProcessed > 0 but
//   sentencesWithFix == 0      -> UART is fine, module just has no
//                                  satellite fix yet. Needs clear sky
//                                  view; cold start can take 30s-2min+
//                                  and MUCH longer indoors.
// - failedChecksum growing fast-> wrong baud rate or noisy wiring.
void printGPSDebug() {
  Serial.println("── GPS DEBUG ──");
  Serial.printf("charsProcessed   : %lu\n", gps.charsProcessed());
  Serial.printf("sentencesWithFix : %lu\n", gps.sentencesWithFix());
  Serial.printf("failedChecksum   : %lu\n", gps.failedChecksum());
  Serial.printf("passedChecksum   : %lu\n", gps.passedChecksum());
  Serial.printf("location.isValid : %s\n", gps.location.isValid() ? "YES" : "NO");
  Serial.printf("satellites       : %d\n", (int)gps.satellites.value());
  Serial.println("Raw NMEA (5s) ─────────────");
  unsigned long t = millis();
  while (millis() - t < 5000) {
    while (gpsSerial.available()) {
      char c = gpsSerial.read();
      Serial.write(c);
      gps.encode(c);
    }
  }
  Serial.println("\n───────────────────────────");
  if (gps.charsProcessed() < 10) {
    Serial.println("!! Nothing received on GPIO16 — check wiring/baud/board type.");
  }
}

// ════════════════════════════════════════════════════════════
//  EEPROM LOG
// ════════════════════════════════════════════════════════════
void saveLog(const char* source) {
  int count = 0;
  EEPROM.get(0, count);
  if (count < 0 || count >= MAX_LOG_ENTRIES) count = 0;
  EventLog e;
  strncpy(e.source, source, 8);
  snprintf(e.uptime, 16, "%lus", millis() / 1000);
  e.lat = gpsLat;
  e.lng = gpsLng;
  EEPROM.put(sizeof(int) + count * sizeof(EventLog), e);
  count++;
  EEPROM.put(0, count);
  EEPROM.commit();
  Serial.printf("[LOG] #%d: %s\n", count, source);
}

void printAllLogs() {
  int count = 0;
  EEPROM.get(0, count);
  Serial.printf("\n── EEPROM LOG (%d entries) ──\n", count);
  for (int i = 0; i < count; i++) {
    EventLog e;
    EEPROM.get(sizeof(int) + i * sizeof(EventLog), e);
    Serial.printf("#%d | %s | %s | %.6f,%.6f\n",
                  i+1, e.source, e.uptime, e.lat, e.lng);
  }
  Serial.println("─────────────────────────────\n");
}

// ════════════════════════════════════════════════════════════
//  LED STROBE + BUZZER TASKS
// ════════════════════════════════════════════════════════════
void strobeTask(void* p) {
  int pat[] = {200,200,200,200,200,200,
               500,200,500,200,500,200,
               200,200,200,200,200,200,800};
  while (sosActive && !cancelledSOS) {
    for (int i = 0; i < 19 && sosActive; i++) {
      digitalWrite(PIN_LED, i%2==0 ? HIGH : LOW);
      vTaskDelay(pdMS_TO_TICKS(pat[i]));
    }
  }
  digitalWrite(PIN_LED, LOW);
  vTaskDelete(NULL);
}

void buzzerTask(void* p) {
  while (sosActive && !cancelledSOS) {
    digitalWrite(PIN_BUZZER, HIGH);
    vTaskDelay(pdMS_TO_TICKS(100));
    digitalWrite(PIN_BUZZER, LOW);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  digitalWrite(PIN_BUZZER, LOW);
  vTaskDelete(NULL);
}

// ════════════════════════════════════════════════════════════
//  I2S MICROPHONE (INMP441) — INIT + ALWAYS-ON MONITOR TASK
//  Continuous RMS level monitoring only (no SD = nowhere to
//  record audio to), used purely for shout/clap SOS triggering.
// ════════════════════════════════════════════════════════════
void initMicrophone() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = AUDIO_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  Serial.println("[I2S] INMP441 READY");
}

void audioTask(void* param) {
  const int BUF = 128;
  int32_t samples[BUF];
  size_t bytesRead = 0;

  while (true) {
    i2s_read(I2S_NUM_0, samples, sizeof(samples), &bytesRead, portMAX_DELAY);
    Serial.println(samples[0]);
    int sampleCount = bytesRead / 4;
    if (sampleCount <= 0) continue;

    // RMS level for both display + sound-trigger
    double sum = 0;
    for (int i = 0; i < sampleCount; i++) {
      float s = (float)(samples[i] >> 8);
      sum += (double)s * s;
    }
    audioLevel = sqrt(sum / sampleCount);

    if (ENABLE_SOUND_TRIGGER && !sosActive && audioLevel > SOUND_TRIGGER_LEVEL) {
      soundFlag = true;
    }
  }
}

// ════════════════════════════════════════════════════════════
//  BLYNK ALERTS
// ════════════════════════════════════════════════════════════
void sendAlert() {

  String mapsUrl;

  if (gpsValid) {
    mapsUrl = "https://maps.google.com/?q=" +
              String(gpsLat, 6) + "," +
              String(gpsLng, 6);
  }
  else {
    mapsUrl = "GPS not available";
  }

  String message =
      "🚨 SAFEBAND_X ALERT!\n"
      "Trigger: " + triggerSrc +
      "\n\nLocation:\n" + mapsUrl;

  // Push notification
  Blynk.logEvent("safeband_alert", message);

  // Dashboard values
  Blynk.virtualWrite(VPIN_STATUS, "SOS ACTIVE");
  Blynk.virtualWrite(VPIN_TRIGGER, triggerSrc);
  Blynk.virtualWrite(VPIN_LOCATION, mapsUrl);

  Serial.println("========== SOS ==========");
  Serial.println(message);
  Serial.println("=========================");
}
void sendCancelAlert() {


  Blynk.logEvent(
      "safeband_cancel",
      "Safeband_X: False alarm cancelled. User is safe."
  );

  Blynk.virtualWrite(VPIN_STATUS, "MONITORING");
  Blynk.virtualWrite(VPIN_TRIGGER, "NONE");
  Blynk.virtualWrite(VPIN_LOCATION, "");

}

// ════════════════════════════════════════════════════════════
//  MAIN SOS SEQUENCE
// ════════════════════════════════════════════════════════════
void triggerSOS(const char* source) {
  if (sosActive) return;
  sosActive    = true;
  cancelledSOS = false;
  triggerSrc   = String(source);

  Serial.printf("\n══ SOS: %s ══\n", source);

  xTaskCreatePinnedToCore(strobeTask, "LED",  2048, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(buzzerTask, "BUZZ", 2048, NULL, 2, NULL, 1);

  // ── 10-second cancel countdown ───────────────────────────
  unsigned long cStart = millis();
  while (millis() - cStart < CANCEL_WINDOW_MS) {
    int remaining = (CANCEL_WINDOW_MS - (millis() - cStart)) / 1000;
    showCancelCountdown(remaining);
    if (digitalRead(PIN_SAFE) == LOW) {
      cancelledSOS = true;
      sosActive    = false;
      delay(300);
      break;
    }
    delay(200);
  }

  if (cancelledSOS) {
    showCancelScreen();
    sendCancelAlert();
    showMonitorScreen();
    return;
  }

  // ── Full SOS ─────────────────────────────────────────────
  pollGPS(2000);
  showSOSScreen(triggerSrc);

  saveLog(source);
  sendAlert();

  showAlertScreen();
  Blynk.virtualWrite(VPIN_STATUS, "ALERT ACTIVE");

  unsigned long rStart = millis();
  while (millis() - rStart < RECORD_DURATION_MS) {
    Blynk.run();
    while (gpsSerial.available()) gps.encode(gpsSerial.read());
    delay(100);
  }

  sosActive = false;
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED, LOW);
  recordIndex++;

  Blynk.virtualWrite(VPIN_STATUS, "MONITORING");
  Blynk.logEvent("safeband_done", "Safeband_X: Alert window complete. Event #" + String(recordIndex-1));

  showMonitorScreen();
  Serial.println("[SOS] Complete. Monitoring.\n");
}

// ════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  Serial.println("\n[BOOT] Safeband_X — ESP32 Dev Board");

  pinMode(PIN_PANIC,  INPUT_PULLUP);
  pinMode(PIN_SAFE,   INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED,    OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED,    LOW);

  // I2C (shared by OLED + MPU6050)
  Wire.begin(MPU_SDA, MPU_SCL);

  initOLED();

  EEPROM.begin(EEPROM_SIZE);

  display.clearDisplay();
  display.setCursor(0,0); display.println("MPU6050...");
  display.display();
  initMPU();

  display.clearDisplay();
  display.setCursor(0,0); display.println("Mic (I2S)...");
  display.display();
  initMicrophone();
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, NULL, 1, NULL, 1);

  // GPS
  gpsSerial.begin(9600, SERIAL_8N1, 16, 17);
  Serial.println("[GPS] UART ready");

  // WiFi
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("WiFi...");
  display.display();

  Serial.println("Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(1000);
  Wi
