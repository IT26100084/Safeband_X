#include "Arduino.h"

bool sos_active = true;
bool stopSignalReceived = false;

SemaphoreHandle_t sdMutex;
unsigned long startTime = 0;

void avi_close() {
  Serial.println("AVI closed (simulated)");
}

void monitorRecording() {

    if (!sos_active) return;      // Check if recording is ON

    unsigned long elapsed = millis() - startTime;     // Calculate elapsed time

    if (elapsed >= 60000 || stopSignalReceived) {     // Stop condition

        sos_active = false;                           // Stop recording flag

        Serial.println("Stopping recording...");

        if (xSemaphoreTake(sdMutex, portMAX_DELAY)) { // Mutex protection

            avi_close();                              // Close AVI file

            xSemaphoreGive(sdMutex);                  // Unlock SD
        }

        Serial.println("Recording stopped");
    }
}

void setup() {
  Serial.begin(115200);

  sdMutex = xSemaphoreCreateMutex();
  startTime = millis();   // simulate recording start
}

void loop() {
  monitorRecording();
}