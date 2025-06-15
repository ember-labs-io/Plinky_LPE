#pragma once
#include "hardware/adc_dac.h"
#include "params.h"
#include "sampler.h"
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
static inline void init_ext_gain_for_recording(void) {
	set_smoother(&ext_gain_smoother, 65535 - adc_get_raw(ADC_A_KNOB));
}

// main

void audio_init(void);
void audio_pre(u32* audio_out, u32* audio_in);
void audio_post(u32* audio_out, u32* audio_in);
