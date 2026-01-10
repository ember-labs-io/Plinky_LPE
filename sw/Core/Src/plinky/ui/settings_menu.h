#pragma once
#include "utils.h"

void open_settings_menu(void);
void press_settings_menu_pad(u8 x, u8 y);
void settings_menu_actions(void);
void settings_encoder_press(bool pressed, u16 duration);
void edit_settings_from_encoder(s8 end_diff);
void draw_settings_menu(void);
void settings_menu_leds(u8 pulse);