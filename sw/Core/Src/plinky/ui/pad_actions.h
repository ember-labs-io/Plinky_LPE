#pragma once
#include "utils.h"

// Any non-synth functionality on the main 8 x 8 grid of pads is called a pad action. This includes selecting parameters
// and mod sources, editing sample slice points, selecting/loading/copying in the load preset ui, etc.

extern u8 long_press_pad;

// shift states represented by the bottom row (Plinky) or left column (Plinky+) of eight pads
// shift states are mutually exclusive

extern ShiftState shift_state;

// utils
void clear_long_press(void);
void press_action_during_shift(void);

// main

void handle_pad_actions(u8 strip_id, Touch* strip_cur);
void handle_pad_action_long_presses(void);

void shift_set_state(ShiftState new_state);
void shift_release_state(void);
void shift_hold_state(void);

// visuals

bool mod_action_pressed(void);
bool pad_actions_oled_visuals(void);
bool shift_states_oled_visuals(void);