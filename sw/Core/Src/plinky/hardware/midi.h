#pragma once
#include "utils.h"

extern u8 midi_chan_pressure[NUM_MIDI_CHANNELS];
extern s16 midi_chan_pitchbend[NUM_MIDI_CHANNELS];

void midi_init(void);
void process_midi(void);
void set_midi_goal_note(u8 string_id, u8 midi_note);

void midi_send_clock(void);
void midi_send_transport(MidiMessageType transport_type);