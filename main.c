#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "genius.h"
#include "genius_audio.h"
#include "genius_input.h"
#include "genius_display.h"
#include "genius_flash.h"

// ── LED helpers ──────────────────────────────────────────────────────────────
static void led_on(int idx)  { gpio_put(LED_PINS[idx], 1); }
static void led_off(int idx) { gpio_put(LED_PINS[idx], 0); }
static void leds_all_off(void) {
    for (int i = 0; i < 4; i++) gpio_put(LED_PINS[i], 0);
}

// ── Generate a sequence step ─────────────────────────────────────────────────
static uint8_t generate_step(int round) {
    if (round >= MULTI_LED_START && (rand() % 3) == 0) {
        int a = rand() % 4;
        int b;
        do { b = rand() % 4; } while (b == a);
        return (uint8_t)((1 << a) | (1 << b));
    }
    return (uint8_t)(1 << (rand() % 4));
}

// ── Show one step (LEDs + tones) ─────────────────────────────────────────────
static void show_step(uint8_t mask) {
    int count = popcount8(mask);
    int tone_dur = 400 / count;

    // Turn on all LEDs in mask
    for (int i = 0; i < 4; i++)
        if (mask & (1 << i)) led_on(i);

    // Play tone for each LED sequentially (LEDs stay on)
    for (int i = 0; i < 4; i++)
        if (mask & (1 << i)) buzzer_tone(TONE_FREQS[i], tone_dur);

    leds_all_off();
    sleep_ms(100);
}

// ── Error animation ──────────────────────────────────────────────────────────
static void show_error(void) {
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) led_on(j);
        buzzer_tone(150, 300);
        leds_all_off();
        sleep_ms(100);
    }
    sleep_ms(400);
}

// ── Check player input for one step ──────────────────────────────────────────
// Returns: 1 = correct, 0 = wrong, -1 = timeout
static int check_step(uint8_t expected_mask, int timeout_ms) {
    int need = popcount8(expected_mask);
    uint8_t got_mask = 0;
    uint8_t prev_mask = 0;

    input_clear();
    input_start_timeout(timeout_ms);
    uint32_t t0 = time_us_32();
    uint32_t timeout_us = (uint32_t)timeout_ms * 1000;

    while (!input_timed_out() && popcount8(got_mask) < need) {
        uint8_t cur = input_get_mask();
        uint8_t new_bits = cur & ~prev_mask;

        // Feedback for each new press
        for (int i = 0; i < 4; i++) {
            if (new_bits & (1 << i)) {
                led_on(i);
                buzzer_tone(TONE_FREQS[i], 150);
                led_off(i);
            }
        }
        got_mask |= cur;
        prev_mask = cur;

        // Update timer bar
        uint32_t elapsed = time_us_32() - t0;
        int pct = 100 - (int)((uint64_t)elapsed * 100 / timeout_us);
        if (pct < 0) pct = 0;
        display_timer_bar(pct);

        sleep_ms(15);
    }

    input_cancel_timeout();
    display_timer_bar(0);

    if (input_timed_out()) return -1;
    return (got_mask == expected_mask) ? 1 : 0;
}

// ── Main ─────────────────────────────────────────────────────────────────────
int main(void) {
    stdio_init_all();
    srand((unsigned int)time_us_32());

    // Init LEDs
    for (int i = 0; i < 4; i++) {
        gpio_init(LED_PINS[i]);
        gpio_set_dir(LED_PINS[i], GPIO_OUT);
    }

    // Init modules
    audio_init();
    input_init();
    display_init_lcd();
    flash_init();

    // ── Game context ─────────────────────────────────────────────────────────
    game_ctx_t ctx;
    ctx.state          = STATE_MENU;
    ctx.high_score     = 0;

    uint32_t hs_solo  = flash_get_highscore_solo();
    uint32_t hs_dupla = flash_get_highscore_dupla();

    // ── State machine ────────────────────────────────────────────────────────
    while (1) {
        switch (ctx.state) {

        // ═══════════════════════════════════════════════════════════════════
        case STATE_MENU: {
            hs_solo  = flash_get_highscore_solo();
            hs_dupla = flash_get_highscore_dupla();
            display_menu(hs_solo, hs_dupla);
            display_wait_menu_touch();
            ctx.mode           = MODE_SOLO;
            ctx.seq_len        = 0;
            ctx.round          = 0;
            ctx.current_player = 0;
            ctx.high_score     = hs_solo;
            ctx.state          = STATE_COUNTDOWN;
            break;
        }

        // ═══════════════════════════════════════════════════════════════════
        case STATE_COUNTDOWN:
            display_countdown();
            audio_play_melody(MELODY_START);
            sleep_ms(300);
            input_clear();
            ctx.state = STATE_SHOW_SEQ;
            break;

        // ═══════════════════════════════════════════════════════════════════
        case STATE_SHOW_SEQ: {
            // Add new step
            ctx.sequence[ctx.seq_len] = generate_step(ctx.seq_len + 1);
            ctx.seq_len++;
            ctx.round = ctx.seq_len;

            display_round_hud(ctx.round, ctx.current_player, ctx.mode);
            sleep_ms(600);

            // Play full sequence
            for (int i = 0; i < ctx.seq_len; i++)
                show_step(ctx.sequence[i]);

            input_clear();
            ctx.state = STATE_PLAYER_INPUT;
            break;
        }

        // ═══════════════════════════════════════════════════════════════════
        case STATE_PLAYER_INPUT: {
            int timeout = get_timeout_ms(ctx.round);
            bool failed = false;

            for (int i = 0; i < ctx.seq_len; i++) {
                int result = check_step(ctx.sequence[i], timeout);
                if (result != 1) {
                    failed = true;
                    break;
                }
            }

            ctx.state = failed ? STATE_GAME_OVER : STATE_ROUND_OK;
            break;
        }

        // ═══════════════════════════════════════════════════════════════════
        case STATE_ROUND_OK:
            audio_play_melody(MELODY_VICTORY);
            sleep_ms(300);
            ctx.state = STATE_SHOW_SEQ;
            break;

        // ═══════════════════════════════════════════════════════════════════
        case STATE_GAME_OVER: {
            show_error();

            // Check high score
            bool new_record = ((uint32_t)ctx.round > hs_solo);
            if (new_record) {
                hs_solo = (uint32_t)ctx.round;
                flash_save_highscore(hs_solo, hs_dupla);
            }

            display_game_over(ctx.round, hs_solo, new_record,
                              MODE_SOLO, 0);

            // Play celebration or sad melody
            if (new_record)
                audio_play_melody(MELODY_MARIO);
            else
                audio_play_melody(MELODY_GAME_OVER);

            audio_play_wav();

            display_wait_touch();
            ctx.state = STATE_MENU;
            break;
        }

        } // switch
    } // while

    return 0;
}
