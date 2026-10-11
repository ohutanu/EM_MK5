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
- GPIO17: shared latch reset (pulse low, feeds both /S inputs). GPIO22, 23, 24, 27: fault flags (CT, coil, battery, temperature latch).
- GPIO5, 6, 13: position sensors S1, S2, S3 (via input guard, **falling edge**)
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

Chain per MOSFET: RP2040 PWM → ULN2803A input → ULN2803A output (sinks the LED current) → ACPL LED cathode; anode → 240 Ω → +5 V; ACPL-**P343/W343** (isolation) → **2EDN7534R** (INA or INB) → 10 Ω Rg → gate.

- 8 PWM optocouplers (one per gate), 4 × 2EDN7534R (each drives a pair of paralleled MOSFETs, one output per MOSFET)
- One ULN2803A sinks the 8 PWM LEDs (the RP2040 cannot drive 8 × 10 mA). It is non-inverting overall: input high = LED on = gate on. 10 kΩ pull-downs on the inputs keep gates off during reset and while the RP2040 pins float. Unused inputs to GND, pin 10 (COM) to +5 V, 100 nF at the supply.
- LED supply is +5 V, not 3.3 V: (5 − 1.55 − 0.9) / 240 Ω ≈ 10.6 mA. At 3.3 V there would be no headroom for the Darlington VCE(sat).
- ULN2803A facts found: 500 mA per channel, VCE(sat) 0.9 V typ and 1.1 V max at 100 mA, 2.7 kΩ internal base resistor, tPLH 130 ns and tPHL 20 ns (test conditions differ from this circuit). **[CONFIRM]** the input threshold with 3.3 V logic at about 10 mA load (the datasheet gives VI(on) for 200 mA and above); a 3.3 V logic high gives about 0.7 mA base current.
- **[CONFIRM]** Dead time: the Darlington turns off slower than on (saturation storage), which shifts the Q1/Q2 edges by an unknown amount. Keep at least 1 µs dead time in the RP2040 and measure the gate waveforms. If the asymmetry is too large, use discrete NPN (BC847) or N-MOSFET (2N7002) low-side sinks run from 3.3 V.
- ACPL-343 facts found: single channel, 4 A peak (3 A min), rail-to-rail output, 200 ns max propagation delay, 100 ns max difference between parts, LED input with hysteresis, 3750 Vrms (P343) or 5 kV (W343). **[CONFIRM]** supply range covers 18 V and the recommended LED current.
- 2EDN7534R facts found: dual-channel 5 A low-side driver, non-inverting inputs, enable pins (high or open = on, low = output off), -12 V input robustness, 20 V supply ceiling. The R variant is TSSOP-8. UVLO of the 7-series is believed to be 4.2 V, which is low for SiC. **[CONFIRM]** UVLO and the enable pin input range (+20 V).
- LED resistor: 240 Ω from +5 V for about 10 mA. **[CONFIRM]** against the ACPL-343 recommended LED current.

### 4.1 Enable groups
- Enable node (2 Q outputs, current and temperature, ANDed by a **74HCT21** on +5 V, both halves with inputs in parallel) → 470 Ω → ACPL-343 LED anode, cathode to GND. No transistor. One half drives the coil and regen 1 LEDs, the other drives regen 2. → ENA and ENB of the 2EDN7534R drivers in the group
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

Signal chain: CT → Rb → TVS → BAT54 peak hold → LTC6752 → TRIP_CT → 74LS279 /R → Q → 74HCT21 → enable optocouplers.

- CT: CR8348-2000-F, 2000:1, primary is the +60 V feed through the ring (1 turn), **after the bulk capacitors and before the coils**. At 75 A the secondary current is 37.5 mA.
- Rb = 56 Ω (2.1 V at 75 A). TVS across the secondary: **bidirectional 15 V (P6KE15CA)**. The old P6KE15A clamps negative swings at about −0.7 V and can stop the core resetting.
- Peak hold: BAT54, Ch = 4.7 nF, Rdis = 100 kΩ.
- Comparator (now shared, see 6.1): **LTC6752 in SOT-23-5**, single supply 2.45 to 3.5 V, powered from +3V3 with 100 nF. 2.9 ns propagation delay, CMOS output (no pull-up). The −IN gets the peak-hold signal, the +IN gets the reference.
- Reference: R1 = 4.7 kΩ from +3V3, R2 = 6.2 kΩ to GND, giving about 1.88 V. Rhyst = 100 kΩ from output to +IN gives about 86 mV (about 3 A) of hysteresis. Trip point about 75 A (150% of 50 A), before the BAT54 drop. **Calibrate with a known current.**
- Latch: see section 6.3. TRIP_CT resets the CT latch. The coil and battery trips are in section 6.1.
- Response time is now about 0.25 µs, set mainly by the ACPL-343 (200 ns max). The earlier 35 ns figure applied to the direct enable path and is no longer valid. Check the IMW40R025M2H short-circuit withstand time. **[CONFIRM]**
- Power-up: the latch state is undefined at power-up. Add a power-on reset so Q starts low, then the Pi pulses GPIO17 once the supplies are stable.
- Limits of the CT: it passes only AC, so it works while PWM is switching, and it is blind to a stuck-on switch (DC).

### 6.1 Coil and battery current trips from the INA240 outputs (changed 11 Oct 2026)
- **Coil trip:** the INA240A2 outputs of coil 1 and coil 2 (50 mV/A, 3.75 V at 75 A) each go through a 2.2 kΩ / 2.2 kΩ divider (1:2) and 220 pF to GND (about 0.25 µs filter), then through a **BAT54 diode-OR (2 diodes, 100 kΩ pull-down)** into one **LTC6752** (SOT-23-5, 3.3 V). VREF 1.88 V (4.7 kΩ / 6.2 kΩ from 3V3), Rhyst 100 kΩ. Output TRIP_COIL. Trip about 75 A on either coil.
- **Battery (regenerative stage) trip:** INA240A1 output (2.048 V + 20 mV/A) with the same divider and filter into its own LTC6752. VREF about 1.79 V (4.7 kΩ / 5.6 kΩ). Trip about +77 A (charging direction only). Output TRIP_BAT. **[CONFIRM]**
- **CT (power stage supply) trip:** its own LTC6752, section 6. Output TRIP_CT.
- **[CONFIRM]** The BAT54 drop in the coil OR (about 0.25 V, changes with temperature) adds to the coil trip level. Trim the divider by measurement.
- Coil 1 and coil 2 share a latch, so the Pi cannot tell them apart from the flag. The AD4691 logs both currents.
- Response of the INA240 paths is about 1 µs plus the ACPL path, slower than the CT path, which stays the fast protection.
- The Q1 shunt sees only Q1 current (pulsed). Recovery current into the battery is covered by the battery channel.

### 6.2 Radiator temperature trip: one comparator for all NTCs (added 11 Oct 2026)
- The five NTC dividers (10 kΩ pull-up to the 4.096 V reference, NTC to GND) fall in voltage as the radiator heats. Five BAT54 diodes (cathodes to the NTC nodes, anodes joined at node M) select the lowest voltage, so M follows the hottest radiator. 100 kΩ from M to 3V3.
- Comparator: **LTC6752** (same part as the other trips), supplied from +3V3 with 100 nF, CMOS output (no pull-up), Rhyst 1 MΩ (about 25 mV, or a few K of hysteresis), 10 nF from M to GND against noise. +IN = M, −IN = VREF about 0.83 V (27 kΩ / 9.1 kΩ from 3V3). Output TRIP_T goes low when hot → /R of the temperature latch. A 2.9 ns part on a slow signal can chatter, so check the hysteresis and filter on the bench. Trip near 80 °C. **[CONFIRM]** NTC type and trip temperature; trim VREF by measurement.
- The diode taps are taken from the divider nodes and add only leakage load. A shorted NTC also trips, an open NTC does not (the AD4691 reading shows it).

### 6.3 Four trip latches in one 74LS279 (changed 11 Oct 2026)
- All four latches are used, one per trip source: **CT** (power stage supply current, TRIP_CT), **coil** (TRIP_COIL), **battery** (regenerative stage, TRIP_BAT) and **radiator temperature** (TRIP_T). The Pi can read which one fired.
- Each latch: /R = its TRIP signal, /S = shared RST net from GPIO17 (10 kΩ pull-up to 3V3), Q high = OK, Q low = tripped. Latches 1 and 3 have two /S inputs: tie /S2 to /S1.
- **74LS279 on +5 V** (5 V TTL, 100 nF decoupling). Inputs: /R from the LTC6752 outputs (3.3 V CMOS, clears VIH 2.0 V, sinks the 0.4 mA LS input current); /S from GPIO17. Outputs: Q low ≤ 0.5 V at 8 mA, Q high 2.7 V min at only 0.4 mA, so Q must not drive anything heavy.
- Q outputs: GPIO22 (CT), GPIO23 (coil), GPIO24 (battery), GPIO27 (temperature), each through a **3.3 kΩ / 6.8 kΩ divider** (the LS Q high is 2.7 V min and can reach 3.4 V or more; the Pi GPIO must not see more than 3.3 V). **[CONFIRM]** Pi GPIO VIH against the divided Q high level (about 1.8 V min).
- Each Q also goes to an input of the **74HCT21** (4-input AND on +5 V, TTL-compatible inputs). Both halves are used with inputs in parallel. Any Q low → output low → LEDs off → gates disabled.
- Power-up: pulse GPIO17 once the supplies and the TRIP inputs are high, otherwise the latches may start tripped.
- **[CONFIRM]** Direct drive: a 74HCT21 on +5 V gives about 3.6 V at 8 mA (two LEDs, 470 Ω, about 4 mA each). Check VOH under load and the ACPL-343 minimum LED threshold current (IFLH). If it fails, add one BSS138 per half to sink the LED current from +5 V.

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
| 22, 23, 24, 27 | Fault flags: CT, coil, battery, temperature latch Q (each through a 3.3 kΩ / 6.8 kΩ divider) |
| 5, 6, 13 | Sensors S1, S2, S3 (falling edge, via guard) |

---

## 10. BILL OF MATERIALS CHANGES (SINCE JULY)

| Part | Qty | Notes |
|------|-----|-------|
| IMW40R025M2H SiC MOSFET | 8 | Two paralleled per switch **[CONFIRM]** |
| 2EDN7534R gate driver | 4 | Replaces UCC27524 |
| ACPL-P343 or W343 | 8 PWM + 2 or 3 enable | Replaces ACPL-314J |
| ULN2803A | 1 | 8 PWM LEDs |
| 74HCT21 | 1 | Dual 4-input AND on +5 V, makes the enable node and drives the enable LEDs directly |
| Resistors 240 Ω (PWM LEDs), 470 Ω (enable LEDs), 10 kΩ | 8 + 3 + 8 | LED series and PWM input pull-downs |
| Resistors 3.3 kΩ and 6.8 kΩ | 4 + 4 | Dividers from the two LS279 Q outputs to the Pi GPIOs |
| LTC6752 (SOT-23-5) | 4 | CT trip (replaces 2 × LT1719), coil trip, battery trip, radiator temperature trip |
| BAT54 | 7 | 2 coil OR + 5 NTC OR |
| 74LS279 | 1 | 5 V TTL, four latches, one per trip source |
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
All ten drawings (including the coil and battery current trips, radiator temperature trip and four-latch schematic) are saved in the companion file EM_MK5_diagrams.html (self-contained, opens in any browser, can be printed to PDF). The unchanged-from-earlier power-stage and gate-drive concepts are described in sections 3 and 4 above, so Claude can also redraw any sheet from this document.

---

## 12. OPEN ITEMS

1. Confirm the paralleled-MOSFET interpretation of "8 PWM".
2. Regeneration enable group: use one enable optocoupler per channel (3 total).
3. Battery INA240 range and the 2.048 V reference buffer.
4. AD4691: supply ranges, input absolute maximum, reference circuit.
5. ACPL-343: supply range at 18 V, LED current, exact part (P343 or W343).
6. 2EDN7534R: UVLO for SiC and enable pin range.
7. RP2040 dead-time implementation and pin assignment (trigger output).
8. CT limitations: DC blindness. Covered by the INA240 coil and battery trips (section 6.1).
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
