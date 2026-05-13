#include <WiFi.h>
#include <driver/i2s.h>

#define WS   26
#define SCK  25
#define SD   35

int frameCount = 0;

void setup() {
  Serial.begin(115200);

  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .dma_buf_count = 4,
    .dma_buf_len = 512
  };

  i2s_pin_config_t pins = {SCK, WS, -1, SD};

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);

  Serial.println("================================");
  Serial.println("Simulation Started");
  Serial.println(" Audio + Video System Active");
  Serial.println("================================");
}

void loop() {

  int audioLevel = random(100, 1000);

  Serial.print("Audio Level: ");
  Serial.println(audioLevel);

  frameCount++;

  Serial.print("Video Frame Captured: ");
  Serial.println(frameCount);

  Serial.println("----------------------------");

  delay(1000);
}
