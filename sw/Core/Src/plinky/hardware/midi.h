#pragma once
#include "synth/params.h"
#include "utils.h"

#define NUM_MIDI_CHANNELS 16

extern u8 midi_chan_pressure[NUM_MIDI_CHANNELS];
extern s16 midi_chan_pitchbend[NUM_MIDI_CHANNELS];

void midi_init(void);
void process_midi(void);
void set_midi_goal_note(u8 string_id, u8 midi_note);