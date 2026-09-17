// 04_clki — Si5351 CLKIN-as-PLL-source lock test (ESP32 V2 port).
//
// Board: Adafruit Feather ESP32 V2 (FQBN esp32:esp32:adafruit_feather_esp32_v2)
//
// This is the test the whole ESP32 pivot was supposed to enable. It proves
// setupPLLSource() actually re-routes PLL_A from the 25 MHz on-board XTAL to
// an external CLKIN reference, and that the PLL re-locks at the new ratio.
//
// Three phases:
//   Phase 1 — XTAL baseline. PLL_A sourced from XTAL (default), CLK0 = 1 MHz.
//             Validates the bench end-to-end before touching CLKIN.
//   Phase 2 — CLKIN @ 14 MHz, mult=50 -> VCO=700 MHz, MS=700 -> CLK0 = 1.000
//   MHz. Phase 3 — CLKIN @ 16 MHz, *same* mult/MS -> CLK0 = 16/14 * 1 MHz
//   = 1.1429 MHz.
//             This is the rigorous check: only CLKIN changes, so if CLK0
//             tracks the ratio we know we're really running off CLKIN and
//             not off XTAL by accident.
//
// LEDC generates CLKIN on GPIO14. Both 14 MHz and 16 MHz are well above the
// 10 MHz Si5351 CLKIN floor (per AN619). VCOs (700 MHz, 800 MHz) sit
// comfortably mid-range (600-900 MHz spec).
//
// Wiring:
//   Si5351 SDA   -> SDA   (GPIO22)
//   Si5351 SCL   -> SCL   (GPIO20)
//   Si5351 CLK0  -> GPIO27 (PCNT unit 0 edge input)
//   Si5351 CLKIN -> GPIO14 (LEDC ch0)
//   Si5351 VIN   -> 3V3, GND -> GND

#include <Adafruit_SI5351.h>
#include <Wire.h>

#include "driver/pulse_cnt.h"

#define CLK0_PIN 27
#define CLKIN_LEDC_PIN 14
#define LEDC_CHAN 0
#define LEDC_RES_BITS 2 // 4 duty steps; we use 2/4 = 50%

static pcnt_unit_handle_t pcnt_unit = NULL;
static pcnt_channel_handle_t pcnt_chan = NULL;

Adafruit_SI5351 clockgen;

// Declared early so arduino-cli's auto forward-declarations of functions
// using `const Phase&` don't trip the preprocessor.
struct Phase {
  const char* name;
  bool use_clkin;
  uint32_t clkin_hz;
  uint32_t target_hz;
  uint32_t pll_mult; // for setupPLL
  uint32_t ms_div;   // for setupMultisynth
};

static bool pcntInit() {
  pcnt_unit_config_t unit_cfg = {};
  unit_cfg.low_limit = -10000;
  unit_cfg.high_limit = 10000;
  unit_cfg.flags.accum_count = 1;
  if (pcnt_new_unit(&unit_cfg, &pcnt_unit) != ESP_OK) {
    return false;
  }

  pcnt_chan_config_t chan_cfg = {};
  chan_cfg.edge_gpio_num = CLK0_PIN;
  chan_cfg.level_gpio_num = -1;
  if (pcnt_new_channel(pcnt_unit, &chan_cfg, &pcnt_chan) != ESP_OK) {
    return false;
  }

  if (pcnt_channel_set_edge_action(pcnt_chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                   PCNT_CHANNEL_EDGE_ACTION_HOLD) != ESP_OK) {
    return false;
  }
  if (pcnt_channel_set_level_action(pcnt_chan, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                    PCNT_CHANNEL_LEVEL_ACTION_KEEP) != ESP_OK) {
    return false;
  }
  if (pcnt_unit_add_watch_point(pcnt_unit, unit_cfg.high_limit) != ESP_OK) {
    return false;
  }
  if (pcnt_unit_add_watch_point(pcnt_unit, unit_cfg.low_limit) != ESP_OK) {
    return false;
  }
  if (pcnt_unit_enable(pcnt_unit) != ESP_OK) {
    return false;
  }
  if (pcnt_unit_clear_count(pcnt_unit) != ESP_OK) {
    return false;
  }
  return true;
}

static uint32_t measureFrequencyHz1s() {
  pcnt_unit_clear_count(pcnt_unit);
  pcnt_unit_start(pcnt_unit);
  uint32_t t0 = millis();
  while ((millis() - t0) < 1000) {
  }
  pcnt_unit_stop(pcnt_unit);

  int count = 0;
  pcnt_unit_get_count(pcnt_unit, &count);
  if (count < 0) {
    count = 0;
  }
  return (uint32_t)count;
}

// Start LEDC at requested frequency on CLKIN_LEDC_PIN. Detach first to allow
// frequency changes mid-test.
static bool startClkin(uint32_t freq_hz) {
  ledcDetach(CLKIN_LEDC_PIN);
  if (!ledcAttach(CLKIN_LEDC_PIN, freq_hz, LEDC_RES_BITS)) {
    return false;
  }
  ledcWrite(CLKIN_LEDC_PIN, 1 << (LEDC_RES_BITS - 1)); // 50% duty
  return true;
}

static bool runPhase(const Phase& p) {
  Serial.println();
  Serial.print("--- ");
  Serial.print(p.name);
  Serial.println(" ---");

  if (p.use_clkin) {
    if (!startClkin(p.clkin_hz)) {
      Serial.println("FAIL: ledcAttach");
      return false;
    }
    Serial.print("LEDC freq actual: ");
    Serial.print(ledcReadFreq(CLKIN_LEDC_PIN));
    Serial.println(" Hz");
    delay(20);
    if (clockgen.setupPLLSource(SI5351_PLL_A, SI5351_PLL_SOURCE_CLKIN,
                                SI5351_CLKIN_DIV_1) != ERROR_NONE) {
      Serial.println("FAIL: setupPLLSource(CLKIN)");
      return false;
    }
  } else {
    // Make sure we're sourced from XTAL.
    if (clockgen.setupPLLSource(SI5351_PLL_A, SI5351_PLL_SOURCE_XTAL,
                                SI5351_CLKIN_DIV_1) != ERROR_NONE) {
      Serial.println("FAIL: setupPLLSource(XTAL)");
      return false;
    }
    ledcDetach(CLKIN_LEDC_PIN);
  }

  clockgen.setupPLL(SI5351_PLL_A, p.pll_mult, 0, 1);
  clockgen.setupMultisynth(0, SI5351_PLL_A, p.ms_div, 0, 1);
  clockgen.enableOutputs(true);

  delay(50); // settle / lock time

  uint32_t measured = measureFrequencyHz1s();

  Serial.print("Target:   ");
  Serial.print(p.target_hz);
  Serial.println(" Hz");
  Serial.print("Measured: ");
  Serial.print(measured);
  Serial.println(" Hz");

  // +/- 2% tolerance — generous for PLL/CLKIN noise but tight enough that
  // a 14 vs 16 ratio (~14% delta) can't be mistaken for a pass.
  uint32_t low = (uint32_t)((double)p.target_hz * 0.98);
  uint32_t high = (uint32_t)((double)p.target_hz * 1.02);
  bool pass = (measured >= low) && (measured <= high);
  Serial.println(pass ? "PHASE: PASS" : "PHASE: FAIL");
  return pass;
}

void setup() {
  Serial.begin(115200);

  Serial.println("Si5351 04_clki test (ESP32 V2)");

  if (clockgen.begin() != ERROR_NONE) {
    Serial.println("FAIL: begin() error");
    Serial.println("RESULTS: 0/3");
    while (1) {
      delay(10);
    }
  }
  if (!pcntInit()) {
    Serial.println("FAIL: PCNT init");
    Serial.println("RESULTS: 0/3");
    while (1) {
      delay(10);
    }
  }

  Phase phases[] = {
      {"P1 XTAL baseline", false, 0, 1000000UL, 36, 900},
      {"P2 CLKIN 14 MHz", true, 14000000UL, 1000000UL, 50, 700},
      // P3: same mult/MS as P2, only CLKIN changes 14->16 MHz.
      // VCO = 16 * 50 = 800 MHz; CLK0 = 800/700 = 1142857 Hz.
      {"P3 CLKIN 16 MHz", true, 16000000UL, 1142857UL, 50, 700},
  };

  int passed = 0;
  for (auto& p : phases) {
    if (runPhase(p)) {
      passed++;
    }
  }

  Serial.println();
  Serial.print("RESULTS: ");
  Serial.print(passed);
  Serial.println("/3");
}

void loop() {}
