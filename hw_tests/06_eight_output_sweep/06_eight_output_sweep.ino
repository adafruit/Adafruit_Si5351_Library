// Adafruit Si5351C eight-output sweep, Feather ESP32 V2.
//
// Wiring: STEMMA QT for power/I2C, OEB -> A1 (25).
// CLK0..CLK7 -> 27, 33, 32, 15, A2 (34), A3 (39), A4 (36), A5 (4).
// CLKIN -> 14 and INTR -> A0 may remain connected; this test does not use them.
//
// Measures one enabled output at a time using one hardware pulse counter,
// routed to each input in turn. Active clocks use a one-second busy gate;
// quiet inputs use a 100 ms busy gate. Elapsed time is measured in microseconds.
// Every point checks frequency, silence on the other seven pins, and silence
// on all eight pins after enableOutputs(false). Both PLL sources are tested.
// CLK0..5 additionally exercise all eight R dividers and fractional division.
// CLK6/7 use even integer dividers only, as supported by the library.
// This checks 15.625 kHz..12.5 MHz; RF maximum frequency, jitter and drive
// strength require separate instruments. The frequency tolerance is 2%.

#include <Adafruit_SI5351.h>
#include <math.h>
#include "driver/pulse_cnt.h"

const uint16_t CLOCK_PINS[8] = {27, 33, 32, 15, A2, A3, A4, A5};
const uint16_t OEB_PIN = A1;
const uint16_t INTEGER_DIVIDERS[4] = {240, 180, 120, 60};
const si5351RDiv_t R_DIVIDERS[8] = {
    SI5351_R_DIV_1, SI5351_R_DIV_2, SI5351_R_DIV_4, SI5351_R_DIV_8,
    SI5351_R_DIV_16, SI5351_R_DIV_32, SI5351_R_DIV_64, SI5351_R_DIV_128};
const uint16_t R_FACTORS[8] = {1, 2, 4, 8, 16, 32, 64, 128};
const uint32_t ACTIVE_GATE_US = 1000000;
const uint32_t SILENT_GATE_US = 100000;
const float FREQUENCY_TOLERANCE = 0.02;
const float SILENT_LIMIT_HZ = 20;

Adafruit_SI5351 clockgen;
pcnt_unit_handle_t counterUnit = NULL;
pcnt_channel_handle_t counterChannel = NULL;
bool deviceReady = false;
uint16_t passedPoints = 0;

void setup() {
  Serial.begin(115200);
  delay(250);

  Serial.println("Adafruit Si5351C eight-output frequency sweep");

  // OEB high disables the clocks while the fixture is initialized.
  digitalWrite(OEB_PIN, HIGH);
  pinMode(OEB_PIN, OUTPUT);
  if (clockgen.begin() != ERROR_NONE) {
    clearOutputsAndHalt("FAIL: begin()");
  }
  deviceReady = true;
  Serial.println("PASS: begin()");
  requireClock(clockgen.setOEBPinMask(0), "FAIL: OEB mask");

  for (uint8_t output = 0; output < 8; output++) {
    pinMode(CLOCK_PINS[output], INPUT);
    Serial.print("CLK");
    Serial.print(output);
    Serial.print(" input on GPIO ");
    Serial.println(CLOCK_PINS[output]);
  }
  initializeCounter();
  Serial.println("PASS: hardware counter initialized");

  // Use different VCO frequencies to expose an incorrect PLL selection.
  const si5351PLL_t plls[2] = {SI5351_PLL_A, SI5351_PLL_B};
  const uint32_t pllFrequencies[2] = {600000000, 750000000};

  for (uint8_t pllIndex = 0; pllIndex < 2; pllIndex++) {
    Serial.println();
    Serial.print("Testing PLL ");
    Serial.println((char)('A' + pllIndex));

    for (uint8_t output = 0; output < 8; output++) {
      for (uint8_t point = 0; point < 4; point++) {
        uint16_t divider = INTEGER_DIVIDERS[point];
        float expectedHz = (float)pllFrequencies[pllIndex] / divider;
        runPoint(output, plls[pllIndex], divider,
                 0, 1, SI5351_R_DIV_1, expectedHz);
      }

      if (output < 6) {
        // A 2 MHz MultiSynth output divided by 1..128 gives 2 MHz..15.625 kHz.
        uint16_t divider = pllFrequencies[pllIndex] / 2000000;
        for (uint8_t rIndex = 0; rIndex < 8; rIndex++) {
          float expectedHz = 2000000.0 / R_FACTORS[rIndex];
          runPoint(output, plls[pllIndex], divider,
                   0, 1, R_DIVIDERS[rIndex], expectedHz);
        }

        float expectedHz = pllFrequencies[pllIndex] / 120.5;
        runPoint(output, plls[pllIndex], 120,
                 1, 2, SI5351_R_DIV_1, expectedHz);
      }
    }
  }

  requireClock(clockgen.enableOutputs(false), "FAIL: final disable");
  digitalWrite(OEB_PIN, HIGH);
  Serial.println();
  Serial.print("RESULTS: ");
  Serial.print(passedPoints);
  Serial.println("/172 points passed");
  Serial.println("All clock outputs disabled; OEB HIGH.");
}

void loop() {
  delay(1000);
}

void clearOutputsAndHalt(const char* message) {
  digitalWrite(OEB_PIN, HIGH);
  Serial.println(message);
  if (deviceReady) {
    if (clockgen.enableOutputs(false) != ERROR_NONE) {
      Serial.println("FAIL: could not disable outputs over I2C; OEB is HIGH");
    }
  }
  Serial.print("RESULTS: FAIL after ");
  Serial.print(passedPoints);
  Serial.println(" passed points");
  while (true) {
    delay(1000);
  }
}

void requireClock(err_t result, const char* message) {
  if (result != ERROR_NONE) {
    clearOutputsAndHalt(message);
  }
}

void requireCounter(esp_err_t result, const char* message) {
  if (result != ESP_OK) {
    Serial.println(esp_err_to_name(result));
    clearOutputsAndHalt(message);
  }
}

void initializeCounter() {
  pcnt_unit_config_t unitConfig = {};
  unitConfig.low_limit = -10000;
  unitConfig.high_limit = 10000;
  unitConfig.flags.accum_count = true;
  requireCounter(pcnt_new_unit(&unitConfig, &counterUnit),
                 "FAIL: allocate counter");
  requireCounter(pcnt_unit_add_watch_point(counterUnit, 10000),
                 "FAIL: positive overflow watch point");
  requireCounter(pcnt_unit_add_watch_point(counterUnit, -10000),
                 "FAIL: negative overflow watch point");
}

float measureFrequency(uint8_t output, uint32_t gateUs) {
  // Reuse the same counter; changing its input requires the disabled state.
  if (counterChannel != NULL) {
    requireCounter(pcnt_unit_disable(counterUnit), "FAIL: disable counter");
    requireCounter(pcnt_del_channel(counterChannel), "FAIL: release counter input");
    counterChannel = NULL;
  }

  pcnt_chan_config_t channelConfig = {};
  channelConfig.edge_gpio_num = CLOCK_PINS[output];
  channelConfig.level_gpio_num = -1;
  // The ESP-IDF driver attempts a pull-up on this input. GPIO34/36/39 do not
  // have pull-ups, so it logs a warning; their digital input paths still work.
  requireCounter(pcnt_new_channel(counterUnit, &channelConfig, &counterChannel),
                 "FAIL: allocate counter channel");
  requireCounter(pcnt_channel_set_edge_action(counterChannel,
                 PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD),
                 "FAIL: configure rising edges");
  requireCounter(pcnt_channel_set_level_action(counterChannel,
                 PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_KEEP),
                 "FAIL: configure counter level");
  requireCounter(pcnt_unit_enable(counterUnit), "FAIL: enable counter");
  requireCounter(pcnt_unit_clear_count(counterUnit), "FAIL: clear counter");
  uint32_t startUs = micros();
  requireCounter(pcnt_unit_start(counterUnit), "FAIL: start counter");
  while (micros() - startUs < gateUs) {
    // Keep the same busy-gate behavior as the verified CLK0 hardware test.
  }
  requireCounter(pcnt_unit_stop(counterUnit), "FAIL: stop counter");
  uint32_t elapsedUs = micros() - startUs;
  int count = 0;
  requireCounter(pcnt_unit_get_count(counterUnit, &count), "FAIL: read counter");
  if (count < 0 || elapsedUs == 0) {
    clearOutputsAndHalt("FAIL: invalid counter measurement");
  }
  return (double)count * 1000000 / elapsedUs;
}

void runPoint(uint8_t output, si5351PLL_t pll,
              uint16_t divider, uint32_t numerator, uint32_t denominator,
              si5351RDiv_t rDivider, float expectedHz) {
  // begin() powers down every output driver, so only this channel is active.
  requireClock(clockgen.begin(), "FAIL: reinitialize outputs");
  requireClock(clockgen.setupPLLSource(SI5351_PLL_A, SI5351_PLL_SOURCE_XTAL),
               "FAIL: PLL A crystal source");
  requireClock(clockgen.setupPLLSource(SI5351_PLL_B, SI5351_PLL_SOURCE_XTAL),
               "FAIL: PLL B crystal source");
  // Keep both PLLs configured so selecting the wrong PLL cannot pass silently.
  requireClock(clockgen.setupPLLInt(SI5351_PLL_A, 24), "FAIL: PLL A setup");
  requireClock(clockgen.setupPLLInt(SI5351_PLL_B, 30), "FAIL: PLL B setup");
  if (output < 6) {
    requireClock(clockgen.setupRdiv(output, rDivider), "FAIL: R divider");
    requireClock(clockgen.setupMultisynth(output, pll, divider, numerator,
                                        denominator), "FAIL: MultiSynth");
  } else {
    requireClock(clockgen.setupMultisynthInt(output, pll, divider),
                 "FAIL: integer MultiSynth");
  }
  digitalWrite(OEB_PIN, LOW);
  requireClock(clockgen.enableOutputs(true), "FAIL: enable outputs");
  delay(20);

  float measuredHz[8];
  measuredHz[output] = measureFrequency(output, ACTIVE_GATE_US);
  Serial.print("CLK");
  Serial.print(output);
  Serial.print(" PLL ");
  if (pll == SI5351_PLL_A) {
    Serial.print("A");
  } else {
    Serial.print("B");
  }
  Serial.print(" expected ");
  Serial.print(expectedHz, 1);
  Serial.print(" Hz, measured ");
  Serial.print(measuredHz[output], 1);
  Serial.println(" Hz");
  if (fabsf(measuredHz[output] - expectedHz) >
      expectedHz * FREQUENCY_TOLERANCE) {
    clearOutputsAndHalt("FAIL: frequency outside 2% tolerance");
  }
  Serial.println("PASS: output frequency");
  for (uint8_t other = 0; other < 8; other++) {
    if (other == output) {
      continue;
    }
    measuredHz[other] = measureFrequency(other, SILENT_GATE_US);
    if (measuredHz[other] > SILENT_LIMIT_HZ) {
      Serial.print("Unexpected clock on CLK");
      Serial.print(other);
      Serial.print(": ");
      Serial.println(measuredHz[other], 1);
      clearOutputsAndHalt("FAIL: other output is active");
    }
  }
  Serial.println("PASS: other seven outputs silent");

  requireClock(clockgen.enableOutputs(false), "FAIL: disable outputs");
  delay(10);
  for (uint8_t other = 0; other < 8; other++) {
    measuredHz[other] = measureFrequency(other, SILENT_GATE_US);
    if (measuredHz[other] > SILENT_LIMIT_HZ) {
      Serial.print("Unexpected clock on CLK");
      Serial.print(other);
      Serial.print(": ");
      Serial.println(measuredHz[other], 1);
      clearOutputsAndHalt("FAIL: disabled output is active");
    }
  }
  Serial.println("PASS: all eight outputs silent after disable");
  Serial.println();
  passedPoints++;
}
