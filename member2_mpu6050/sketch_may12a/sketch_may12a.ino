#include <Wire.h>
#include <MPU6050.h>

MPU6050 mpu;

// This variable changes inside interrupt
volatile bool impactDetected = false;

// Interrupt Service Routine (ISR)
// VERY IMPORTANT:
// Keep ISR extremely small and fast
void mpuISR() {
  impactDetected = true;
}

void setup() {

  // Start serial monitor
  Serial.begin(115200);

  // Start I2C communication
  Wire.begin();

  // Initialize MPU6050
  mpu.initialize();

  // Check MPU6050 connection
  if (mpu.testConnection()) {
    Serial.println("MPU6050 connected successfully");
  } else {
    Serial.println("MPU6050 connection failed");
    while (1); // Stop program forever
  }

  // -----------------------------
  // MOTION DETECTION SETTINGS
  // -----------------------------

  // Set motion threshold
  // Higher value = less sensitive
  // Start with 20 for testing
  mpu.setMotionDetectionThreshold(20);

  // Motion must last this long
  // Value is in milliseconds
  mpu.setMotionDetectionDuration(1);

  // Enable motion interrupt
  mpu.setIntMotionEnabled(true);

  // -----------------------------
  // INTERRUPT PIN SETUP
  // -----------------------------

  // Pin 12 receives interrupt signal
  pinMode(12, INPUT);

  // Attach interrupt
  // When signal rises HIGH -> call mpuISR
  attachInterrupt(
    digitalPinToInterrupt(12),
    mpuISR,
    RISING
  );

  Serial.println("Impact detection system ready");
}

void loop() {

  // Check if interrupt happened
  if (impactDetected == true) {

    // Reset flag immediately
    impactDetected = false;

    // Trigger SOS function
    trigger_sos("IMPACT");
  }

  // Small delay for stability
  delay(10);
}

// -----------------------------
// SOS FUNCTION
// -----------------------------
void trigger_sos(String reason) {

  Serial.println("SOS TRIGGERED");
  Serial.print("Reason: ");
  Serial.println(reason);

  // Put your emergency code here
  // Example:
  // Send SMS
  // Activate buzzer
  // Send GPS location
}
