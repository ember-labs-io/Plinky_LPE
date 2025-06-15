#pragma once
#include "arp.h"
#include "params.h"
#include "utils.h"

// conditional steps are used by the arpeggiator and the sequencer
// a conditional step can either advance or not advance in the sequence, and can either play or not play, based on the
// chance and euclid len parameters

typedef struct ConditionalStep {
	s8 euclid_len;
	u8 euclid_trigs;
	s32 density;
	bool play_step;
	bool advance_step;
} ConditionalStep;

static void do_conditional_step(ConditionalStep* c_step, bool chord_mode) {
	u8 len_abs = abs(c_step->euclid_len);                            // max 64
	u8 dens_abs = clampi((abs(c_step->density) + 256) >> 9, 0, 128); // density, 128 equals 100%
	bool cond_trig;

	// 2+ length: euclidian sequencing
	if (len_abs > 1) {
		float k = dens_abs / 128.f;                                                           // chance in 0-1 range
		cond_trig = (floor(c_step->euclid_trigs * k) != floor(c_step->euclid_trigs * k - k)); // euclidian trigger
	}
	// 0 or 1 length
	else {
		// chord mode: trigger is always true, some notes are suppressed according to density value
		if (chord_mode)
			cond_trig = true;
		// default: density is used as a true random trigger percentage
		else
			cond_trig = (rand() & 127) < dens_abs;
	}

	c_step->euclid_trigs++;
	if (len_abs)
		c_step->euclid_trigs %= len_abs;

	// if either euclid_len or euclid_dens are negative we advance on all steps but silence non-triggered steps
	if ((c_step->euclid_len < 0) ^ (c_step->density < 0)) {
		c_step->advance_step = true;
		c_step->play_step = cond_trig;
	}
	// if they are the same polarity (both positive / negative) we stop non-triggered steps from advancing
	else {
		c_step->advance_step = cond_trig;
		// non-advanced steps play if gate length is 100%
		if ((param_val(P_GATE_LENGTH) >> 8) == 256)
			c_step->play_step = true;
		else
			c_step->play_step = cond_trig;
	}
}