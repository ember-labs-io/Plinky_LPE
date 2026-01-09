#pragma once
#include "utils.h"

// utils
void midi_precalc_bends(void);
void midi_clear_all(void);
void midi_panic(void);

// main
void init_midi(void);
void midi_tick(void);
bool midi_try_get_touch(u8 string_id, s16* pressure, s16* position, s8* note_number, u8* start_velocity,
                        s32* pitchbend_pitch);

// cue midi out
void midi_send_clock(void);
void midi_send_transport(MidiMessageType transport_type);
void midi_send_param(Param param_id);

void debug_log(const char* format, ...);