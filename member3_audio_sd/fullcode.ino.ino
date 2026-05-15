
#include "Arduino.h"
#include "FS.h"
#include "SD_MMC.h"
#include "esp_camera.h"

// ================= GLOBALS =================
bool sos_active = false;
bool stopSignalReceived = false;

QueueHandle_t cameraQueue;
SemaphoreHandle_t sdMutex;

unsigned long startTime = 0;
int event_count = 1;

// ================= CAMERA CONFIG =================
camera_config_t config = {
  .pin_pwdn = 32,
  .pin_reset = -1,
  .pin_xclk = 0,
  .pin_sccb_sda = 26,
  .pin_sccb_scl = 27,

  .pin_d7 = 35,
  .pin_d6 = 34,
  .pin_d5 = 39,
  .pin_d4 = 36,
  .pin_d3 = 21,
  .pin_d2 = 19,
  .pin_d1 = 18,
  .pin_d0 = 5,
  .pin_vsync = 25,
  .pin_href = 23,
  .pin_pclk = 22,

  .xclk_freq_hz = 20000000,
  .ledc_timer = LEDC_TIMER_0,
  .ledc_channel = LEDC_CHANNEL_0,

  .pixel_format = PIXFORMAT_JPEG,
  .frame_size = FRAMESIZE_QVGA,
  .jpeg_quality = 12,
  .fb_count = 1
};

// ================= AVI SIMULATION =================
bool avi_open(const char *filename) {
  Serial.print("AVI open: ");
  Serial.println(filename);
  return true;
}

void avi_write_frame(uint8_t *buf, size_t len) {
  Serial.print("Frame written: ");
  Serial.println(len);
}

void avi_close() {
  Serial.println("AVI closed");
}

// ================= STEP 2 =================
bool startRecording() {

  char filename[32];
  sprintf(filename, "/SOS_%03d.avi", event_count++);

  Serial.println(filename);

  if (!avi_open(filename)) return false;

  startTime = millis();
  sos_active = true;

  return true;
}

// ================= STEP 3 CAMERA TASK =================
void cameraTask(void *pvParameters) {

  camera_fb_t *fb;

  while (1) {

    if (sos_active) {

      fb = esp_camera_fb_get();
      if (!fb) continue;

      xQueueSend(cameraQueue, &fb, portMAX_DELAY);
    }

    vTaskDelay(1);
  }
}

// ================= STEP 4 SD TASK =================
void sdWriterTask(void *pvParameters) {

  camera_fb_t *fb;

  while (1) {

    if (xQueueReceive(cameraQueue, &fb, portMAX_DELAY)) {

      if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {

        avi_write_frame(fb->buf, fb->len);

        xSemaphoreGive(sdMutex);
      }

      esp_camera_fb_return(fb);
    }
  }
}

// ================= STEP 5 MONITOR =================
void monitorRecording() {

  if (!sos_active) return;

  if (millis() - startTime >= 60000 || stopSignalReceived) {

    sos_active = false;

    Serial.println("Stopping recording...");

    if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {

      avi_close();

      xSemaphoreGive(sdMutex);
    }

    Serial.println("Recording stopped");
  }
}

// ================= SETUP =================
void setup() {

  Serial.begin(115200);

  // Camera init
  esp_camera_init(&config);

  // SD init
  SD_MMC.begin("/sdcard", true);

  // Queue + Mutex
  cameraQueue = xQueueCreate(10, sizeof(camera_fb_t *));
  sdMutex = xSemaphoreCreateMutex();

  // Tasks
  xTaskCreatePinnedToCore(cameraTask, "CAM", 10000, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(sdWriterTask, "SD", 10000, NULL, 1, NULL, 1);

  // Start recording
  startRecording();
}

// ================= LOOP =================
void loop() {
  monitorRecording();
}