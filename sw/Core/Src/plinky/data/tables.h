#pragma once
#include "utils.h"

#define WAVETABLE_SIZE (1022 + 9) // 9 octaves, top octave is 512 samples
#define NUM_WAVETABLES 17

extern const float pitches[1025];
extern const short sigmoid[65536];
extern const u8 rndtab[65536];
extern const u16 wavetable_octave_offset[17];
extern __attribute__((section(".wavetableSection"))) const short wavetable[NUM_WAVETABLES][WAVETABLE_SIZE];

float table_interp(const float* table, int x);
float lpf_k(int x);