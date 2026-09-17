// 05_oeb — OEB pin output-enable test (ESP32 V2 Feather, Si5351C)
//
// Hardware:
//   CLK0 (Si5351) -> GPIO27 (Feather D27) -> PCNT unit 0
//   OEB  (Si5351) -> GPIO25 (Feather A1), driven push-pull by MCU
//   I2C  via STEMMA QT (SDA=22, SCL=20)
//
// OEB is active-low: pin LOW  -> outputs enabled
//                    pin HIGH -> outputs high-Z
//
// Test (4 phases, CLK0 = 1 MHz from XTAL, target +/- 5%):
//   P1: OEB=LOW,  reg9 mask=0x00              -> CLK0 ~1 MHz   (baseline)
//   P2: OEB=HIGH, reg9 mask=0x00              -> CLK0  0 Hz    (silenced by
//   pin) P3: OEB=LOW,  reg9 mask=0x00              -> CLK0 ~1 MHz   (restored)
//   P4: OEB=HIGH, reg9 mask=0x01 (CLK0 masked)-> CLK0 ~1 MHz   (immune to pin)
//
// P4 is the smoking gun: same OEB pin state as P2, but reg-9 bit 0 set means
// CLK0 ignores the pin. If P4 reads ~1 MHz the OEB pin-mask register is live.

#include <Adafruit_SI5351.h>

#include "driver/pulse_cnt.h"

Adafruit_SI5351 clockgen;

static const uint8_t CLK0_PIN = 27;
static const uint8_t OEB_PIN = 25;

static pcnt_unit_handle_t pcnt_unit = NULL;
static pcnt_channel_handle_t pcnt_chan = NULL;

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

static uint32_t measureFrequencyHz(uint32_t window_ms) {
  pcnt_unit_stop(pcnt_unit);
  pcnt_unit_clear_count(pcnt_unit);
  pcnt_unit_start(pcnt_unit);
  uint32_t t0 = millis();
  while ((millis() - t0) < window_ms) {
    // busy wait
  }
  pcnt_unit_stop(pcnt_unit);
  int count = 0;
  pcnt_unit_get_count(pcnt_unit, &count);
  if (count < 0) {
    count = 0;
  }
  return (uint32_t)((uint64_t)count * 1000ULL / window_ms);
}

static bool phase(const char* label, bool oeb_high, uint8_t oeb_mask,
                  uint32_t target_hz, bool expect_silent) {
  clockgen.setOEBPinMask(oeb_mask);
  digitalWrite(OEB_PIN, oeb_high ? HIGH : LOW);
  delay(20);

  uint32_t measured = measureFrequencyHz(500);
  bool ok;
  if (expect_silent) {
    // Allow a tiny floor for PCNT noise / partial cycle on stop edge.
    ok = (measured < 1000);
  } else {
    uint32_t low = (uint32_t)((double)target_hz * 0.95);
    uint32_t high = (uint32_t)((double)target_hz * 1.05);
    ok = (measured >= low) && (measured <= high);
  }

  Serial.print("\n--- ");
  Serial.print(label);
  Serial.println(" ---");
  Serial.print("OEB pin: ");
  Serial.println(oeb_high ? "HIGH (de-asserted)" : "LOW (asserted)");
  Serial.print("reg9 mask: 0x");
  Serial.println(oeb_mask, HEX);
  Serial.print("Target:   ");
  Serial.print(expect_silent ? 0 : target_hz);
  Serial.println(" Hz");
  Serial.print("Measured: ");
  Serial.print(measured);
  Serial.println(" Hz");
  Serial.println(ok ? "PHASE: PASS" : "PHASE: FAIL");
  return ok;
}

void setup() {
  Serial.begin(115200);

  Serial.println("Si5351 05_oeb test (ESP32 V2)");

  // Hold OEB LOW (outputs enabled) BEFORE configuring the chip so we never
  // glitch the world up.
  pinMode(OEB_PIN, OUTPUT);
  digitalWrite(OEB_PIN, LOW);

  if (clockgen.begin() != ERROR_NONE) {
    Serial.println("FAIL: begin() error");
    Serial.println("RESULTS: 0/4");
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
    Serial.println("RESULTS: 0/4");
    while (1) {
      delay(10);
    }
  }

  delay(10);

  uint8_t pass = 0;
  if (phase("P1 OEB=LOW  baseline", /*high=*/false, 0x00, 1000000, false))
    pass++;
  if (phase("P2 OEB=HIGH outputs silent", /*high=*/true, 0x00, 1000000, true))
    pass++;
  if (phase("P3 OEB=LOW  restored", /*high=*/false, 0x00, 1000000, false))
    pass++;
  if (phase("P4 OEB=HIGH mask=0x01 CLK0 immune", /*high=*/true, 0x01, 1000000,
            false))
    pass++;

  // Park in a safe state: OEB LOW, mask clear.
  clockgen.setOEBPinMask(0x00);
  digitalWrite(OEB_PIN, LOW);

  Serial.print("\nRESULTS: ");
  Serial.print(pass);
  Serial.println("/4");
}

void loop() {}
