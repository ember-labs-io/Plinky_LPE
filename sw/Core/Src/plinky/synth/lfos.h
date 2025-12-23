#pragma once
#include "utils.h"

// mod sources A, B, X and Y are referred to as (complex) lfos

extern s32 lfo_cur[NUM_LFOS];

void lfos_tick(void);
void draw_lfos(void);