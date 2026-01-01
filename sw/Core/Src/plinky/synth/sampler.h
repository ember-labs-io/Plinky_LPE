#pragma once
#include "utils.h"

extern SamplerMode sampler_mode;

void open_sampler(u8 with_sample_id);

void sampler_recording_tick(u32* dst, u32* audioin);
void start_erasing_sample_buffer(void);
void clear_flash_sample(void);
void start_recording_sample(void);
void write_flash_sample_blocks(void);
void sampler_record_slice_point(void);
void try_stop_recording_sample(void);
void finish_recording_sample(void);

static inline void stop_recording_sample(void) {
	sampler_mode = SM_STOPPING1;
}

// slices

void sampler_adjust_cur_slice_point(float diff);
void sampler_adjust_slice_point_from_touch(u8 slice_id, u16 touch_pos, bool init_slice);
void sampler_adjust_cur_slice_pitch(s8 diff);

// modes

void sampler_toggle_play_mode(void);
void sampler_iterate_loop_mode(void);

// visuals

u8 get_waveform4(SampleInfo* s, int x);
u16 getwaveform4zoom(SampleInfo* s, int x, int zoom);

void sampler_oled_visuals(void);

void update_peak_hist(void);
void sampler_leds(u8 pulse_half, u8 pulse);
u8 ext_audio_led(u8 x, u8 y);
