

/* ─── PIN DEFINITIONS ─────────────────────────────────── */
#define PANIC_BUTTON  33
#define SAFE_BUTTON   3

/* ─── TIMING CONSTANTS ────────────────────────────────── */
const unsigned long DEBOUNCE_DELAY   = 20;     // ms
const unsigned long HOLD_DURATION    = 3000;   // 3 seconds for SOS
const unsigned long CANCEL_WINDOW    = 10000;  // 10 seconds to cancel

/* ─── PANIC BUTTON STATE ──────────────────────────────── */
int  panicLastReading   = HIGH;
int  panicButtonState   = HIGH;
bool panicHeld          = false;
unsigned long panicPressStart    = 0;
unsigned long panicDebounceTime  = 0;

/* ─── SAFE BUTTON STATE ───────────────────────────────── */
int  safeLastReading    = HIGH;
int  safeButtonState    = HIGH;
unsigned long safeDebounceTime   = 0;

/* ─── SOS STATE ───────────────────────────────────────── */
volatile bool sos_active     = false;
unsigned long sos_start_time = 0;   // when SOS was triggered

/* ================================================================
   trigger_sos()
   Called by EITHER the panic button OR the MPU6050 interrupt.
   Source = "BUTTON" or "IMPACT"
   ================================================================ */
void trigger_sos(String source) {
  if (sos_active) return;   // already active — ignore duplicate

  sos_active     = true;
  sos_start_time = millis();

  Serial.println("╔══════════════════════════════╗");
  Serial.println("║   SOS TRIGGERED — " + source + (source == "BUTTON" ? "      ║" : "      ║"));
  Serial.println("╚══════════════════════════════╝");
  Serial.println("Cancel window: 10 seconds");

  // TODO (M1 integration): notify FreeRTOS tasks here
  // xEventGroupSetBits(sosEventGroup, SOS_BIT);
}

/* ================================================================
   cancel_sos()
   Called when safe button pressed within 10 seconds of SOS.
   ================================================================ */
void cancel_sos() {
  sos_active = false;

  Serial.println("╔══════════════════════════════╗");
  Serial.println("║      SOS CANCELLED — SAFE    ║");
  Serial.println("╚══════════════════════════════╝");

  // TODO (M4 integration): send Blynk "Safe" notification
  // Blynk.logEvent("sos_cancel", "Safeband_X — FALSE ALARM. Device is safe.");

  // TODO (M6 integration): stop siren and LED
  // digitalWrite(SIREN_PIN, LOW);
  // digitalWrite(LED_PIN, LOW);
}

/* ================================================================
   readPanicButton()
   Call this every loop iteration.
   Handles debounce and 3-second hold detection.
   ================================================================ */
void readPanicButton() {
  int reading = digitalRead(PANIC_BUTTON);

  // Debounce — reset timer on any state change
  if (reading != panicLastReading) {
    panicDebounceTime = millis();
  }

  if ((millis() - panicDebounceTime) > DEBOUNCE_DELAY) {
    if (reading != panicButtonState) {
      panicButtonState = reading;

      if (panicButtonState == LOW) {
        // Button just pressed — start hold timer
        panicPressStart = millis();
        panicHeld       = false;
        Serial.println("Panic button pressed — hold for 3 seconds...");
      } else {
        // Button released before 3 seconds
        if (!panicHeld) {
          Serial.println("Released too early — not triggered");
        }
        panicPressStart = 0;
        panicHeld       = false;
      }
    }
  }

  // Check 3-second hold
  if (panicButtonState == LOW &&
      !panicHeld &&
      panicPressStart > 0 &&
      (millis() - panicPressStart >= HOLD_DURATION)) {

    panicHeld = true;
    trigger_sos("BUTTON");
  }

  panicLastReading = reading;
}

/* ================================================================
   readSafeButton()
   Call this every loop iteration.
   Only active during the 10-second cancel window after SOS.
   ================================================================ */
void readSafeButton() {
  int reading = digitalRead(SAFE_BUTTON);

  // Debounce
  if (reading != safeLastReading) {
    safeDebounceTime = millis();
  }

  if ((millis() - safeDebounceTime) > DEBOUNCE_DELAY) {
    if (reading != safeButtonState) {
      safeButtonState = reading;

      // Button just pressed
      if (safeButtonState == LOW) {

        // Only cancel if SOS is active AND within 10-second window
        if (sos_active &&
            (millis() - sos_start_time) <= CANCEL_WINDOW) {
          cancel_sos();
        }

        // Pressed outside cancel window — ignore
        else if (sos_active) {
          Serial.println("Cancel window expired — SOS continues");
        }
      }
    }
  }

  safeLastReading = reading;
}

/* ================================================================
   checkCancelWindow()
   Call this every loop iteration.
   Prints a warning when 10-second cancel window closes.
   ================================================================ */
void checkCancelWindow() {
  static bool windowClosed = false;

  if (sos_active && !windowClosed) {
    unsigned long elapsed = millis() - sos_start_time;

    if (elapsed > CANCEL_WINDOW) {
      windowClosed = true;
      Serial.println("Cancel window closed — SOS is now permanent");
      Serial.println("Full SOS sequence running...");
    }
  }

  // Reset flag when SOS is cleared
  if (!sos_active) {
    windowClosed = false;
  }
}

/* ================= SETUP =================== */
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(PANIC_BUTTON, INPUT_PULLUP);
  pinMode(SAFE_BUTTON,  INPUT_PULLUP);

  Serial.println("Safeband_X — Member 1");
  Serial.println("Panic button : GPIO 33 — hold 3 seconds for SOS");
  Serial.println("Safe button  : GPIO 3  — press within 10s to cancel");
  Serial.println("Ready.");
}

/* ================= LOOP ==================== */
void loop() {
  readPanicButton();
  readSafeButton();
  checkCancelWindow();

  delay(10);  // small yield — replace with vTaskDelay when FreeRTOS added
}