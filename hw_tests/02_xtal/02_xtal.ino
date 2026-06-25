// 02_xtal — Si5351 TCXO reference frequency-count test (ESP32 V2 port).
//
// Board: Adafruit Feather ESP32 V2 (FQBN esp32:esp32:adafruit_feather_esp32_v2)
//
// Configures PLL_A = 36 * 25 MHz = 900 MHz then divides by 900 to emit
// 1.000 MHz on CLK0. Measures CLK0 by counting rising edges with the ESP32
// PCNT (Pulse Counter) hardware peripheral for exactly 1000 ms, and asserts
// the count is within +/- 5% of 1,000,000.
//
// Wiring:
//   Si5351 SDA  -> SDA (GPIO22)
//   Si5351 SCL  -> SCL (GPIO20)
//   Si5351 CLK0 -> GPIO27 (PCNT unit 0 edge input)
//   Si5351 VIN  -> 3V3, GND -> GND
//
// PCNT is 16-bit per unit, so we configure +/- 10000 limits with the
// accumulator flag set; the driver auto-rolls the running total and
// pcnt_unit_get_count() returns the signed 32-bit accumulated count.

#include <Adafruit_SI5351.h>
#include <Wire.h>

#include "driver/pulse_cnt.h"

#define CLK0_PIN 27

static pcnt_unit_handle_t pcnt_unit = NULL;
static pcnt_channel_handle_t pcnt_chan = NULL;

Adafruit_SI5351 clockgen;

static bool pcntInit() {
  pcnt_unit_config_t unit_cfg = {};
  unit_cfg.low_limit = -10000;
  unit_cfg.high_limit = 10000;
  unit_cfg.flags.accum_count = 1; // accumulate across +/- watch points
  if (pcnt_new_unit(&unit_cfg, &pcnt_unit) != ESP_OK) {
    return false;
  }

  pcnt_chan_config_t chan_cfg = {};
  chan_cfg.edge_gpio_num = CLK0_PIN;
  chan_cfg.level_gpio_num = -1;
  if (pcnt_new_channel(pcnt_unit, &chan_cfg, &pcnt_chan) != ESP_OK) {
    return false;
  }

  // Count on RISING edge only (positive increment), HOLD on falling.
  if (pcnt_channel_set_edge_action(pcnt_chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                   PCNT_CHANNEL_EDGE_ACTION_HOLD) != ESP_OK) {
    return false;
  }
  if (pcnt_channel_set_level_action(pcnt_chan, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                    PCNT_CHANNEL_LEVEL_ACTION_KEEP) != ESP_OK) {
    return false;
  }

  // Watch points let the accumulator capture multiples of high_limit/low_limit.
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
    // Busy gate for exactly 1000 ms. PCNT runs in hardware.
  }
  pcnt_unit_stop(pcnt_unit);

  int count = 0;
  pcnt_unit_get_count(pcnt_unit, &count);
  if (count < 0) {
    count = 0; // shouldn't happen on rising-edge-only count
  }
  return (uint32_t)count;
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("Si5351 02_xtal test (ESP32 V2)");

  // Power the I2C rail (Feather V2 STEMMA QT power gate).
  pinMode(NEOPIXEL_I2C_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_I2C_POWER, HIGH);
  delay(10);

  if (clockgen.begin() != ERROR_NONE) {
    Serial.println("FAIL: begin() error");
    Serial.println("RESULTS: 0/1");
    while (1) {
      delay(10);
    }
  }

  // PLL_A = 25 MHz * 36 = 900 MHz; CLK0 = 900 MHz / 900 = 1.000 MHz.
  clockgen.setupPLL(SI5351_PLL_A, 36, 0, 1);
  clockgen.setupMultisynth(0, SI5351_PLL_A, 900, 0, 1);
  clockgen.enableOutputs(true);

  if (!pcntInit()) {
    Serial.println("FAIL: PCNT init");
    Serial.println("RESULTS: 0/1");
    while (1) {
      delay(10);
    }
  }

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
