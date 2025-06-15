#pragma once
#include "param_defs.h"
#include "utils.h"

// mod sources A, B, X and Y are referred to as (complex) lfos

#define NUM_LFOS 4
#define LFO_SCOPE_FRAMES 16

typedef enum LfoShape {
	LFO_TRI,
	LFO_SIN,
	LFO_SMOOTH_NOISE,
	LFO_STEP_NOISE,
	LFO_BI_SQUARE,
	LFO_SQUARE,
	LFO_CASTLE,
	LFO_SAW,
	LFO_BI_TRIGS,
	LFO_TRIGS,
	LFO_ENV,
	NUM_LFO_SHAPES,
} LfoShape;

extern s32 param_with_lfo[NUM_PARAMS];

void update_lfos(void);
void apply_lfo_mods(Param param_id);
void draw_lfos(void);