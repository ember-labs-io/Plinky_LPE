#pragma once
#include "utils.h"

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