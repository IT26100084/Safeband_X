#define BLYNK_TEMPLATE_ID "TMPL6i0K_89cw"
#define BLYNK_TEMPLATE_NAME "Safeband X"
#define BLYNK_AUTH_TOKEN "WTIZGnWAn_wH5MDnlvd-lJA9icgUckRc"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>

char ssid[] = "Nish iPhone";
char pass[] = "nishsube1234";

void setup() {
  Serial.begin(115200);
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
}

void loop() {
  Blynk.run();
}

