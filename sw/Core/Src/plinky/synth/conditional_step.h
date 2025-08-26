#pragma once
#include "params.h"
#include "utils.h"

// conditional steps are used by the arpeggiator and the sequencer
// a conditional step can either advance or not advance in the sequence, and can either play or not play, based on the
// chance and euclid len parameters

static void do_conditional_step(ConditionalStep* c_step, bool chord_mode) {
	u8 steps = abs(c_step->euclid_len);
	// step-length of 1 does not exist
	if (steps)
		steps++;
	u8 dens_abs = clampi((abs(c_step->density) + 256) >> 9, 0, 128); // density, 128 equals 100%
	bool cond_trig;

	if (steps == 0) {
		// chord mode: trigger is always true, some notes are suppressed according to density value
		if (chord_mode)
			cond_trig = true;
		// default: density is used as a true random trigger percentage
		else
			cond_trig = (rand() & 127) < dens_abs;
	}
	// 2+ length: euclidian sequencing
	else {
		float k = dens_abs / 128.f;                                                           // chance in 0-1 range
		cond_trig = (floor(c_step->euclid_trigs * k) != floor(c_step->euclid_trigs * k - k)); // euclidian trigger
	}

	c_step->euclid_trigs++;
	if (steps)
		c_step->euclid_trigs %= steps;

	// skip mode: always advance, silence non-triggered steps
	if (c_step->density > 0) {
		c_step->advance_step = true;
		c_step->play_step = cond_trig;
	}
	// hold mode: don't advance on non-triggered steps
	else {
		c_step->advance_step = cond_trig;
		// non-advanced steps play if gate length is 100%
		if ((param_val(P_GATE_LENGTH) >> 8) == 256)
			c_step->play_step = true;
		else
			c_step->play_step = cond_trig;
	}
}