#pragma once
#include "synth.h"
#include "utils.h"

// Strings in plinky are the virtual equivalent of touchstrips. Touchstrips can only register physical touches from the
// user. Strings can also register virtual touches: triggered by latching, the arpeggiator or the sequencer. Strings
// combine physical and virtual touches and use these to trigger plinky's voices

// which of these can we keep local?
extern u8 string_touched;     // sampler & params
extern u8 string_touch_start; // arp

extern u8 midi_note[NUM_STRINGS];
extern u8 midi_channel[NUM_STRINGS];
extern u8 midi_pressure_override;
extern u8 midi_pitch_override;
extern u8 midi_suppress;

Touch* get_string_touch(u8 string_id);
Touch* sorted_string_touch_ptr(u8 string_id);

void clear_latch(void);

void generate_string_touches(void);
void strings_rcv_midi(u8 status, u8 d1, u8 d2);
void strings_clear_midi(void);
// this only exists for midi output - remove after midi cleanup
Touch* get_string_touch_prev(u8 string_id, u8 frames_back);