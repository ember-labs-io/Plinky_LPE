#pragma once
#include "gfx/gfx.h"
#include "utils.h"

// this manages drawing visuals on the oled display

const char* note_name(int note);

void flash_message(Font fnt, const char* msg, const char* submsg);
void flash_parameter(u8 param_id);

void clear_scope_pixel(u8 x);
void put_scope_pixel(u8 x, u8 y);

void draw_oled_visuals(void);
