#include <Arduino.h>
#include <Wire.h>

constexpr uint8_t SDA_PIN = 8; //blue
constexpr uint8_t SCL_PIN = 9; //yellow

constexpr uint8_t MPU_address = 0x68;
constexpr uint8_t MPU_address = 0x3c;

void setup() {
  Serial.begin(115200);
  delay(300);
  Wire.begin(SDA_PIN, SCL_PIN);

  Serial.println("\nScanning the I2C bus...");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {        // 0 = someone ACKed
      Serial.printf("  I2C device found at 0x%02X\n", addr);
      found++;
    }
  }
  Serial.println(found ? "Done." : "Nothing found — check wiring, power, and GND.");
}

void loop() {}