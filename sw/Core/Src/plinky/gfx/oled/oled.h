#pragma once
#include "utils.h"

#define OLED_WIDTH 128
#define OLED_HEIGHT 32
#define OLED_BUFFER_SIZE OLED_HEIGHT / 8 * OLED_WIDTH + 1 // first byte is always 0x40

u8* oled_buffer(void);
void oled_init(void);
void oled_clear(void);
void oled_flip(void);
void oled_flip_with_buffer(u8* buffer);

void oled_open_debug_buffer(u8 row);
void oled_close_debug_buffer();