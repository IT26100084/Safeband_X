// ============================================================
// Safeband X — Member 4 (IT26100942)
// Role: GPS Neo-6M + Blynk Notifications
// Board: AI Thinker ESP32-CAM
// Group: WD025 | IT1040 | SLIIT
// ============================================================

// --- Blynk credentials (must be before any #include) ---
#define BLYNK_TEMPLATE_ID   "TMPL6i0K_89cw"
#define BLYNK_TEMPLATE_NAME "Safeband X"
#define BLYNK_AUTH_TOKEN    "WTIZGnWAn_wH5MDnlvd-lJA9icgUckRc"

// --- Libraries ---
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>  // Blynk library for ESP32
#include <HardwareSerial.h>    // For GPS serial communication
#include <TinyGPS++.h>         // For parsing GPS NMEA data

// ============================================================
// WiFi Credentials
// ============================================================
char ssid[] = "Nish iPhone";
char pass[] = "nishsube1234";

// ============================================================
// GPS Pin Definitions
// Using HardwareSerial(1) with custom pins
// (GPIO 1 & 3 are UART0 — shared with Serial.print, avoid!)
// Discussed with M1 — using GPIO 16 (RX) and GPIO 17 (TX)
// ============================================================
#define GPS_RX_PIN 16   // ESP32 receives data FROM GPS
#define GPS_TX_PIN 17   // ESP32 sends data TO GPS

// ============================================================
// Button Pin Definition
// Panic button — GPIO 33 (INPUT_PULLUP, other leg to GND)
// ============================================================
#define BUTTON_PIN 33

// ============================================================
// GPS Objects
// ============================================================
HardwareSerial gpsSerial(1);   // Use UART1 for GPS
TinyGPSPlus gps;               // TinyGPS++ object to parse data

// ============================================================
// FreeRTOS Task Handles
// ============================================================
TaskHandle_t wifiTaskHandle;
TaskHandle_t gpsTaskHandle;

// ============================================================
// CORE 0 — WiFi + Blynk Task
// Blynk.run() must be called continuously on Core 0
// ============================================================
void wifiTask(void *pvParameters) {
  Serial.println("WiFi Task started on Core 0");

  // Connect to WiFi and Blynk server
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  // Keep Blynk connection alive
  while (1) {
    Blynk.run();   // Handles Blynk communication continuously
    vTaskDelay(10 / portTICK_PERIOD_MS);  // Small delay to prevent watchdog reset
  }
}

// ============================================================
// CORE 1 — GPS Task
// Reads GPS data and builds Google Maps URL
// ============================================================
void gpsTask(void *pvParameters) {
  Serial.println("GPS Task started on Core 1");

  // Initialise HardwareSerial(1) at 9600 baud with custom pins
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println("GPS Serial initialised at 9600 baud");

  while (1) {

    // Feed each byte from GPS module into TinyGPS++ parser
    while (gpsSerial.available() > 0) {
      gps.encode(gpsSerial.read());  // Parse incoming GPS bytes
    }

    // Check if a new valid location has been received
    if (gps.location.isUpdated() && gps.location.isValid()) {

      // Extract latitude and longitude
      float lat = gps.location.lat();   // Latitude
      float lng = gps.location.lng();   // Longitude

      // Build Google Maps URL with 6 decimal places
      String mapsURL = "https://maps.google.com/?q="
                       + String(lat, 6)
                       + ","
                       + String(lng, 6);

      // Print to Serial Monitor for testing
      Serial.println("GPS Location Updated!");
      Serial.print("Latitude  : "); Serial.println(lat, 6);
      Serial.print("Longitude : "); Serial.println(lng, 6);
      Serial.print("Maps URL  : "); Serial.println(mapsURL);

      // Send SOS alert with Maps URL via Blynk notification
      Blynk.logEvent("sos_alert", mapsURL);
      Serial.println("SOS Alert sent via Blynk!");
    }

    vTaskDelay(1000 / portTICK_PERIOD_MS);  // Check GPS every 1 second
  }
}

// ============================================================
// SETUP — Runs once at startup
// ============================================================
void setup() {

  // Start Serial Monitor for debugging
  // NOTE: When GPS is active on UART0 pins, Serial.print
  // may stop. We use HardwareSerial(1) to avoid this.
  Serial.begin(115200);
  Serial.println("Safeband X — Member 4 Booting...");

  // Setup panic button pin
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // Button other leg to GND
  Serial.println("Button initialised on GPIO 33");

  // --------------------------------------------------------
  // Create WiFi + Blynk task on CORE 0
  // --------------------------------------------------------
  xTaskCreatePinnedToCore(
    wifiTask,         // Task function
    "WiFi Task",      // Task name
    10000,            // Stack size
    NULL,             // Parameters
    1,                // Priority
    &wifiTaskHandle,  // Task handle
    0                 // Core 0
  );

  // --------------------------------------------------------
  // Create GPS task on CORE 1
  // --------------------------------------------------------
  xTaskCreatePinnedToCore(
    gpsTask,          // Task function
    "GPS Task",       // Task name
    10000,            // Stack size
    NULL,             // Parameters
    1,                // Priority
    &gpsTaskHandle,   // Task handle
    1                 // Core 1
  );

  Serial.println("All tasks created. System running.");
}

// ============================================================
// LOOP — Empty because FreeRTOS tasks handle everything
// ============================================================
void loop() {
  // Nothing here — FreeRTOS tasks handle all work
  // Do not put code here
}
