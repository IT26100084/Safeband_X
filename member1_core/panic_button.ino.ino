#define PANIC_BUTTON 33

unsigned long buttonPressStart = 0;
unsigned long lastDebounceTime = 0;

const unsigned long debounceDelay = 20;

bool lastButtonReading = HIGH;
bool buttonState = HIGH;
bool buttonHeld = false;

void setup() {
  Serial.begin(115200);

  // Enable internal pull-up resistor
  pinMode(PANIC_BUTTON, INPUT_PULLUP);
}

void loop() {

  // Read current button state
  bool reading = digitalRead(PANIC_BUTTON);

  // Check if button state changed
  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
  }

  // Only accept the change if stable for 20ms
  if ((millis() - lastDebounceTime) > debounceDelay) {

    // Confirm stable state change
    if (reading != buttonState) {
      buttonState = reading;

      // Button pressed
      if (buttonState == LOW) {
        buttonPressStart = millis();
        buttonHeld = false;
      }

      // Button released
      else {
        buttonPressStart = 0;
        buttonHeld = false;
      }
    }
  }

  // Detect 3-second hold
  if (buttonState == LOW &&
      !buttonHeld &&
      (millis() - buttonPressStart >= 3000)) {

    Serial.println("SOS TRIGGERED — BUTTON");
    buttonHeld = true;
  }

  // Save current reading for next loop
  lastButtonReading = reading;
}