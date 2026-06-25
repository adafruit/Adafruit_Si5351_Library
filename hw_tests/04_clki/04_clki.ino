// 04_clki — Verify setupPLLSource() routes CLKIN to PLL_A
//
// BENCH WIRING (note the D9 -> D11 move from earlier sketches):
//   Metro/Uno D11 (OC2A) -> Si5351 CLKIN     (reference, Timer2-generated)
//   Si5351   CLK0        -> Metro/Uno D5 (T1) (measurement, Timer1 counter)
//   Si5351   XTAL pads    : 25 MHz crystal (still populated, unused this test)
//
// Timer1 generates nothing here -- it is the T1 counter on D5.
// Timer2 generates CLKIN on D11 in CTC mode (OC2A toggle).
// Timer0 stays free for millis()/delay().
//
// Plan: drive CLKIN at 8 MHz (Timer2 OC2A max with prescaler=1),
// lock PLL_A at 800 MHz off CLKIN (multiplier 100), set
// CLK0 = PLL_A / 800 = 1.000 MHz, count CLK0 edges on T1 over a
// 100 ms gate, assert within +/- 1%.
// Then drop CLKIN to 4 MHz -- PLL_A reprograms to mult 200 (still
// 800 MHz), CLK0 stays 1 MHz. If both phases hit ~1 MHz CLKIN
// selection is real. If phase 1 passes and phase 2 fails, the
// chip's CLKIN lock floor sits between 4 and 8 MHz (datasheet
// says 10 MHz min, we're probing margin).

#include <Adafruit_SI5351.h>

Adafruit_SI5351 clockgen;

// --- Timer2 CTC on OC2A (D11) ---------------------------------------------
// f_out = F_CPU / (2 * N * (OCR2A + 1)), N = prescaler (1 here)
static void clkin_start(uint8_t ocr2a) {
  pinMode(11, OUTPUT);
  TCCR2A = 0;
  TCCR2B = 0;
  TCNT2 = 0;
  OCR2A = ocr2a;
  // CTC mode (WGM21=1), toggle OC2A on compare match (COM2A0=1)
  TCCR2A = _BV(WGM21) | _BV(COM2A0);
  TCCR2B = _BV(CS20);  // prescaler = 1
}

// --- Timer1 as external counter on T1 (D5) --------------------------------
static void counter_reset(void) {
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
}
static void counter_start_t1_rising(void) {
  // CS1[2:0] = 111 -> external clock on T1, rising edge
  TCCR1B = _BV(CS12) | _BV(CS11) | _BV(CS10);
}
static uint16_t counter_stop(void) {
  TCCR1B = 0;
  return TCNT1;
}

// Sample CLK0 frequency over `gate_ms` using Timer1/T1.
// Returns measured Hz. 16-bit counter limits us to ~65 kHz at 1 s gate,
// so use a short gate at high freqs.
static uint32_t measure_clk0_hz(uint16_t gate_ms) {
  counter_reset();
  // Align to a fresh millis() tick to reduce gate jitter.
  uint32_t t0 = millis();
  while (millis() == t0) { /* spin */
  }
  t0 = millis();
  counter_start_t1_rising();
  while ((uint32_t)(millis() - t0) < gate_ms) { /* spin */
  }
  uint16_t ticks = counter_stop();
  // Hz = ticks * (1000 / gate_ms)
  return (uint32_t)ticks * 1000UL / gate_ms;
}

static bool within_pct(uint32_t got, uint32_t want, uint8_t pct) {
  uint32_t tol = (uint64_t)want * pct / 100ULL;
  return (got + tol >= want) && (got <= want + tol);
}

void setup(void) {
  Serial.begin(115200);
  while (!Serial) { /* wait USB */
  }
  Serial.println(F("04_clki: PLL source = CLKIN test"));

  if (clockgen.begin() != ERROR_NONE) {
    Serial.println(F("FAIL: clockgen.begin()"));
    while (1) { /* halt */
    }
  }
  Serial.println(F("Si5351 init OK"));

  // CLK0 driven by MS0 driven by PLL_A. PLL_A source = CLKIN.
  // Enable CLK0 output enable (handled by setupMultisynth).
}

void loop(void) {
  // ---- Phase 1: CLKIN = 8 MHz, target CLK0 = 1 MHz ----
  Serial.println(F("\n-- Phase 1: CLKIN=8MHz, mult=100, CLK0=1MHz --"));
  clkin_start(0);  // 8.000 MHz on D11 (F_CPU/2)
  delay(5);

  if (clockgen.setupPLLSource(SI5351_PLL_A, SI5351_PLL_SOURCE_CLKIN,
                              SI5351_CLKIN_DIV_1) != ERROR_NONE) {
    Serial.println(F("FAIL: setupPLLSource phase 1"));
    while (1) {
    }
  }
  // PLL_A = CLKIN * 100 = 800 MHz
  clockgen.setupPLL(SI5351_PLL_A, 100, 0, 1);
  // MS0 divider = 800 -> CLK0 = 1.000 MHz
  clockgen.setupMultisynth(0, SI5351_PLL_A, 800, 0, 1);
  clockgen.enableOutputs(true);

  delay(50);                            // let PLL lock + outputs settle
  uint32_t hz1 = measure_clk0_hz(100);  // 100 ms gate -> ~100k counts
  Serial.print(F("CLK0 measured: "));
  Serial.print(hz1);
  Serial.println(F(" Hz (want 1000000)"));
  bool ok1 = within_pct(hz1, 1000000UL, 1);
  Serial.println(ok1 ? F("PHASE 1 PASS") : F("PHASE 1 FAIL"));

  // ---- Phase 2: CLKIN = 4 MHz, target CLK0 = 1 MHz (mult=200) ----
  Serial.println(F("\n-- Phase 2: CLKIN=4MHz, mult=200, CLK0=1MHz --"));
  clockgen.enableOutputs(false);
  clkin_start(1);  // 4.000 MHz on D11
  delay(5);

  if (clockgen.setupPLLSource(SI5351_PLL_A, SI5351_PLL_SOURCE_CLKIN,
                              SI5351_CLKIN_DIV_1) != ERROR_NONE) {
    Serial.println(F("FAIL: setupPLLSource phase 2"));
    while (1) {
    }
  }
  clockgen.setupPLL(SI5351_PLL_A, 200, 0, 1);  // 4 MHz * 200 = 800 MHz
  clockgen.setupMultisynth(0, SI5351_PLL_A, 800, 0, 1);
  clockgen.enableOutputs(true);

  delay(50);
  uint32_t hz2 = measure_clk0_hz(100);
  Serial.print(F("CLK0 measured: "));
  Serial.print(hz2);
  Serial.println(F(" Hz (want 1000000)"));
  bool ok2 = within_pct(hz2, 1000000UL, 1);
  Serial.println(ok2 ? F("PHASE 2 PASS") : F("PHASE 2 FAIL"));

  Serial.println((ok1 && ok2) ? F("\n== 04_clki PASS ==")
                              : F("\n== 04_clki FAIL =="));
  while (1) { /* halt */
  }
}
