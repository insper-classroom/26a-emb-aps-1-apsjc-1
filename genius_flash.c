#include "genius_flash.h"
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <string.h>

// ── Flash layout ─────────────────────────────────────────────────────────────
// Use last sector of flash for persistent storage
#define FLASH_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define FLASH_MAGIC         0x47454E49u  // "GENI"

typedef struct {
    uint32_t magic;
    uint32_t solo_best;
    uint32_t dupla_best;
} flash_data_t;

static uint32_t cached_solo  = 0;
static uint32_t cached_dupla = 0;

// ── Read from flash ──────────────────────────────────────────────────────────
void flash_init(void) {
    const flash_data_t *data =
        (const flash_data_t *)(XIP_BASE + FLASH_TARGET_OFFSET);

    if (data->magic == FLASH_MAGIC) {
        cached_solo  = data->solo_best;
        cached_dupla = data->dupla_best;
    } else {
        cached_solo  = 0;
        cached_dupla = 0;
    }
}

uint32_t flash_get_highscore_solo(void)  { return cached_solo; }
uint32_t flash_get_highscore_dupla(void) { return cached_dupla; }

// ── Write to flash ───────────────────────────────────────────────────────────
void flash_save_highscore(uint32_t solo, uint32_t dupla) {
    // Only write if changed
    if (solo == cached_solo && dupla == cached_dupla) return;

    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, FLASH_PAGE_SIZE);

    flash_data_t *d = (flash_data_t *)page;
    d->magic     = FLASH_MAGIC;
    d->solo_best = solo;
    d->dupla_best = dupla;

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_TARGET_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts(ints);

    cached_solo  = solo;
    cached_dupla = dupla;
}
