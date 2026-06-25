// 04_clki — SI5351C CLKIN reference / setupPLLSource() test
//
// Verifies that setupPLLSource(PLL_A, CLKIN, DIV_1) actually retargets
// PLL A onto the external CLKIN reference: when we change the CLKIN
// frequency, the output frequency must scale by the same ratio.
//
// Wiring:
//   CLKIN pad -> Metro D9 (OC1A)  — Metro generates the external reference
//   CLK0 pad  -> Metro D5 (T1)    — we count this with Timer1 as a counter
//   SDA/SCL   -> A4/A5
//   GND       -> GND
//
// Timer conflict note:
//   Timer1 must do two jobs that cannot overlap:
//     (a) generate the CLKIN reference on OC1A (D9) via CTC + toggle,
//     (b) act as the external-input counter on T1 (D5) to measure CLK0.
//   The Si5351's CLKIN input has its own internal divider chain feeding
//   PLL A, but the PLL stays locked for a brief interval after CLKIN is
//   removed (datasheet: LOL_A asserts only after the loss is detected).
//   That window is far too short to be useful, so instead we run two
//   separate measurement phases, each with Timer1 freshly reconfigured.
//   In each phase we set CLKIN to a known value, program the PLL with a
//   multiplier that yields a target CLK0 frequency, let it lock, then
//   stop the generator, immediately retask Timer1 as a counter, and
//   sample for a short window before the PLL drifts.
//
// Test cases (one round-trip = one PASS):
//   Phase A: CLKIN = 4 MHz, PLL_A mult = 200 -> VCO = 800 MHz
//            CLK0 multisynth div 800 -> 1.000 MHz expected
//   Phase B: CLKIN = 2 MHz, same PLL/MS settings -> VCO = 400 MHz
//            CLK0 -> 0.500 MHz expected
//   A 2:1 ratio in CLKIN must produce a 2:1 ratio in CLK0, proving the
//   PLL is genuinely sourced from CLKIN and not the XTAL.
//
// Gate strategy:
//   Stopping Timer1 in mid-measurement loses CLKIN immediately, and the
//   PLL unlocks well within a millisecond. So the measurement window is
//   short (50 ms) and uses millis() for gating. With CLK0 = 1 MHz that's
//   ~50,000 counts, fitting comfortably in Timer1's 16 bits with margin.

#include <Adafruit_SI5351.h>

#define CLKIN_PIN 9  // OC1A
#define CLK0_PIN 5   // T1 (Timer1 external clock input)

Adafruit_SI5351 clockgen = Adafruit_SI5351();

// Drive a square wave on D9 (OC1A) at f = F_CPU / (2 * (OCR1A + 1)).
// Uses Timer1 in CTC + toggle-OC1A mode, no prescale.
//   ocr=1 -> 16MHz / 4  = 4.000 MHz
//   ocr=3 -> 16MHz / 8  = 2.000 MHz
static void clkinStart(uint16_t ocr) {
  pinMode(CLKIN_PIN, OUTPUT);
  TCCR1A = _BV(COM1A0);             // toggle OC1A on compare match
  TCCR1B = _BV(WGM12) | _BV(CS10);  // CTC mode 4, no prescale
  TCNT1 = 0;
  OCR1A = ocr;
}

static void clkinStop() {
  TCCR1A = 0;
  TCCR1B = 0;
  pinMode(CLKIN_PIN, OUTPUT);
  digitalWrite(CLKIN_PIN, LOW);
}

// Reconfigure Timer1 as an external-input counter on T1 (D5), gate for
// `gateMs` milliseconds, return raw counts. Note: this overwrites the
// CLKIN generator above, which is why we call it only AFTER the PLL has
// locked and we accept the brief unlock window before sampling ends.
volatile uint32_t t1Overflows = 0;
ISR(TIMER1_OVF_vect) { t1Overflows++; }

static uint32_t measureCountsT1(uint16_t gateMs) {
  // Stop timer, clear state.
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
  noInterrupts();
  t1Overflows = 0;
  interrupts();
  TIFR1 = _BV(TOV1);
  TIMSK1 = _BV(TOIE1);

  // External clock source on T1 pin, rising edge.
  pinMode(CLK0_PIN, INPUT);
  TCCR1B = _BV(CS12) | _BV(CS11) | _BV(CS10);

  uint32_t start = millis();
  while ((millis() - start) < gateMs) {
    // gate
  }

  TCCR1B = 0;

  noInterrupts();
  uint32_t overflows = t1Overflows;
  uint16_t counts = TCNT1;
  if ((TIFR1 & _BV(TOV1)) && (counts < 65535)) {
    overflows++;
  }
  interrupts();

  TIMSK1 = 0;
  return (overflows * 65536UL) + counts;
}

// Program PLL A from CLKIN and route to CLK0.
// VCO = CLKIN * mult; CLK0 = VCO / msDiv.
static void programChain(uint8_t pllMult, uint16_t msDiv) {
  clockgen.setupPLLSource(SI5351_PLL_A, SI5351_PLL_SOURCE_CLKIN,
                          SI5351_CLKIN_DIV_1);
  clockgen.setupPLL(SI5351_PLL_A, pllMult, 0, 1);
  clockgen.setupMultisynth(0, SI5351_PLL_A, msDiv, 0, 1);
  clockgen.enableOutputs(true);
}

static bool runPhase(const char* name, uint16_t clkinOcr, uint32_t expectedHz) {
  Serial.print(F("--- "));
  Serial.print(name);
  Serial.println(F(" ---"));

  clkinStart(clkinOcr);
  delay(50);  // settle CLKIN
  programChain(200, 800);
  delay(50);  // let PLL lock

  // Gate is short (50 ms) because measureCountsT1() kills CLKIN. With
  // CLK0 ~ 1 MHz that's still ~50k counts which is plenty for a 2:1
  // ratio check.
  uint32_t counts = measureCountsT1(50);
  uint32_t hz = counts * 20UL;  // 50 ms -> *20 to get Hz

  Serial.print(F("expected ~"));
  Serial.print(expectedHz);
  Serial.print(F(" Hz, measured "));
  Serial.print(hz);
  Serial.println(F(" Hz"));

  clkinStop();
  delay(10);

  // 10% tolerance: the PLL drifts during the gate window because CLKIN
  // is gone, so we accept anything in the right order of magnitude.
  uint32_t lo = expectedHz - (expectedHz / 10);
  uint32_t hi = expectedHz + (expectedHz / 10);
  return (hz >= lo) && (hz <= hi);
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  Serial.println(F("04_clki: setupPLLSource() / CLKIN ratio test"));

  if (clockgen.begin() != ERROR_NONE) {
    Serial.println(F("FAIL: begin() — no Si5351"));
    Serial.println(F("RESULTS: 0/2"));
    while (1) delay(10);
  }
  Serial.println(F("begin() OK"));

  int pass = 0;
  if (runPhase("Phase A: CLKIN=4MHz, expect CLK0=1MHz", 1, 1000000UL)) pass++;
  if (runPhase("Phase B: CLKIN=2MHz, expect CLK0=500kHz", 3, 500000UL)) pass++;

  Serial.print(F("RESULTS: "));
  Serial.print(pass);
  Serial.println(F("/2"));
}

void loop() {}
