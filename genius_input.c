#include "genius_input.h"
#include "genius.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"

#define DEBOUNCE_US 50000u

// ── State ────────────────────────────────────────────────────────────────────
static volatile uint8_t btn_mask    = 0;
static volatile bool    timed_out   = false;
static alarm_id_t       active_alarm = 0;

// ── IRQ callback ─────────────────────────────────────────────────────────────
static void btn_callback(uint gpio, uint32_t events) {
    static uint32_t last[4] = {0};
    uint32_t now = time_us_32();

    if (!(events & GPIO_IRQ_EDGE_FALL)) return;

    int idx = -1;
    for (int i = 0; i < 4; i++) {
        if (gpio == (uint)BTN_PINS[i]) { idx = i; break; }
    }
    if (idx < 0) return;
    if ((now - last[idx]) < DEBOUNCE_US) return;
    last[idx] = now;
    btn_mask |= (1 << idx);
}

// ── Timer alarm callback ─────────────────────────────────────────────────────
static int64_t timeout_cb(alarm_id_t id, void *data) {
    (void)id; (void)data;
    timed_out = true;
    return 0;
}

// ── Public API ───────────────────────────────────────────────────────────────
void input_init(void) {
    for (int i = 0; i < 4; i++) {
        gpio_init(BTN_PINS[i]);
        gpio_set_dir(BTN_PINS[i], GPIO_IN);
        gpio_pull_up(BTN_PINS[i]);
    }
    gpio_set_irq_enabled_with_callback(BTN_PINS[0], GPIO_IRQ_EDGE_FALL, true, &btn_callback);
    for (int i = 1; i < 4; i++) {
        gpio_set_irq_enabled(BTN_PINS[i], GPIO_IRQ_EDGE_FALL, true);
    }
}

void input_clear(void) {
    btn_mask  = 0;
    timed_out = false;
}

void input_start_timeout(int ms) {
    timed_out = false;
    active_alarm = add_alarm_in_ms(ms, timeout_cb, NULL, false);
}

void input_cancel_timeout(void) {
    if (active_alarm) {
        cancel_alarm(active_alarm);
        active_alarm = 0;
    }
}

bool input_timed_out(void) {
    return timed_out;
}

uint8_t input_get_mask(void) {
    return btn_mask;
}
