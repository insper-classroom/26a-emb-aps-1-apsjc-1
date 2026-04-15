#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include "tft_lcd_ili9341/ili9341/ili9341.h"
#include "tft_lcd_ili9341/gfx/gfx_ili9341.h"
#include "tft_lcd_ili9341/touch_resistive/touch_resistive.h"

#include "audio_voz.h"

// ═══════════════════════════════════════════════════════════════════════════════
// CONSTANTES E TIPOS
// ═══════════════════════════════════════════════════════════════════════════════

#define SCREEN_W        320
#define SCREEN_H        240
#define SCREEN_ROTATION 3

#define MAX_SEQ         100
#define MULTI_LED_START 7
#define DEBOUNCE_US     50000u
#define PIN_AUDIO       10

#define TIMEOUT_EASY    5000
#define TIMEOUT_MEDIUM  4000
#define TIMEOUT_HARD    3000
#define TIMEOUT_EXPERT  2000

// GPIOs
const int LED_PINS[4]   = {5, 2, 3, 4};
const int BTN_PINS[4]   = {9, 6, 7, 8};
const int TONE_FREQS[4] = {262, 330, 392, 523};

// State machine
typedef enum {
    STATE_MENU,
    STATE_COUNTDOWN,
    STATE_SHOW_SEQ,
    STATE_PLAYER_INPUT,
    STATE_ROUND_OK,
    STATE_GAME_OVER,
} game_state_t;

typedef enum {
    MODE_SOLO,
    MODE_DUPLA,
} game_mode_t;

typedef struct {
    game_state_t state;
    game_mode_t  mode;
    uint8_t      sequence[MAX_SEQ];
    int          seq_len;
    int          current_player;
    int          round;
    uint32_t     high_score;
} game_ctx_t;

// Notas musicais
typedef struct {
    uint16_t freq;
    uint16_t duration_ms;
} note_t;

typedef enum {
    MELODY_VICTORY,
    MELODY_MARIO,
    MELODY_GAME_OVER,
    MELODY_START,
} melody_id_t;

// Layout do menu
#define MENU_BTN_X    60
#define MENU_BTN_Y    110
#define MENU_BTN_W    200
#define MENU_BTN_H    60
#define TIMER_BAR_Y   32
#define TIMER_BAR_H   8

// Flash
#define FLASH_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define FLASH_MAGIC         0x47454E49u

typedef struct {
    uint32_t magic;
    uint32_t solo_best;
} flash_data_t;

// ═══════════════════════════════════════════════════════════════════════════════
// HELPERS
// ═══════════════════════════════════════════════════════════════════════════════

static int get_timeout_ms(int round) {
    if (round <= 3)  return TIMEOUT_EASY;
    if (round <= 7)  return TIMEOUT_MEDIUM;
    if (round <= 12) return TIMEOUT_HARD;
    return TIMEOUT_EXPERT;
}

static int popcount8(uint8_t x) {
    int c = 0;
    while (x) { c += x & 1; x >>= 1; }
    return c;
}

// ═══════════════════════════════════════════════════════════════════════════════
// FLASH - HIGH SCORE PERSISTENTE
// ═══════════════════════════════════════════════════════════════════════════════

static uint32_t cached_best = 0;

void flash_init(void) {
    const flash_data_t *data =
        (const flash_data_t *)(XIP_BASE + FLASH_TARGET_OFFSET);
    if (data->magic == FLASH_MAGIC) {
        cached_best = data->solo_best;
    }
}

uint32_t flash_get_best(void) { return cached_best; }

void flash_save_best(uint32_t best) {
    if (best == cached_best) return;

    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, FLASH_PAGE_SIZE);
    flash_data_t *d = (flash_data_t *)page;
    d->magic     = FLASH_MAGIC;
    d->solo_best = best;

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_TARGET_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts(ints);

    cached_best = best;
}

// ═══════════════════════════════════════════════════════════════════════════════
// AUDIO - PWM SPEAKER + MELODIAS
// ═══════════════════════════════════════════════════════════════════════════════

static volatile int  wav_position = 0;
static volatile bool wav_playing  = false;

void pwm_irq_handler(void) {
    pwm_clear_irq(pwm_gpio_to_slice_num(PIN_AUDIO));
    if (!wav_playing) {
        pwm_set_gpio_level(PIN_AUDIO, 0);
        return;
    }
    if (wav_position < (WAV_DATA_LENGTH << 3) - 1) {
        int sample = (int)WAV_DATA[wav_position >> 3] - 127;
        sample *= 4;
        sample += 127;
        if (sample > 255) sample = 255;
        if (sample < 0)   sample = 0;
        pwm_set_gpio_level(PIN_AUDIO, (uint16_t)sample);
        wav_position++;
    } else {
        wav_position = 0;
        wav_playing  = false;
        pwm_set_gpio_level(PIN_AUDIO, 0);
    }
}

#define NOTE_C4  262
#define NOTE_D4  294
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_C5  523
#define NOTE_E5  659
#define NOTE_G5  784
#define NOTE_C6  1047

static const note_t melody_victory[]   = {{NOTE_C5,80},{NOTE_E5,80},{NOTE_G5,80},{NOTE_C6,160},{0,0}};
static const note_t melody_mario[]     = {{NOTE_E5,125},{NOTE_E5,125},{0,65},{NOTE_E5,125},{0,65},{NOTE_C5,125},{NOTE_E5,125},{0,65},{NOTE_G5,250},{0,250},{NOTE_G4,250},{0,0}};
static const note_t melody_game_over[] = {{NOTE_G4,200},{0,50},{NOTE_F4,200},{0,50},{NOTE_E4,200},{0,50},{NOTE_D4,400},{0,0}};
static const note_t melody_start[]     = {{NOTE_G4,100},{NOTE_C5,100},{NOTE_E5,100},{NOTE_G5,200},{0,0}};

static const note_t *melody_table[] = {
    [MELODY_VICTORY]   = melody_victory,
    [MELODY_MARIO]     = melody_mario,
    [MELODY_GAME_OVER] = melody_game_over,
    [MELODY_START]     = melody_start,
};

void audio_init(void) {
    gpio_set_function(PIN_AUDIO, GPIO_FUNC_PWM);
    int slice = pwm_gpio_to_slice_num(PIN_AUDIO);
    pwm_clear_irq(slice);
    pwm_set_irq_enabled(slice, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_irq_handler);
    irq_set_enabled(PWM_IRQ_WRAP, true);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, 4.0f);
    pwm_config_set_wrap(&cfg, 255);
    pwm_init(slice, &cfg, true);
    pwm_set_gpio_level(PIN_AUDIO, 0);
}

void buzzer_tone(int freq, int duration_ms) {
    if (freq <= 0) { sleep_ms(duration_ms); return; }

    int slice = pwm_gpio_to_slice_num(PIN_AUDIO);
    irq_set_enabled(PWM_IRQ_WRAP, false);
    pwm_set_irq_enabled(slice, false);

    uint32_t sysclk = clock_get_hz(clk_sys);
    uint16_t wrap = 10000;
    float clkdiv = (float)sysclk / ((float)freq * (wrap + 1));
    if (clkdiv < 1.0f)   clkdiv = 1.0f;
    if (clkdiv > 255.0f) clkdiv = 255.0f;

    pwm_set_clkdiv(slice, clkdiv);
    pwm_set_wrap(slice, wrap);
    pwm_set_gpio_level(PIN_AUDIO, wrap / 2);
    sleep_ms(duration_ms);

    pwm_set_gpio_level(PIN_AUDIO, 0);
    pwm_set_clkdiv(slice, 4.0f);
    pwm_set_wrap(slice, 255);
    pwm_clear_irq(slice);
    pwm_set_irq_enabled(slice, true);
    irq_set_enabled(PWM_IRQ_WRAP, true);
}

void audio_play_melody(melody_id_t id) {
    const note_t *m = melody_table[id];
    while (m->freq || m->duration_ms) {
        buzzer_tone(m->freq, m->duration_ms);
        m++;
    }
}

void audio_play_wav(void) {
    wav_position = 0;
    wav_playing  = true;
    while (wav_playing) sleep_ms(1);
}

// ═══════════════════════════════════════════════════════════════════════════════
// INPUT - BOTOES COM IRQ + TIMEOUT POR ALARM
// ═══════════════════════════════════════════════════════════════════════════════

static volatile uint8_t btn_mask  = 0;
static volatile bool    timed_out = false;
static alarm_id_t       active_alarm = 0;

void btn_callback(uint gpio, uint32_t events) {
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
    btn_mask |= (uint8_t)(1 << idx);
}

int64_t timeout_cb(alarm_id_t id, void *data) {
    (void)id; (void)data;
    timed_out = true;
    return 0;
}

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
    if (active_alarm) { cancel_alarm(active_alarm); active_alarm = 0; }
}

// ═══════════════════════════════════════════════════════════════════════════════
// DISPLAY - TELA LCD ILI9341
// ═══════════════════════════════════════════════════════════════════════════════

static uint16_t timer_color(int pct) {
    if (pct > 60) return ILI9341_GREEN;
    if (pct > 30) return ILI9341_YELLOW;
    return ILI9341_RED;
}

void display_init_lcd(void) {
    LCD_initDisplay();
    LCD_setRotation(SCREEN_ROTATION);
    gfx_init();
    gfx_clear();
    configure_touch();
}

void display_menu(uint32_t hi_score) {
    gfx_fillRect(0, 0, SCREEN_W, SCREEN_H, ILI9341_BLACK);
    gfx_setTextSize(5);
    gfx_setTextColor(ILI9341_WHITE);
    gfx_drawText(50, 20, "GENIUS");

    gfx_fillRect(MENU_BTN_X, MENU_BTN_Y, MENU_BTN_W, MENU_BTN_H, ILI9341_GREEN);
    gfx_setTextSize(4);
    gfx_setTextColor(ILI9341_BLACK);
    gfx_drawText(MENU_BTN_X + 25, MENU_BTN_Y + 14, "JOGAR");

    char buf[24];
    gfx_setTextSize(2);
    gfx_setTextColor(ILI9341_YELLOW);
    sprintf(buf, "BEST: %lu", (unsigned long)hi_score);
    gfx_drawText(100, 200, buf);
}

void display_wait_menu_touch(void) {
    int px, py, tx, ty;
    while (readPoint(&px, &py)) sleep_ms(50);
    while (1) {
        if (readPoint(&px, &py)) {
            gfx_touchTransform(SCREEN_ROTATION, px, py, &tx, &ty);
            if (tx >= MENU_BTN_X && tx <= MENU_BTN_X + MENU_BTN_W &&
                ty >= MENU_BTN_Y && ty <= MENU_BTN_Y + MENU_BTN_H) {
                gfx_fillRect(MENU_BTN_X, MENU_BTN_Y, MENU_BTN_W, MENU_BTN_H, ILI9341_YELLOW);
                gfx_setTextSize(4);
                gfx_setTextColor(ILI9341_BLACK);
                gfx_drawText(MENU_BTN_X + 25, MENU_BTN_Y + 14, "JOGAR");
                sleep_ms(300);
                return;
            }
        }
        sleep_ms(30);
    }
}

void display_countdown(void) {
    const char *nums[] = {"3", "2", "1", "GO!"};
    const uint16_t colors[] = {ILI9341_RED, ILI9341_YELLOW, ILI9341_GREEN, ILI9341_WHITE};
    for (int i = 0; i < 4; i++) {
        gfx_fillRect(0, 0, SCREEN_W, SCREEN_H, ILI9341_BLACK);
        gfx_setTextSize(7);
        gfx_setTextColor(colors[i]);
        gfx_drawText((i < 3) ? 140 : 80, 80, nums[i]);
        sleep_ms(i < 3 ? 700 : 400);
    }
    gfx_fillRect(0, 0, SCREEN_W, SCREEN_H, ILI9341_BLACK);
}

void display_round_hud(int round) {
    gfx_fillRect(0, 0, SCREEN_W, 40, ILI9341_BLACK);
    char buf[20];
    sprintf(buf, "ROUND %d", round);
    gfx_setTextSize(2);
    gfx_setTextColor(ILI9341_WHITE);
    gfx_drawText(10, 8, buf);

    if (round >= MULTI_LED_START) {
        gfx_setTextSize(1);
        gfx_setTextColor(ILI9341_ORANGE);
        gfx_drawText(130, 12, "MULTI!");
    }
}

void display_timer_bar(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int filled = (SCREEN_W * pct) / 100;
    int empty  = SCREEN_W - filled;
    if (filled > 0) gfx_fillRect(0, TIMER_BAR_Y, filled, TIMER_BAR_H, timer_color(pct));
    if (empty  > 0) gfx_fillRect(filled, TIMER_BAR_Y, empty, TIMER_BAR_H, ILI9341_BLACK);
}

void display_game_over(int round, uint32_t best, bool new_record) {
    gfx_fillRect(0, 0, SCREEN_W, SCREEN_H, ILI9341_BLACK);

    gfx_setTextSize(4);
    gfx_setTextColor(ILI9341_RED);
    gfx_drawText(30, 20, "GAME OVER");

    char buf[24];
    sprintf(buf, "SCORE: %d", round);
    gfx_setTextSize(3);
    gfx_setTextColor(ILI9341_WHITE);
    gfx_drawText(60, 85, buf);

    sprintf(buf, "BEST: %lu", (unsigned long)best);
    gfx_setTextSize(2);
    gfx_setTextColor(ILI9341_YELLOW);
    gfx_drawText(90, 130, buf);

    if (new_record) {
        gfx_setTextSize(3);
        gfx_setTextColor(ILI9341_YELLOW);
        gfx_drawText(30, 160, "NEW RECORD!");
    }

    gfx_setTextSize(1);
    gfx_setTextColor(ILI9341_WHITE);
    gfx_drawText(90, 220, "TAP TO CONTINUE");
}

void display_wait_touch(void) {
    int px, py;
    while (readPoint(&px, &py)) sleep_ms(50);
    while (!readPoint(&px, &py)) sleep_ms(30);
    sleep_ms(200);
}

// ═══════════════════════════════════════════════════════════════════════════════
// GAME LOGIC
// ═══════════════════════════════════════════════════════════════════════════════

static void led_on(int idx)   { gpio_put(LED_PINS[idx], 1); }
static void led_off(int idx)  { gpio_put(LED_PINS[idx], 0); }
static void leds_all_off(void) {
    for (int i = 0; i < 4; i++) gpio_put(LED_PINS[i], 0);
}

static uint8_t generate_step(int round) {
    if (round >= MULTI_LED_START && (rand() % 3) == 0) {
        int a = rand() % 4;
        int b;
        do { b = rand() % 4; } while (b == a);
        return (uint8_t)((1 << a) | (1 << b));
    }
    return (uint8_t)(1 << (rand() % 4));
}

static void show_step(uint8_t mask) {
    int count = popcount8(mask);
    int tone_dur = 400 / count;
    for (int i = 0; i < 4; i++)
        if (mask & (1 << i)) led_on(i);
    for (int i = 0; i < 4; i++)
        if (mask & (1 << i)) buzzer_tone(TONE_FREQS[i], tone_dur);
    leds_all_off();
    sleep_ms(100);
}

static void show_error(void) {
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) led_on(j);
        buzzer_tone(150, 300);
        leds_all_off();
        sleep_ms(100);
    }
    sleep_ms(400);
}

// Returns: 1 = correct, 0 = wrong, -1 = timeout
static int check_step(uint8_t expected_mask, int timeout_ms) {
    int need = popcount8(expected_mask);
    uint8_t got_mask  = 0;
    uint8_t prev_mask = 0;

    input_clear();
    input_start_timeout(timeout_ms);
    uint32_t t0 = time_us_32();
    uint32_t timeout_us = (uint32_t)timeout_ms * 1000;

    while (!timed_out && popcount8(got_mask) < need) {
        uint8_t cur = btn_mask;
        uint8_t new_bits = cur & ~prev_mask;
        for (int i = 0; i < 4; i++) {
            if (new_bits & (1 << i)) {
                led_on(i);
                buzzer_tone(TONE_FREQS[i], 150);
                led_off(i);
            }
        }
        got_mask |= cur;
        prev_mask = cur;

        uint32_t elapsed = time_us_32() - t0;
        int pct = 100 - (int)((uint64_t)elapsed * 100 / timeout_us);
        if (pct < 0) pct = 0;
        display_timer_bar(pct);
        sleep_ms(15);
    }

    input_cancel_timeout();
    display_timer_bar(0);

    if (timed_out) return -1;
    return (got_mask == expected_mask) ? 1 : 0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// MAIN - MAQUINA DE ESTADOS
// ═══════════════════════════════════════════════════════════════════════════════

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

    game_ctx_t ctx;
    ctx.state = STATE_MENU;
    uint32_t hs = flash_get_best();

    // State machine
    while (1) {
        switch (ctx.state) {

        case STATE_MENU:
            hs = flash_get_best();
            display_menu(hs);
            display_wait_menu_touch();
            ctx.mode    = MODE_SOLO;
            ctx.seq_len = 0;
            ctx.round   = 0;
            ctx.state   = STATE_COUNTDOWN;
            break;

        case STATE_COUNTDOWN:
            display_countdown();
            audio_play_melody(MELODY_START);
            sleep_ms(300);
            input_clear();
            ctx.state = STATE_SHOW_SEQ;
            break;

        case STATE_SHOW_SEQ:
            if (ctx.seq_len >= MAX_SEQ) {
                ctx.state = STATE_GAME_OVER;
                break;
            }
            ctx.sequence[ctx.seq_len] = generate_step(ctx.seq_len + 1);
            ctx.seq_len++;
            ctx.round = ctx.seq_len;
            display_round_hud(ctx.round);
            sleep_ms(600);
            for (int i = 0; i < ctx.seq_len; i++)
                show_step(ctx.sequence[i]);
            input_clear();
            ctx.state = STATE_PLAYER_INPUT;
            break;

        case STATE_PLAYER_INPUT: {
            int timeout = get_timeout_ms(ctx.round);
            bool failed = false;
            for (int i = 0; i < ctx.seq_len; i++) {
                if (check_step(ctx.sequence[i], timeout) != 1) {
                    failed = true;
                    break;
                }
            }
            ctx.state = failed ? STATE_GAME_OVER : STATE_ROUND_OK;
            break;
        }

        case STATE_ROUND_OK:
            audio_play_melody(MELODY_VICTORY);
            sleep_ms(300);
            ctx.state = STATE_SHOW_SEQ;
            break;

        case STATE_GAME_OVER: {
            show_error();
            bool new_record = ((uint32_t)ctx.round > hs);
            if (new_record) {
                hs = (uint32_t)ctx.round;
                flash_save_best(hs);
            }
            display_game_over(ctx.round, hs, new_record);
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
