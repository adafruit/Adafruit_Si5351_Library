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
  while (!Serial) {
    delay(10);
  }

  Serial.println("Si5351 01_i2c_begin test");

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
