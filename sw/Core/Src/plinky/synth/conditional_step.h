#pragma once
#include "utils.h"

// conditional steps are used by the arpeggiator and the sequencer
// a conditional step can either advance or not advance in the sequence, and can either play or not play, based on the
// chance and euclid len parameters

static void do_conditional_step(ConditionalStep* c_step) {
	u8 steps = abs(c_step->euclid_len);
	u8 dens_abs = clampi((abs(c_step->density) + 256) >> 9, 0, 128); // density, 128 equals 100%
	bool cond_trig;

	// step-length of 1 does not exist
	if (steps)
		steps++;
	// 0 length: density is used as a true random trigger percentage
	if (steps == 0)
		cond_trig = (rand() & 127) < dens_abs;
	// 2+ length: euclidian sequencing
	else {
		float k = dens_abs / 128.f;                                                           // chance in 0-1 range
		cond_trig = (floor(c_step->euclid_trigs * k) != floor(c_step->euclid_trigs * k - k)); // euclidian trigger
	}

	// play based on the euclidian condition
	c_step->play_step = cond_trig;

	// wait mode: don't advance on non-played steps
	c_step->advance_step = c_step->density < 0 ? cond_trig : true;

	// increment step
	c_step->euclid_trigs++;
	if (steps)
		c_step->euclid_trigs %= steps;
}
