#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "audio_voz.h"
#include "display.h"

#define MAX_SEQ   100
#define DEBOUNCE  50000u
#define AUDIO_PIN 10

// ── GPIOs ─────────────────────────────────────────────────────────────────────
const int LED_AMARELO  = 5;
const int LED_AZUL     = 4;
const int LED_VERDE    = 3;
const int LED_VERMELHO = 2;

const int BTN_AMARELO  = 9;
const int BTN_AZUL     = 8;
const int BTN_VERDE    = 7;
const int BTN_VERMELHO = 6;

const int BUZZER_PIN = 21;

const int LED_PINS[4] = {5, 2, 3, 4}; // amarelo, vermelho, verde, azul
const int FREQS[4]    = {262, 330, 392, 523};

// ── Áudio IRQ ─────────────────────────────────────────────────────────────────
volatile int  wav_position  = 0;
volatile bool audio_playing = false;

void pwm_interrupt_handler() {
    pwm_clear_irq(pwm_gpio_to_slice_num(AUDIO_PIN));
    if (!audio_playing) {
        pwm_set_gpio_level(AUDIO_PIN, 0);
        return;
    }
    if (wav_position < (WAV_DATA_LENGTH << 3) - 1) {
        pwm_set_gpio_level(AUDIO_PIN, WAV_DATA[wav_position >> 3] / 2);
        wav_position++;
    } else {
        wav_position   = 0;
        audio_playing  = false;
        pwm_set_gpio_level(AUDIO_PIN, 0);
    }
}

void play_audio(void) {
    wav_position  = 0;
    audio_playing = true;
    while (audio_playing) __wfi();
}

// ── Botões IRQ ────────────────────────────────────────────────────────────────
volatile int btn_pressed = -1;

void btn_callback(uint gpio, uint32_t events) {
    static uint32_t last_time[4] = {0, 0, 0, 0};
    uint32_t now = time_us_32();

    if (events & GPIO_IRQ_EDGE_FALL) {
        int idx = -1;
        if      (gpio == (uint)BTN_AMARELO)  idx = 0;
        else if (gpio == (uint)BTN_VERMELHO) idx = 1;
        else if (gpio == (uint)BTN_VERDE)    idx = 2;
        else if (gpio == (uint)BTN_AZUL)     idx = 3;

        if (idx == -1) return;
        if ((now - last_time[idx]) < DEBOUNCE) return;
        last_time[idx] = now;
        btn_pressed = idx;
    }
}

// ── Buzzer ────────────────────────────────────────────────────────────────────
void buzzer_play(int freq, int duration_ms) {
    // Desabilita IRQ do PWM de áudio para não interferir no timing
    irq_set_enabled(PWM_IRQ_WRAP, false);

    int half_period_us = 1000000 / (freq * 2);
    int cycles = (duration_ms * 1000) / (half_period_us * 2);
    for (int i = 0; i < cycles; i++) {
        gpio_put(BUZZER_PIN, 1);
        sleep_us(half_period_us);
        gpio_put(BUZZER_PIN, 0);
        sleep_us(half_period_us);
    }

    // Reabilita IRQ do PWM de áudio
    irq_set_enabled(PWM_IRQ_WRAP, true);
}

// ── Tela Start ───────────────────────────────────────────────────────────────
#define BTN_X  40
#define BTN_Y  120
#define BTN_W  160
#define BTN_H  80

void draw_start_screen(void) {
    lcd_fill(COLOR_BLACK);
    lcd_draw_text(60, 40, "SIMON", COLOR_WHITE, COLOR_BLACK, 5);
    lcd_fill_rect(BTN_X, BTN_Y, BTN_W, BTN_H, COLOR_GREEN);
    lcd_draw_text(BTN_X + 20, BTN_Y + 25, "START", COLOR_BLACK, COLOR_GREEN, 4);
}

void wait_start_touch(void) {
    draw_start_screen();
    // Espera soltar (caso esteja tocando)
    int tx, ty;
    while (touch_read(&tx, &ty)) sleep_ms(50);

    // Espera tocar no botão
    while (1) {
        if (touch_read(&tx, &ty)) {
            if (tx >= BTN_X && tx <= BTN_X + BTN_W &&
                ty >= BTN_Y && ty <= BTN_Y + BTN_H) {
                // Feedback visual
                lcd_fill_rect(BTN_X, BTN_Y, BTN_W, BTN_H, COLOR_YELLOW);
                lcd_draw_text(BTN_X + 20, BTN_Y + 25, "START", COLOR_BLACK, COLOR_YELLOW, 4);
                sleep_ms(300);
                lcd_fill(COLOR_BLACK);
                break;
            }
        }
        sleep_ms(30);
    }
}

// ── LEDs ──────────────────────────────────────────────────────────────────────
void led_on(int idx)  { gpio_put(LED_PINS[idx], 1); }
void led_off(int idx) { gpio_put(LED_PINS[idx], 0); }

// ── Jogo ──────────────────────────────────────────────────────────────────────
void show_step(int color) {
    led_on(color);
    buzzer_play(FREQS[color], 400);
    led_off(color);
    sleep_ms(100);
}

void show_error(void) {
    for (int i = 0; i < 3; i++) {
        gpio_put(LED_AMARELO,  1);
        gpio_put(LED_VERMELHO, 1);
        gpio_put(LED_VERDE,    1);
        gpio_put(LED_AZUL,     1);
        buzzer_play(150, 300);
        gpio_put(LED_AMARELO,  0);
        gpio_put(LED_VERMELHO, 0);
        gpio_put(LED_VERDE,    0);
        gpio_put(LED_AZUL,     0);
        sleep_ms(100);
    }
    sleep_ms(800);
}

void show_win(void) {
    led_on(0); buzzer_play(FREQS[0], 120); led_off(0);
    led_on(1); buzzer_play(FREQS[1], 120); led_off(1);
    led_on(2); buzzer_play(FREQS[2], 120); led_off(2);
    led_on(3); buzzer_play(FREQS[3], 120); led_off(3);
}

int wait_button(void) {
    btn_pressed = -1;
    while (btn_pressed == -1) sleep_ms(1);
    int b = btn_pressed;
    btn_pressed = -1;
    led_on(b);
    buzzer_play(FREQS[b], 200);
    led_off(b);
    return b;
}

int main() {
    set_sys_clock_khz(176000, true);
    stdio_init_all();
    srand((unsigned int)time_us_32());

    // LEDs
    gpio_init(LED_AMARELO);  gpio_set_dir(LED_AMARELO,  GPIO_OUT);
    gpio_init(LED_VERMELHO); gpio_set_dir(LED_VERMELHO, GPIO_OUT);
    gpio_init(LED_VERDE);    gpio_set_dir(LED_VERDE,    GPIO_OUT);
    gpio_init(LED_AZUL);     gpio_set_dir(LED_AZUL,     GPIO_OUT);

    // Buzzer
    gpio_init(BUZZER_PIN); gpio_set_dir(BUZZER_PIN, GPIO_OUT);

    // Botões
    gpio_init(BTN_AMARELO);  gpio_set_dir(BTN_AMARELO,  GPIO_IN); gpio_pull_up(BTN_AMARELO);
    gpio_init(BTN_VERMELHO); gpio_set_dir(BTN_VERMELHO, GPIO_IN); gpio_pull_up(BTN_VERMELHO);
    gpio_init(BTN_VERDE);    gpio_set_dir(BTN_VERDE,    GPIO_IN); gpio_pull_up(BTN_VERDE);
    gpio_init(BTN_AZUL);     gpio_set_dir(BTN_AZUL,     GPIO_IN); gpio_pull_up(BTN_AZUL);

    // IRQs botões
    gpio_set_irq_enabled_with_callback(BTN_AMARELO,  GPIO_IRQ_EDGE_FALL, true, &btn_callback);
    gpio_set_irq_enabled(BTN_VERMELHO, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(BTN_VERDE,    GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(BTN_AZUL,     GPIO_IRQ_EDGE_FALL, true);

    // Áudio PWM IRQ
    gpio_set_function(AUDIO_PIN, GPIO_FUNC_PWM);
    int audio_slice = pwm_gpio_to_slice_num(AUDIO_PIN);
    pwm_clear_irq(audio_slice);
    pwm_set_irq_enabled(audio_slice, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_interrupt_handler);
    irq_set_enabled(PWM_IRQ_WRAP, true);
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 8.0f);
    pwm_config_set_wrap(&config, 250);
    pwm_init(audio_slice, &config, true);
    pwm_set_gpio_level(AUDIO_PIN, 0);

    // Display
    display_init();

    int sequence[MAX_SEQ];
    int seq_len = 0;

    while (1) {
        seq_len = 0;

        wait_start_touch();
        sleep_ms(500);
        btn_pressed = -1;

        while (1) {
            sequence[seq_len] = rand() % 4;
            seq_len++;

            sleep_ms(600);
            for (int i = 0; i < seq_len; i++) show_step(sequence[i]);

            btn_pressed = -1;

            int correct = 1;
            for (int i = 0; i < seq_len; i++) {
                int pressed = wait_button();
                if (pressed != sequence[i]) {
                    correct = 0;
                    break;
                }
            }

            if (!correct) {
                show_error();
                play_audio();
                break;
            }

            show_win();
            sleep_ms(400);
        }
    }

    return 0;
}