/**************************************************************************/
/*!
    @file     setfrequency.ino

    Demonstrates the setFrequency() convenience helper, which computes the
    PLL multiplier, Multisynth divider and R divider for you given a target
    output frequency in Hz. No manual divider math required.

    The caller selects which PLL drives each output. Outputs sharing a PLL
    are retuned together, so here CLK0 uses PLL A and CLK1 uses PLL B so the
    two frequencies are independent.

    Tested against the Adafruit Si5351 breakout.
*/
/**************************************************************************/

#include <Adafruit_SI5351.h>
#include <Wire.h>

Adafruit_SI5351 clockgen = Adafruit_SI5351();

void setup(void) {
  Serial.begin(9600);
  while (!Serial)
    delay(10); // wait for native USB (e.g. RP2040, SAMD)

  Serial.println("Si5351 setFrequency Test");
  Serial.println("");

  if (clockgen.begin() != ERROR_NONE) {
    Serial.println("Ooops, no Si5351 detected ... Check your wiring!");
    while (1)
      delay(10);
  }
  Serial.println("OK!");

  /* CLK0 on PLL A -> 13.56 MHz (NFC carrier) */
  if (clockgen.setFrequency(0, SI5351_PLL_A, 13560000UL) == ERROR_NONE)
    Serial.println("CLK0 = 13.56 MHz (PLL A)");
  else
    Serial.println("CLK0 setFrequency failed");

  /* CLK1 on PLL B -> 1 MHz */
  if (clockgen.setFrequency(1, SI5351_PLL_B, 1000000UL) == ERROR_NONE)
    Serial.println("CLK1 = 1 MHz (PLL B)");
  else
    Serial.println("CLK1 setFrequency failed");

  /* CLK2 on PLL B -> 32.768 kHz (uses the R divider automatically) */
  if (clockgen.setFrequency(2, SI5351_PLL_B, 32768UL) == ERROR_NONE)
    Serial.println("CLK2 = 32.768 kHz (PLL B, R divider)");
  else
    Serial.println("CLK2 setFrequency failed");

  clockgen.enableOutputs(true);
  Serial.println("Outputs enabled.");
}

void loop(void) {}
