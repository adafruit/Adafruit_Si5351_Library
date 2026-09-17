/*
   Adafruit Si5351 phase-offset example.

   CLK0 and CLK1 share PLLA and generate 10 MHz clocks, with CLK1 delayed
   by 90 degrees (25 ns). Observe both outputs with an oscilloscope.
   This example assumes a 25 MHz crystal. If OEB is exposed, hold it low.

   Phase offsets are available on CLK0..CLK5, with MultiSynth dividers > 8.
   CLK6 and CLK7 do not support phase offsets.
*/

#include <Adafruit_SI5351.h>

Adafruit_SI5351 clockgen;

void setup() {
  Serial.begin(115200);
  // Wait for Serial Monitor on native USB boards; remove for standalone use.
  while (!Serial) {
    delay(10);
  }
  delay(250);

  Serial.println("Adafruit Si5351 phase-offset example");

  // begin() disables the outputs while we configure their frequencies/phases.
  if (clockgen.begin() != ERROR_NONE) {
    halt("Could not initialize the Si5351.");
  }

  // A 25 MHz crystal multiplied by 24 gives a 600 MHz PLL frequency.
  if (clockgen.setupPLLInt(SI5351_PLL_A, 24) != ERROR_NONE) {
    halt("Could not configure PLLA.");
  }

  // Both outputs use the same PLL and divider: 600 MHz / 60 = 10 MHz.
  if (clockgen.setupMultisynth(0, SI5351_PLL_A, 60, 0, 1) != ERROR_NONE) {
    halt("Could not configure CLK0.");
  }
  if (clockgen.setupMultisynth(1, SI5351_PLL_A, 60, 0, 1) != ERROR_NONE) {
    halt("Could not configure CLK1.");
  }
  if (clockgen.setupRdiv(0, SI5351_R_DIV_1) != ERROR_NONE) {
    halt("Could not configure the CLK0 R divider.");
  }
  if (clockgen.setupRdiv(1, SI5351_R_DIV_1) != ERROR_NONE) {
    halt("Could not configure the CLK1 R divider.");
  }

  // Offsets are 0..127 quarter-VCO-period steps, not degrees.
  // Set the reference to 0 too: both outputs must leave integer mode.
  if (clockgen.setPhaseOffset(0, 0) != ERROR_NONE) {
    halt("Could not set the CLK0 phase offset.");
  }
  // 60 / (4 * 600 MHz) = 25 ns, one quarter of the 10 MHz output period.
  if (clockgen.setPhaseOffset(1, 60) != ERROR_NONE) {
    halt("Could not set the CLK1 phase offset.");
  }

  // Reset once after ALL phase offsets are set, then enable the outputs.
  // Reapply offsets and reset again if you change the MultiSynth settings.
  if (clockgen.resetPLL(SI5351_PLL_A) != ERROR_NONE) {
    halt("Could not reset PLLA.");
  }
  if (clockgen.enableOutputs(true) != ERROR_NONE) {
    halt("Could not enable the outputs.");
  }
  Serial.println("CLK0 and CLK1: 10 MHz, CLK1 delay: 90 degrees (25 ns).");
}

void loop() {}

void halt(const char* message) {
  Serial.println(message);
  clockgen.enableOutputs(false);
  while (true) {
    delay(10);
  }
}
