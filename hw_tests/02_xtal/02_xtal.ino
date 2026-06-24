#include <Adafruit_SI5351.h>

Adafruit_SI5351 clockgen;

volatile uint32_t timer1Overflows = 0;

ISR(TIMER1_OVF_vect) {
  timer1Overflows++;
}

uint32_t measureFrequencyHz1s() {
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;

  noInterrupts();
  timer1Overflows = 0;
  interrupts();

  TIFR1 = _BV(TOV1);
  TIMSK1 = _BV(TOIE1);

  TCCR1B = _BV(CS12) | _BV(CS11) | _BV(CS10);

  uint32_t start = millis();
  while ((millis() - start) < 1000) {
    // Gate for exactly 1000 ms.
  }

  TCCR1B = 0;

  noInterrupts();
  uint32_t overflows = timer1Overflows;
  uint16_t counts = TCNT1;
  if ((TIFR1 & _BV(TOV1)) && (counts < 65535)) {
    overflows++;
  }
  interrupts();

  TIMSK1 = 0;

  return (overflows * 65536UL) + counts;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  if (clockgen.begin() != ERROR_NONE) {
    Serial.println("FAIL: begin() error");
    Serial.println("RESULTS: 0/1");
    while (1) {
      delay(10);
    }
  }

  clockgen.setupPLL(SI5351_PLL_A, 36, 0, 1);
  clockgen.setupMultisynth(0, SI5351_PLL_A, 900, 0, 1);
  clockgen.enableOutputs(true);

  delay(10);

  uint32_t measured = measureFrequencyHz1s();
  bool pass = (measured >= 950000UL) && (measured <= 1050000UL);

  Serial.println("Target: 1000000 Hz");
  Serial.print("Measured: ");
  Serial.print(measured);
  Serial.println(" Hz");

  if (pass) {
    Serial.println("PASS: within tolerance");
    Serial.println("RESULTS: 1/1");
  } else {
    Serial.println("FAIL: out of tolerance");
    Serial.println("RESULTS: 0/1");
  }
}

void loop() {}
