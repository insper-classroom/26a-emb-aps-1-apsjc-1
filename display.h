#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

// Orientação retrato (240x320)
#define LCD_WIDTH  240
#define LCD_HEIGHT 320

// Cores RGB565
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F

void display_init(void);

void lcd_fill(uint16_t color);
void lcd_fill_rect(int x, int y, int w, int h, uint16_t color);
void lcd_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg, int scale);
void lcd_draw_text(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale);

// Lê o touch. Retorna true se algum dedo está pressionando, e preenche x/y em coordenadas de pixel.
bool touch_read(int *x_out, int *y_out);

#endif
