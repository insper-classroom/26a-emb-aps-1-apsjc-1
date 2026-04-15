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

#define BTN0_PIN 9
#define BTN1_PIN 6
#define BTN2_PIN 7
#define BTN3_PIN 8

#define LED0_PIN 5
#define LED1_PIN 2
#define LED2_PIN 3
#define LED3_PIN 4

#define TONE0_FREQ 262
#define TONE1_FREQ 330
#define TONE2_FREQ 392
#define TONE3_FREQ 523

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
    int          round;
} game_ctx_t;

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

#define MENU_BTN_X    60
#define MENU_BTN_Y    110
#define MENU_BTN_W    200
#define MENU_BTN_H    60
#define TIMER_BAR_Y   32
#define TIMER_BAR_H   8

#define FLASH_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define FLASH_MAGIC         0x47454E49u

typedef struct {
    uint32_t magic;
    uint32_t solo_best;
} flash_data_t;

typedef struct {
    volatile uint8_t btn_mask;
    volatile uint32_t last_btn0_us;
    volatile uint32_t last_btn1_us;
    volatile uint32_t last_btn2_us;
    volatile uint32_t last_btn3_us;
    volatile int wav_position;
    volatile bool wav_playing;
} irq_shared_t;

// Pode ficar global porque é compartilhado com IRQ
static irq_shared_t g_irq = {0};

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
    while (x) {
        c += x & 1u;
        x >>= 1u;
    }
    return c;
}

static int led_pin_from_idx(int idx) {
    switch (idx) {
        case 0: return LED0_PIN;
        case 1: return LED1_PIN;
        case 2: return LED2_PIN;
        default: return LED3_PIN;
    }
}

static int btn_pin_from_idx(int idx) {
    switch (idx) {
        case 0: return BTN0_PIN;
        case 1: return BTN1_PIN;
        case 2: return BTN2_PIN;
        default: return BTN3_PIN;
    }
}

static int tone_from_idx(int idx) {
    switch (idx) {
        case 0: return TONE0_FREQ;
        case 1: return TONE1_FREQ;
        case 2: return TONE2_FREQ;
        default: return TONE3_FREQ;
    }
}

static void input_clear(void) {
    g_irq.btn_mask = 0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// FLASH - HIGH SCORE PERSISTENTE
// ═══════════════════════════════════════════════════════════════════════════════

static uint32_t flash_read_best(void) {
    const flash_data_t *data =
        (const flash_data_t *)(XIP_BASE + FLASH_TARGET_OFFSET);

    if (data->magic == FLASH_MAGIC) {
        return data->solo_best;
    }
    return 0;
}

static void flash_save_best(uint32_t best) {
    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, FLASH_PAGE_SIZE);

    flash_data_t *d = (flash_data_t *)page;
    d->magic     = FLASH_MAGIC;
    d->solo_best = best;

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_TARGET_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
}

// ═══════════════════════════════════════════════════════════════════════════════
// AUDIO - PWM SPEAKER + MELODIAS
// ═══════════════════════════════════════════════════════════════════════════════

#define NOTE_C4  262
#define NOTE_D4  294
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_C5  523
#define NOTE_E5  659
#define NOTE_G5  784
#define NOTE_C6  1047

static const note_t melody_victory[] = {
    {NOTE_C5, 80}, {NOTE_E5, 80}, {NOTE_G5, 80}, {NOTE_C6, 160}, {0, 0}
};

static const note_t melody_mario[] = {
    {NOTE_E5, 125}, {NOTE_E5, 125}, {0, 65}, {NOTE_E5, 125}, {0, 65},
    {NOTE_C5, 125}, {NOTE_E5, 125}, {0, 65}, {NOTE_G5, 250}, {0, 250},
    {NOTE_G4, 250}, {0, 0}
};

static const note_t melody_game_over[] = {
    {NOTE_G4, 200}, {0, 50}, {NOTE_F4, 200}, {0, 50}, {NOTE_E4, 200},
    {0, 50}, {NOTE_D4, 400}, {0, 0}
};

static const note_t melody_start[] = {
    {NOTE_G4, 100}, {NOTE_C5, 100}, {NOTE_E5, 100}, {NOTE_G5, 200}, {0, 0}
};

static const note_t *get_melody(melody_id_t id) {
    switch (id) {
        case MELODY_VICTORY:   return melody_victory;
        case MELODY_MARIO:     return melody_mario;
        case MELODY_GAME_OVER: return melody_game_over;
        default:               return melody_start;
    }
}

void pwm_irq_handler(void) {
    int slice = pwm_gpio_to_slice_num(PIN_AUDIO);
    pwm_clear_irq(slice);

    if (!g_irq.wav_playing) {
        pwm_set_gpio_level(PIN_AUDIO, 0);
        return;
    }

    if (g_irq.wav_position < ((WAV_DATA_LENGTH << 3) - 1)) {
        int sample = (int)WAV_DATA[g_irq.wav_position >> 3] - 127;
        sample *= 4;
        sample += 127;

        if (sample > 255) sample = 255;
        if (sample < 0)   sample = 0;

        pwm_set_gpio_level(PIN_AUDIO, (uint16_t)sample);
        g_irq.wav_position++;
    } else {
        g_irq.wav_position = 0;
        g_irq.wav_playing  = false;
        pwm_set_gpio_level(PIN_AUDIO, 0);
    }
}

static void audio_init(void) {
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

static void buzzer_tone(int freq, int duration_ms) {
    if (freq <= 0) {
        sleep_ms(duration_ms);
        return;
    }

    int slice = pwm_gpio_to_slice_num(PIN_AUDIO);
    irq_set_enabled(PWM_IRQ_WRAP, false);
    pwm_set_irq_enabled(slice, false);

    uint32_t sysclk = clock_get_hz(clk_sys);
    uint16_t wrap = 10000;
    float clkdiv = (float)sysclk / ((float)freq * (float)(wrap + 1));

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

static void audio_play_melody(melody_id_t id) {
    const note_t *m = get_melody(id);

    while (m->freq || m->duration_ms) {
        buzzer_tone(m->freq, m->duration_ms);
        m++;
    }
}

static void audio_play_wav(void) {
    g_irq.wav_position = 0;
    g_irq.wav_playing  = true;

    while (g_irq.wav_playing) {
        sleep_ms(1);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// INPUT - BOTOES COM IRQ + TIMEOUT POR ALARM
// ═══════════════════════════════════════════════════════════════════════════════

static void register_button_press(int idx, uint32_t now) {
    switch (idx) {
        case 0:
            if ((now - g_irq.last_btn0_us) >= DEBOUNCE_US) {
                g_irq.last_btn0_us = now;
                g_irq.btn_mask |= (uint8_t)(1u << 0);
            }
            break;
        case 1:
            if ((now - g_irq.last_btn1_us) >= DEBOUNCE_US) {
                g_irq.last_btn1_us = now;
                g_irq.btn_mask |= (uint8_t)(1u << 1);
            }
            break;
        case 2:
            if ((now - g_irq.last_btn2_us) >= DEBOUNCE_US) {
                g_irq.last_btn2_us = now;
                g_irq.btn_mask |= (uint8_t)(1u << 2);
            }
            break;
        case 3:
            if ((now - g_irq.last_btn3_us) >= DEBOUNCE_US) {
                g_irq.last_btn3_us = now;
                g_irq.btn_mask |= (uint8_t)(1u << 3);
            }
            break;
        default:
            break;
    }
}

void btn_callback(uint gpio, uint32_t events) {
    if ((events & GPIO_IRQ_EDGE_FALL) == 0u) {
        return;
    }

    uint32_t now = time_us_32();

    if (gpio == (uint)BTN0_PIN) {
        register_button_press(0, now);
        return;
    }

    if (gpio == (uint)BTN1_PIN) {
        register_button_press(1, now);
        return;
    }

    if (gpio == (uint)BTN2_PIN) {
        register_button_press(2, now);
        return;
    }

    if (gpio == (uint)BTN3_PIN) {
        register_button_press(3, now);
        return;
    }
}

static int64_t timeout_cb(alarm_id_t id, void *data) {
    (void)id;
    volatile bool *flag = (volatile bool *)data;
    *flag = true;
    return 0;
}

static void input_init(void) {
    gpio_init(BTN0_PIN);
    gpio_set_dir(BTN0_PIN, GPIO_IN);
    gpio_pull_up(BTN0_PIN);

    gpio_init(BTN1_PIN);
    gpio_set_dir(BTN1_PIN, GPIO_IN);
    gpio_pull_up(BTN1_PIN);

    gpio_init(BTN2_PIN);
    gpio_set_dir(BTN2_PIN, GPIO_IN);
    gpio_pull_up(BTN2_PIN);

    gpio_init(BTN3_PIN);
    gpio_set_dir(BTN3_PIN, GPIO_IN);
    gpio_pull_up(BTN3_PIN);

    gpio_set_irq_enabled_with_callback(BTN0_PIN, GPIO_IRQ_EDGE_FALL, true, &btn_callback);
    gpio_set_irq_enabled(BTN1_PIN, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(BTN2_PIN, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(BTN3_PIN, GPIO_IRQ_EDGE_FALL, true);
}

// ═══════════════════════════════════════════════════════════════════════════════
// DISPLAY - TELA LCD ILI9341
// ═══════════════════════════════════════════════════════════════════════════════

static uint16_t timer_color(int pct) {
    if (pct > 60) return ILI9341_GREEN;
    if (pct > 30) return ILI9341_YELLOW;
    return ILI9341_RED;
}

static void display_init_lcd(void) {
    LCD_initDisplay();
    LCD_setRotation(SCREEN_ROTATION);
    gfx_init();
    gfx_clear();
    configure_touch();
}

static void display_menu(uint32_t hi_score) {
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

static void display_wait_menu_touch(void) {
    int px, py, tx, ty;

    while (readPoint(&px, &py)) {
        sleep_ms(50);
    }

    while (1) {
        if (readPoint(&px, &py)) {
            gfx_touchTransform(SCREEN_ROTATION, px, py, &tx, &ty);
            if (tx >= MENU_BTN_X && tx <= (MENU_BTN_X + MENU_BTN_W) &&
                ty >= MENU_BTN_Y && ty <= (MENU_BTN_Y + MENU_BTN_H)) {
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

static void display_countdown(void) {
    static const char *nums[] = {"3", "2", "1", "GO!"};
    static const uint16_t colors[] = {
        ILI9341_RED, ILI9341_YELLOW, ILI9341_GREEN, ILI9341_WHITE
    };

    for (int i = 0; i < 4; i++) {
        gfx_fillRect(0, 0, SCREEN_W, SCREEN_H, ILI9341_BLACK);
        gfx_setTextSize(7);
        gfx_setTextColor(colors[i]);
        gfx_drawText((i < 3) ? 140 : 80, 80, nums[i]);
        sleep_ms((i < 3) ? 700 : 400);
    }

    gfx_fillRect(0, 0, SCREEN_W, SCREEN_H, ILI9341_BLACK);
}

static void display_round_hud(int round) {
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

static void display_timer_bar(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    int filled = (SCREEN_W * pct) / 100;
    int empty  = SCREEN_W - filled;

    if (filled > 0) {
        gfx_fillRect(0, TIMER_BAR_Y, filled, TIMER_BAR_H, timer_color(pct));
    }
    if (empty > 0) {
        gfx_fillRect(filled, TIMER_BAR_Y, empty, TIMER_BAR_H, ILI9341_BLACK);
    }
}

static void display_game_over(int round, uint32_t best, bool new_record) {
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

static void display_wait_touch(void) {
    int px, py;

    while (readPoint(&px, &py)) {
        sleep_ms(50);
    }
    while (!readPoint(&px, &py)) {
        sleep_ms(30);
    }

    sleep_ms(200);
}

// ═══════════════════════════════════════════════════════════════════════════════
// GAME LOGIC
// ═══════════════════════════════════════════════════════════════════════════════

static void led_on(int idx) {
    gpio_put(led_pin_from_idx(idx), 1);
}

static void led_off(int idx) {
    gpio_put(led_pin_from_idx(idx), 0);
}

static void leds_all_off(void) {
    led_off(0);
    led_off(1);
    led_off(2);
    led_off(3);
}

static uint8_t generate_step(int round) {
    if (round >= MULTI_LED_START && (rand() % 3) == 0) {
        int a = rand() % 4;
        int b = rand() % 4;

        while (b == a) {
            b = rand() % 4;
        }

        return (uint8_t)((1u << a) | (1u << b));
    }

    return (uint8_t)(1u << (rand() % 4));
}

static void show_step(uint8_t mask) {
    int count = popcount8(mask);
    int tone_dur = 400 / count;

    if (mask & (1u << 0)) led_on(0);
    if (mask & (1u << 1)) led_on(1);
    if (mask & (1u << 2)) led_on(2);
    if (mask & (1u << 3)) led_on(3);

    if (mask & (1u << 0)) buzzer_tone(tone_from_idx(0), tone_dur);
    if (mask & (1u << 1)) buzzer_tone(tone_from_idx(1), tone_dur);
    if (mask & (1u << 2)) buzzer_tone(tone_from_idx(2), tone_dur);
    if (mask & (1u << 3)) buzzer_tone(tone_from_idx(3), tone_dur);

    leds_all_off();
    sleep_ms(100);
}

static void show_error(void) {
    for (int i = 0; i < 3; i++) {
        led_on(0);
        led_on(1);
        led_on(2);
        led_on(3);

        buzzer_tone(150, 300);

        leds_all_off();
        sleep_ms(100);
    }
    sleep_ms(400);
}

// Returns: 1 = correct, 0 = wrong, -1 = timeout
static int check_step(uint8_t expected_mask, int timeout_ms) {
    int need = popcount8(expected_mask);
    uint8_t got_mask = 0;
    uint8_t prev_mask = 0;
    volatile bool step_timed_out = false;

    input_clear();

    alarm_id_t alarm = add_alarm_in_ms(timeout_ms, timeout_cb, (void *)&step_timed_out, false);
    uint32_t t0 = time_us_32();
    uint32_t timeout_us = (uint32_t)timeout_ms * 1000u;

    while (!step_timed_out && popcount8(got_mask) < need) {
        uint8_t cur = g_irq.btn_mask;
        uint8_t new_bits = (uint8_t)(cur & (uint8_t)(~prev_mask));

        if (new_bits & (1u << 0)) {
            led_on(0);
            buzzer_tone(tone_from_idx(0), 150);
            led_off(0);
        }
        if (new_bits & (1u << 1)) {
            led_on(1);
            buzzer_tone(tone_from_idx(1), 150);
            led_off(1);
        }
        if (new_bits & (1u << 2)) {
            led_on(2);
            buzzer_tone(tone_from_idx(2), 150);
            led_off(2);
        }
        if (new_bits & (1u << 3)) {
            led_on(3);
            buzzer_tone(tone_from_idx(3), 150);
            led_off(3);
        }

        got_mask |= cur;
        prev_mask = cur;

        uint32_t elapsed = time_us_32() - t0;
        int pct = 100 - (int)(((uint64_t)elapsed * 100u) / timeout_us);
        if (pct < 0) pct = 0;

        display_timer_bar(pct);
        sleep_ms(15);
    }

    cancel_alarm(alarm);
    display_timer_bar(0);

    if (step_timed_out) {
        return -1;
    }

    return (got_mask == expected_mask) ? 1 : 0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// MAIN - MAQUINA DE ESTADOS
// ═══════════════════════════════════════════════════════════════════════════════

int main(void) {
    stdio_init_all();
    srand((unsigned int)time_us_32());

    gpio_init(LED0_PIN);
    gpio_set_dir(LED0_PIN, GPIO_OUT);

    gpio_init(LED1_PIN);
    gpio_set_dir(LED1_PIN, GPIO_OUT);

    gpio_init(LED2_PIN);
    gpio_set_dir(LED2_PIN, GPIO_OUT);

    gpio_init(LED3_PIN);
    gpio_set_dir(LED3_PIN, GPIO_OUT);

    leds_all_off();

    audio_init();
    input_init();
    display_init_lcd();

    game_ctx_t ctx;
    ctx.state = STATE_MENU;
    ctx.mode = MODE_SOLO;
    ctx.seq_len = 0;
    ctx.round = 0;

    uint32_t hs = flash_read_best();

    while (1) {
        switch (ctx.state) {
            case STATE_MENU:
                hs = flash_read_best();
                display_menu(hs);
                display_wait_menu_touch();
                ctx.mode = MODE_SOLO;
                ctx.seq_len = 0;
                ctx.round = 0;
                ctx.state = STATE_COUNTDOWN;
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

                for (int i = 0; i < ctx.seq_len; i++) {
                    show_step(ctx.sequence[i]);
                }

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

                if (new_record) {
                    audio_play_melody(MELODY_MARIO);
                } else {
                    audio_play_melody(MELODY_GAME_OVER);
                }

                audio_play_wav();
                display_wait_touch();
                ctx.state = STATE_MENU;
                break;
            }

            default:
                ctx.state = STATE_MENU;
                break;
        }
    }

    return 0;
}