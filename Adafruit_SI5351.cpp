/**************************************************************************/
/*!
 * @file     Adafruit_SI5351.cpp
 *
 * @mainpage Adafruit Si5351 Library
 *
 * @author   K. Townsend (Adafruit Industries)
 *
 * @brief    Driver for the SI5351 160MHz Clock Gen
 *
 * @section  REFERENCES
 *
 * Si5351A/B/C Datasheet:
 * http://www.silabs.com/Support%20Documents/TechnicalDocs/Si5351.pdf
 *
 * Manually Generating an Si5351 Register Map:
 * http://www.silabs.com/Support%20Documents/TechnicalDocs/AN619.pdf
 *
 * @section license License
 *
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2014, Adafruit Industries
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the
 * names of its contributors may be used to endorse or promote products
 * derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**************************************************************************/
#if defined(__AVR__)
#include <avr/pgmspace.h>
#include <util/delay.h>
#elif defined(ESP8266) || defined(ESP32) || \
    (defined(ARDUINO_ARCH_RP2040) && !defined(__MBED__))
#include "pgmspace.h"
#else
#define pgm_read_byte(addr) \
  (*(const unsigned char*)(addr)) //!< Reads byte from address
#endif
#include <Adafruit_BusIO_Register.h>
#include <Adafruit_SI5351.h>
#include <stdlib.h>

/**************************************************************************/
/*!
    Constructor
*/
/**************************************************************************/
Adafruit_SI5351::Adafruit_SI5351(void) {
  m_si5351Config.initialised = false;
  m_si5351Config.crystalFreq = SI5351_CRYSTAL_FREQ_25MHZ;
  m_si5351Config.crystalLoad = SI5351_CRYSTAL_LOAD_10PF;
  m_si5351Config.crystalPPM = 30;
  m_si5351Config.plla_configured = false;
  m_si5351Config.plla_freq = 0;
  m_si5351Config.pllb_configured = false;
  m_si5351Config.pllb_freq = 0;

  for (uint8_t i = 0; i < 6; i++) {
    lastRdivValue[i] = 0;
  }
}

/**************************************************************************/
/*!
    @brief  Initializes I2C and configures the breakout (call this function
   before doing anything else)

    @param  theWire The I2C (Wire) bus to use.
*/
/**************************************************************************/
err_t Adafruit_SI5351::begin(TwoWire* theWire) {
  /* Initialise I2C */
  if (i2c_dev)
    delete i2c_dev;
  i2c_dev = new Adafruit_I2CDevice(SI5351_ADDRESS, theWire);
  if (!i2c_dev->begin())
    return ERROR_I2C_DEVICENOTFOUND;

  /* Disable all outputs setting CLKx_DIS high */
  ASSERT_STATUS(write8(SI5351_REGISTER_3_OUTPUT_ENABLE_CONTROL, 0xFF));

  /* Power down all output drivers */
  ASSERT_STATUS(write8(SI5351_REGISTER_16_CLK0_CONTROL, 0x80));
  ASSERT_STATUS(write8(SI5351_REGISTER_17_CLK1_CONTROL, 0x80));
  ASSERT_STATUS(write8(SI5351_REGISTER_18_CLK2_CONTROL, 0x80));
  ASSERT_STATUS(write8(SI5351_REGISTER_19_CLK3_CONTROL, 0x80));
  ASSERT_STATUS(write8(SI5351_REGISTER_20_CLK4_CONTROL, 0x80));
  ASSERT_STATUS(write8(SI5351_REGISTER_21_CLK5_CONTROL, 0x80));
  ASSERT_STATUS(write8(SI5351_REGISTER_22_CLK6_CONTROL, 0x80));
  ASSERT_STATUS(write8(SI5351_REGISTER_23_CLK7_CONTROL, 0x80));

  /* Set the load capacitance for the XTAL */
  ASSERT_STATUS(write8(SI5351_REGISTER_183_CRYSTAL_INTERNAL_LOAD_CAPACITANCE,
                       m_si5351Config.crystalLoad));

  /* Disable spread spectrum output. */
  enableSpreadSpectrum(false);

  /* Set interrupt masks as required (see Register 2 description in AN619).
     By default, ClockBuilder Desktop sets this register to 0x18.
     Note that the least significant nibble must remain 0x8, but the most
     significant nibble may be modified to suit your needs. */

  /* Reset the PLL config fields just in case we call init again */
  m_si5351Config.plla_configured = false;
  m_si5351Config.plla_freq = 0;
  m_si5351Config.pllb_configured = false;
  m_si5351Config.pllb_freq = 0;

  /* All done! */
  m_si5351Config.initialised = true;

  return ERROR_NONE;
}

/**************************************************************************/
/*!
    @brief  Configures the Si5351 with config settings generated in
            ClockBuilder. You can use this function to make sure that
            your HW is properly configure and that there are no problems
            with the board itself.

            Running this function should provide the following output:
            * Channel 0: 120.00 MHz
            * Channel 1: 12.00  MHz
            * Channel 2: 13.56  MHz

    @note   This will overwrite all of the config registers!
*/
/**************************************************************************/
err_t Adafruit_SI5351::setClockBuilderData(void) {
  uint16_t i = 0;

  /* Make sure we've called init first */
  ASSERT(m_si5351Config.initialised, ERROR_DEVICENOTINITIALISED);

  /* Disable all outputs setting CLKx_DIS high */
  ASSERT_STATUS(write8(SI5351_REGISTER_3_OUTPUT_ENABLE_CONTROL, 0xFF));

  /* Writes configuration data to device using the register map contents
         generated by ClockBuilder Desktop (registers 15-92 + 149-170) */
  for (i = 0; i < sizeof(m_si5351_regs_15to92_149to170) / 2; i++) {
    ASSERT_STATUS(write8(m_si5351_regs_15to92_149to170[i][0],
                         m_si5351_regs_15to92_149to170[i][1]));
  }

  /* Apply soft reset */
  ASSERT_STATUS(write8(SI5351_REGISTER_177_PLL_RESET, 0xAC));

  /* Enabled desired outputs (see Register 3) */
  ASSERT_STATUS(write8(SI5351_REGISTER_3_OUTPUT_ENABLE_CONTROL, 0x00));

  return ERROR_NONE;
}

/**************************************************************************/
/*!
  @brief  Sets the multiplier for the specified PLL using integer values

  @param  pll   The PLL to configure, which must be one of the following:
                - SI5351_PLL_A
                - SI5351_PLL_B
  @param  mult  The PLL integer multiplier (must be between 15 and 90)
*/
/**************************************************************************/
err_t Adafruit_SI5351::setupPLLInt(si5351PLL_t pll, uint8_t mult) {
  return setupPLL(pll, mult, 0, 1);
}

/**************************************************************************/
/*!
    @brief  Selects the reference source (XTAL or CLKIN) for the specified PLL,
            and optionally sets the CLKIN pre-divider.

    @param  pll       The PLL whose reference source is being configured:
                      - SI5351_PLL_A
                      - SI5351_PLL_B
    @param  source    Reference source for this PLL:
                      - SI5351_PLL_SOURCE_XTAL  (use the on-board crystal)
                      - SI5351_PLL_SOURCE_CLKIN (use the external CLKIN pin)
    @param  clkinDiv  CLKIN pre-divider (reg 15, bits [7:6]). The CLKIN input
                      to the PLLs must be in the 10..40 MHz range; pick a
                      divider that brings your CLKIN signal into that window.
                      Only written when @p source == SI5351_PLL_SOURCE_CLKIN,
                      so that switching one PLL back to XTAL does not clobber
                      the divider used by the other PLL.
                      - SI5351_CLKIN_DIV_1 (default)
                      - SI5351_CLKIN_DIV_2
                      - SI5351_CLKIN_DIV_4
                      - SI5351_CLKIN_DIV_8

    @section Register Map (AN619)

    Register 15 -- PLL Input Source

       Bit  | Field
      ------+-----------------------------------
       7:6  | CLKIN_DIV[1:0]
       5:4  | (reserved)
        3   | PLLB_SRC  (0 = XTAL, 1 = CLKIN)
        2   | PLLA_SRC  (0 = XTAL, 1 = CLKIN)
       1:0  | (reserved)

    Both PLLA_SRC and PLLB_SRC are single-bit fields, so we use
    Adafruit_BusIO_RegisterBits to read-modify-write only the affected
    bit and leave the other PLL's source (and the reserved bits) intact.

    @return ERROR_NONE on success, or ERROR_I2C_TRANSACTION on a bus failure.
*/
/**************************************************************************/
err_t Adafruit_SI5351::setupPLLSource(si5351PLL_t pll,
                                     si5351PLLSource_t source,
                                     si5351ClkinDiv_t clkinDiv) {
  Adafruit_BusIO_Register src_reg(i2c_dev,
                                  SI5351_REGISTER_15_PLL_INPUT_SOURCE);

  /* PLLA_SRC = bit 2, PLLB_SRC = bit 3.  Each is a 1-bit field. */
  uint8_t srcBit = (pll == SI5351_PLL_A) ? 2 : 3;
  Adafruit_BusIO_RegisterBits pll_src_bit(&src_reg, 1, srcBit);
  if (!pll_src_bit.write(source & 0x01))
    return ERROR_I2C_TRANSACTION;

  /* CLKIN_DIV (bits [7:6]) is shared by both PLLs. Only program it when
     this PLL is actually being switched to CLKIN, so that flipping one PLL
     back to XTAL doesn't stomp the divider the other PLL is still using. */
  if (source == SI5351_PLL_SOURCE_CLKIN) {
    Adafruit_BusIO_RegisterBits clkin_div_bits(&src_reg, 2, 6);
    if (!clkin_div_bits.write(clkinDiv & 0x03))
      return ERROR_I2C_TRANSACTION;
  }

  return ERROR_NONE;
}

/**************************************************************************/
/*!
    @brief  Sets the multiplier for the specified PLL

    @param  pll   The PLL to configure, which must be one of the following:
                  - SI5351_PLL_A
                  - SI5351_PLL_B
    @param  mult  The PLL integer multiplier (must be between 15 and 90)
    @param  num   The 20-bit numerator for fractional output (0..1,048,575).
                  Set this to '0' for integer output.
    @param  denom The 20-bit denominator for fractional output (1..1,048,575).
                  Set this to '1' or higher to avoid divider by zero errors.

    @section PLL Configuration

    fVCO is the PLL output, and must be between 600..900MHz, where:

        fVCO = fXTAL * (a+(b/c))

    fXTAL = the crystal input frequency
    a     = an integer between 15 and 90
    b     = the fractional numerator (0..1,048,575)
    c     = the fractional denominator (1..1,048,575)

    NOTE: Try to use integers whenever possible to avoid clock jitter
    (only use the a part, setting b to '0' and c to '1').

    See: http://www.silabs.com/Support%20Documents/TechnicalDocs/AN619.pdf
*/
/**************************************************************************/
err_t Adafruit_SI5351::setupPLL(si5351PLL_t pll, uint8_t mult, uint32_t num,
                                uint32_t denom) {
  uint32_t P1; /* PLL config register P1 */
  uint32_t P2; /* PLL config register P2 */
  uint32_t P3; /* PLL config register P3 */

  /* Basic validation */
  ASSERT(m_si5351Config.initialised, ERROR_DEVICENOTINITIALISED);
  ASSERT((mult > 14) && (mult < 91),
         ERROR_INVALIDPARAMETER);                   /* mult = 15..90 */
  ASSERT(denom > 0, ERROR_INVALIDPARAMETER);        /* Avoid divide by zero */
  ASSERT(num <= 0xFFFFF, ERROR_INVALIDPARAMETER);   /* 20-bit limit */
  ASSERT(denom <= 0xFFFFF, ERROR_INVALIDPARAMETER); /* 20-bit limit */

  /* Feedback Multisynth Divider Equation
   *
   * where: a = mult, b = num and c = denom
   *
   * P1 register is an 18-bit value using following formula:
   *
   * 	P1[17:0] = 128 * mult + floor(128*(num/denom)) - 512
   *
   * P2 register is a 20-bit value using the following formula:
   *
   * 	P2[19:0] = 128 * num - denom * floor(128*(num/denom))
   *
   * P3 register is a 20-bit value using the following formula:
   *
   * 	P3[19:0] = denom
   */

  /* Set the main PLL config registers */
  if (num == 0) {
    /* Integer mode */
    P1 = 128 * mult - 512;
    P2 = num;
    P3 = denom;
  } else {
    /* Fractional mode */
    P1 =
        (uint32_t)(128 * mult + floor(128 * ((float)num / (float)denom)) - 512);
    P2 = (uint32_t)(128 * num -
                    denom * floor(128 * ((float)num / (float)denom)));
    P3 = denom;
  }

  /* Get the appropriate starting point for the PLL registers */
  uint8_t baseaddr = (pll == SI5351_PLL_A ? 26 : 34);

  /* The datasheet is a nightmare of typos and inconsistencies here! */
  ASSERT_STATUS(write8(baseaddr, (P3 & 0x0000FF00) >> 8));
  ASSERT_STATUS(write8(baseaddr + 1, (P3 & 0x000000FF)));
  ASSERT_STATUS(write8(baseaddr + 2, (P1 & 0x00030000) >> 16));
  ASSERT_STATUS(write8(baseaddr + 3, (P1 & 0x0000FF00) >> 8));
  ASSERT_STATUS(write8(baseaddr + 4, (P1 & 0x000000FF)));
  ASSERT_STATUS(write8(baseaddr + 5,
                       ((P3 & 0x000F0000) >> 12) | ((P2 & 0x000F0000) >> 16)));
  ASSERT_STATUS(write8(baseaddr + 6, (P2 & 0x0000FF00) >> 8));
  ASSERT_STATUS(write8(baseaddr + 7, (P2 & 0x000000FF)));

  /* Reset both PLLs */
  ASSERT_STATUS(write8(SI5351_REGISTER_177_PLL_RESET, (1 << 7) | (1 << 5)));

  /* Store the frequency settings for use with the Multisynth helper */
  if (pll == SI5351_PLL_A) {
    float fvco =
        m_si5351Config.crystalFreq * (mult + ((float)num / (float)denom));
    m_si5351Config.plla_configured = true;
    m_si5351Config.plla_freq = (uint32_t)floor(fvco);
  } else {
    float fvco =
        m_si5351Config.crystalFreq * (mult + ((float)num / (float)denom));
    m_si5351Config.pllb_configured = true;
    m_si5351Config.pllb_freq = (uint32_t)floor(fvco);
  }

  return ERROR_NONE;
}

/**************************************************************************/
/*!
    @brief  Configures a Multisynth divider in integer mode.

    Outputs 0..5 use the full fractional MultiSynths driven in integer mode
    (num=0, denom=1). Outputs 6 and 7 are hardware integer-only MultiSynths
    controlled by a single divider register each (regs 90/91), and are
    handled separately here.

    @param  output    The output channel to configure (0..7).
    @param  pllSource The PLL input source to use, which must be one of:
                      - SI5351_PLL_A
                      - SI5351_PLL_B
    @param  div       The integer divider. For CLK0..CLK5 use one of
                      SI5351_MULTISYNTH_DIV_4/6/8. For CLK6/CLK7 use an
                      even value in the range 6..254.
    @return ERROR_NONE on success, otherwise an appropriate error code.
*/
/**************************************************************************/
err_t Adafruit_SI5351::setupMultisynthInt(uint8_t output, si5351PLL_t pllSource,
                                          uint8_t div) {
  /* CLK6 and CLK7 are integer-only MultiSynths with a single divider
     register each (regs 90/91), configured separately from CLK0..CLK5. */
  if ((output == 6) || (output == 7)) {
    ASSERT(m_si5351Config.initialised, ERROR_DEVICENOTINITIALISED);
    ASSERT(div >= 6, ERROR_INVALIDPARAMETER);
    ASSERT(div <= 254, ERROR_INVALIDPARAMETER);
    ASSERT((div % 2) == 0, ERROR_INVALIDPARAMETER); /* Must be even */

    if (pllSource == SI5351_PLL_A) {
      ASSERT(m_si5351Config.plla_configured, ERROR_INVALIDPARAMETER);
    } else {
      ASSERT(m_si5351Config.pllb_configured, ERROR_INVALIDPARAMETER);
    }

    uint8_t paramReg = (output == 6)
                           ? SI5351_REGISTER_90_MULTISYNTH6_PARAMETERS
                           : SI5351_REGISTER_91_MULTISYNTH7_PARAMETERS;
    uint8_t ctrlReg = (output == 6) ? SI5351_REGISTER_22_CLK6_CONTROL
                                    : SI5351_REGISTER_23_CLK7_CONTROL;

    ASSERT_STATUS(write8(paramReg, div));

    uint8_t clkControlReg = 0x0C; /* 8mA drive, not inverted, powered up */
    if (pllSource == SI5351_PLL_B)
      clkControlReg |= (1 << 5); /* Uses PLLB */
    clkControlReg |= (1 << 6);   /* Integer mode (always for MS6/MS7) */

    ASSERT_STATUS(write8(ctrlReg, clkControlReg));

    return ERROR_NONE;
  }

  /* CLK0..CLK5: integer-mode output via the full fractional Multisynth. */
  return setupMultisynth(output, pllSource, div, 0, 1);
}

/**************************************************************************/
/*!
    @brief  Configures the R divider for a given output channel.

    The R divider provides a final power-of-two division stage (1..128)
    after the Multisynth, extending the usable output range down to low
    frequencies. It is a 3-bit field occupying bits 4..6 of the channel's
    MSx_PARAMETERS_3 register. The shifted value is cached in
    lastRdivValue[] so setupMultisynth() can preserve it when rewriting
    the same parameter byte.

    @param  output  The output channel to configure (0..5).
    @param  div     The R divider value, one of:
                    - SI5351_R_DIV_1
                    - SI5351_R_DIV_2
                    - SI5351_R_DIV_4
                    - SI5351_R_DIV_8
                    - SI5351_R_DIV_16
                    - SI5351_R_DIV_32
                    - SI5351_R_DIV_64
                    - SI5351_R_DIV_128
    @return ERROR_NONE on success, ERROR_INVALIDPARAMETER if the channel is
            out of range, or ERROR_I2C_TRANSACTION on a bus failure.
*/
/**************************************************************************/
err_t Adafruit_SI5351::setupRdiv(uint8_t output, si5351RDiv_t div) {
  ASSERT(output < 6, ERROR_INVALIDPARAMETER); /* Channel range (CLK0..CLK5) */

  /* MSx_PARAMETERS_3 registers are 8 bytes apart (44, 52, 60, ...). */
  uint8_t Rreg = SI5351_REGISTER_44_MULTISYNTH0_PARAMETERS_3 + (output * 8);

  /* The R divider is a 3-bit field occupying bits 4..6 of the register. */
  Adafruit_BusIO_Register rdiv_reg(i2c_dev, Rreg);
  Adafruit_BusIO_RegisterBits rdiv_bits(&rdiv_reg, 3, 4);

  uint8_t divider = div & 0x07;
  if (!rdiv_bits.write(divider))
    return ERROR_I2C_TRANSACTION;

  /* Cache the shifted value for reuse in setupMultisynth's parameter buffer. */
  lastRdivValue[output] = divider << 4;
  return ERROR_NONE;
}

/**************************************************************************/
/*!
    @brief  Automatically computes and applies the PLL multiplier,
            Multisynth divider and R divider needed to generate a desired
            output frequency on a given channel, then configures the
            hardware. This is a convenience wrapper over setupPLL(),
            setupMultisynth() and setupRdiv().

    @param  output  The output channel to use (0..2).
    @param  pll     The PLL to drive this output, either SI5351_PLL_A or
                    SI5351_PLL_B. The caller selects the PLL; note that any
                    other output already sharing this PLL will be retuned.
    @param  freq    Desired output frequency in Hz (8000 .. 150000000).

    @return ERROR_NONE on success, or ERROR_INVALIDPARAMETER if the
            requested frequency cannot be synthesised within the
            hardware limits.

    @section Algorithm

    The output is generated as:

        fOUT = (fXTAL * (M)) / (D * R)

    where fXTAL is the 25 MHz reference, M = a + b/c is the fractional
    PLL feedback multiplier, D is the (integer) Multisynth divider and
    R is the output R divider (1..128). To keep output jitter low the
    fractional part is placed entirely on the PLL while the Multisynth
    runs in integer mode.

    The solver:
      1. Applies the R divider (doubling until the pre-R frequency is at
         least 600 kHz) so low frequencies stay within Multisynth range.
      2. Chooses the largest even Multisynth divider D that keeps the VCO
         within its 600..900 MHz lock range.
      3. Computes the fractional PLL multiplier M to hit the VCO target,
         using a denominator of 1048575 for maximum resolution.

    @note   Frequencies above 150 MHz require the DIVBY4 path and are not
            yet supported; they return ERROR_INVALIDPARAMETER.

    @note   The output is left enabled/disabled exactly as it was; call
            enableOutputs() as needed.
*/
/**************************************************************************/
err_t Adafruit_SI5351::setFrequency(uint8_t output, si5351PLL_t pll,
                                    uint32_t freq) {
  ASSERT(m_si5351Config.initialised, ERROR_DEVICENOTINITIALISED);
  /* Channel range: limited to 0..2 until the 8-channel expansion lands. */
  ASSERT(output < 3, ERROR_INVALIDPARAMETER);
  ASSERT(freq >= 8000UL, ERROR_INVALIDPARAMETER);
  ASSERT(freq <= 150000000UL, ERROR_INVALIDPARAMETER);

  const uint32_t fxtal = m_si5351Config.crystalFreq; /* 25 MHz */
  const uint32_t vco_min = 600000000UL;
  const uint32_t vco_max = 900000000UL;
  const uint32_t ms_min = 600000UL; /* keeps Multisynth divider <= 1800 */
  const uint32_t denom = 0xFFFFFUL; /* 1048575, max fractional resolution */

  /* Step 1: engage the R divider until the pre-R frequency is in range. */
  uint32_t f_ms = freq;
  uint8_t rdiv = 0;
  while ((f_ms < ms_min) && (rdiv < 7)) {
    f_ms <<= 1;
    rdiv++;
  }

  /* Step 2: largest even Multisynth divider keeping the VCO in band. */
  uint32_t D = vco_max / f_ms;
  if (D > 1800UL)
    D = 1800UL;
  D &= ~1UL; /* force even for lowest jitter */
  if (D < 6UL)
    D = 6UL;

  uint32_t fvco = f_ms * D;
  ASSERT((fvco >= vco_min) && (fvco <= vco_max), ERROR_INVALIDPARAMETER);

  /* Step 3: fractional PLL multiplier M = mult + num/denom. */
  uint32_t mult = fvco / fxtal;
  uint32_t rem = fvco % fxtal;
  uint32_t num = (uint32_t)(((uint64_t)rem * denom) / fxtal);
  ASSERT((mult >= 15UL) && (mult <= 90UL), ERROR_INVALIDPARAMETER);

  /* Apply: PLL (fractional) -> Multisynth (integer) -> R divider. */
  ASSERT_STATUS(setupPLL(pll, (uint8_t)mult, num, denom));
  ASSERT_STATUS(setupMultisynth(output, pll, D, 0, 1));
  ASSERT_STATUS(setupRdiv(output, (si5351RDiv_t)rdiv));

  return ERROR_NONE;
}

/**************************************************************************/
/*!
    @brief  Configures the Multisynth divider, which determines the
            output clock frequency based on the specified PLL input.

    @param  output    The output channel to use (0..2)
    @param  pllSource	The PLL input source to use, which must be one of:
                      - SI5351_PLL_A
                      - SI5351_PLL_B
    @param  div       The integer divider for the Multisynth output.
                      If pure integer values are used, this value must
                      be one of:
                      - SI5351_MULTISYNTH_DIV_4
                      - SI5351_MULTISYNTH_DIV_6
                      - SI5351_MULTISYNTH_DIV_8
                      If fractional output is used, this value must be
                      between 8 and 900.
    @param  num       The 20-bit numerator for fractional output
                      (0..1,048,575). Set this to '0' for integer output.
    @param  denom     The 20-bit denominator for fractional output
                      (1..1,048,575). Set this to '1' or higher to
                      avoid divide by zero errors.

    @section Output Clock Configuration

    The multisynth dividers are applied to the specified PLL output,
    and are used to reduce the PLL output to a valid range (500kHz
    to 160MHz). The relationship can be seen in this formula, where
    fVCO is the PLL output frequency and MSx is the multisynth
    divider:

        fOUT = fVCO / MSx

    Valid multisynth dividers are 4, 6, or 8 when using integers,
    or any fractional values between 8 + 1/1,048,575 and 900 + 0/1

    The following formula is used for the fractional mode divider:

        a + b / c

    a = The integer value, which must be 4, 6 or 8 in integer mode (MSx_INT=1)
        or 8..900 in fractional mode (MSx_INT=0).
    b = The fractional numerator (0..1,048,575)
    c = The fractional denominator (1..1,048,575)

    @note   Try to use integers whenever possible to avoid clock jitter

    @note   For output frequencies > 150MHz, you must set the divider
            to 4 and adjust to PLL to generate the frequency (for example
            a PLL of 640 to generate a 160MHz output clock). This is not
            yet supported in the driver, which limits frequencies to
            500kHz .. 150MHz.

    @note   For frequencies below 500kHz (down to 8kHz) Rx_DIV must be
            used, but this isn't currently implemented in the driver.
*/
/**************************************************************************/
err_t Adafruit_SI5351::setupMultisynth(uint8_t output, si5351PLL_t pllSource,
                                       uint32_t div, uint32_t num,
                                       uint32_t denom) {
  uint32_t P1; /* Multisynth config register P1 */
  uint32_t P2; /* Multisynth config register P2 */
  uint32_t P3; /* Multisynth config register P3 */

  /* Basic validation */
  ASSERT(m_si5351Config.initialised, ERROR_DEVICENOTINITIALISED);
  ASSERT(output < 6, ERROR_INVALIDPARAMETER);       /* Channel range (CLK0..5)*/
  ASSERT(div > 3, ERROR_INVALIDPARAMETER);          /* Divider integer value */
  ASSERT(div < 2049, ERROR_INVALIDPARAMETER);       /* Divider integer value */
  ASSERT(denom > 0, ERROR_INVALIDPARAMETER);        /* Avoid divide by zero */
  ASSERT(num <= 0xFFFFF, ERROR_INVALIDPARAMETER);   /* 20-bit limit */
  ASSERT(denom <= 0xFFFFF, ERROR_INVALIDPARAMETER); /* 20-bit limit */

  /* Make sure the requested PLL has been initialised */
  if (pllSource == SI5351_PLL_A) {
    ASSERT(m_si5351Config.plla_configured, ERROR_INVALIDPARAMETER);
  } else {
    ASSERT(m_si5351Config.pllb_configured, ERROR_INVALIDPARAMETER);
  }

  /* Output Multisynth Divider Equations
   *
   * where: a = div, b = num and c = denom
   *
   * P1 register is an 18-bit value using following formula:
   *
   * 	P1[17:0] = 128 * a + floor(128*(b/c)) - 512
   *
   * P2 register is a 20-bit value using the following formula:
   *
   * 	P2[19:0] = 128 * b - c * floor(128*(b/c))
   *
   * P3 register is a 20-bit value using the following formula:
   *
   * 	P3[19:0] = c
   */

  /* Set the main PLL config registers */
  if (num == 0) {
    /* Integer mode */
    P1 = 128 * div - 512;
    P2 = 0;
    P3 = denom;
  } else if (denom == 1) {
    /* Fractional mode, simplified calculations */
    P1 = 128 * div + 128 * num - 512;
    P2 = 128 * num - 128;
    P3 = 1;
  } else {
    /* Fractional mode */
    P1 = (uint32_t)(128 * div + floor(128 * ((float)num / (float)denom)) - 512);
    P2 = (uint32_t)(128 * num -
                    denom * floor(128 * ((float)num / (float)denom)));
    P3 = denom;
  }

  /* Get the appropriate starting point for the PLL registers */
  /* MSx_PARAMETERS_1 registers are 8 bytes apart (42, 50, 58, ...). */
  uint8_t baseaddr = SI5351_REGISTER_42_MULTISYNTH0_PARAMETERS_1 + (output * 8);

  /* Set the MSx config registers */
  /* Burst mode: register address auto-increases */
  uint8_t sendBuffer[9];
  sendBuffer[0] = baseaddr;
  sendBuffer[1] = (P3 & 0xFF00) >> 8;
  sendBuffer[2] = P3 & 0xFF;
  sendBuffer[3] = ((P1 & 0x30000) >> 16) | lastRdivValue[output];
  /* AN619 sec 4.1.3: for fout > 150 MHz the MultiSynth must run in DIVBY4
     mode with a divider of exactly 4. Set MSx_DIVBY4[1:0] = 0b11 (bits 3:2 of
     this register). In this mode P1/P2/P3 are forced to 0/0/1 (handled below).
     Without these bits a div==4 config is rejected and the output is silent. */
  if (div == 4) {
    /* DIVBY4 mode is integer-only; a fractional div==4 is invalid. */
    ASSERT(num == 0, ERROR_INVALIDPARAMETER);
    sendBuffer[3] |= 0x0C;
  }
  sendBuffer[4] = (P1 & 0xFF00) >> 8;
  sendBuffer[5] = P1 & 0xFF;
  sendBuffer[6] = ((P3 & 0xF0000) >> 12) | ((P2 & 0xF0000) >> 16);
  sendBuffer[7] = (P2 & 0xFF00) >> 8;
  sendBuffer[8] = P2 & 0xFF;
  ASSERT_STATUS(writeN(sendBuffer, 9));

  /* Configure the clk control and enable the output */
  /* TODO: Check if the clk control byte needs to be updated. */
  uint8_t clkControlReg = 0x0F; /* 8mA drive strength, MS0 as CLK0 source, Clock
                                   not inverted, powered up */
  if (pllSource == SI5351_PLL_B)
    clkControlReg |= (1 << 5); /* Uses PLLB */
  if (num == 0)
    clkControlReg |= (1 << 6); /* Integer mode */
  /* CLK0..CLK5 control registers are contiguous (16..21). */
  ASSERT_STATUS(
      write8(SI5351_REGISTER_16_CLK0_CONTROL + output, clkControlReg));

  return ERROR_NONE;
}

/**************************************************************************/
/*!
    @brief  Enables or disables all clock outputs
    @param  enabled Whether output is enabled
    @return ERROR_NONE
*/
/**************************************************************************/
err_t Adafruit_SI5351::enableOutputs(bool enabled) {
  /* Make sure we've called init first */
  ASSERT(m_si5351Config.initialised, ERROR_DEVICENOTINITIALISED);

  /* Enabled desired outputs (see Register 3) */
  ASSERT_STATUS(
      write8(SI5351_REGISTER_3_OUTPUT_ENABLE_CONTROL, enabled ? 0x00 : 0xFF));

  return ERROR_NONE;
}

/**************************************************************************/
/*!
    @brief  Reads the current device status register.

    @param  device_status Pointer to where register 0 value will be stored.
    @return ERROR_NONE on success, or an I2C/parameter error.
*/
/**************************************************************************/
err_t Adafruit_SI5351::readDeviceStatus(uint8_t* device_status) {
  ASSERT(device_status != NULL, ERROR_INVALIDPARAMETER);
  ASSERT_STATUS(read8(SI5351_REGISTER_0_DEVICE_STATUS, device_status));
  return ERROR_NONE;
}

/**************************************************************************/
/*!
    @brief  Reads the sticky interrupt status register.

    @param  sticky Pointer to where register 1 value will be stored.
    @return ERROR_NONE on success, or an I2C/parameter error.
*/
/**************************************************************************/
err_t Adafruit_SI5351::readStickyStatus(uint8_t* sticky) {
  ASSERT(sticky != NULL, ERROR_INVALIDPARAMETER);
  ASSERT_STATUS(read8(SI5351_REGISTER_1_INTERRUPT_STATUS_STICKY, sticky));
  return ERROR_NONE;
}

/**************************************************************************/
/*!
    @brief  Clears all sticky interrupt flags.

    @return ERROR_NONE on success, or ERROR_I2C_TRANSACTION on failure.
*/
/**************************************************************************/
err_t Adafruit_SI5351::clearStickyStatus(void) {
  ASSERT_STATUS(write8(SI5351_REGISTER_1_INTERRUPT_STATUS_STICKY, 0x00));
  return ERROR_NONE;
}

/**************************************************************************/
/*!
    @brief  Sets the interrupt status mask register.

    @param  mask Register 2 bitmask where 1 masks (disables) an interrupt
   source.
    @return ERROR_NONE on success, or ERROR_I2C_TRANSACTION on failure.
*/
/**************************************************************************/
err_t Adafruit_SI5351::setInterruptMask(uint8_t mask) {
  ASSERT_STATUS(write8(SI5351_REGISTER_2_INTERRUPT_STATUS_MASK, mask));
  return ERROR_NONE;
}

/**************************************************************************/
/*!
    @brief  Enables or disables spread spectrum
    @param  enabled Whether spread spectrum output is enabled
    @return ERROR_NONE
*/
/**************************************************************************/
err_t Adafruit_SI5351::enableSpreadSpectrum(bool enabled) {
  /* Spread spectrum enable is a single bit (bit 7) of register 149. */
  Adafruit_BusIO_Register ssc_reg(
      i2c_dev, SI5351_REGISTER_149_SPREAD_SPECTRUM_PARAMETERS);
  Adafruit_BusIO_RegisterBits ssc_enable(&ssc_reg, 1, 7);

  if (!ssc_enable.write(enabled ? 1 : 0))
    return ERROR_I2C_TRANSACTION;

  return ERROR_NONE;
}

/* ---------------------------------------------------------------------- */
/* PRUVATE FUNCTIONS                                                      */
/* ---------------------------------------------------------------------- */

/**************************************************************************/
/*!
    @brief  Writes a register and an 8 bit value over I2C
*/
/**************************************************************************/
err_t Adafruit_SI5351::write8(uint8_t reg, uint8_t value) {
  uint8_t buffer[2] = {reg, value};
  if (i2c_dev->write(buffer, 2)) {
    return ERROR_NONE;
  } else {
    return ERROR_I2C_TRANSACTION;
  }
}

/**************************************************************************/
/*!
    @brief  Writes a raw buffer of bytes to the device over I2C.

    The first byte of the buffer is the starting register address; the
    remaining bytes are written sequentially. Used to send the multi-byte
    PLL and Multisynth parameter blocks in a single transaction.

    @param  data  Pointer to the buffer to send. data[0] is the register
                  address, followed by n-1 data bytes.
    @param  n     Total number of bytes to write, including the address.
    @return ERROR_NONE on success, or ERROR_I2C_TRANSACTION on a bus
            failure.
*/
/**************************************************************************/
err_t Adafruit_SI5351::writeN(uint8_t* data, uint8_t n) {
  if (i2c_dev->write(data, n)) {
    return ERROR_NONE;
  } else {
    return ERROR_I2C_TRANSACTION;
  }
}

/**************************************************************************/
/*!
    @brief  Reads an 8 bit value over I2C
*/
/**************************************************************************/
err_t Adafruit_SI5351::read8(uint8_t reg, uint8_t* value) {
  if (i2c_dev->write_then_read(&reg, 1, value, 1)) {
    return ERROR_NONE;
  } else {
    return ERROR_I2C_TRANSACTION;
  }
}
