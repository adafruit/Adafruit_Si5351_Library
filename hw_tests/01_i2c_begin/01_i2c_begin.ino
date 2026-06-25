// 01_i2c_begin — Si5351 I2C presence + begin() smoke test (ESP32 V2 port).
//
// Board: Adafruit Feather ESP32 V2 (FQBN esp32:esp32:adafruit_feather_esp32_v2)
//
// Wiring:
//   Si5351 SDA -> Feather SDA (GPIO22)
//   Si5351 SCL -> Feather SCL (GPIO20)
//   Si5351 VIN -> Feather 3V3, GND -> GND
//
// Feather V2 quirk: GPIO2 (NEOPIXEL_I2C_POWER) gates the on-board STEMMA QT
// I2C power rail. We drive it HIGH before Wire.begin() so the bus is powered
// whether the user wired via QT or to the side header.

#include <Adafruit_SI5351.h>
#include <Wire.h>

Adafruit_SI5351 clockgen = Adafruit_SI5351();

static void printHexAddr(uint8_t addr) {
  Serial.print("0x");
  if (addr < 0x10) {
    Serial.print("0");
  }
  Serial.print(addr, HEX);
}

void setup() {
  int passed = 0;
  bool foundSi5351 = false;

  Serial.begin(115200);
  // Feather V2 uses a CP2102 UART bridge, so Serial is always ready; small
  // delay just lets the host open the port before we start printing.
  delay(2000);

  Serial.println("Si5351 01_i2c_begin test (ESP32 V2)");

  // Enable I2C power rail (Feather V2 STEMMA QT power gate).
  pinMode(NEOPIXEL_I2C_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_I2C_POWER, HIGH);
  delay(10);

  Wire.begin();
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("Found ");
      printHexAddr(addr);
      Serial.println();
      if (addr == 0x60) {
        foundSi5351 = true;
      }
    }
  }

  if (foundSi5351) {
    Serial.println("PASS: Si5351 present at 0x60");
    passed++;
  } else {
    Serial.println("FAIL: 0x60 not found");
  }

  err_t rc = clockgen.begin();
  if (rc == ERROR_NONE) {
    Serial.println("PASS: begin() ok");
    passed++;
  } else {
    Serial.print("FAIL: begin() rc=");
    Serial.println((int)rc);
  }

  Serial.print("RESULTS: ");
  Serial.print(passed);
  Serial.println("/2 tests passed");
}

void loop() {
  delay(1000);
}
