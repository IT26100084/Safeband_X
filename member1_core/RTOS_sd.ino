#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// =====================================================
// EVENT GROUP BITS
// =====================================================

#define SOS_EVENT_BIT    (1 << 0)

// =====================================================
// GLOBAL FLAGS
// =====================================================

volatile bool sos_active = false;
volatile bool mpu_interrupt_flag = false;

// =====================================================
// EVENT GROUP HANDLE
// =====================================================

EventGroupHandle_t sos_event_group;

// =====================================================
// QUEUES
// =====================================================

// Camera queue (Core 0 → Core 1)
QueueHandle_t cameraFrameQueue;

// Audio queue (Core 1 → SD writer)
QueueHandle_t audioChunkQueue;

// =====================================================
// SEMAPHORES
// =====================================================

// SD card protection semaphore
SemaphoreHandle_t sdCardMutex;

// =====================================================
// DATA STRUCTURES
// =====================================================

typedef struct {
  uint8_t frameData[1024];
  size_t length;
} CameraFrame;

typedef struct {
  uint8_t audioData[512];
  size_t length;
} AudioChunk;

// =====================================================
// TRIGGER SOS FUNCTION
// =====================================================

void trigger_sos(String source) {

  // Prevent repeated triggers
  if (sos_active) {
    return;
  }

  sos_active = true;

  Serial.print("SOS TRIGGERED — ");
  Serial.println(source);

  // Notify all waiting tasks simultaneously
  xEventGroupSetBits(
    sos_event_group,
    SOS_EVENT_BIT
  );
}

// =====================================================
// MPU6050 INTERRUPT SERVICE ROUTINE
// =====================================================

void IRAM_ATTR mpu6050_isr() {

  // NEVER call trigger_sos() here
  // ISR should be extremely short

  mpu_interrupt_flag = true;
}

// =====================================================
// MAIN MONITOR TASK
// =====================================================

void monitorTask(void *pvParameters) {

  while (true) {

    // Check MPU interrupt flag safely outside ISR
    if (mpu_interrupt_flag) {

      mpu_interrupt_flag = false;

      trigger_sos("IMPACT");
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// =====================================================
// CAMERA TASK (CORE 0)
// =====================================================

void cameraTask(void *pvParameters) {

  CameraFrame frame;

  while (true) {

    // Example dummy frame
    frame.length = 100;

    // Send frame to queue
    xQueueSend(
      cameraFrameQueue,
      &frame,
      portMAX_DELAY
    );

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// =====================================================
// AUDIO TASK (CORE 1)
// =====================================================

void audioTask(void *pvParameters) {

  AudioChunk chunk;

  while (true) {

    chunk.length = 256;

    xQueueSend(
      audioChunkQueue,
      &chunk,
      portMAX_DELAY
    );

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// =====================================================
// SD WRITER TASK
// =====================================================

void sdWriterTask(void *pvParameters) {

  AudioChunk receivedChunk;

  while (true) {

    if (xQueueReceive(
          audioChunkQueue,
          &receivedChunk,
          portMAX_DELAY
        ) == pdTRUE) {

      // Lock SD card access
      if (xSemaphoreTake(sdCardMutex, portMAX_DELAY)) {

        Serial.println("Writing audio chunk to SD card");

        // SD write operations go here

        // Release SD card
        xSemaphoreGive(sdCardMutex);
      }
    }
  }
}

// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  // Create event group
  sos_event_group = xEventGroupCreate();

  // Create queues
  cameraFrameQueue = xQueueCreate(
    5,
    sizeof(CameraFrame)
  );

  audioChunkQueue = xQueueCreate(
    10,
    sizeof(AudioChunk)
  );

  // Create mutex
  sdCardMutex = xSemaphoreCreateMutex();

  // MPU6050 interrupt pin
  pinMode(2, INPUT_PULLUP);

  attachInterrupt(
    digitalPinToInterrupt(2),
    mpu6050_isr,
    RISING
  );

  // Create tasks

  xTaskCreatePinnedToCore(
    monitorTask,
    "Monitor Task",
    4096,
    NULL,
    1,
    NULL,
    1
  );

  xTaskCreatePinnedToCore(
    cameraTask,
    "Camera Task",
    4096,
    NULL,
    1,
    NULL,
    0
  );

  xTaskCreatePinnedToCore(
    audioTask,
    "Audio Task",
    4096,
    NULL,
    1,
    NULL,
    1
  );

  xTaskCreatePinnedToCore(
    sdWriterTask,
    "SD Writer Task",
    4096,
    NULL,
    1,
    NULL,
    1
  );
}

// =====================================================
// LOOP
// =====================================================

void loop() {

  // Main loop unused
}