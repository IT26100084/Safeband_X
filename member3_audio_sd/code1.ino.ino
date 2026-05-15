#include "FS.h"    
#include "SD_MMC.h"

bool setupSD() {      //Create a function name 

// Initialize SD card
  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("SD Mount Failed");      // Prints error if SD initialization fails
    return false;                           // SD failed
  }

  File file = SD_MMC.open("/test.txt", FILE_WRITE);     // write data into file

  if (!file || !file.print("Hello SD")) {   // Checks did the file open fail? or writing fail?
    Serial.println("Write Failed");       
    return false;                                  
  }

  file.close();                            // Closes file safely

  Serial.println("SD Card OK");

  return true;                             // Tells everything worked
}

// Setup function
void setup() {

  Serial.begin(115200);

  if (setupSD()) {                        // Calls SD initialization function
    Serial.println("Ready");
  }
}

void loop() {

}
