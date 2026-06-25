// 03_intr — INTR pin + LOS_CLKIN sticky-status, ESP32 V2 Feather
//
// Hardware:
//   INTR  (Si5351) -> GPIO26 (Feather A0) via INPUT_PULLUP (open-drain,
//   active-low) CLKIN (Si5351) -> GPIO14 (Feather "14"/A6), driven by LEDC I2C
//   via STEMMA QT (SDA=22, SCL=20)
//
// Test:
//   Phase 1 (no CLKIN): LOS_CLKIN sticky bit must be set, INTR must be LOW
//   (asserted) Phase 2 (8 MHz CLKIN via LEDC): clear sticky; LOS_CLKIN must
//   stay clear, INTR HIGH
//
// Note: empirically the Si5351 LOS_CLKIN detector has a freq threshold between
// 1 and 8 MHz. 100kHz/1MHz LEDC stimuli do NOT clear LOS. 8 MHz does.

#include <Adafruit_SI5351.h>

Adafruit_SI5351 clockgen;

static const uint8_t CLKIN_PIN = 14;
static const uint8_t INTR_PIN = 26;
static const uint8_t LOS_CLKIN_BIT = 0x10; // reg 1 bit 4

static bool phase(const __FlashStringHelper* label, bool wantLosClkin,
                  bool wantIntrAsserted) {
  uint8_t status = 0;
  uint8_t sticky = 0;
  clockgen.readDeviceStatus(&status);
  clockgen.readStickyStatus(&sticky);
  bool losClkin = (sticky & LOS_CLKIN_BIT) != 0;
  bool intrAsserted = (digitalRead(INTR_PIN) == LOW);
  bool ok = (losClkin == wantLosClkin) && (intrAsserted == wantIntrAsserted);
  Serial.print(label);
  Serial.print(F("  status=0x"));
  Serial.print(status, HEX);
  Serial.print(F("  sticky=0x"));
  Serial.print(sticky, HEX);
  Serial.print(F("  LOS_CLKIN_sticky="));
  Serial.print(losClkin);
  Serial.print(F("  INTR_asserted="));
  Serial.print(intrAsserted);
  Serial.print(F("  -> "));
  Serial.println(ok ? F("PASS") : F("FAIL"));
  return ok;
}

void setup() {
  Serial.begin(115200);
  Serial.println(F("03_intr: INTR + LOS_CLKIN sticky test (ESP32 V2)"));

  pinMode(INTR_PIN, INPUT_PULLUP);

  if (clockgen.begin() != ERROR_NONE) {
    Serial.println(F("FAIL: begin() — no Si5351"));
    while (1) {
      delay(10);
    }
  }

  // Unmask all interrupt sources so INTR pin reflects every sticky flag.
  clockgen.setInterruptMask(0x00);

  uint8_t pass = 0;

  // Phase 1: CLKIN floating, no stimulus
  pinMode(CLKIN_PIN, INPUT);
  clockgen.clearStickyStatus();
  delay(100);
  if (phase(F("Phase1 (no CLKIN)"), /*wantLosSticky=*/true,
            /*wantIntrAsserted=*/true)) {
    pass++;
  }

  // Phase 2: drive 8 MHz CLKIN via LEDC
  bool ledcOk =
      ledcAttach(CLKIN_PIN, 8000000UL, 3); // 8 MHz, 3-bit res (8 steps)
  ledcWrite(CLKIN_PIN, 4);                 // ~50% duty
  delay(50);
  // Clear sticky AFTER clock is running so LOS_CLKIN can stay clear
  clockgen.clearStickyStatus();
  delay(200);
  Serial.print(F("ledcAttach="));
  Serial.print(ledcOk ? F("OK") : F("FAIL"));
  Serial.print(F("  ledcReadFreq="));
  Serial.println(ledcReadFreq(CLKIN_PIN));
  if (phase(F("Phase2 (8 MHz CLKIN)"), /*wantLosSticky=*/false,
            /*wantIntrAsserted=*/false)) {
    pass++;
  }
  ledcDetach(CLKIN_PIN);

  Serial.print(F("RESULT: "));
  Serial.print(pass);
  Serial.println(F("/2"));
}

void loop() {}
