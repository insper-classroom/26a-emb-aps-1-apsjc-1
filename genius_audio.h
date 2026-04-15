#ifndef GENIUS_AUDIO_H
#define GENIUS_AUDIO_H

#include <stdint.h>

// ── Note definition for melodies ─────────────────────────────────────────────
typedef struct {
    uint16_t freq;
    uint16_t duration_ms;
} note_t;

// ── Melody IDs ───────────────────────────────────────────────────────────────
typedef enum {
    MELODY_VICTORY,     // short ascending after correct round
    MELODY_MARIO,       // Mario-style for new high score
    MELODY_GAME_OVER,   // sad descending
    MELODY_START,       // game start fanfare
} melody_id_t;

// ── API ──────────────────────────────────────────────────────────────────────
void audio_init(void);
void buzzer_tone(int freq, int duration_ms);
void audio_play_melody(melody_id_t id);
void audio_play_wav(void);

#endif
