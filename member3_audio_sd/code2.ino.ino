#include "FS.h"
#include "SD_MMC.h"

int event_count = 1;
unsigned long startTime;

// for compiling + testing logic
bool avi_open(const char* filename) {
  Serial.print("AVI open (simulated): ");
  Serial.println(filename);
  return true;
}

bool startRecording() {

  char filename[32];
  sprintf(filename, "/SOS_%03d.avi", event_count++);      // Creates unique file names for event counts. sprintf() is a C function used to format text into a string (char array).
                                                                
  Serial.print("Recording: ");
  Serial.println(filename);

  // Start AVI recording system.  IMPORTANT: only AVI muxer handles the file
  if (!avi_open(filename)) {
    Serial.println("AVI open failed");
    return false;
  }

  startTime = millis();     //Start timer
  return true;
}

void setup() {

  Serial.begin(115200);

  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("SD Failed");
    return;
  }

  Serial.println("SD Ready");

  // Simulate SOS trigger
  startRecording();
}

void loop() {

}
