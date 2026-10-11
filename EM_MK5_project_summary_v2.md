# EM_MK5 — Two-Channel Electromagnet Shuttle Controller
## Project Summary v2 — October 2026 (supersedes the July 2026 summary)

Upload this document to a new Claude chat to continue the project with full context.
Items marked **[CONFIRM]** are my assumptions or things to verify against datasheets before building.

---

## 1. APPLICATION OVERVIEW (unchanged)

A 100 mm toroidal NdFeB magnet is shuttled left-to-right and back between two 3 kW electromagnets, at 5 or more mechanical cycles per second. Each electromagnet energises alternately.

- Bus voltage: 60 V DC
- Current per channel: 50 A, 3 kW per channel
- PWM frequency: 20 kHz
- Coil (estimated): L ≈ 20 mH, R ≈ 1.2 Ω, τ ≈ 17 ms
- Energy recovery: 52 V LiFePO4 battery (16S), about 210 W average at 5 Hz

---

## 2. SYSTEM ARCHITECTURE (CURRENT)

### 2.1 Master: Raspberry Pi 5
- Python control loop, feed-forward, fault management, TFT GUI, logging
- SPI0 to RP2040 (CE0) and AD4691 (CE1). SPI1 to TFT, I2C1 to touch controller
- GPIO17: shared latch reset (pulse low, feeds all four /S inputs). GPIO22, 23, 24, 27: fault flags, one per trip latch (CT, coil 1, coil 2, battery). **[CONFIRM]** GPIO24 is free.
- GPIO5, 6, 13: position sensors S1, S2, S3 (via input guard, **falling edge**)
- GPIO23 and GPIO27 are used again as per-latch fault flags (section 6.2)
- MCP3208 and ADS8688A are **removed** (replaced by AD4691 and the RP2040 ADC)

### 2.2 PWM and supply monitoring: RP2040 (SPI slave)
- **8 PWM outputs** on GPIO0 to GPIO7 (PWM slices 0 to 3). Each slice gives one complementary pair: channel A = Q1 gate, channel B = Q2 gate (inverted, with dead time). **[CONFIRM]** pin assignment.
- Dead time: the 2EDN7534R has no built-in dead time. Insert at least 500 ns, plus ACPL delay mismatch (100 ns max). Start at about 1 µs.
- ADC0: bus voltage. ADC1: battery voltage. Both read through the divider in section 8, and the Pi gets the values over SPI.
- Trigger output: was GP4, which now conflicts with PWM. Reassign to another GPIO. **[CONFIRM]**

### 2.3 Precision ADC: AD4691 (replaces ADS8688A and MCP3208)
- 16-channel, 16-bit, 500 kSPS multiplexed SAR with per-channel averaging, 4-wire SPI
- External reference 2.4 to 5.25 V. **4.096 V chosen.** Needs AVDD, VIO and a core supply (VDD or LDO_IN). **[CONFIRM]** exact ranges.
- Channel map:
  - CH0: INA240A2, coil 1 current (0 to about 80 A, see section 5)
  - CH1: INA240A2, coil 2 current
  - CH2: INA240A1 (gain 20), battery current, with REF pin at 2.048 V. **[CONFIRM]**
  - CH3 to CH7: NTC ×5, 10 kΩ pull-ups to the reference (ratiometric)
  - CH8 to CH15: spare
- RC filter at each input: 100 Ω + 1 nF. No built-in input overvoltage tolerance was found in the sources read, so check the absolute maximum rating and keep the INA240 on a 5 V supply.

### 2.4 Display
5 or 7 inch, 800×480, SPI1 video, I2C capacitive touch (unchanged).

---

## 3. POWER STAGE (PER CHANNEL)

### 3.1 Topology (unchanged)
Coil from +60 V (via the CT) to node A. Q1 (main) from node A through the shunt to GND. Q2 (recovery, synchronous) from node A to battery positive. Dead time between Q1 and Q2.

### 3.2 Switches **[CONFIRM: interpretation of "8 PWM"]**
Each switch is **two paralleled SiC MOSFETs**, each with its own PWM signal, driver output and 10 Ω gate resistor: Q1a, Q1b, Q2a, Q2b per channel. Total 8 × IMW40R025M2H (400 V, 65 A, 25.5 mΩ, TO-247-3). Gate drive 18 V.

### 3.3 Other power parts (unchanged)
- Flyback diode VS-60EPU06, one per coil
- Snubber 1 across D1: 10 Ω 2 W + 10 nF C0G
- Snubber 2 across Q1: 4.7 Ω 2 W + 4.7 nF C0G
- Bus TVS P6KE100A
- Shunts BVN-M-R001-1.0 (1 mΩ, 4-terminal Kelvin): coil 1, coil 2, battery
- Battery: 52 V LiFePO4 16S with BMS rated 50 A or more

---

## 4. GATE DRIVE (CURRENT)

Chain per MOSFET: RP2040 PWM → 74LVC244 buffer → 180 Ω → **ACPL-P343/W343** (isolation) → **2EDN7534R** (INA or INB) → 10 Ω Rg → gate.

- 8 PWM optocouplers (one per gate), 4 × 2EDN7534R (each drives a pair of paralleled MOSFETs, one output per MOSFET)
- One 74LVC244 buffers the 8 PWM LEDs (the RP2040 cannot source 8 × 10 mA). Pull-downs on the buffer inputs keep gates off during reset.
- ACPL-343 facts found: single channel, 4 A peak (3 A min), rail-to-rail output, 200 ns max propagation delay, 100 ns max difference between parts, LED input with hysteresis, 3750 Vrms (P343) or 5 kV (W343). **[CONFIRM]** supply range covers 18 V and the recommended LED current.
- 2EDN7534R facts found: dual-channel 5 A low-side driver, non-inverting inputs, enable pins (high or open = on, low = output off), -12 V input robustness, 20 V supply ceiling. The R variant is TSSOP-8. UVLO of the 7-series is believed to be 4.2 V, which is low for SiC. **[CONFIRM]** UVLO and the enable pin input range (+20 V).
- LED resistor: 180 Ω for about 10 mA. **[CONFIRM]**

### 4.1 Enable groups
- Latch Q → 74LVC244 buffer → 180 Ω → ACPL-343 → ENA and ENB of the 2EDN7534R drivers in the group
- As requested: one enable optocoupler for the coil-control group (4 × Q1 gates) and one for the regeneration group (4 × Q2 gates)
- **KNOWN PROBLEM:** the regeneration group spans both channels. Their Q2 sources are at node A1 and node A2, which are at different voltages, so one optocoupler output cannot share its ground with both. Fix: one enable optocoupler per channel for the regeneration side (3 in total).
- Fail-safe: if the 3.3 V logic or the latch output drops, the LEDs turn off and all drivers disable.

### 4.2 Isolated 18 V supplies
- Supply A: all Q1 drivers (common reference, Q1 sources)
- Supply B1 and B2: one per channel, referenced to that channel's node A (Q2 side)
- At least 3 supplies. The B0505S named in the old summary is a 5 V to 5 V module and is not suitable. Use about 18 V isolated converters.
- 100 nF + 1 µF close to every VCC and VDD pin.

---

## 5. CURRENT SENSING

- Shunt 1 mΩ → INA240. Gain 50 V/V (A2) gives 50 mV/A: 2.5 V at 50 A, 3.75 V at 75 A.
- Coil channels: INA240A2, REF to GND, output 0 to about 4 V, fits the 4.096 V reference.
- Battery channel: the old summary numbers (1.65 V reference, 2.5 V at +50 A) do not match gain 50. Proposed: INA240A1 (gain 20, 20 mV/A) with REF at 2.048 V (buffered). Output 1.05 V to 3.05 V for ±50 A. **[CONFIRM]**
- INA240 chosen over Hall sensors: immune to the stray magnetic fields.

---

## 6. CURRENT TRIP (ONE CHANNEL, ON THE MAIN SUPPLY)

Signal chain: CT → Rb → TVS → BAT54 peak hold → LTC6752 → TRIP_N → 74HC279 /R → Q → enable optocouplers.

- CT: CR8348-2000-F, 2000:1, primary is the +60 V feed through the ring (1 turn), **after the bulk capacitors and before the coils**. At 75 A the secondary current is 37.5 mA.
- Rb = 56 Ω (2.1 V at 75 A). TVS across the secondary: **bidirectional 15 V (P6KE15CA)**. The old P6KE15A clamps negative swings at about −0.7 V and can stop the core resetting.
- Peak hold: BAT54, Ch = 4.7 nF, Rdis = 100 kΩ.
- Comparator: **LTC6752 in SOT-23-5**, single supply 2.45 to 3.5 V, powered from +3V3 with 100 nF. 2.9 ns propagation delay, CMOS output (no pull-up). The −IN gets the peak-hold signal, the +IN gets the reference.
- Reference: R1 = 4.7 kΩ from +3V3, R2 = 6.2 kΩ to GND, giving about 1.88 V. Rhyst = 100 kΩ from output to +IN gives about 86 mV (about 3 A) of hysteresis. Trip point about 75 A (150% of 50 A), before the BAT54 drop. **Calibrate with a known current.**
- Latch: see section 6.2. The CT trip (TRIP_N) resets its own latch in the 74HC279.
- Response time is now about 0.25 µs, set mainly by the ACPL-343 (200 ns max). The earlier 35 ns figure applied to the direct enable path and is no longer valid. Check the IMW40R025M2H short-circuit withstand time. **[CONFIRM]**
- Power-up: the latch state is undefined at power-up. Add a power-on reset so Q starts low, then the Pi pulses GPIO17 once the supplies are stable.
- Limits of the CT: it passes only AC, so it works while PWM is switching, and it is blind to a stuck-on switch (DC).

### 6.1 Backup trip from the INA240 outputs (added 11 Oct 2026)
- Three more LTC6752 comparators (SOT-23-5, 3.3 V): coil 1, coil 2 and battery. They cover a stuck-on switch and slower overcurrents.
- Coil channels: INA240A2 output (50 mV/A, 3.75 V at 75 A) → 2.2 kΩ / 2.2 kΩ divider (1:2) → 220 pF to GND (about 0.25 µs filter, blanks switching spikes) → comparator −IN. VREF 1.88 V (4.7 kΩ / 6.2 kΩ from 3V3), Rhyst 100 kΩ. Trip about 75 A per coil.
- Battery channel: INA240A1 output (2.048 V + 20 mV/A) with the same divider and filter. VREF about 1.79 V (4.7 kΩ / 5.6 kΩ). Trip about +77 A (charging direction only). **[CONFIRM]**
- Each comparator output goes straight to the /R input of its own latch (coil 1, coil 2, battery). The earlier BAT54 wired-AND on the trip side is **removed**.
- Response is about 1 µs plus the ACPL path, slower than the CT trip, so the CT stays the fast protection.
- The Q1 shunt sees only Q1 current (pulsed). Recovery current into the battery is covered by the battery channel.
- The INA240 outputs also feed the AD4691 as before. The divider (4.4 kΩ total) loads each INA240 output lightly.

### 6.2 Four trip latches in one 74HC279 (changed 11 Oct 2026)
- One latch per trip source: CT, coil 1, coil 2, battery. The Pi can read which one fired.
- Each latch: /R = its TRIP signal, /S = shared RST net from GPIO17 (10 kΩ pull-up to 3V3), Q high = OK, Q low = tripped.
- Q outputs: each to its own Pi GPIO (22, 23, 24, 27), and each through a BAT54 to a **wired-AND enable node** with one 10 kΩ pull-up. Any Q low pulls the node low. The node drives the 74LVC244 buffer → 180 Ω → the two enable ACPL-343 optocouplers.
- The 74HC279 has four latches, but latches 1 and 3 have **two /S inputs**. Tie /S2 to /S1 on those. All four latches are now used, so there are no unused latches to tie off.
- Power-up: pulse GPIO17 once the supplies and the TRIP inputs are high, otherwise the latches may start tripped.
- **[CONFIRM]** The BAT54 drop adds about 0.3 V to the node low level, still below the 74LVC244 VIL at 3.3 V. Check with the datasheet.

---

## 7. POSITION SENSORS S1, S2, S3

- Air-core inductive PNP, 10 to 30 V output (ferrite-core inductive, Hall and reed types rejected). Examples: Contrinex DW-AD-623-M18, Pepperl+Fuchs NBN8-18GM50-E2, ifm IGC204.
- The old SN74LVC1T45 suggestion is wrong for 10 to 30 V (its inputs top out at 5.5 V). The GTL2003 is also unsuitable. Digital logic elsewhere is all 3.3 V, so no level shifter is needed.
- **Input guard per sensor:** 3.3 kΩ ½ W series resistor → LED of an optocoupler (quad, e.g. TLP291-4) with a 1N4148 anti-parallel across the LED → transistor side with 10 kΩ pull-up to 3V3 → 330 Ω → BAT54S clamp to 3V3 and GND → Pi GPIO. Optional 100 pF at the pin.
- LED current is about 3 mA at 10 V and 9 mA at 30 V. An active sensor pulls the GPIO **low**: use falling-edge interrupts.

---

## 8. SUPPLY VOLTAGE MEASUREMENT (RP2040 ADC)

- Divider for both bus and battery: **51 kΩ / 2.2 kΩ**, 1%, with 100 nF at the node, 1 kΩ series resistor and BAT54S clamp at the RP2040 pin.
- Results: 60 V → 2.49 V, 70 V → 2.9 V, full scale about 80 V. Battery 57.6 V → 2.4 V.
- Old summary values were wrong: 33 kΩ / 10 kΩ gives about 12 V from 52 V (would destroy the pin). 100 kΩ / 5.6 kΩ saturates at about 62 V, so the 70 V limit could not be detected.
- The RP2040 ADC is 12-bit with modest accuracy: calibrate, average, and filter the ADC supply.
- Voltage feed-forward is unchanged: duty_ff = duty_setpoint × (V_nominal / V_supply). Limits: below 20 V or above 70 V, disable both channels.

---

## 9. GPIO AND SPI SUMMARY

| Bus | Device | Purpose |
|-----|--------|---------|
| SPI0 | RP2040 (CE0) | 8 PWM control, supply voltages |
| SPI0 | AD4691 (CE1) | Current and temperature ADC |
| SPI1 | TFT display | Video |
| I2C1 | Touch controller | Touch input |

| Pi GPIO | Function |
|---------|----------|
| 17 | Latch reset (pulse low) |
| 22, 23, 24, 27 | Fault flags: CT, coil 1, coil 2, battery latch Q |
| 5, 6, 13 | Sensors S1, S2, S3 (falling edge, via guard) |

---

## 10. BILL OF MATERIALS CHANGES (SINCE JULY)

| Part | Qty | Notes |
|------|-----|-------|
| IMW40R025M2H SiC MOSFET | 8 | Two paralleled per switch **[CONFIRM]** |
| 2EDN7534R gate driver | 4 | Replaces UCC27524 |
| ACPL-P343 or W343 | 8 PWM + 2 or 3 enable | Replaces ACPL-314J |
| 74LVC244 | 1 or 2 | LED buffers (PWM, enable) |
| LTC6752 (SOT-23-5) | 4 | 1 for the CT trip (replaces 2 × LT1719) plus 3 for the INA240 backup trip |
| BAT54 (enable wired-AND) | 4 | One per latch Q, plus a 10 kΩ pull-up |
| 74HC279 | 1 | Four latches, one per trip source |
| CR8348-2000-F | 1 | On the bus feed (was 2) |
| AD4691 + 4.096 V reference | 1 | Replaces ADS8688A and MCP3208 |
| INA240 | 3 | 2 × A2 (coil), 1 × A1 (battery) |
| Quad optocoupler (e.g. TLP291-4) | 1 | Sensor guard |
| Isolated 18 V converters | 3 or more | Supply A, B1, B2 |
| Removed | | 74HC08, UCC27524, ACPL-314J, TC4452, LT1719, MCP3208, ADS8688A, SN74LVC1T45 |

Unchanged from the July BOM: shunts, flyback diodes, snubbers, bus TVS, NTCs, battery and BMS, Pi 5, TFT, RP2040.

---

## 11. DRAWINGS PRODUCED IN THE LAST CHAT

System sheets: (1) control, ADC and sensors; (2) power path and protection, one channel; (3) gate drive groups.
Schematics: power stage with paralleled switches; gate driver for one MOSFET pair; four trip latches with enable optocouplers; current trip front end (LTC6752); measurement inputs (AD4691 and RP2040 dividers); sensor input guard.
All ten drawings (including the INA240 backup trip and the four-latch schematic) are saved in the companion file EM_MK5_diagrams.html (self-contained, opens in any browser, can be printed to PDF). The unchanged-from-earlier power-stage and gate-drive concepts are described in sections 3 and 4 above, so Claude can also redraw any sheet from this document.

---

## 12. OPEN ITEMS

1. Confirm the paralleled-MOSFET interpretation of "8 PWM".
2. Regeneration enable group: use one enable optocoupler per channel (3 total).
3. Battery INA240 range and the 2.048 V reference buffer.
4. AD4691: supply ranges, input absolute maximum, reference circuit.
5. ACPL-343: supply range at 18 V, LED current, exact part (P343 or W343).
6. 2EDN7534R: UVLO for SiC and enable pin range.
7. RP2040 dead-time implementation and pin assignment (trigger output).
8. CT limitations: DC blindness. Covered by the INA240 backup trip (section 6.1).
9. Latch power-up state.
10. Isolated 18 V converter selection.
11. Still open from July: PCB layout, 60 V / 100 A bus supply, thermal design, BMS selection, software, full costed BOM.

---

## 13. SOURCES USED

- LTC6752 family: https://www.radiolocman.com/datasheet/pdf.html?di=120941
- ACPL-P343/W343: https://docs.broadcom.com/doc/AV02-2928EN
- 2EDN753x: https://www.infineon.com/assets/row/public/documents/24/49/infineon-eicedrive-2edn753x-2edn853x-2edn743x-datasheet-en.pdf
- AD4691: https://www.analog.com/en/products/ad4691.html and https://docs.kernel.org/iio/ad4691.html

---

## 14. NOTES

- User is Romanian, based near Bucharest. Arduino and basic C experience, VS Code with PlatformIO.
- Hardware first, software deferred.
- GitHub: github.com/ohutanu/EM_2026 (KiCad project EM_MK5).
- User requested English corrections on all future text.
