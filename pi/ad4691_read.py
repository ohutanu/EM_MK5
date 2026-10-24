#!/usr/bin/env python3
"""
EM_MK5: read the AD4691 (16-channel, 16-bit, 500 kSPS SAR ADC) from a Raspberry Pi 5.

Wiring (Pi 5 SPI0):  SCLK, MOSI, MISO as usual.  CE1 -> AD4691 CS and CNV tied together.
(The RP2040 slave is on CE0 of the same bus.)

Mode used: "manual mode" (CNV tied to CS). Every CS-low frame starts a conversion and
returns the result of the PREVIOUS frame. To read N channels we send N channel-select
words and then one NOOP; the first reply is discarded.

Register map and protocol taken from the Linux IIO driver drivers/iio/adc/ad4691.c.
It has NOT been tested on hardware. Items marked [CONFIRM] need the datasheet.

Install:  sudo apt install python3-spidev     (and enable SPI: raspi-config -> Interface)
Run:      python3 ad4691_read.py
"""
import math
import sys
import time

try:
    import spidev
except ImportError:  # allows running the self-test on a PC
    spidev = None

# ---------------- configuration ----------------
SPI_BUS, SPI_DEV = 0, 1          # /dev/spidev0.1  (CE1)
SPI_SPEED_HZ = 10_000_000        # start low; raise after it works. Max speed: [CONFIRM]
SPI_MODE = 0b00                  # [CONFIRM] CPOL/CPHA from the datasheet
VREF = 4.096                     # external reference, volts
USE_INTERNAL_LDO = True          # True: LDO fed from LDO_IN, False: external 1.8 V on VDD. [CONFIRM]
OSC_FREQ_INDEX = 1               # AD4691: 1 = 500 kHz (as in the driver)

# registers
SPI_CONFIG_A = 0x000
DEVICE_SETUP = 0x020
REF_CTRL = 0x021
OSC_FREQ_REG = 0x023
SW_RESET = 0x81                  # BIT7 | BIT0
MANUAL_MODE = 1 << 2
LDO_EN = 1 << 4
REF_4P096 = 3                    # REF_CTRL[4:2] code for 4.096 V (driver enum)
EXIT_COMMAND = 0x5000


def adc_chan(ch):
    """16-bit channel-select word for manual mode (driver: (0x10+ch)<<3 in the high byte)."""
    return (((0x10 + ch) << 3) & 0xFF) << 8


# channel assignment from the EM_MK5 summary
CH_COIL1, CH_COIL2, CH_BATT = 0, 1, 2
CH_NTC = [3, 4, 5, 6, 7]
ACTIVE = [0, 1, 2, 3, 4, 5, 6, 7]

# sensor scaling
INA240A2_V_PER_A = 0.050         # coil channels, REF = GND
INA240A1_V_PER_A = 0.020         # battery channel, gain 20
INA240A1_REF_V = 2.048
NTC_R_PULLUP = 10_000.0          # pull-up to VREF, NTC on the low side
NTC_R25, NTC_BETA = 10_000.0, 3950.0   # [CONFIRM] your thermistor


class AD4691:
    def __init__(self, spi):
        self.spi = spi
        self.manual = False

    # ---- register access (only valid while NOT in manual mode) ----
    def write_reg(self, reg, val):
        self.spi.xfer2([(reg >> 8) & 0x7F, reg & 0xFF, val & 0xFF])

    def read_reg(self, reg):
        rx = self.spi.xfer2([0x80 | ((reg >> 8) & 0x7F), reg & 0xFF, 0x00])
        return rx[2]

    def update_bits(self, reg, mask, value):
        v = self.read_reg(reg)
        self.write_reg(reg, (v & ~mask) | (value & mask))

    # ---- setup ----
    def reset(self):
        self.write_reg(SPI_CONFIG_A, SW_RESET)
        time.sleep(0.002)                      # datasheet/driver: wait 300 us

    def configure(self):
        self.reset()
        ref = (REF_4P096 << 2)                 # external ref on REF pin: REFBUF_EN stays 0
        self.write_reg(REF_CTRL, ref)
        if self.read_reg(REF_CTRL) != ref:
            raise RuntimeError("SPI read-back failed (check wiring, SPI mode, CS on CE1)")
        self.update_bits(DEVICE_SETUP, LDO_EN, LDO_EN if USE_INTERNAL_LDO else 0)
        self.write_reg(OSC_FREQ_REG, OSC_FREQ_INDEX)

    def enter_manual_mode(self):
        self.update_bits(DEVICE_SETUP, MANUAL_MODE, MANUAL_MODE)
        self.manual = True

    def exit_manual_mode(self):
        if self.manual:
            self.spi.xfer2([(EXIT_COMMAND >> 8) & 0xFF, EXIT_COMMAND & 0xFF])
            self.manual = False

    # ---- conversions ----
    def read_raw(self, channels=ACTIVE):
        """One pass over the channels. Returns {channel: raw 16-bit code}."""
        out = {}
        prev = None
        for ch in list(channels) + [None]:     # last frame is the NOOP
            word = 0x0000 if ch is None else adc_chan(ch)
            rx = self.spi.xfer2([word >> 8, word & 0xFF])
            if prev is not None:
                out[prev] = (rx[0] << 8) | rx[1]
            prev = ch
        return out

    @staticmethod
    def volts(raw):
        return raw * VREF / 65536.0


def ntc_celsius(v):
    if v <= 0.001 or v >= VREF - 0.001:
        return float("nan")                    # shorted or open sensor
    r = NTC_R_PULLUP * v / (VREF - v)
    inv_t = 1.0 / 298.15 + math.log(r / NTC_R25) / NTC_BETA
    return 1.0 / inv_t - 273.15


def decode(raw):
    v = {ch: AD4691.volts(code) for ch, code in raw.items()}
    return {
        "coil1_A": v[CH_COIL1] / INA240A2_V_PER_A,
        "coil2_A": v[CH_COIL2] / INA240A2_V_PER_A,
        "batt_A": (v[CH_BATT] - INA240A1_REF_V) / INA240A1_V_PER_A,
        "ntc_C": [ntc_celsius(v[c]) for c in CH_NTC],
    }


def main():
    if spidev is None:
        sys.exit("python3-spidev is not installed")
    spi = spidev.SpiDev()
    spi.open(SPI_BUS, SPI_DEV)
    spi.max_speed_hz = SPI_SPEED_HZ
    spi.mode = SPI_MODE
    spi.bits_per_word = 8
    adc = AD4691(spi)
    try:
        adc.configure()
        adc.enter_manual_mode()
        while True:
            d = decode(adc.read_raw())
            temps = " ".join(f"{t:5.1f}" for t in d["ntc_C"])
            print(f"coil1 {d['coil1_A']:7.2f} A  coil2 {d['coil2_A']:7.2f} A  "
                  f"batt {d['batt_A']:7.2f} A  NTC C: {temps}")
            time.sleep(0.1)
    except KeyboardInterrupt:
        pass
    finally:
        adc.exit_manual_mode()
        spi.close()


if __name__ == "__main__":
    main()
