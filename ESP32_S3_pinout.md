# EM_MK5: ESP32-S3-WROOM-1 instead of the Raspberry Pi Pico (RP2040)

Date: 25 Oct 2026. Pins chosen from GPIO1-18, 21 and 38-42 only, so they are free on every
WROOM-1 variant (no strapping, flash, PSRAM or USB pins). **[CONFIRM]** every pin against the
ESP32-S3-WROOM-1 module datasheet before routing the board.

| Pico signal | Pico pin | ESP32-S3 pin | Notes |
|---|---|---|---|
| PWM_CH1 to CH8 | GP6 to GP13 | GPIO4, 5, 6, 7, 15, 16, 17, 18 | Pairs (CH1,CH2), (CH3,CH4), (CH5,CH6), (CH7,CH8). MCPWM0 operators 0-2, MCPWM1 operator 0. Hardware dead time, at least 1 µs. |
| SPI MISO, SCK, CS, MOSI (to the Pi) | GP19, GP18, GP17, GP16 | GPIO13, GPIO12, GPIO10, GPIO11 | SPI2 (FSPI) slave with DMA. MISO is the ESP32 output. |
| MainSupp | GP28 (ADC2) | GPIO1 | ADC1 |
| 5V_Meas | GP27 (ADC1) | GPIO2 | ADC1 |
| 3.3V_Meas | GP26 (ADC0) | GPIO8 | ADC1 |
| ADC_VREF | pin 35 | none | No reference pin. Remove the REF_3.3 net. |
| Ext_Trigg1, Ext_Trigg2 | GP2, GP3 | GPIO14, GPIO21 | Outputs (read from the arrow shapes). |
| Ext_Trigg3, Ext_Trigg4 | GP21, GP22 | GPIO9, GPIO38 | Inputs. |
| UART TX / RX (J1) | GP0, GP1 | GPIO43 (TX0), GPIO44 (RX0) | Also the programming port. |
| I2C SDA / SCL (J2) | GP4, GP5 | GPIO41, GPIO42 | |
| Alarm LED (D3) | GP14 | GPIO39 | |
| pGood LED (D2) | GP15 | GPIO40 | |
| RUN button (SW1) | RUN | EN | 10 kΩ pull-up to 3V3 plus 1 µF to GND, button to GND. |
| (none) | | GPIO0 to a BOOT button to GND | Download mode. |
| USB (optional) | | GPIO19 (D-), GPIO20 (D+) | Native USB programming, left unassigned. |

## Differences from the Pico board
- **3.3 V supply:** the module has no 3V3 output. Add an LDO of at least 600 mA (for example
  AP2112K-3.3) from +5 V, with 10 µF and 100 nF at input and output, in place of Pico pin 36.
  D1 (MBR130SFT) is only needed to OR USB 5 V with an external 5 V.
- **ADC range:** linear only up to about 2.5 V (about 3.1 V at the highest attenuation). Resize the
  dividers for MainSupp and 5V_Meas, and add a divider (for example 10 kΩ / 10 kΩ) on 3.3V_Meas.
  The ADC is not ratiometric to 3.3 V. Use ADC1 only, calibrate, average samples, keep Wi-Fi off.
- **PWM:** MCPWM has a hardware dead-time generator. Use ESP-IDF (PlatformIO: board
  `esp32-s3-devkitc-1`, framework `espidf` or `arduino, espidf`).
- **SPI slave:** transaction-based with fixed length. Keep the Pi clock moderate (a few MHz).
- **Boot:** pins float during reset. The 10 kΩ pull-downs on the ULN2803 inputs keep the gates off.
