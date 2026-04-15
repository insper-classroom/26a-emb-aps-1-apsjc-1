#ifndef GENIUS_DISPLAY_H
#define GENIUS_DISPLAY_H

#include "genius.h"

void display_init_lcd(void);
void display_menu(uint32_t hi_solo, uint32_t hi_dupla);
int  display_wait_menu_touch(void);   // returns MODE_SOLO or MODE_DUPLA
void display_countdown(void);
void display_round_hud(int round, int player, game_mode_t mode);
void display_timer_bar(int pct);
void display_game_over(int round, uint32_t best, bool new_record,
                       game_mode_t mode, int winner);
void display_wait_touch(void);

#endif
