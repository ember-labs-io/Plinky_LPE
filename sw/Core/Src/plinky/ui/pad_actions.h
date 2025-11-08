#pragma once
#include "utils.h"

// Any press on a pad that is not playing the synth is a pad action

extern ShiftState shift_state;

// main

void handle_pad_actions(u8 strip_id);
void pad_actions_frame(void);
void pad_actions_keep_edit_mode_open(void);

// visuals

bool ptn_edit_active(void);
bool mod_action_pressed(void);
bool pad_actions_oled_visuals(void);
bool shift_states_oled_visuals(void);
u8 ui_load_long_press_led(u8 x, u8 y, u8 pulse_8x);