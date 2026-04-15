#include "genius_audio.h"
#include "genius.h"
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "audio_voz.h"

// ── PWM audio state (WAV playback) ───────────────────────────────────────────
static volatile int  wav_position  = 0;
static volatile bool wav_playing   = false;

static void pwm_irq_handler(void) {
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
        wav_position  = 0;
        wav_playing   = false;
        pwm_set_gpio_level(PIN_AUDIO, 0);
    }
}

// ── Melodies ─────────────────────────────────────────────────────────────────
#define NOTE_C4  262
#define NOTE_D4  294
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_D5  587
#define NOTE_E5  659
#define NOTE_G5  784
#define NOTE_C6  1047

static const note_t melody_victory[] = {
    {NOTE_C5, 80}, {NOTE_E5, 80}, {NOTE_G5, 80}, {NOTE_C6, 160},
    {0, 0}
};

static const note_t melody_mario[] = {
    {NOTE_E5, 125}, {NOTE_E5, 125}, {0, 65},
    {NOTE_E5, 125}, {0, 65},
    {NOTE_C5, 125}, {NOTE_E5, 125}, {0, 65},
    {NOTE_G5, 250}, {0, 250},
    {NOTE_G4, 250},
    {0, 0}
};

static const note_t melody_game_over[] = {
    {NOTE_G4, 200}, {0, 50},
    {NOTE_F4, 200}, {0, 50},
    {NOTE_E4, 200}, {0, 50},
    {NOTE_D4, 400},
    {0, 0}
};

static const note_t melody_start[] = {
    {NOTE_G4, 100}, {NOTE_C5, 100}, {NOTE_E5, 100}, {NOTE_G5, 200},
    {0, 0}
};

static const note_t *melody_table[] = {
    [MELODY_VICTORY]   = melody_victory,
    [MELODY_MARIO]     = melody_mario,
    [MELODY_GAME_OVER] = melody_game_over,
    [MELODY_START]     = melody_start,
};

// ── Init ─────────────────────────────────────────────────────────────────────
void audio_init(void) {
    // PWM audio on PIN_AUDIO (speaker)
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

// ── Tone via PWM no speaker (substitui o buzzer) ────────────────────────────
void buzzer_tone(int freq, int duration_ms) {
    if (freq <= 0) {
        sleep_ms(duration_ms);
        return;
    }

    int slice = pwm_gpio_to_slice_num(PIN_AUDIO);

    // Desabilita IRQ do WAV enquanto toca o tom
    irq_set_enabled(PWM_IRQ_WRAP, false);
    pwm_set_irq_enabled(slice, false);

    // Configura PWM para gerar a frequencia do tom
    uint32_t sysclk = clock_get_hz(clk_sys);
    uint16_t wrap = 10000;
    float clkdiv = (float)sysclk / ((float)freq * (wrap + 1));
    if (clkdiv < 1.0f)   clkdiv = 1.0f;
    if (clkdiv > 255.0f) clkdiv = 255.0f;

    pwm_set_clkdiv(slice, clkdiv);
    pwm_set_wrap(slice, wrap);
    pwm_set_gpio_level(PIN_AUDIO, wrap / 2);  // 50% duty = onda quadrada

    sleep_ms(duration_ms);

    // Silencia e restaura config do WAV
    pwm_set_gpio_level(PIN_AUDIO, 0);
    pwm_set_clkdiv(slice, 4.0f);
    pwm_set_wrap(slice, 255);
    pwm_clear_irq(slice);
    pwm_set_irq_enabled(slice, true);
    irq_set_enabled(PWM_IRQ_WRAP, true);
}

// ── Play melody ──────────────────────────────────────────────────────────────
void audio_play_melody(melody_id_t id) {
    const note_t *m = melody_table[id];
    while (m->freq || m->duration_ms) {
        buzzer_tone(m->freq, m->duration_ms);
        m++;
    }
}

// ── Play WAV (blocking) ─────────────────────────────────────────────────────
void audio_play_wav(void) {
    wav_position = 0;
    wav_playing  = true;
    while (wav_playing) sleep_ms(1);
}
