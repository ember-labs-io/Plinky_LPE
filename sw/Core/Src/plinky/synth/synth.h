#pragma once
#include "utils.h"

// this module manages the basic sound generation of plinky, it generates oscillators for each of the eight voices based
// on the virtual touches in the eight strings, applies the envelope and basic sound parameters
// this module also sends out pitch/pressure/gate cv signals based on the generated oscillators

extern Voice voices[NUM_VOICES];

void handle_synth_voices(u32* dst);

u8 draw_high_note(void);
void draw_max_pres(void);
void draw_voices(bool show_latch);