#pragma once
#include "utils.h"

// this module handles mixing audio levels throughout the synth and applying audio effects

extern short* reverb_ram_buf;
extern short* delay_ram_buf;

// sampler stuff
extern s16 audio_in_peak;
extern s16 audio_in_hold;
void reverb_clear(void);
void delay_clear(void);

// possibly move to sampler

extern ValueSmoother ext_gain_smoother;
void init_ext_gain_for_recording(void);

// main

void audio_init(void);
void audio_pre(u32* audio_out, u32* audio_in);
void audio_post(u32* audio_out, u32* audio_in);
