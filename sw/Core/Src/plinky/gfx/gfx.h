#pragma once
#include "oled/oled.h"
#include "utils.h"

// graphics functions for writing to the oled

typedef enum Font {
	BOLD = 16,
	F_8 = 0,
	F_12,
	F_16,
	F_20,
	F_24,
	F_28,
	F_32,
	F_8_BOLD = BOLD,
	F_12_BOLD,
	F_16_BOLD,
	F_20_BOLD,
	F_24_BOLD,
	F_28_BOLD,
	F_32_BOLD,
} Font;

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