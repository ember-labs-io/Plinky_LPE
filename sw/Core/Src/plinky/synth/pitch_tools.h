#pragma once
#include "hardware/midi.h"
#include "params.h"
#include "utils.h"

#define PITCH_PER_SEMI 512
#define PITCH_BASE ((32768 - (12 << 9)) + 512 + 101) // pitch value for C4

// center-map pitch 0 to midi note 24
#define quad_pitch_to_midi_note(quad_pitch)                                                                            \
	clampi((quad_pitch + 2 * PITCH_PER_SEMI) / (4 * PITCH_PER_SEMI) + 24, 0, 127)

#define midi_note_to_pitch_offset(midi_note, midi_channel)                                                             \
	(((midi_note - 24) << 9) + midi_chan_pitchbend[midi_channel] / 8)

#define pitch_to_scale_steps(pitch, scale) (((pitch / 512) * scale_table[scale][0] + 1) / 12)

#define steps_in_scale(scale) scale_table[scale][0]

static inline s32 pitch_at_step(Scale scale, u8 step) {
	u8 oct = step / steps_in_scale(scale);
	step -= oct * steps_in_scale(scale);
	if (step < 0) {
		step += steps_in_scale(scale);
		oct--;
	}
	return oct * (12 * 512) + scale_table[scale][step + 1];
}

static inline u8 scale_steps_at_string(Scale scale, u8 string_id) {
	static u8 scale_steps[NUM_STRINGS];
	static u8 step_hash[NUM_STRINGS];

	// first string is our starting point - always zero
	if (string_id == 0)
		return 0;

	s8 stride_semitones = maxi(0, param_val_poly(P_COLUMN, string_id));

	// we basically lazy-generate the scale steps table - whenever the hash changes (scale or column has changed) we
	// recalculate the scale steps for this string and reuse it until the hash changes again
	u8 new_hash = stride_semitones + (scale << 4);

	if (new_hash != step_hash[string_id]) {
		s8 summed_semis = 0;
		u8 used_steps[MAX_SCALE_STEPS] = {1, 0};
		const u16* scale_pitch = scale_table[scale];
		u8 steps_in_scale = *scale_pitch++;
		// calculate the scale steps for strings [1] through [requested string]
		for (int s_id = 1; s_id <= string_id; ++s_id) {
			summed_semis += stride_semitones;  // semis goal (global)
			u8 goal_semis = summed_semis % 12; // semis goal (octave range)
			u8 best_step_id = 0;
			s8 best_delta = 0;   // deltas range from -6 to +6
			s8 best_score = 127; // scores range from -96 to 108
			// loop through steps in scale
			for (u8 step_id = 0; step_id < steps_in_scale; ++step_id) {
				s8 candidate_semis = scale_pitch[step_id] / 512;
				s8 delta_semis = candidate_semis - goal_semis;
				if (delta_semis < -6) {
					delta_semis += 12;
					candidate_semis += 12;
				}
				else if (delta_semis > 6) {
					delta_semis -= 12;
					candidate_semis -= 12;
				}
				// penalise steps we have used more often
				s8 score = abs(delta_semis) * 16 + used_steps[step_id];
				if (score < best_score) {
					best_score = score;
					best_step_id = step_id;
					best_delta = delta_semis;
				}
			}
			used_steps[best_step_id]++;
			summed_semis += best_delta; // adjust the summed globals according to the chosen step

			// we are calculating scale steps for all strings below us - we might as well save and reuse them
			step_hash[s_id] = new_hash;
			scale_steps[s_id] = best_step_id + (summed_semis / 12) * steps_in_scale;
		}
	}
	return scale_steps[string_id];
}

static inline int string_pitch_at_pad(u8 string_id, u8 pad_y) {
	Scale scale = param_val_poly(P_SCALE, string_id);
	// pitch calculation:
	return
	    // calculate pitch offset, based on
	    pitch_at_step(
	        // the scale
	        scale,
	        // the step-offset set by "degree"
	        param_val_poly(P_DEGREE, string_id) +
	            // the step-offset of this string based on "column"
	            scale_steps_at_string(scale, string_id) +
	            // the step-offset caused by the pad_id on the string
	            pad_y)
	    +
	    // add this to the pitch of the bottom-left pad_id
	    12
	        * (
	            // octave offset
	            (param_val_poly(P_OCT, string_id) << 9) +
	            // pitch offset
	            (param_val_poly(P_PITCH, string_id) >> 7));
}

static inline int string_center_pitch(u8 string_id) {
	return (string_pitch_at_pad(string_id, 0) + string_pitch_at_pad(string_id, 7)) / 2;
}