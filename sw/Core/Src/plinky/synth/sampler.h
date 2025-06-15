#pragma once
#include "hardware/mem_defs.h"
#include "synth.h"
#include "utils.h"

#define MAX_SAMPLE_VOICES 6
#define MAX_SAMPLE_LEN (1024 * 1024 * 2)  // max sample length in samples
#define AVG_GRAINBUF_SAMPLE_SIZE (64 + 4) // 2 extra for interpolation, 2 extra for SPI address at the start
#define GRAINBUF_BUDGET (AVG_GRAINBUF_SAMPLE_SIZE * 32)

typedef enum SamplerMode {
	SM_PREVIEW,   // previewing a recorded sample
	SM_ERASING,   // clearing sample memory
	SM_PRE_ARMED, // ready to be armed (rec level can be adjusted here)
	SM_ARMED,     // armed for auto-recording when audio starts
	SM_RECORDING, // recording sample
	SM_STOPPING1, // we stop for 4 cycles to write 0s at the end
	SM_STOPPING2,
	SM_STOPPING3,
	SM_STOPPING4,
} SamplerMode;

#define NUM_GRAINS 32

extern SamplerMode sampler_mode;

// spi
extern int grain_pos[32];
extern s16 grain_buf[GRAINBUF_BUDGET];
extern s16 grain_buf_end[32];

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
