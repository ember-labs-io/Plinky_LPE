#pragma once
#include "utils.h"

u8* oled_buffer(void);
void oled_init(void);
void oled_clear(void);
void oled_flip(void);
void oled_flip_with_buffer(u8* buffer);

void oled_open_debug_buffer(u8 row);
void oled_close_debug_buffer();