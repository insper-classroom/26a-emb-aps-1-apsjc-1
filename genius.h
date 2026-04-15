#ifndef GENIUS_H
#define GENIUS_H

#include <stdint.h>
#include <stdbool.h>

// ── Screen (landscape after rotation 3) ──────────────────────────────────────
#define SCREEN_W        320
#define SCREEN_H        240
#define SCREEN_ROTATION 3

// ── Game limits ──────────────────────────────────────────────────────────────
#define MAX_SEQ         100
#define MULTI_LED_START 7
#define NUM_LEDS        4

// ── Timeout per difficulty tier (ms) ─────────────────────────────────────────
#define TIMEOUT_EASY    5000
#define TIMEOUT_MEDIUM  4000
#define TIMEOUT_HARD    3000
#define TIMEOUT_EXPERT  2000

// ── GPIO pins ────────────────────────────────────────────────────────────────
#define PIN_LED_AMARELO   5
#define PIN_LED_AZUL      4
#define PIN_LED_VERDE     3
#define PIN_LED_VERMELHO  2

#define PIN_BTN_AMARELO   9
#define PIN_BTN_AZUL      8
#define PIN_BTN_VERDE     7
#define PIN_BTN_VERMELHO  6

#define PIN_BUZZER        21
#define PIN_AUDIO         10

// ── Shared arrays (index: 0=amarelo 1=vermelho 2=verde 3=azul) ──────────────
static const int LED_PINS[4] = {5, 2, 3, 4};
static const int BTN_PINS[4] = {9, 6, 7, 8};
static const int TONE_FREQS[4] = {262, 330, 392, 523};

// ── State machine ────────────────────────────────────────────────────────────
typedef enum {
    STATE_MENU,
    STATE_COUNTDOWN,
    STATE_SHOW_SEQ,
    STATE_PLAYER_INPUT,
    STATE_ROUND_OK,
    STATE_GAME_OVER,
} game_state_t;

// ── Game modes ───────────────────────────────────────────────────────────────
typedef enum {
    MODE_SOLO,
    MODE_DUPLA,
    MODE_COUNT
} game_mode_t;

// ── Game context ─────────────────────────────────────────────────────────────
typedef struct {
    game_state_t state;
    game_mode_t  mode;
    uint8_t      sequence[MAX_SEQ];   // bitmask per step (bit 0-3)
    int          seq_len;
    int          current_player;      // 0 or 1 (dupla)
    int          round;
    uint32_t     high_score;
} game_ctx_t;

// ── Helpers ──────────────────────────────────────────────────────────────────
static inline int get_timeout_ms(int round) {
    if (round <= 3)  return TIMEOUT_EASY;
    if (round <= 7)  return TIMEOUT_MEDIUM;
    if (round <= 12) return TIMEOUT_HARD;
    return TIMEOUT_EXPERT;
}

static inline int popcount8(uint8_t x) {
    int c = 0;
    while (x) { c += x & 1; x >>= 1; }
    return c;
}

#endif
