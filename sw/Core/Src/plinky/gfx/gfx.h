#pragma once
#include "data/fonts.h"
#include "data/icons.h"
#include "data/logo.h"
#include "oled/oled.h"
#include "utils.h"

// graphics functions for writing to the oled

extern u8 gfx_text_color; // 0 = black, 1 = white, 2 = upper shadow, 3 = lower shadow

void gfx_init();

void put_pixel(int x, int y, int c);
void vline(int x1, int y1, int y2, int c);
void hline(int x1, int y1, int x2, int c);

void fill_rectangle(int x1, int y1, int x2, int y2);
void half_rectangle(int x1, int y1, int x2, int y2);
void inverted_rectangle(int x1, int y1, int x2, int y2);

int draw_icon(int x, int y, unsigned char c, int textcol);

int str_width(Font f, const char* buf);
int str_height(Font f, const char* buf);
int draw_str(int x, int y, Font f, const char* buf);
int fdraw_str(int x, int y, Font f, const char* fmt, ...);
int drawstr_noright(int x, int y, Font f, const char* buf);

void gfx_debug(u8 row, const char* fmt, ...);