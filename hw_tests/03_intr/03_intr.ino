// 03_intr — SI5351C !INTR hardware interrupt pin test
//
// Verifies the open-drain, active-low INTR pin asserts on loss-of-CLKIN
// and releases when a valid CLKIN reference is present.
//
// Wiring:
//   INTR pad  -> Metro D2 (INT0), 4.7k pullup to 3V3 (open-drain, active LOW)
//   CLKIN pad -> Metro D9 (OC1A)  — Metro generates the external reference
//   SDA/SCL   -> A4/A5
//   GND       -> GND
//
// Mask note (datasheet): a SET mask bit = that interrupt is IGNORED. We must
// mask every source EXCEPT LOS_CLKIN, else an unused-PLL fault (e.g. LOL_B)
// holds INTR low regardless of CLKIN. Mask value 0xEF = ignore all but bit4.

#include <Adafruit_SI5351.h>

#define INTR_PIN 2
#define CLKIN_PIN 9 // OC1A

Adafruit_SI5351 clockgen = Adafruit_SI5351();

// Drive ~4 MHz on D9 (OC1A) via Timer1 hardware toggle as a CLKIN reference.
static void clkinStart() {
  pinMode(CLKIN_PIN, OUTPUT);
  TCCR1A = _BV(COM1A0);            // toggle OC1A on compare match
  TCCR1B = _BV(WGM12) | _BV(CS10); // CTC, no prescale
  OCR1A = 1;                       // toggle every 2 cycles -> 16MHz/4 = 4MHz
}

static void clkinStop() {
  TCCR1A = 0;
  TCCR1B = 0;
  pinMode(CLKIN_PIN, OUTPUT);
  digitalWrite(CLKIN_PIN, LOW); // hold low, not floating
}

void setup() {
  Serial.begin(115200);
  Serial.println(F("03_intr: INTR hardware interrupt test"));

  pinMode(INTR_PIN, INPUT_PULLUP); // external 4.7k also present

  if (clockgen.begin() != ERROR_NONE) {
    Serial.println(F("FAIL: begin() — no Si5351"));
    Serial.println(F("RESULTS: 0/1"));
    while (1)
      delay(10);
  }
  Serial.println(F("begin() OK"));

  // Enable only the LOS_CLKIN interrupt (ignore all others).
  clockgen.setInterruptMask(0xEF);
  clockgen.clearStickyStatus();

  int pass = 0, total = 2;

  // Phase 1: valid CLKIN present -> no fault -> INTR should be HIGH.
  Serial.println(F("Enabling CLKIN reference on D9..."));
  clkinStart();
  delay(50);
  clockgen.clearStickyStatus();
  delay(50);
  int withClkin = digitalRead(INTR_PIN);
  Serial.print(F("With CLKIN: INTR = "));
  Serial.println(withClkin ? F("HIGH") : F("LOW"));
  if (withClkin == HIGH) {
    pass++;
  } else {
    Serial.println(F("  expected HIGH (no fault)"));
  }

  // Phase 2: stop CLKIN -> LOS_CLKIN fault -> INTR should be LOW.
  Serial.println(F("Stopping CLKIN..."));
  clkinStop();
  delay(50);
  clockgen.clearStickyStatus();
  delay(50);
  int noClkin = digitalRead(INTR_PIN);
  Serial.print(F("No CLKIN: INTR = "));
  Serial.println(noClkin ? F("HIGH") : F("LOW"));
  uint8_t st = 0;
  clockgen.readDeviceStatus(&st);
  Serial.print(F("status reg = 0x"));
  Serial.println(st, HEX);
  if (noClkin == LOW && (st & SI5351_STATUS_LOS_CLKIN)) {
    pass++;
  } else {
    Serial.println(F("  expected LOW + LOS_CLKIN set"));
  }

  Serial.print(F("RESULTS: "));
  Serial.print(pass);
  Serial.print(F("/"));
  Serial.println(total);
}

void loop() {}
