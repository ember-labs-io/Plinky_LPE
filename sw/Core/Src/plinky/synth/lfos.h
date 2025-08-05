#pragma once
#include "utils.h"

// mod sources A, B, X and Y are referred to as (complex) lfos

extern s32 lfo_cur[NUM_LFOS];

void update_lfo_scope(void);
void update_lfo(u8 lfo_id);
void draw_lfos(void);