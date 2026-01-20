#pragma once
#include "utils.h"

// utils
void midi_precalc_bends(void);
void midi_clear_all(void);
void midi_panic(void);

// main
void init_midi(void);
void midi_tick(void);
void set_mpe_channels(u8 zone, u8 num_chans);
void midi_push_preset(void);
bool midi_try_get_touch(u8 string_id, s16* pressure, s16* position, u8* note_number, u8* start_velocity,
                        s32* pitchbend_pitch);

// cue midi out
void midi_send_clock(void);
void midi_send_transport(MidiMessageType transport_type);
void midi_send_param(Param param_id);

// visuals
void draw_sysex_flag(void);

void debug_log(const char* format, ...);