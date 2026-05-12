#include <MPU6050.h>

MPU6050 mpu;

void setup() {
  Serial.begin(115200);

  // Small delay for serial stability
  delay(1000);

  Serial.println("Starting MPU6050 test...");

  // Initialize MPU6050
  mpu.initialize();

  // Test connection
  if (mpu.testConnection()) {
    Serial.println("MPU6050 connection SUCCESS ✅");
  } else {
    Serial.println("MPU6050 connection FAILED ❌");
  }
}

void loop() {
  // Nothing needed here for now
}
