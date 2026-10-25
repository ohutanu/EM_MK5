/*
  EM_MK5: 4 complementary PWM pairs with HARDWARE dead time on the ESP32-S3 (ESP-IDF v5.x, MCPWM).

  Pairs (see ESP32_S3_pinout.md):  A = Q1 gate, B = Q2 gate
    pair 0: GPIO4  / GPIO5    (MCPWM group 0, operator 0)
    pair 1: GPIO6  / GPIO7    (MCPWM group 0, operator 1)
    pair 2: GPIO15 / GPIO16   (MCPWM group 0, operator 2)
    pair 3: GPIO17 / GPIO18   (MCPWM group 1, operator 0)

  Method: generator A makes an edge-aligned PWM. The dead-time submodule delays the rising edge of A
  and feeds generator B with an inverted copy whose rising edge is delayed too, so the two
  outputs never overlap and the gap on each transition is DEAD_NS.

  Not tested on hardware and not compiled here. Check the API names against your ESP-IDF version
  (docs: "Motor Control Pulse Width Modulator (MCPWM)"). [CONFIRM]
  The two groups run on separate timers, so pair 3 is not phase-locked to pairs 0-2.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "driver/mcpwm_prelude.h"

#define RES_HZ      80000000UL                   // timer and dead-time resolution (12.5 ns)
#define PWM_HZ      20000UL
#define PERIOD_TICKS (RES_HZ / PWM_HZ)           // 4000 ticks, edge-aligned (count up)
#define DEAD_NS     1000UL                       // dead time, start at about 1 us and tune
#define DEAD_TICKS  ((DEAD_NS * RES_HZ) / 1000000000UL)   // 80 ticks
#define DUTY_MAX    0.90f

#define NUM_PAIRS 4
static const int pin_a[NUM_PAIRS] = {4, 6, 15, 17};
static const int pin_b[NUM_PAIRS] = {5, 7, 16, 18};
static const int group_of[NUM_PAIRS] = {0, 0, 0, 1};

static mcpwm_timer_handle_t timer[2];
static mcpwm_cmpr_handle_t  cmp[NUM_PAIRS];
static mcpwm_gen_handle_t   gen_a[NUM_PAIRS], gen_b[NUM_PAIRS];

static void pwm_init(void)
{
    for (int g = 0; g < 2; g++) {
        mcpwm_timer_config_t tc = {
            .group_id = g,
            .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
            .resolution_hz = RES_HZ,
            .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
            .period_ticks = PERIOD_TICKS,
        };
        ESP_ERROR_CHECK(mcpwm_new_timer(&tc, &timer[g]));
    }

    mcpwm_oper_handle_t oper[NUM_PAIRS];
    for (int p = 0; p < NUM_PAIRS; p++) {
        mcpwm_operator_config_t oc = { .group_id = group_of[p] };
        ESP_ERROR_CHECK(mcpwm_new_operator(&oc, &oper[p]));
        ESP_ERROR_CHECK(mcpwm_operator_connect_timer(oper[p], timer[group_of[p]]));

        mcpwm_comparator_config_t cc = { .flags.update_cmp_on_tez = true };
        ESP_ERROR_CHECK(mcpwm_new_comparator(oper[p], &cc, &cmp[p]));

        mcpwm_generator_config_t ga = { .gen_gpio_num = pin_a[p] };
        mcpwm_generator_config_t gb = { .gen_gpio_num = pin_b[p] };
        ESP_ERROR_CHECK(mcpwm_new_generator(oper[p], &ga, &gen_a[p]));
        ESP_ERROR_CHECK(mcpwm_new_generator(oper[p], &gb, &gen_b[p]));

        // A: high at timer empty, low at compare match
        ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(gen_a[p],
            MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)));
        ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(gen_a[p],
            MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, cmp[p], MCPWM_GEN_ACTION_LOW)));

        // dead time: A rising edge delayed; B = inverted A with its rising edge delayed
        mcpwm_dead_time_config_t dt_a = { .posedge_delay_ticks = DEAD_TICKS, .negedge_delay_ticks = 0 };
        ESP_ERROR_CHECK(mcpwm_generator_set_dead_time(gen_a[p], gen_a[p], &dt_a));
        mcpwm_dead_time_config_t dt_b = { .posedge_delay_ticks = 0, .negedge_delay_ticks = DEAD_TICKS,
                                          .flags.invert_output = true };
        ESP_ERROR_CHECK(mcpwm_generator_set_dead_time(gen_a[p], gen_b[p], &dt_b));

        // start in the safe state: both outputs forced low
        ESP_ERROR_CHECK(mcpwm_generator_set_force_level(gen_a[p], 0, true));
        ESP_ERROR_CHECK(mcpwm_generator_set_force_level(gen_b[p], 0, true));
        ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(cmp[p], 0));
    }

    for (int g = 0; g < 2; g++) {
        ESP_ERROR_CHECK(mcpwm_timer_enable(timer[g]));
        ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer[g], MCPWM_TIMER_START_NO_STOP));
    }
}

static void pair_off(int p)
{
    ESP_ERROR_CHECK(mcpwm_generator_set_force_level(gen_a[p], 0, true));
    ESP_ERROR_CHECK(mcpwm_generator_set_force_level(gen_b[p], 0, true));
}

// duty = Q1 (A) on-time fraction, 0..1
static void pair_set_duty(int p, float duty)
{
    if (duty <= 0.0f) { pair_off(p); return; }
    if (duty > DUTY_MAX) duty = DUTY_MAX;
    ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(cmp[p], (uint32_t)(duty * PERIOD_TICKS)));
    ESP_ERROR_CHECK(mcpwm_generator_set_force_level(gen_a[p], -1, true));   // release the force
    ESP_ERROR_CHECK(mcpwm_generator_set_force_level(gen_b[p], -1, true));
}

void app_main(void)
{
    pwm_init();
    // demo: ramp all pairs from 10 % to 60 %. The real control replaces this.
    while (1) {
        for (float d = 0.10f; d <= 0.60f; d += 0.05f) {
            for (int p = 0; p < NUM_PAIRS; p++) pair_set_duty(p, d);
            printf("duty %.2f\n", d);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}
