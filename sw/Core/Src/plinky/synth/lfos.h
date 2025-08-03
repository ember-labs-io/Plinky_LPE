#pragma once
#include "utils.h"

// mod sources A, B, X and Y are referred to as (complex) lfos

extern s32 param_with_lfo[NUM_PARAMS];

void update_lfos(void);
void apply_lfo_mods(Param param_id);
void draw_lfos(void);