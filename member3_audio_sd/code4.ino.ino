#include "esp_camera.h"

QueueHandle_t cameraQueue;      // Hold frames from camera
SemaphoreHandle_t sdMutex;      // Prevent SD card conflicts

bool avi_write_frame(uint8_t *buf, size_t len) {            //Fixes compile error
  Serial.print("AVI frame written, size: ");
  Serial.println(len);
  return true;
}

void sdWriterTask(void *pvParameters) {     // SD writer task

    camera_fb_t *fb;

    while (1) {

        if (xQueueReceive(cameraQueue, &fb, portMAX_DELAY)) {     // Waits until a frame arrives from camera

            if (fb != NULL) {     // Check frame

                if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {     // Lock SD card

                    avi_write_frame(fb->buf, fb->len);            // Write frame

                    xSemaphoreGive(sdMutex);                      // Unlock SD
                }

                esp_camera_fb_return(fb);                        // Free memory 
            }
        }
    }
}

void setup() {

    Serial.begin(115200);

    // Create queue (must match Step 3)
    cameraQueue = xQueueCreate(10, sizeof(camera_fb_t *));

    // Create mutex for SD safety
    sdMutex = xSemaphoreCreateMutex();

    // Start SD writer task on Core 1
    xTaskCreatePinnedToCore(
        sdWriterTask,
        "SDWriter",
        10000,
        NULL,
        1,
        NULL,
        1
    );
}

void loop() {
    // Empty because FreeRTOS tasks handle everything
}