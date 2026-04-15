#ifndef GENIUS_INPUT_H
#define GENIUS_INPUT_H

#include <stdint.h>
#include <stdbool.h>

void    input_init(void);
void    input_clear(void);
void    input_start_timeout(int ms);
void    input_cancel_timeout(void);
bool    input_timed_out(void);
uint8_t input_get_mask(void);

#endif
