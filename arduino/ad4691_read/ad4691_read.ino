/*
  EM_MK5: read the AD4691 (16-channel, 16-bit SAR ADC) with an Arduino.

  Wiring:  SCK, MOSI, MISO to the board's SPI pins.  CS_PIN -> AD4691 CS and CNV tied together.
  IMPORTANT: the AD4691 is a 3.3 V (or lower) device. Use a 3.3 V board (RP2040, ESP32, Due,
  Teensy). On a 5 V board (Uno, Nano, Mega) you need level shifters on SCK, MOSI and CS.

  Manual mode: every CS-low frame starts a conversion and returns the result of the PREVIOUS
  frame. To read N channels we send N channel-select words plus one NOOP frame.

  Register map taken from the Linux IIO driver (drivers/iio/adc/ad4691.c). NOT tested on
  hardware. Items marked [CONFIRM] must be checked against the datasheet.
*/
#include <SPI.h>

// ---------------- configuration ----------------
const uint8_t  CS_PIN = 10;
const uint32_t SPI_HZ = 4000000;      // start low, raise later. Max speed: [CONFIRM]
const uint8_t  SPI_MODE_USED = SPI_MODE0;   // [CONFIRM] CPOL/CPHA
const float    VREF = 4.096f;         // external reference, volts
const bool     USE_INTERNAL_LDO = true;     // true: LDO from LDO_IN, false: external 1.8 V on VDD [CONFIRM]
const uint8_t  OSC_FREQ_INDEX = 1;    // AD4691: 1 = 500 kHz

// registers
const uint16_t REG_SPI_CONFIG_A = 0x000;
const uint16_t REG_DEVICE_SETUP = 0x020;
const uint16_t REG_REF_CTRL     = 0x021;
const uint16_t REG_OSC_FREQ     = 0x023;
const uint8_t  SW_RESET         = 0x81;
const uint8_t  MANUAL_MODE      = 1 << 2;
const uint8_t  LDO_EN           = 1 << 4;
const uint8_t  REF_4P096        = 3;          // REF_CTRL[4:2] code for 4.096 V
const uint16_t EXIT_COMMAND     = 0x5000;

// EM_MK5 channel map
const uint8_t CH_COIL1 = 0, CH_COIL2 = 1, CH_BATT = 2;
const uint8_t CH_NTC_FIRST = 3, NTC_COUNT = 5;
const uint8_t NUM_CH = 8;             // channels 0..7

// sensor scaling
const float INA240A2_V_PER_A = 0.050f;      // coil channels
const float INA240A1_V_PER_A = 0.020f;      // battery channel
const float INA240A1_REF_V   = 2.048f;
const float NTC_R_PULLUP = 10000.0f;        // pull-up to VREF, NTC on the low side
const float NTC_R25 = 10000.0f, NTC_BETA = 3950.0f;   // [CONFIRM] your thermistor

SPISettings adcSpi(SPI_HZ, MSBFIRST, SPI_MODE_USED);
bool manualMode = false;

// ---------- low-level SPI ----------
void writeReg(uint16_t reg, uint8_t val) {
  SPI.beginTransaction(adcSpi);
  digitalWrite(CS_PIN, LOW);
  SPI.transfer((reg >> 8) & 0x7F);
  SPI.transfer(reg & 0xFF);
  SPI.transfer(val);
  digitalWrite(CS_PIN, HIGH);
  SPI.endTransaction();
}

uint8_t readReg(uint16_t reg) {
  SPI.beginTransaction(adcSpi);
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(0x80 | ((reg >> 8) & 0x7F));
  SPI.transfer(reg & 0xFF);
  uint8_t v = SPI.transfer(0x00);
  digitalWrite(CS_PIN, HIGH);
  SPI.endTransaction();
  return v;
}

void updateBits(uint16_t reg, uint8_t mask, uint8_t value) {
  uint8_t v = readReg(reg);
  writeReg(reg, (v & ~mask) | (value & mask));
}

// 16-bit frame in manual mode; returns the previous conversion result
uint16_t frame(uint16_t word) {
  SPI.beginTransaction(adcSpi);
  digitalWrite(CS_PIN, LOW);
  uint16_t r = SPI.transfer16(word);
  digitalWrite(CS_PIN, HIGH);          // CNV high time must be >= 430 ns
  SPI.endTransaction();
  delayMicroseconds(3);                // conversion time before the next frame
  return r;
}

uint16_t chanWord(uint8_t ch) {        // driver: ((0x10 + ch) << 3) in the high byte
  return (uint16_t)(((0x10 + ch) << 3) & 0xFF) << 8;
}

// ---------- ADC control ----------
bool adcBegin() {
  writeReg(REG_SPI_CONFIG_A, SW_RESET);
  delay(2);                            // 300 us minimum
  uint8_t ref = REF_4P096 << 2;        // external reference: REFBUF_EN stays 0
  writeReg(REG_REF_CTRL, ref);
  if (readReg(REG_REF_CTRL) != ref) return false;   // wiring / SPI mode problem
  updateBits(REG_DEVICE_SETUP, LDO_EN, USE_INTERNAL_LDO ? LDO_EN : 0);
  writeReg(REG_OSC_FREQ, OSC_FREQ_INDEX);
  updateBits(REG_DEVICE_SETUP, MANUAL_MODE, MANUAL_MODE);
  manualMode = true;
  return true;
}

void adcExitManual() {
  if (!manualMode) return;
  frame(EXIT_COMMAND);
  manualMode = false;
}

// reads channels 0..NUM_CH-1 into raw[]
void readAll(uint16_t raw[NUM_CH]) {
  frame(chanWord(0));                  // reply discarded
  for (uint8_t ch = 1; ch < NUM_CH; ch++) raw[ch - 1] = frame(chanWord(ch));
  raw[NUM_CH - 1] = frame(0x0000);     // NOOP returns the last channel
}

float toVolts(uint16_t raw) { return raw * VREF / 65536.0f; }

float ntcCelsius(float v) {
  if (v < 0.001f || v > VREF - 0.001f) return NAN;   // shorted or open
  float r = NTC_R_PULLUP * v / (VREF - v);
  float invT = 1.0f / 298.15f + log(r / NTC_R25) / NTC_BETA;
  return 1.0f / invT - 273.15f;
}

void setup() {
  Serial.begin(115200);
  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, HIGH);
  SPI.begin();
  if (!adcBegin()) {
    Serial.println(F("AD4691 not responding: check wiring, 3.3 V logic and SPI mode"));
    while (true) delay(1000);
  }
  Serial.println(F("AD4691 ready"));
}

void loop() {
  uint16_t raw[NUM_CH];
  readAll(raw);
  float coil1 = toVolts(raw[CH_COIL1]) / INA240A2_V_PER_A;
  float coil2 = toVolts(raw[CH_COIL2]) / INA240A2_V_PER_A;
  float batt  = (toVolts(raw[CH_BATT]) - INA240A1_REF_V) / INA240A1_V_PER_A;
  Serial.print(F("coil1 ")); Serial.print(coil1, 2);
  Serial.print(F(" A  coil2 ")); Serial.print(coil2, 2);
  Serial.print(F(" A  batt ")); Serial.print(batt, 2);
  Serial.print(F(" A  NTC C:"));
  for (uint8_t i = 0; i < NTC_COUNT; i++) {
    Serial.print(' ');
    Serial.print(ntcCelsius(toVolts(raw[CH_NTC_FIRST + i])), 1);
  }
  Serial.println();
  delay(100);
}
