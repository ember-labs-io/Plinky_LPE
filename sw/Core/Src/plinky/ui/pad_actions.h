#pragma once
#include "utils.h"

// Any non-synth functionality on the main 8 x 8 grid of pads is called a pad action. This includes selecting parameters
// and mod sources, editing sample slice points, selecting/loading/copying in the load preset ui, etc.

extern u8 long_press_pad;

// main

void handle_pad_actions(u8 strip_id, Touch* strip_cur);
void handle_pad_action_long_presses(void);

// visuals

bool mod_action_pressed(void);
bool pad_actions_oled_visuals(void);