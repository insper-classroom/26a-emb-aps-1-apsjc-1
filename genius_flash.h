#ifndef GENIUS_FLASH_H
#define GENIUS_FLASH_H

#include <stdint.h>

void     flash_init(void);
uint32_t flash_get_highscore_solo(void);
uint32_t flash_get_highscore_dupla(void);
void     flash_save_highscore(uint32_t solo, uint32_t dupla);

#endif
