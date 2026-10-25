# EM_MK5: ESP32-S3-WROOM-1 instead of the Raspberry Pi Pico (RP2040)

Date: 25 Oct 2026. Pins chosen from GPIO1-18, 21, 38-42 and 47, so they are free on every
WROOM-1 variant (no strapping, flash, PSRAM or USB pins). **[CONFIRM]** every pin against the
ESP32-S3-WROOM-1 module datasheet before routing the board.

| Pico signal | Pico pin | ESP32-S3 pin | Notes |
|---|---|---|---|
| PWM_CH1 to CH8 | GP6 to GP13 | GPIO4 to GPIO7, GPIO35, GPIO36, GPIO37, GPIO11 | Pairs (CH1,CH2) = IO4/IO5, (CH3,CH4) = IO6/IO7, (CH5,CH6) = IO35/IO36, (CH7,CH8) = IO37/IO11. MCPWM, hardware dead time. |
| SPI MOSI, CS, SCK, MISO (to the Pi) | GP16, GP17, GP18, GP19 | GPIO13, GPIO12, GPIO14, GPIO21 | SPI2 slave through the GPIO matrix (the fast IOMUX pins 10-13 are used by PWM). Keep the Pi clock to a few MHz. **[CONFIRM]** max slave clock. |
| MainSupp | GP28 (ADC2) | GPIO1 | ADC1 |
| 5V_Meas | GP27 (ADC1) | GPIO2 | ADC1 |
| 3.3V_Meas | GP26 (ADC0) | GPIO15 | ADC2 channel 4. Wi-Fi must stay off. |
| Analog_4 (was ADC_VREF) | pin 35 | GPIO16 | ADC2 channel 5. Wi-Fi must stay off. |
| Ext_Trigg1, Ext_Trigg2 | GP2, GP3 | GPIO17, GPIO18 | Outputs (read from the arrow shapes). |
| Ext_Trigg3, Ext_Trigg4 | GP21, GP22 | GPIO38, GPIO39 | Inputs. |
| UART TX / RX (J1) | GP0, GP1 | GPIO43 (TX0), GPIO44 (RX0) | Also the programming port. |
| I2C SDA / SCL (J2) | GP4, GP5 | GPIO41, GPIO42 | |
| Alarm LED (D3) | GP14 | GPIO40 | |
| pGood LED (D2) | GP15 | GPIO47 | **[CONFIRM]** GPIO47 may run at 1.8 V on octal-PSRAM modules, so the LED is dimmer. |
| RUN button (SW1) | RUN | EN | 10 kΩ pull-up to 3V3 plus 1 µF to GND, button to GND. |
| (none) | | GPIO0 to a BOOT button to GND | Download mode. |
| USB (optional) | | GPIO19 (D-), GPIO20 (D+) | Native USB programming, left unassigned. |

## Differences from the Pico board
- **3.3 V supply:** the module has no 3V3 output. Add an LDO of at least 600 mA (for example
  AP2112K-3.3) from +5 V, with 10 µF and 100 nF at input and output, in place of Pico pin 36.
  D1 (MBR130SFT) is only needed to OR USB 5 V with an external 5 V.
- **ADC range:** linear only up to about 2.5 V (about 3.1 V at the highest attenuation). Resize the
  dividers for MainSupp and 5V_Meas, and add a divider (for example 10 kΩ / 10 kΩ) on 3.3V_Meas.
  The ADC is not ratiometric to 3.3 V. Two inputs are on ADC2, which only works with Wi-Fi off.
  Calibrate and average samples. If this is a problem, move two signals to the AD4691 spare channels.
- **PWM:** MCPWM has a hardware dead-time generator. Use ESP-IDF (PlatformIO: board
  `esp32-s3-devkitc-1`, framework `espidf` or `arduino, espidf`).
- **SPI slave:** transaction-based with fixed length. Keep the Pi clock moderate (a few MHz).
- **Boot:** pins float during reset. The 10 kΩ pull-downs on the ULN2803 inputs keep the gates off.
- IO48 (module pin 25): Status LED (green) with 1 kΩ. Module is the N16R2 (quad flash, quad PSRAM), so IO47/IO48 are 3.3 V and IO35-IO37 are free.
- New analog inputs on ADC1: Analog_5 = IO8 (pin 12), Analog_6 = IO9 (pin 17), Analog_7 = IO10 (pin 18). Each has a divider, 1 kΩ and 100 nF, below about 2.5 V.
- Analog protection: every analog input has 1 kΩ, 100 nF and two BAT54 clamps at the pin (one to +2V5, one to GND). U4 REF3025 makes the +2V5 rail from +3V3 (1 µF out).
- Final analog map: IO1 MainSupp (52 V battery, 100k/3.9k), IO2 5V_Meas (12k/10k), IO15 3.3V_Meas (4.7k/10k, ADC2), IO8 12V_Meas (56k/10k), IO9 NTC1 and IO10 NTC2 (10 kΩ NTC to GND, 10 kΩ pull-up to +2V5). IO16 is spare. This replaces Analog_4 to Analog_7.
- Batt_V (battery 48 to 52 V, 58.4 V full): IO16 (pin 9, ADC2), 100k/3.9k, 1 kΩ, 100 nF and the BAT54 clamps.
- ADC1 only (Wi-Fi safe), final: IO1 MainSupp (52 V), IO2 5V_Meas, IO4 3.3V_Meas, IO5 Batt_V (48 to 52 V), IO8 12V_Meas, IO9 NTC1, IO10 NTC2. PWM_CH1/CH2 moved to IO15/IO16 (digital use of ADC2 pins), so pairs are IO15/IO16, IO6/IO7, IO35/IO36, IO37/IO11. This replaces the ADC2 assignments above.
- Final digital map: PWM_CH1/CH2 on IO38/IO39 (pins 31/32), so the pairs are IO38/IO39, IO6/IO7, IO35/IO36, IO37/IO11. Ext_Trigg1 to Ext_Trigg4 on IO17, IO18, IO15, IO16 (pins 10, 11, 8, 9), which are ADC2 pins used as digital I/O. Ext_Trigg1/2 are outputs, Ext_Trigg3/4 are inputs. This replaces the earlier PWM and trigger assignments.
- Final schematic: ESP32_S3_schematic_final.html/.png. Last free pins now carry User LED 1 to 3: IO3 (pin 15), IO45 (pin 26), IO46 (pin 16), each 1 kΩ to an LED to GND. These are strapping pins: keep the LEDs off during reset and boot.
- U4 changed to REF5025 (2.5 V, 0.05%), with a 1 kΩ bleed resistor from +2V5 to GND so the BAT54 clamps can sink current.
- NTC pull-ups now use a 4.096 V REF5040 (U8) with 22 kΩ pull-ups (keeps the pin under 2.5 V down to about 0 °C). REF5025 (U4) remains only as the +2V5 clamp rail.
