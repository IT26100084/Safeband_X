#include "esp_camera.h"

bool sos_active = true;

QueueHandle_t cameraQueue;    // FreeRTOS queue variable

// Camera Task (Core 0)
void cameraTask(void *pvParameters) {

  camera_fb_t *fb;
  Serial.println("Camera task started");

  while (1) {

    if (sos_active) {         // SOS check ( SOS OFF---> no recording , SOS ON---> Start capturing frames)

      fb = esp_camera_fb_get();   // Capture image from camera
      if (!fb) continue;          // Check if capture failed
        Serial.println("Frame captured");

      if (xQueueSend(cameraQueue, &fb, 10) != pdPASS) {     // Send frame to queue
        Serial.println("Frame sent to queue");
      }else{ 
        esp_camera_fb_return(fb);  // drop frame if queue full
      }
    }

    vTaskDelay(1);    // Delay for stability
  }
}

void setup() {

  Serial.begin(115200);

  // Create queue (stores pointers to camera frames)
  cameraQueue = xQueueCreate(10, sizeof(camera_fb_t *));

  // Start camera task on Core 0
  xTaskCreatePinnedToCore(
    cameraTask,
    "CameraTask",
    10000,
    NULL,
    1,
    NULL,
    0
  );
}

void loop() {}