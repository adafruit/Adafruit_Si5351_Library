/*
   Si5351C-B 8-output example for the Adafruit Si5351C breakout.

   Demonstrates driving all eight clock outputs:
     - CLK0..CLK5 are full fractional MultiSynths (a + b/c)
     - CLK6 and CLK7 are integer-only MultiSynths, each controlled by a
       single divider register (regs 90/91). Their divider must be an
       EVEN integer in the range 6..254.

   Both PLLs are shared across the outputs:
     - PLLA -> CLK0, CLK1, CLK2, CLK6
     - PLLB -> CLK3, CLK4, CLK5, CLK7
*/

#include <Adafruit_SI5351.h>

Adafruit_SI5351 clockgen = Adafruit_SI5351();

void setup(void) {
  Serial.begin(9600);
  while (!Serial)
    delay(10); // wait for native USB (e.g. RP2040, SAMD)
  Serial.println("Si5351C-B 8-Output Test");
  Serial.println("");

  /* Initialise the IC */
  if (clockgen.begin() != ERROR_NONE) {
    Serial.println(
        "Ooops, no Si5351 detected ... Check your wiring or I2C ADDR!");
    while (1)
      delay(10);
  }
  Serial.println("OK!");

  /* ---- PLLA @ 900 MHz (25 MHz * 36) feeds CLK0..2 and CLK6 ---- */
  Serial.println("Set PLLA to 900 MHz (25 MHz * 36)");
  clockgen.setupPLLInt(SI5351_PLL_A, 36);

  /* CLK0: 900 / 8  = 112.5 MHz  (integer mode) */
  Serial.println("Set CLK0 to 112.5 MHz (900 / 8, integer mode)");
  clockgen.setupMultisynthInt(0, SI5351_PLL_A, SI5351_MULTISYNTH_DIV_8);
  /* CLK1: 900 / 50 = 18 MHz */
  Serial.println("Set CLK1 to 18 MHz (900 / 50)");
  clockgen.setupMultisynth(1, SI5351_PLL_A, 50, 0, 1);
  /* CLK2: 900 / 90 = 10 MHz, then R-divide by 8 -> 1.25 MHz */
  Serial.println("Set CLK2 to 1.25 MHz (900 / 90, then R-div by 8)");
  clockgen.setupMultisynth(2, SI5351_PLL_A, 90, 0, 1);
  clockgen.setupRdiv(2, SI5351_R_DIV_8);

  /* ---- PLLB @ 616.66667 MHz (25 MHz * 24 + 2/3) feeds CLK3..5 and CLK7 ----
   */
  Serial.println("Set PLLB to 616.667 MHz (25 MHz * 24 + 2/3)");
  clockgen.setupPLL(SI5351_PLL_B, 24, 2, 3);

  /* CLK3: 616.667 / 45.5 = 13.553 MHz (fractional) */
  Serial.println("Set CLK3 to 13.553 MHz (616.667 / 45.5, fractional)");
  clockgen.setupMultisynth(3, SI5351_PLL_B, 45, 1, 2);
  /* CLK4: 616.667 / 60 = 10.278 MHz */
  Serial.println("Set CLK4 to 10.278 MHz (616.667 / 60)");
  clockgen.setupMultisynth(4, SI5351_PLL_B, 60, 0, 1);
  /* CLK5: 616.667 / 100 = 6.167 MHz */
  Serial.println("Set CLK5 to 6.167 MHz (616.667 / 100)");
  clockgen.setupMultisynth(5, SI5351_PLL_B, 100, 0, 1);

  /* ---- Integer-only outputs CLK6 and CLK7 (even divider 6..254) ---- */
  /* CLK6: 900 / 100 = 9 MHz from PLLA */
  Serial.println("Set CLK6 to 9 MHz (900 / 100, integer-only, from PLLA)");
  clockgen.setupMultisynth6(SI5351_PLL_A, 100);
  /* CLK7: 616.667 / 50 = 12.333 MHz from PLLB */
  Serial.println(
      "Set CLK7 to 12.333 MHz (616.667 / 50, integer-only, from PLLB)");
  clockgen.setupMultisynth7(SI5351_PLL_B, 50);

  /* Enable all outputs */
  clockgen.enableOutputs(true);
  Serial.println("");
  Serial.println("All 8 outputs enabled.");
}

void loop(void) {}
