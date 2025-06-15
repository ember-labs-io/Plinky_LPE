#pragma once
#include "params.h"
#include "strings.h"
#include "utils.h"

typedef int Pitch;

typedef enum Scale {
	S_MAJOR,
	S_MINOR,
	S_HARMMINOR,
	S_PENTA,
	S_PENTAMINOR,
	S_HIRAJOSHI,
	S_INSEN,
	S_IWATO,
	S_MINYO,

	S_FIFTHS,
	S_TRIADMAJOR,
	S_TRIADMINOR,

	S_DORIAN,
	S_PHYRGIAN,
	S_LYDIAN,
	S_MIXOLYDIAN,
	S_AEOLIAN,
	S_LOCRIAN,

	S_BLUESMINOR,
	S_BLUESMAJOR,

	S_ROMANIAN,
	S_WHOLETONE,

	S_HARMONICS,
	S_HEXANY,
	S_JUST,

	S_CHROMATIC,
	S_DIMINISHED,
	NUM_SCALES,
} Scale;

const static char* const scalenames[NUM_SCALES] = {
    [S_MAJOR] = "Major",
    [S_MINOR] = "Minor",
    [S_HARMMINOR] = "Harmonic Min",
    [S_PENTA] = "Penta Maj",
    [S_PENTAMINOR] = "Penta Min",
    [S_HIRAJOSHI] = "Hirajoshi",
    [S_INSEN] = "Insen",
    [S_IWATO] = "Iwato",
    [S_MINYO] = "Minyo",
    [S_FIFTHS] = "Fifths",
    [S_TRIADMAJOR] = "Triad Maj",
    [S_TRIADMINOR] = "Triad Min",
    [S_DORIAN] = "Dorian",
    [S_PHYRGIAN] = "Phrygian",
    [S_LYDIAN] = "Lydian",
    [S_MIXOLYDIAN] = "Mixolydian",
    [S_AEOLIAN] = "Aeolian",
    [S_LOCRIAN] = "Lacrian",
    [S_BLUESMINOR] = "Blues Min",
    [S_BLUESMAJOR] = "Blues Maj",
    [S_ROMANIAN] = "Romanian",
    [S_WHOLETONE] = "Wholetone",
    [S_HARMONICS] = "Harmonics",
    [S_HEXANY] = "Hexany",
    [S_JUST] = "Just",
    [S_CHROMATIC] = "Chromatic",
    [S_DIMINISHED] = "Diminished",
};

#define C (0 * 512)
#define Cs (1 * 512)
#define D (2 * 512)
#define Ds (3 * 512)
#define E (4 * 512)
#define F (5 * 512)
#define Fs (6 * 512)
#define G (7 * 512)
#define Gs (8 * 512)
#define A (9 * 512)
#define As (10 * 512)
#define B (11 * 512)

#define Es F
#define Bs C

#define Ab Gs
#define Bb As
#define Cb B
#define Db Cs
#define Eb Ds
#define Fb E
#define Gb Fs

#define CENTS(c) (((c) * 512) / 100)

#define MAX_SCALE_STEPS 12

const static u16 scale_table[NUM_SCALES][16] = {
    [S_CHROMATIC] = {12, C, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B},
    [S_MAJOR] = {7, C, D, E, F, G, A, B},
    [S_MINOR] = {7, C, D, Eb, F, G, Ab, Bb},
    [S_HARMMINOR] = {7, C, D, Ds, F, G, Gs, B},
    [S_PENTA] = {5, C, D, E, G, A},
    [S_PENTAMINOR] = {5, C, Ds, F, G, As},
    [S_HIRAJOSHI] = {5, C, D, Ds, G, Gs},
    [S_INSEN] = {5, C, Cs, F, G, As},
    [S_IWATO] = {5, C, Cs, F, Fs, As},
    [S_MINYO] = {5, C, D, F, G, A},

    [S_FIFTHS] = {2, C, G},
    [S_TRIADMAJOR] = {3, C, E, G},
    [S_TRIADMINOR] = {3, C, Eb, G},

    // these are dups of major/minor/rotations thereof, but lets throw them in anyway
    [S_DORIAN] = {7, C, D, Ds, F, G, A, As},
    [S_PHYRGIAN] = {7, C, Db, Eb, F, G, Ab, Bb},
    [S_LYDIAN] = {7, C, D, E, Fs, G, A, B},
    [S_MIXOLYDIAN] = {7, C, D, E, F, G, A, Bb},
    [S_AEOLIAN] = {7, C, D, Eb, F, G, Ab, Bb},
    [S_LOCRIAN] = {7, C, Db, Eb, F, Gb, Ab, Bb},

    [S_BLUESMAJOR] = {6, C, D, Ds, E, G, A},
    [S_BLUESMINOR] = {6, C, Ds, F, Fs, G, As},

    [S_ROMANIAN] = {7, C, D, Ds, Fs, G, A, As},
    [S_WHOLETONE] = {6, C, D, E, Fs, Gs, As},

    // microtonal stuff
    [S_HARMONICS] = {4, C, E - CENTS(14), G + CENTS(2), Bb - CENTS(31)},
    [S_HEXANY] = {5, CENTS(0), CENTS(386), CENTS(498), CENTS(702),
                  CENTS(814)}, // kinda C,E,F,G,G# but the E is quite flat

    [S_JUST] = {7, CENTS(0), CENTS(204), CENTS(386), CENTS(498), CENTS(702), CENTS(884), CENTS(1088)},
    [S_DIMINISHED] = {8, C, D, Ds, F, Fs, Gs, A, B},
};

#undef C
#undef D
#undef E
#undef F
#undef G
#undef A
#undef B
#undef Cs
#undef Ds
#undef Es
#undef Fs
#undef Gs
#undef As
#undef Bs
#undef Cb
#undef Db
#undef Eb
#undef Fb
#undef Gb
#undef Ab
#undef Bb

// Alex notes:
//
// pitch table is (64*8) steps per semitone, ie 512 per semitone
// so heres my maths, this comes out at 435
// 8887421 comes from the value of pitch when playing a C
// the pitch of middle c in plinky as written is (4.0/(65536.0*65536.0/8887421.0/31250.0f))
// which is 1.0114729530400526 too low
// which is 0.19749290999 semitones flat
// *512 = 101. so I need to add 101 to pitch_base

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