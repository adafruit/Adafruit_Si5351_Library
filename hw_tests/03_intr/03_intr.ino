// 03_intr — SI5351C !INTR hardware interrupt pin test (ESP32 V2 port).
//
// Verifies the open-drain, active-low INTR pin asserts on loss-of-CLKIN
// and releases when a valid CLKIN reference is present.
//
// Board: Adafruit Feather ESP32 V2 (FQBN esp32:esp32:adafruit_feather_esp32_v2)
//
// Wiring:
//   Si5351 SDA   -> Feather SDA   (GPIO22)
//   Si5351 SCL   -> Feather SCL   (GPIO20)
//   Si5351 INTR  -> Feather A0    (GPIO26), 4.7k external pullup to 3V3
//   Si5351 CLKIN -> Feather A6    (GPIO14)  -- LEDC PWM reference
//   Si5351 VIN   -> 3V3, GND -> GND
//
// Mask note (datasheet): a SET mask bit means that interrupt is IGNORED.
// 0xEF = ignore all except bit 4 (LOS_CLKIN).

#include <Adafruit_SI5351.h>
#include <Wire.h>

#define INTR_PIN 26  // A0
#define CLKIN_PIN 14 // A6

Adafruit_SI5351 clockgen = Adafruit_SI5351();

static void printStatus(const __FlashStringHelper* tag) {
  uint8_t st = 0;
  clockgen.readDeviceStatus(&st);
  int intr = digitalRead(INTR_PIN);
  Serial.print(tag);
  Serial.print(F("  INTR="));
  Serial.print(intr ? F("HIGH") : F("LOW"));
  Serial.print(F("  status=0x"));
  Serial.print(st, HEX);
  Serial.print(F("  LOS_CLKIN="));
  Serial.println((st & SI5351_STATUS_LOS_CLKIN) ? F("1") : F("0"));
}

static void clkinDriveStatic(int level) {
  pinMode(CLKIN_PIN, OUTPUT);
  digitalWrite(CLKIN_PIN, level);
}

static void clkinLedc(uint32_t freqHz, uint8_t resBits) {
  bool ok = ledcAttach(CLKIN_PIN, freqHz, resBits);
  ledcWrite(CLKIN_PIN, 1 << (resBits - 1)); // 50% duty
  uint32_t actual = ledcReadFreq(CLKIN_PIN);
  Serial.print(F("  ledcAttach="));
  Serial.print(ok ? F("OK") : F("FAIL"));
  Serial.print(F("  freq="));
  Serial.print(actual);
  Serial.println(F(" Hz"));
}

static void clkinLedcDetach() {
  ledcDetach(CLKIN_PIN);
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println(F("03_intr DIAGNOSTIC: INTR + LOS_CLKIN sweep"));

  pinMode(NEOPIXEL_I2C_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_I2C_POWER, HIGH);
  delay(10);

  pinMode(INTR_PIN, INPUT_PULLUP);

  if (clockgen.begin() != ERROR_NONE) {
    Serial.println(F("FAIL: begin() — no Si5351"));
    while (1) {
      delay(10);
    }
  }
  Serial.println(F("begin() OK"));

  clockgen.setInterruptMask(0xEF);
  clockgen.clearStickyStatus();
  delay(50);

  // Baseline: CLKIN pin not driven by us at all
  pinMode(CLKIN_PIN, INPUT);
  clockgen.clearStickyStatus();
  delay(100);
  printStatus(F("baseline (CLKIN floating)"));

  // Drive CLKIN HIGH statically — LOS should stay asserted (no edges)
  clkinDriveStatic(HIGH);
  clockgen.clearStickyStatus();
  delay(100);
  printStatus(F("static HIGH"));

  // Drive CLKIN LOW statically — LOS should stay asserted
  clkinDriveStatic(LOW);
  clockgen.clearStickyStatus();
  delay(100);
  printStatus(F("static LOW"));

  // Try 100 kHz LEDC
  Serial.println(F("--- 100 kHz LEDC ---"));
  clkinLedc(100000UL, 8);
  delay(50);
  clockgen.clearStickyStatus();
  delay(200);
  printStatus(F("100 kHz"));
  clkinLedcDetach();
  clkinDriveStatic(LOW);
  delay(50);

  // Try 1 MHz LEDC
  Serial.println(F("--- 1 MHz LEDC ---"));
  clkinLedc(1000000UL, 5);
  delay(50);
  clockgen.clearStickyStatus();
  delay(200);
  printStatus(F("1 MHz"));
  clkinLedcDetach();
  clkinDriveStatic(LOW);
  delay(50);

  // Try 8 MHz LEDC (matches Si5351 PLL CLKIN spec)
  Serial.println(F("--- 8 MHz LEDC ---"));
  clkinLedc(8000000UL, 3);
  delay(50);
  clockgen.clearStickyStatus();
  delay(200);
  printStatus(F("8 MHz"));
  clkinLedcDetach();
  clkinDriveStatic(LOW);
  delay(50);

  // Final: back to silence
  Serial.println(F("--- back to silence ---"));
  clockgen.clearStickyStatus();
  delay(100);
  printStatus(F("silent"));

  Serial.println(F("done."));
}

void loop() {}
