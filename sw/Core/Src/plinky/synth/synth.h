#pragma once
#include "utils.h"

// this module manages the basic sound generation of plinky, it generates oscillators for each of the eight voices based
// on the virtual touches in the eight strings, applies the envelope and basic sound parameters
// this module also sends out pitch/pressure/gate cv signals based on the generated oscillators

const SynthString* get_synth_string(u8 string_id);

// utils
s32 quant_pitch_to_scale(s32 pitch, Scale scale);
s16 step_at_string(u8 string_id, Scale scale); // only for led_viz!
u8 find_string_for_pitch(s32 pitch, bool quantize);
u16 string_position_from_pitch(u8 string_id, s32 pitch);
void clear_latch(void);
void clear_synth_string(u8 string_id);
void clear_synth_strings(void);

// spi
s16* grain_buf_ptr(void);
u32 grain_address(u8 grain_id);
s16 grain_buf_end_get(u8 grain_id);

// main
void generate_string_touches(void);
void handle_synth_voices(u32* dst);

// visuals
u8 draw_high_note(void);
void draw_max_pres(void);
void draw_voices(bool show_latch);
void draw_sample_playback(SampleInfo* s);