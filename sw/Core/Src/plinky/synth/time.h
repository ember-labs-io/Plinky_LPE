#pragma once
#include "utils.h"

#define SAMPLES_PER_TICK 64
#define NUM_SYNC_DIVS 22

static u16 const sync_divs_32nds[NUM_SYNC_DIVS] = {1,  2,  3,  4,  5,  6,  8,  10,  12,  16,  20,
                                                   24, 32, 40, 48, 64, 80, 96, 128, 160, 192, 256};

extern u32 synth_tick;
extern u16 bpm_10x;

extern bool pulse_32nd;
extern u16 counter_32nds;

extern bool using_internal_clock; // for ui

void clock_tick(void);
void cue_clock_resync(void);
void clock_resync(void);

void trigger_tap_tempo(void);
void trigger_cv_clock(void);
void trigger_midi_clock(void);