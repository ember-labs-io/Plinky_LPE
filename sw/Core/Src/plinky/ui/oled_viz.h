#pragma once
#include "utils.h"

// this manages drawing visuals on the oled display

void flash_message(Font fnt, const char* msg, const char* submsg);

void clear_scope_pixel(u8 x);
void put_scope_pixel(u8 x, u8 y);

void draw_oled_visuals(void);
