#pragma once
#include "utils.h"

#define SAMPLES_PER_TICK 64
#define NUM_SYNC_DIVS 22

typedef enum ClockType {
	CLK_INTERNAL,
	CLK_MIDI,
	CLK_CV,
} ClockType;

static float const max_swing = 0.5f; // 0.3333f represents triplet-feel swing
static u16 const sync_divs_32nds[NUM_SYNC_DIVS] = {1,  2,  3,  4,  5,  6,  8,  10,  12,  16,  20,
                                                   24, 32, 40, 48, 64, 80, 96, 128, 160, 192, 256};

extern ClockType clock_type;
extern u32 synth_tick;
extern u16 bpm_10x;

extern bool pulse_32nd;
extern u16 counter_32nds;

u32 clock_pos_q16(u16 loop_32nds);

void trigger_cv_clock(void);
void trigger_tap_tempo(void);
void cue_clock_reset(void);
void clock_reset(void);

void clock_tick(void);

void clock_rcv_midi(u8 midi_status);