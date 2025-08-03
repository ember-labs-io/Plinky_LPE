#pragma once
#include "utils.h"

extern SamplerMode sampler_mode;

// spi
extern int grain_pos[NUM_GRAINS];
extern s16 grain_buf_end[NUM_GRAINS];

s16* grain_buf_ptr(void);

int using_sampler(void);
void open_sampler(u8 with_sample_id);

// play sampler audio

void sampler_recording_tick(u32* dst, u32* audioin);
void apply_sample_lpg_noise(u8 voice_id, Voice* voice, float goal_lpg, float noise_diff, float drive, u32* dst);
void sampler_playing_tick(void);

// recording samples

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
void draw_sample_playback(SampleInfo* s);

void update_peak_hist(void);
void sampler_leds(u8 pulse_half, u8 pulse);
u8 ext_audio_led(u8 x, u8 y);
