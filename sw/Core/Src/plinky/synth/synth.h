#pragma once
#include "utils.h"

// this module manages the basic sound generation of plinky, it generates oscillators for each of the eight voices based
// on the virtual touches in the eight strings, applies the envelope and basic sound parameters
// this module also sends out pitch/pressure/gate cv signals based on the generated oscillators

#define NUM_VOICES 8
#define OSCS_PER_VOICE 4

typedef struct Osc {
	u32 phase;
	u32 prev_sample;
	s32 phase_diff;
	s32 goal_phase_diff;
	s32 pitch;
} Osc;

typedef struct GrainPair {
	int fpos24;
	int pos[2];
	int vol24;
	int dvol24;
	int dpos24;
	float grate_ratio;
	float multisample_grate;
	int bufadjust; // for reverse grains, we adjust the dma buffer address by this many samples
	int outflags;
} GrainPair;

typedef struct Voice {
	// oscillator (sampler only uses the pitch value)
	Osc osc[OSCS_PER_VOICE];
	// env 1
	float env1_lvl;
	bool env1_decaying;
	ValueSmoother lpg_smoother[2];
	// env 1 visuals
	float env1_peak;
	float env1_norm;
	// env 2
	float env2_lvl;
	u16 env2_lvl16;
	bool env2_decaying;
	// noise
	float noise_lvl;
	// sampler state
	GrainPair grain_pair[2];
	int playhead8;
	u8 slice_id;
	u16 touch_pos_start;
	ValueSmoother touch_pos;
} Voice;

extern Voice voices[NUM_VOICES];

void handle_synth_voices(u32* dst);

u8 draw_high_note(void);
void draw_max_pres(void);
void draw_voices(void);