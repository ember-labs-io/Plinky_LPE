#pragma once
#include "utils.h"

static inline s32 pitch_at_step(s8 step, Scale scale) {
	u8 steps_in_scale = scale_table[scale][0];
	s16 oct = step / steps_in_scale;
	step -= oct * steps_in_scale;
	if (step < 0) {
		step += steps_in_scale;
		oct--;
	}
	return oct * PITCH_PER_OCT + scale_table[scale][step + 1];
}

static inline s16 step_at_string(u8 string_id, Scale scale) {
	static u16 string_hash[NUM_STRINGS - 1];
	static u16 string_start_step[NUM_STRINGS - 1];
	static u8 string_start_semis[NUM_STRINGS - 1];

	if (string_id == 0)
		return param_index_poly(PP_DEGREE, string_id);

	u8 first_stale_string = 0;
	u16 new_string_hash[NUM_STRINGS - 1];
	u8 string_column[NUM_STRINGS - 1];
	u8 string_scale[NUM_STRINGS - 1];

	// loop downwards from our string to 1 until we find the first up-to-date string
	for (u8 s_id = string_id; s_id >= 1; s_id--) {
		string_column[s_id - 1] = param_index_poly(PP_COLUMN, s_id);
		string_scale[s_id - 1] = param_index_poly(PP_SCALE, s_id);
		new_string_hash[s_id - 1] = string_column[s_id - 1] + (string_scale[s_id - 1] << 4);
		// found up to date string
		if (new_string_hash[s_id - 1] == string_hash[s_id - 1])
			break;
		first_stale_string = s_id;
	}

	// recalculate from first_invalid to string_id
	if (first_stale_string > 0) {
		for (u8 s_id = first_stale_string; s_id <= string_id; ++s_id) {
			// invalidate higher strings when we reach the originally requested string
			if (s_id == string_id)
				for (u8 s = string_id + 1; s < NUM_STRINGS; ++s)
					string_hash[s - 1] = 0;

			// find base octave and pitch in that octave
			u8 start_semis = string_column[s_id - 1];
			if (s_id > 1)
				start_semis += string_start_semis[s_id - 2];
			u8 base_oct = start_semis / 12;
			u16 pitch_in_base_oct = SEMIS_TO_PITCH(start_semis % 12);

			// find closest scale step
			const u16* scale_pitch = scale_table[string_scale[s_id - 1]];
			u8 steps_in_scale = *scale_pitch++;

			// estimate by linear mapping
			u8 estimated_step = pitch_in_base_oct * (steps_in_scale - 1) / scale_pitch[steps_in_scale - 1];
			u16 least_dist = abs(scale_pitch[estimated_step] - pitch_in_base_oct);

			u8 step_in_oct = estimated_step;
			bool check_higher_steps = true;

			// search downward
			if (step_in_oct > 0) {
				do {
					step_in_oct--;
					u16 dist = abs(scale_pitch[step_in_oct] - pitch_in_base_oct);
					if (dist >= least_dist) {
						step_in_oct++;
						break;
					}
					least_dist = dist;
					// if we found a closer step downwards, we know we won't find a closer step upwards
					check_higher_steps = false;
				} while (step_in_oct > 0);
			}

			// search upward
			if (check_higher_steps && step_in_oct < steps_in_scale - 1) {
				step_in_oct = estimated_step;
				do {
					step_in_oct++;
					u16 dist = abs(scale_pitch[step_in_oct] - pitch_in_base_oct);
					if (dist >= least_dist) {
						step_in_oct--;
						break;
					}
					least_dist = dist;
				} while (step_in_oct < steps_in_scale - 1);
			}

			// check first note of next octave
			if (step_in_oct == steps_in_scale - 1 && PITCH_PER_OCT - pitch_in_base_oct < least_dist) {
				step_in_oct = 0;
				base_oct++;
			}

			// save results
			string_hash[s_id - 1] = new_string_hash[s_id - 1];
			string_start_semis[s_id - 1] = base_oct * 12 + PITCH_TO_SEMIS(scale_pitch[step_in_oct]);
			string_start_step[s_id - 1] = base_oct * steps_in_scale + step_in_oct;
		}
	}
	// return with degree offset
	return (string_id == 0 ? 0 : string_start_step[string_id - 1]) + param_index_poly(PP_DEGREE, string_id);
}

static inline s32 string_center_pitch(u8 string_id, Scale scale) {
	s16 step = step_at_string(string_id, scale) + 3;
	u8 steps_in_scale = scale_table[scale][0];
	// get octave
	s16 oct = step / steps_in_scale;
	step -= oct * steps_in_scale;
	if (step < 0) {
		step += steps_in_scale;
		oct--;
	}
	// get pitch-in-octave of pad 3
	s32 pitch3 = scale_table[scale][step + 1];
	// get pitch-in-octave of pad 4
	step = (step + 1) % steps_in_scale;
	s32 pitch4 = scale_table[scale][step + 1];
	if (pitch4 < pitch3)
		pitch4 += PITCH_PER_OCT;
	// return average plus octave offset
	return ((pitch3 + pitch4) >> 1) + oct * PITCH_PER_OCT;
}