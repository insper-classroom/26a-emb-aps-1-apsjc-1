#include "display.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"

// ===== Pinagem =====
#define PIN_SCK   18
#define PIN_MOSI  19
#define PIN_CS    17
#define PIN_DC    22
#define PIN_RST   16

#define PIN_XP    14   // X+  (digital)
#define PIN_XM    26   // X-  (ADC0)
#define PIN_YP    27   // Y+  (ADC1)
#define PIN_YM    20   // Y-  (digital)

#define ADC_XM_CH 0
#define ADC_YP_CH 1

#define LCD_SPI   spi0

// ===== SPI helpers =====
static inline void cs_low(void)  { gpio_put(PIN_CS, 0); }
static inline void cs_high(void) { gpio_put(PIN_CS, 1); }
static inline void dc_cmd(void)  { gpio_put(PIN_DC, 0); }
static inline void dc_data(void) { gpio_put(PIN_DC, 1); }

static void lcd_write_cmd(uint8_t cmd) {
    dc_cmd();
    cs_low();
    spi_write_blocking(LCD_SPI, &cmd, 1);
    cs_high();
}

static void lcd_write_data(const uint8_t *data, size_t len) {
    dc_data();
    cs_low();
    spi_write_blocking(LCD_SPI, data, len);
    cs_high();
}

static void lcd_write_data_byte(uint8_t b) {
    lcd_write_data(&b, 1);
}

static void lcd_set_window(int x0, int y0, int x1, int y1) {
    uint8_t buf[4];
    lcd_write_cmd(0x2A);
    buf[0] = x0 >> 8; buf[1] = x0 & 0xFF;
    buf[2] = x1 >> 8; buf[3] = x1 & 0xFF;
    lcd_write_data(buf, 4);

    lcd_write_cmd(0x2B);
    buf[0] = y0 >> 8; buf[1] = y0 & 0xFF;
    buf[2] = y1 >> 8; buf[3] = y1 & 0xFF;
    lcd_write_data(buf, 4);

    lcd_write_cmd(0x2C);
}

void lcd_fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_WIDTH)  w = LCD_WIDTH  - x;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    lcd_set_window(x, y, x + w - 1, y + h - 1);

    uint8_t hi = color >> 8, lo = color & 0xFF;
    uint8_t buf[64];
    for (int i = 0; i < 32; i++) { buf[i*2] = hi; buf[i*2+1] = lo; }

    int total = w * h;
    dc_data();
    cs_low();
    while (total > 0) {
        int n = total > 32 ? 32 : total;
        spi_write_blocking(LCD_SPI, buf, n * 2);
        total -= n;
    }
    cs_high();
}

void lcd_fill(uint16_t color) {
    lcd_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
}

// ===== Fonte 5x7 (subset: A-Z, 0-9, espaço, !) =====
// Cada char = 7 bytes (linhas). Bit 4 = coluna esquerda, bit 0 = direita.
typedef struct { char c; uint8_t rows[7]; } glyph_t;

static const glyph_t FONT[] = {
    {'A', {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
    {'C', {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}},
    {'D', {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}},
    {'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}},
    {'F', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
    {'G', {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}},
    {'H', {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'I', {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F}},
    {'J', {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}},
    {'K', {0x11,0x12,0x14,0x18,0x14,0x12,0x11}},
    {'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
    {'M', {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}},
    {'N', {0x11,0x19,0x15,0x15,0x13,0x11,0x11}},
    {'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'P', {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
    {'Q', {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}},
    {'R', {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
    {'S', {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}},
    {'T', {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
    {'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'V', {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
    {'W', {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}},
    {'X', {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
    {'Y', {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}},
    {'Z', {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
    {'0', {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}},
    {'1', {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
    {'2', {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}},
    {'3', {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}},
    {'4', {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}},
    {'5', {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
    {'6', {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}},
    {'7', {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
    {'8', {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}},
    {'9', {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}},
    {'!', {0x04,0x04,0x04,0x04,0x04,0x00,0x04}},
};
#define FONT_LEN (sizeof(FONT)/sizeof(FONT[0]))

static const uint8_t *get_glyph(char c) {
    for (size_t i = 0; i < FONT_LEN; i++) {
        if (FONT[i].c == c) return FONT[i].rows;
    }
    return NULL;
}

void lcd_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg, int scale) {
    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;
    if (c == ' ') return; // espaço apenas avança no draw_text

    const uint8_t *g = get_glyph(c);
    if (!g) return;

    int w = 5 * scale;
    int h = 7 * scale;
    if (x < 0 || y < 0 || x + w > LCD_WIDTH || y + h > LCD_HEIGHT) return;

    lcd_set_window(x, y, x + w - 1, y + h - 1);

    uint8_t fg_hi = fg >> 8, fg_lo = fg & 0xFF;
    uint8_t bg_hi = bg >> 8, bg_lo = bg & 0xFF;
    uint8_t rowbuf[5 * 8 * 2]; // 5 cols * scale_max=8 * 2 bytes

    dc_data();
    cs_low();
    for (int row = 0; row < 7; row++) {
        uint8_t bits = g[row];
        int idx = 0;
        for (int col = 0; col < 5; col++) {
            int on = bits & (1 << (4 - col));
            uint8_t hi = on ? fg_hi : bg_hi;
            uint8_t lo = on ? fg_lo : bg_lo;
            for (int sx = 0; sx < scale; sx++) {
                rowbuf[idx++] = hi;
                rowbuf[idx++] = lo;
            }
        }
        for (int sy = 0; sy < scale; sy++) {
            spi_write_blocking(LCD_SPI, rowbuf, idx);
        }
    }
    cs_high();
}

void lcd_draw_text(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale) {
    int cx = x;
    int step = (5 * scale) + scale; // 1 col de espaço entre chars (escalada)
    while (*s) {
        if (*s == ' ') {
            // espaço: pinta fundo e avança
            lcd_fill_rect(cx, y, step, 7 * scale, bg);
        } else {
            lcd_draw_char(cx, y, *s, fg, bg, scale);
            // pinta o gap entre chars com bg
            lcd_fill_rect(cx + 5 * scale, y, scale, 7 * scale, bg);
        }
        cx += step;
        s++;
    }
}

// ===== Init do LCD =====
void display_init(void) {
    // CS/DC/RST como GPIO antes de qualquer SPI
    gpio_init(PIN_CS);  gpio_set_dir(PIN_CS,  GPIO_OUT); gpio_put(PIN_CS,  1);
    gpio_init(PIN_DC);  gpio_set_dir(PIN_DC,  GPIO_OUT); gpio_put(PIN_DC,  1);
    gpio_init(PIN_RST); gpio_set_dir(PIN_RST, GPIO_OUT); gpio_put(PIN_RST, 1);

    // SPI a 8 MHz (conservador para fios de protoboard)
    spi_init(LCD_SPI, 8 * 1000 * 1000);
    spi_set_format(LCD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    // Reset por hardware (timing folgado)
    sleep_ms(50);
    gpio_put(PIN_RST, 0);
    sleep_ms(100);
    gpio_put(PIN_RST, 1);
    sleep_ms(250);

    // Sequência de init padrão do ILI9341
    lcd_write_cmd(0x01); sleep_ms(150); // Software reset

    lcd_write_cmd(0xCF); { uint8_t d[]={0x00,0xC1,0x30}; lcd_write_data(d,3); }
    lcd_write_cmd(0xED); { uint8_t d[]={0x64,0x03,0x12,0x81}; lcd_write_data(d,4); }
    lcd_write_cmd(0xE8); { uint8_t d[]={0x85,0x00,0x78}; lcd_write_data(d,3); }
    lcd_write_cmd(0xCB); { uint8_t d[]={0x39,0x2C,0x00,0x34,0x02}; lcd_write_data(d,5); }
    lcd_write_cmd(0xF7); lcd_write_data_byte(0x20);
    lcd_write_cmd(0xEA); { uint8_t d[]={0x00,0x00}; lcd_write_data(d,2); }
    lcd_write_cmd(0xC0); lcd_write_data_byte(0x23); // Power control 1
    lcd_write_cmd(0xC1); lcd_write_data_byte(0x10); // Power control 2
    lcd_write_cmd(0xC5); { uint8_t d[]={0x3E,0x28}; lcd_write_data(d,2); } // VCOM
    lcd_write_cmd(0xC7); lcd_write_data_byte(0x86);
    lcd_write_cmd(0x36); lcd_write_data_byte(0x48); // MADCTL: portrait, BGR
    lcd_write_cmd(0x3A); lcd_write_data_byte(0x55); // 16bpp
    lcd_write_cmd(0xB1); { uint8_t d[]={0x00,0x18}; lcd_write_data(d,2); }
    lcd_write_cmd(0xB6); { uint8_t d[]={0x08,0x82,0x27}; lcd_write_data(d,3); }
    lcd_write_cmd(0xF2); lcd_write_data_byte(0x00);
    lcd_write_cmd(0x26); lcd_write_data_byte(0x01);
    lcd_write_cmd(0x11); sleep_ms(120); // Sleep out
    lcd_write_cmd(0x29);                // Display ON

    // ADC para o touch
    adc_init();

    // TESTE VISUAL: pinta vermelho 500ms, depois verde 500ms, depois azul 500ms.
    // Se você vir essas 3 cores em sequência, init OK.
    // Se a tela ficar branca/preta, o problema é fiação ou SPI.
    lcd_fill(COLOR_RED);   sleep_ms(500);
    lcd_fill(COLOR_GREEN); sleep_ms(500);
    lcd_fill(COLOR_BLUE);  sleep_ms(500);
    lcd_fill(COLOR_BLACK);
}

// ===== Touch resistivo de 4 fios =====
static void pin_drive(int pin, int value) {
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_disable_pulls(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, value);
}

static void pin_hi_z(int pin) {
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_set_dir(pin, GPIO_IN);
    gpio_disable_pulls(pin);
}

static int read_adc_pin(int pin, int channel) {
    adc_gpio_init(pin);
    adc_select_input(channel);
    // descarta primeira leitura
    (void)adc_read();
    return adc_read();
}

static int read_touch_x_raw(void) {
    // X = gradiente em Y+ -> Y-, lê em X- (ADC)
    pin_drive(PIN_YP, 1);
    pin_drive(PIN_YM, 0);
    pin_hi_z(PIN_XP);
    sleep_us(200);
    return read_adc_pin(PIN_XM, ADC_XM_CH);
}

static int read_touch_y_raw(void) {
    // Y = gradiente em X+ -> X-, lê em Y+ (ADC)
    pin_drive(PIN_XP, 1);
    pin_drive(PIN_XM, 0);
    pin_hi_z(PIN_YM);
    sleep_us(200);
    return read_adc_pin(PIN_YP, ADC_YP_CH);
}

bool touch_read(int *x_out, int *y_out) {
    // Detecção: pull-up em X+, Y- = 0, Y+ Hi-Z, lê ADC em X-.
    // Sem dedo -> alto (~4095). Com dedo -> cai bastante.
    gpio_set_function(PIN_XP, GPIO_FUNC_SIO);
    gpio_set_dir(PIN_XP, GPIO_IN);
    gpio_pull_up(PIN_XP);
    pin_drive(PIN_YM, 0);
    pin_hi_z(PIN_YP);
    sleep_us(200);
    int detect = read_adc_pin(PIN_XM, ADC_XM_CH);
    gpio_disable_pulls(PIN_XP);

    if (detect > 3500) return false; // sem toque

    int rx = read_touch_x_raw();
    int ry = read_touch_y_raw();

    // Calibração rude. Ajuste se o toque ficar deslocado.
    const int X_MIN = 350, X_MAX = 3750;
    const int Y_MIN = 350, Y_MAX = 3750;

    int xp = (rx - X_MIN) * LCD_WIDTH  / (X_MAX - X_MIN);
    int yp = (ry - Y_MIN) * LCD_HEIGHT / (Y_MAX - Y_MIN);
    if (xp < 0) xp = 0; if (xp >= LCD_WIDTH)  xp = LCD_WIDTH  - 1;
    if (yp < 0) yp = 0; if (yp >= LCD_HEIGHT) yp = LCD_HEIGHT - 1;

    *x_out = xp;
    *y_out = yp;
    return true;
}
