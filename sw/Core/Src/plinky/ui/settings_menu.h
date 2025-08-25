#pragma once
#include "utils.h"

void open_settings_menu(void);
void select_settings_item(u8 x, u8 y);
void settings_encoder_press(void);
void edit_settings_from_encoder(s8 end_diff);
void draw_settings_menu(void);
void settings_menu_leds(u8 pulse);