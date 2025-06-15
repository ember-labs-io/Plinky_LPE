#pragma once
#include "utils.h"

enum EWavetables {
	WT_SAW,
	WT_SQUARE,
	WT_SIN,
	WT_SIN_2,
	WT_FM,
	WT_SIN_FOLD1,
	WT_SIN_FOLD2,
	WT_SIN_FOLD3,
	WT_NOISE_FOLD1,
	WT_NOISE_FOLD2,
	WT_NOISE_FOLD3,
	WT_WHITE_NOISE,
	WT_UNUSED1,
	WT_UNUSED2,
	WT_UNUSED3,
	WT_UNUSED4,
	WT_UNUSED5,
};

#define WAVETABLE_SIZE (1022 + 9) // 9 octaves, top octave is 512 samples
#define NUM_WAVETABLES 17

extern const float pitches[1025];
extern const short sigmoid[65536];
extern const u8 rndtab[65536];
extern const u16 wavetable_octave_offset[17];
extern __attribute__((section(".wavetableSection"))) const short wavetable[NUM_WAVETABLES][WAVETABLE_SIZE];

float table_interp(const float* table, int x);
float lpf_k(int x);