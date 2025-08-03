#pragma once
#include "utils.h"

// Main columns are numbered 0-7, left-to-right
// Pads are numbered 0-7, top-to-bottom

// Shift-pads are on column 8
// Shift-pads are numbered 0-7, left-to-right (Plinky) or top-to-bottom (Plinky+)

extern u8 leds[NUM_TOUCHSTRIPS][PADS_PER_STRIP];

void leds_init(void);
void leds_update(void);
void leds_bootswish(void);

static inline u8 led_add_gamma(s16 i) {
	if (i < 0)
		return 0;
	if (i >= 255)
		return 255;
	return (i * i) >> 8;
}
