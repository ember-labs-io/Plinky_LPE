#pragma once
#include "utils.h"

// shift states represented by the bottom row (Plinky) or left column (Plinky+) of eight pads
// shift states are mutually exclusive

extern ShiftState shift_state;

void press_action_during_shift(void);

void shift_set_state(ShiftState new_state);
void shift_release_state(void);
void shift_hold_state(void);

bool shift_states_oled_visuals(void);
