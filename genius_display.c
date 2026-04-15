#include "genius_display.h"
#include "genius.h"
#include "pico/stdlib.h"

#include "tft_lcd_ili9341/ili9341/ili9341.h"
#include "tft_lcd_ili9341/gfx/gfx_ili9341.h"
#include "tft_lcd_ili9341/touch_resistive/touch_resistive.h"

#include <stdio.h>

// ── Layout constants (landscape 320x240) ─────────────────────────────────────
#define MENU_TITLE_Y     20
#define MENU_BTN_X       60
#define MENU_BTN_Y       110
#define MENU_BTN_W       200
#define MENU_BTN_H       60
#define MENU_HSCORE_Y    200

#define TIMER_BAR_X      0
#define TIMER_BAR_Y      32
#define TIMER_BAR_W      SCREEN_W
#define TIMER_BAR_H      8

// ── Helper: color from percentage ────────────────────────────────────────────
static uint16_t timer_color(int pct) {
    if (pct > 60) return ILI9341_GREEN;
    if (pct > 30) return ILI9341_YELLOW;
    return ILI9341_RED;
}

// ── Init ─────────────────────────────────────────────────────────────────────
void display_init_lcd(void) {
    LCD_initDisplay();
    LCD_setRotation(SCREEN_ROTATION);
    gfx_init();
    gfx_clear();
    configure_touch();
}

// ── Menu ─────────────────────────────────────────────────────────────────────
void display_menu(uint32_t hi_solo, uint32_t hi_dupla) {
    (void)hi_dupla;
    gfx_fillRect(0, 0, SCREEN_W, SCREEN_H, ILI9341_BLACK);

    // Title
    gfx_setTextSize(5);
    gfx_setTextColor(ILI9341_WHITE);
    gfx_drawText(50, MENU_TITLE_Y, "GENIUS");

    // JOGAR button (centered, big)
    gfx_fillRect(MENU_BTN_X, MENU_BTN_Y, MENU_BTN_W, MENU_BTN_H, ILI9341_GREEN);
    gfx_setTextSize(4);
    gfx_setTextColor(ILI9341_BLACK);
    gfx_drawText(MENU_BTN_X + 25, MENU_BTN_Y + 14, "JOGAR");

    // High score
    char buf[24];
    gfx_setTextSize(2);
    gfx_setTextColor(ILI9341_YELLOW);
    sprintf(buf, "BEST: %lu", (unsigned long)hi_solo);
    gfx_drawText(100, MENU_HSCORE_Y, buf);
}

int display_wait_menu_touch(void) {
    int px, py, tx, ty;

    // Drain any existing touch
    while (readPoint(&px, &py)) sleep_ms(50);

    while (1) {
        if (readPoint(&px, &py)) {
            gfx_touchTransform(SCREEN_ROTATION, px, py, &tx, &ty);

            // JOGAR button
            if (tx >= MENU_BTN_X && tx <= MENU_BTN_X + MENU_BTN_W &&
                ty >= MENU_BTN_Y && ty <= MENU_BTN_Y + MENU_BTN_H) {
                gfx_fillRect(MENU_BTN_X, MENU_BTN_Y, MENU_BTN_W, MENU_BTN_H, ILI9341_YELLOW);
                gfx_setTextSize(4);
                gfx_setTextColor(ILI9341_BLACK);
                gfx_drawText(MENU_BTN_X + 25, MENU_BTN_Y + 14, "JOGAR");
                sleep_ms(300);
                return MODE_SOLO;
            }
        }
        sleep_ms(30);
    }
}

// ── Countdown 3-2-1-GO ──────────────────────────────────────────────────────
void display_countdown(void) {
    const char *nums[] = {"3", "2", "1", "GO!"};
    const uint16_t colors[] = {ILI9341_RED, ILI9341_YELLOW, ILI9341_GREEN, ILI9341_WHITE};

    for (int i = 0; i < 4; i++) {
        gfx_fillRect(0, 0, SCREEN_W, SCREEN_H, ILI9341_BLACK);
        gfx_setTextSize(7);
        gfx_setTextColor(colors[i]);
        int x = (i < 3) ? 140 : 80;
        gfx_drawText(x, 80, nums[i]);
        sleep_ms(i < 3 ? 700 : 400);
    }
    gfx_fillRect(0, 0, SCREEN_W, SCREEN_H, ILI9341_BLACK);
}

// ── Round HUD ────────────────────────────────────────────────────────────────
void display_round_hud(int round, int player, game_mode_t mode) {
    // Clear HUD area
    gfx_fillRect(0, 0, SCREEN_W, 40, ILI9341_BLACK);

    char buf[20];
    sprintf(buf, "ROUND %d", round);
    gfx_setTextSize(2);
    gfx_setTextColor(ILI9341_WHITE);
    gfx_drawText(10, 8, buf);

    if (mode == MODE_DUPLA) {
        sprintf(buf, "P%d", player + 1);
        gfx_setTextSize(3);
        gfx_setTextColor(player == 0 ? ILI9341_CYAN : ILI9341_MAGENTA);
        gfx_drawText(260, 4, buf);
    }

    // Difficulty indicator
    gfx_setTextSize(1);
    if (round >= MULTI_LED_START) {
        gfx_setTextColor(ILI9341_ORANGE);
        gfx_drawText(130, 12, "MULTI!");
    }
}

// ── Timer bar ────────────────────────────────────────────────────────────────
void display_timer_bar(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    int filled_w = (TIMER_BAR_W * pct) / 100;
    int empty_w  = TIMER_BAR_W - filled_w;

    if (filled_w > 0)
        gfx_fillRect(TIMER_BAR_X, TIMER_BAR_Y, filled_w, TIMER_BAR_H, timer_color(pct));
    if (empty_w > 0)
        gfx_fillRect(TIMER_BAR_X + filled_w, TIMER_BAR_Y, empty_w, TIMER_BAR_H, ILI9341_BLACK);
}

// ── Game Over ────────────────────────────────────────────────────────────────
void display_game_over(int round, uint32_t best, bool new_record,
                       game_mode_t mode, int winner) {
    gfx_fillRect(0, 0, SCREEN_W, SCREEN_H, ILI9341_BLACK);

    (void)mode;
    (void)winner;

    // Title
    gfx_setTextSize(4);
    gfx_setTextColor(ILI9341_RED);
    gfx_drawText(30, 20, "GAME OVER");

    // Score
    char buf[24];
    sprintf(buf, "SCORE: %d", round);
    gfx_setTextSize(3);
    gfx_setTextColor(ILI9341_WHITE);
    gfx_drawText(60, 85, buf);

    // Best
    sprintf(buf, "BEST: %lu", (unsigned long)best);
    gfx_setTextSize(2);
    gfx_setTextColor(ILI9341_YELLOW);
    gfx_drawText(90, 130, buf);

    // New record
    if (new_record) {
        gfx_setTextSize(3);
        gfx_setTextColor(ILI9341_YELLOW);
        gfx_drawText(30, 160, "NEW RECORD!");
    }

    // Tap to continue
    gfx_setTextSize(1);
    gfx_setTextColor(ILI9341_WHITE);
    gfx_drawText(90, 220, "TAP TO CONTINUE");
}

// ── Wait for any touch ───────────────────────────────────────────────────────
void display_wait_touch(void) {
    int px, py;
    // Drain
    while (readPoint(&px, &py)) sleep_ms(50);
    // Wait for new touch
    while (!readPoint(&px, &py)) sleep_ms(30);
    sleep_ms(200);
}
