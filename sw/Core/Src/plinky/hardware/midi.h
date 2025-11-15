#pragma once
#include "utils.h"

// utils
bool midi_string_used(u8 string_id);
void midi_try_get_touch(u8 string_id, s16* pressure, s16* position);
s32 midi_get_pitch(u8 string_id);
void midi_try_end_note(u8 string_id);
void midi_clear_all(void);

// main
void init_midi(void);
void process_midi(void);

// cue midi out
void midi_send_clock(void);
void midi_send_transport(MidiMessageType transport_type);
void midi_set_goal_note(u8 string_id, u8 midi_note);

void debug_log(const char* format, ...);