# Adafruit Si5351 [![Build Status](https://github.com/adafruit/Adafruit_Si5351_Library/workflows/Arduino%20Library%20CI/badge.svg)](https://github.com/adafruit/Adafruit_Si5351_Library/actions) [![Documentation](https://raw.githubusercontent.com/adafruit/ci-arduino/master/assets/doxygen_badge.svg)](https://adafruit.github.io/Adafruit_Si5351_Library/html/index.html)

Arduino library for Adafruit Si5351 clock generator breakouts, including the
[three-output Si5351A breakout](https://www.adafruit.com/product/2045) and the
eight-output Si5351C breakout. The chip communicates over I2C using SDA and SCL.

See the [Si5351A guide](https://learn.adafruit.com/adafruit-si5351-clock-generator-breakout)
for wiring and tutorials, and the
[library documentation](https://adafruit.github.io/Adafruit_Si5351_Library/html/index.html)
for details on each function.

## Installation

In the Arduino IDE Library Manager, search for **Adafruit Si5351 Library** and
install it. Install **Adafruit BusIO** when prompted for dependencies.

For the latest development version, use **Code > Download ZIP** on this repository,
then **Sketch > Include Library > Add .ZIP Library** in the Arduino IDE. Install
Adafruit BusIO through Library Manager if it is not already installed.

## Examples

Open an example from **File > Examples > Adafruit Si5351 Library**:

- [si5351](examples/si5351/si5351.ino): Configure PLLs, integer and fractional
  MultiSynth dividers, and an R divider for CLK0–CLK2.
- [setfrequency](examples/setfrequency/setfrequency.ino): Set target frequencies
  in hertz with the `setFrequency()` helper, which supports CLK0–CLK2.
- [eightoutput](examples/eightoutput/eightoutput.ino): Configure all eight
  Si5351C outputs using both PLLs.
- [phaseoffset](examples/phaseoffset/phaseoffset.ino): Generate two 10 MHz
  outputs with a 90-degree phase offset.

Outputs share two PLLs. Changing a PLL frequency also changes the frequencies of
the other outputs using that PLL.

On the Si5351C, CLK0–CLK5 support fractional dividers and initial phase offsets.
Phase offsets require a MultiSynth divider greater than 8; the phase example shows
the configuration and PLL reset sequence. CLK6 and CLK7 use even integer dividers
from 6 to 254 and do not support phase offsets.

## Support and license

Adafruit invests time and resources providing this open source code. Please
support Adafruit and open-source hardware by purchasing products from Adafruit!

Written by Limor Fried/Ladyada for Adafruit Industries.
BSD license; all text above must be included in any redistribution.
