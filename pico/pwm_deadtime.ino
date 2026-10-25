/*
  EM_MK5: software dead time on the RP2040 PWM channels GP6..GP13 (Raspberry Pi Pico).

  GP6/GP7 = slice 3, GP8/GP9 = slice 4, GP10/GP11 = slice 5, GP12/GP13 = slice 6.
  Each slice is one complementary pair: channel A (even GP) = Q1 gate, channel B (odd GP) = Q2 gate.

  Method: phase-correct mode (counter counts 0..TOP..0), channel B output inverted.
    A is high while counter <  CC_A
    B is high while counter >= CC_B
  so the gap on each transition is (CC_B - CC_A) counter clocks. At 125 MHz and clkdiv 1
  one count = 8 ns, so 1 us dead time = 125 counts.

  Works with the Arduino-Pico core (earlephilhower) or the Pico SDK. NOT tested on hardware.
  Writes to the compare registers take effect at the next counter wrap [CONFIRM in datasheet].
*/
#include <Arduino.h>
#include "hardware/pwm.h"
#include "hardware/gpio.h"

const uint8_t  FIRST_PIN  = 6;                 // GP6..GP13
const uint8_t  NUM_PAIRS  = 4;
const uint32_t SYS_HZ     = 125000000UL;       // [CONFIRM] sys clock (clock_get_hz(clk_sys))
const uint32_t PWM_HZ     = 20000UL;
const uint16_t TOP        = SYS_HZ / (2UL * PWM_HZ) - 1;   // 3124 -> 20 kHz phase-correct
const uint16_t PERIOD     = TOP + 1;           // counts per half period
const uint16_t DEAD_COUNT = 125;               // 1 us at 125 MHz. Start at about 1 us, tune on the scope.
const float    DUTY_MAX   = 0.90f;             // limit, keeps B on-time above zero

uint8_t sliceOf(uint8_t pair) { return pwm_gpio_to_slice_num(FIRST_PIN + 2 * pair); }

// both outputs low (safe state)
void pairOff(uint8_t pair) {
  uint8_t s = sliceOf(pair);
  pwm_set_chan_level(s, PWM_CHAN_A, 0);        // A never high
  pwm_set_chan_level(s, PWM_CHAN_B, PERIOD);   // inverted B never high
}

// duty 0..1 = Q1 (A) on-time fraction; Q2 (B) is the complement minus the dead time
void pairSetDuty(uint8_t pair, float duty) {
  if (duty <= 0.0f) { pairOff(pair); return; }
  if (duty > DUTY_MAX) duty = DUTY_MAX;
  uint16_t ccA = (uint16_t)(duty * PERIOD + 0.5f);
  uint16_t ccB = ccA + DEAD_COUNT;
  if (ccB > PERIOD) ccB = PERIOD;
  uint8_t s = sliceOf(pair);
  pwm_set_chan_level(s, PWM_CHAN_A, ccA);
  pwm_set_chan_level(s, PWM_CHAN_B, ccB);
}

void pwmBegin() {
  uint32_t mask = 0;
  for (uint8_t p = 0; p < NUM_PAIRS; p++) {
    uint8_t pinA = FIRST_PIN + 2 * p;
    uint8_t s = sliceOf(p);
    gpio_set_function(pinA,     GPIO_FUNC_PWM);
    gpio_set_function(pinA + 1, GPIO_FUNC_PWM);
    pwm_config c = pwm_get_default_config();
    pwm_config_set_phase_correct(&c, true);
    pwm_config_set_clkdiv(&c, 1.0f);
    pwm_config_set_wrap(&c, TOP);
    pwm_config_set_output_polarity(&c, false, true);   // A normal, B inverted
    pwm_init(s, &c, false);                            // configured, not started
    pairOff(p);
    pwm_set_counter(s, 0);
    mask |= 1u << s;
  }
  pwm_set_mask_enabled(mask);                          // start all four slices together
}

void setup() {
  Serial.begin(115200);
  pwmBegin();
}

void loop() {
  // demo: ramp the duty of all pairs from 10 % to 60 %. Put the real control here.
  for (float d = 0.10f; d <= 0.60f; d += 0.05f) {
    for (uint8_t p = 0; p < NUM_PAIRS; p++) pairSetDuty(p, d);
    Serial.println(d);
    delay(500);
  }
}
