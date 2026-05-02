#include "synth.h"
#include "arp.h"
#include "audio_tools.h"
#include "gfx/gfx.h"
#include "hardware/adc_dac.h"
#include "hardware/memory.h"
#include "hardware/midi.h"
#include "hardware/spi.h"
#include "hardware/touchstrips.h"
#include "params.h"
#include "sampler.h"
#include "sequencer.h"
#include "time.h"

typedef struct Osc {
	u32 phase;
	u32 prev_sample;
	s32 phase_diff;
	u16 pitch;
} Osc;

typedef struct GrainPair {
	int fpos24;
	int pos[2];
	int vol24;
	int dvol24;
	int dpos24;
	float grate_ratio;
	float multisample_grate;
	int bufadjust; // for reverse grains, we adjust the dma buffer address by this many samples
	int outflags;
} GrainPair;

typedef struct Voice {
	// oscillator (sampler only uses the pitch value)
	Osc osc[OSCS_PER_VOICE];
	// the pitch of osc[2] with pitch modulation and portamento applied
	u16 pitch;
	// env 1
	float env1_lvl;
	bool env1_decaying;
	ValueSmoother lpg_smoother[2];
	// env 1 visuals
	float env1_peak;
	float env1_norm;
	// noise
	float noise_lvl;
	// sampler state
	GrainPair grain_pair[2];
	int playhead8;
	u8 slice_id;
	u16 touch_pos_start;
	ValueSmoother touch_pos;
} Voice;

// the pitches[1025] table is centered around approx B1: pitches[512] leads to a 61.035Hz tone
// to scale this to a true B1 = 61.735Hz / 61.035Hz = scale by 1.0115
// in semitones: 12 × log₂(1.0115) = 0.198 semis
// in pitch value: 0.198 * 512 = 101
// we add 101 to go from the center pitch value (1 << 15) to a true B1
// then we subtract 11 semitones to arrive at C1

#define BOTTOM_PAD_OCTS 2 // Octaves from C-1
#define BOTTOM_PAD_SEMIS (12 * BOTTOM_PAD_OCTS)
#define BOTTOM_PAD_PITCH SEMIS_TO_PITCH(BOTTOM_PAD_SEMIS)

// pitch value offset for pitches A4 = 430Hz through 445Hz
const static s16 ref_pitch_offset[16] = {-102, -82, -62, -41, -21, 0, 20, 40, 61, 81, 101, 121, 141, 161, 181, 201};
static u32 tuning_offset;

static Voice voices[NUM_VOICES];
static SynthString synth_string[2][NUM_STRINGS];
static Touch touch_frames[NUM_STRINGS][NUM_TOUCH_FRAMES];
static Touch touch_sorted[NUM_STRINGS][NUM_TOUCH_FRAMES];

static u8 write_frame;
static u8 play_frame;
static SynthString* write_strings = synth_string[0];
static SynthString* play_strings = synth_string[1];

static u8 phys_touch_mask = 0;
static u8 before_arp_touch_mask = 0;
static bool cv_trig_out_high = false; // should cv trigger be high?
static bool cv_gate_out_high = false; // should cv gate be high?
static u16 max_env_lvl = 0;           // highest pressure envelope seen
static u8 low_string_id = 255;        // lowest touched string
static u8 high_string_id = 255;       // highest touched string
static u8 high_string_note = 0;       // note on highest touched string

// precalc arrays for mapping steps to strings
static u8 string_oct_valid = 0;
static u8 string_scale_valid = 0;
static u8 string_start_step_valid = 0;
static u8 string_root_pitch_valid = 0;

const SynthString* get_synth_string(u8 string_id) {
	return &play_strings[string_id];
}

// === UTILS === //

// only valid if NUM_NOTES == 97 (8 octaves + 1 step)
#define MAX_VALID_SCALE_STEP(scale) (steps_in_scale[scale] << 3)

#define MIDI_TUNING_IS_ACTIVE(note_nr) (global_data.midi_tuning_active[(note_nr) >> 5] & (1u << ((note_nr) & 31)))
#define MIDI_TUNING_SET_ACTIVE(note_nr) (global_data.midi_tuning_active[(note_nr) >> 5] |= (1u << ((note_nr) & 31)))
#define USING_MIDI_TUNING(note_number) (sys_params.midi_tuning && MIDI_TUNING_IS_ACTIVE(note_number))

static u16 pitch_at_note_with_midi_tuning(u8 note_number) {
	return USING_MIDI_TUNING(note_number) ? global_data.midi_tuning_pitch[note_number] : NOTE_NR_TO_PITCH(note_number);
}

static s8 string_oct(u8 string_id) {
	static s8 oct[NUM_STRINGS] = {};
	u8 mask = 1 << string_id;
	if (!(string_oct_valid & mask)) {
		oct[string_id] = param_index_multi(MP_OCT, string_id);
		string_oct_valid |= mask;
	}
	return oct[string_id];
}

static Scale string_scale(u8 string_id) {
	static Scale scale[NUM_STRINGS] = {};
	u8 mask = 1 << string_id;
	if (!(string_scale_valid & mask)) {
		scale[string_id] = param_index_multi(MP_SCALE, string_id);
		string_scale_valid |= mask;
	}
	return scale[string_id];
}

static u16 string_root_pitch(u8 string_id) {
	static u16 root_pitch[NUM_STRINGS] = {};
	u8 mask = 1 << string_id;
	if (!(string_root_pitch_valid & mask)) {
		root_pitch[string_id] = SEMIS_TO_PITCH(param_index_multi(MP_ROOT, string_id));
		string_root_pitch_valid |= mask;
	}
	return root_pitch[string_id];
}

// absolute number of steps at the bottom-pad of string_id - includes MP_OCT, does not include MP_DEGREE
static s16 string_start_step(u8 string_id) {
	static u16 string_hash[NUM_STRINGS] = {};
	static s16 start_step[NUM_STRINGS] = {};
	// does not save octave offset, string 0 always starts at BOTTOM_PAD_SEMIS
	static u8 string_start_semis[NUM_STRINGS] = {BOTTOM_PAD_SEMIS};
	// we add the octave steps at each return statement
	s16 oct_steps = string_oct(string_id) * steps_in_scale[string_scale(string_id)];

	// string was updated this frame
	if (string_start_step_valid & (1u << string_id))
		return start_step[string_id] + oct_steps;

	// string 0 doesn't need any further mapping
	if (string_id == 0) {
		start_step[0] = steps_in_scale[string_scale(0)] * BOTTOM_PAD_OCTS;
		string_start_step_valid |= 1;
		return start_step[0] + oct_steps;
	}

	u16 new_string_hash[NUM_STRINGS];
	u8 column_param[NUM_STRINGS];
	Scale scale_param[NUM_STRINGS];
	u8 first_stale_string = 255;

	// loop downwards from our string to string 1 until we find the first up-to-date string
	for (u8 s_id = string_id; s_id >= 1; s_id--) {
		column_param[s_id] = param_index_multi(MP_COLUMN, s_id);
		scale_param[s_id] = string_scale(s_id);
		new_string_hash[s_id] = column_param[s_id] + (scale_param[s_id] << 4);
		if (new_string_hash[s_id] == string_hash[s_id])
			break;
		first_stale_string = s_id;
	}

	// no stale strings, this string doesn't need updating
	if (first_stale_string == 255) {
		string_start_step_valid |= 1 << string_id;
		return start_step[string_id] + oct_steps;
	}

	// recalculate stale strings up to string_id
	u8 old_start_semis = string_start_semis[string_id];
	for (u8 s_id = first_stale_string; s_id <= string_id; s_id++) {
		Scale scale = scale_param[s_id];
		u8 goal_semis = string_start_semis[s_id - 1] + column_param[s_id];
		u8 base_oct = goal_semis / 12;
		u16 pitch_in_oct = SEMIS_TO_PITCH(goal_semis % 12);
		u8 scale_steps = steps_in_scale[scale];
		const u16* step_pitch = scale_table[scale];

		// estimate closest step by linear mapping
		u8 closest_step = pitch_in_oct * scale_steps / PITCH_PER_OCT;
		u16 least_dist = abs(step_pitch[closest_step] - pitch_in_oct);
		bool check_higher_steps = true;

		// search downward
		while (closest_step > 0) {
			closest_step--;
			u16 dist = abs(step_pitch[closest_step] - pitch_in_oct);
			if (dist >= least_dist) {
				closest_step++;
				break;
			}
			least_dist = dist;
			// if we found a closer step downwards, we know we won't find a closer step upwards
			check_higher_steps = false;
		}

		// search upward
		if (check_higher_steps) {
			while (closest_step < scale_steps - 1) {
				closest_step++;
				u16 dist = abs(step_pitch[closest_step] - pitch_in_oct);
				if (dist >= least_dist) {
					closest_step--;
					break;
				}
				least_dist = dist;
			}
		}

		// check first note of next octave
		if (closest_step == scale_steps - 1 && PITCH_PER_OCT - pitch_in_oct < least_dist) {
			closest_step = 0;
			base_oct++;
		}

		// resync at root notes to prevent semitone error accumulation
		string_start_semis[s_id] = closest_step == 0 ? base_oct * 12 : goal_semis;

		// save results
		string_hash[s_id] = new_string_hash[s_id];
		start_step[s_id] = base_oct * scale_steps + closest_step;
		string_start_step_valid |= 1 << s_id;
	}

	// invalidate higher strings
	if (string_start_semis[string_id] != old_start_semis)
		for (u8 s_id = string_id + 1; s_id < NUM_STRINGS; s_id++)
			string_hash[s_id] = 0;

	return start_step[string_id] + oct_steps;
}

static u16 pitch_at_step_with_midi_tuning(u8 step, Scale scale, u8 steps_in_scale) {
	u8 octs = step / steps_in_scale;
	u16 pitch_in_oct = scale_table[scale][step % steps_in_scale];
	u8 note_number = 12 * octs + PITCH_TO_NOTE_NR(pitch_in_oct);
	return USING_MIDI_TUNING(note_number) ? global_data.midi_tuning_pitch[note_number]
	                                      : OCTS_TO_PITCH(octs) + pitch_in_oct;
}

static u16 pitch_at_step(u8 step, Scale scale, u8 steps_in_scale) {
	return OCTS_TO_PITCH(step / steps_in_scale) + scale_table[scale][step % steps_in_scale];
}

static u16 quant_pitch_to_scale_with_step(u16 pitch, u8 string_id, u8* out_step) {
	Scale scale = string_scale(string_id);
	u8 scale_steps = steps_in_scale[scale];

	// adjust for root note
	u16 root_pitch_offset = -string_root_pitch(string_id) + PITCH_PER_OCT;
	pitch += root_pitch_offset;

	// estimate closest by linear mapping
	u8 step = pitch * scale_steps / PITCH_PER_OCT;
	u16 best_distance = abs(pitch - pitch_at_step(step, scale, scale_steps));

	// find step in scale closest to pitch
	if (pitch - pitch_at_step(step, scale, scale_steps) > 0) {
		// search up
		while (true) {
			step++;
			u16 step_pitch = pitch_at_step(step, scale, scale_steps);
			if (step_pitch > MAX_PITCH + root_pitch_offset)
				break;
			u16 distance = abs(pitch - step_pitch);
			if (distance >= best_distance)
				break;
			best_distance = distance;
		}
		step--;
	}
	else if (pitch - pitch_at_step(step, scale, scale_steps) < 0) {
		// search down
		while (true) {
			step--;
			s32 step_pitch = pitch_at_step(step, scale, scale_steps);
			if (step_pitch < root_pitch_offset)
				break;
			u16 distance = abs(pitch - step_pitch);
			if (distance >= best_distance)
				break;
			best_distance = distance;
		}
		step++;
	}

	// return found step
	*out_step = step;

	// adjust back for root note
	return pitch_at_step(step, scale, scale_steps) - root_pitch_offset;
}

u16 quant_pitch_to_scale(u16 pitch, u8 string_id) {
	u8 dummy;
	return quant_pitch_to_scale_with_step(pitch, string_id, &dummy);
}

static u16 string_center_pitch(u8 string_id) {
	Scale scale = string_scale(string_id);
	u8 scale_steps = steps_in_scale[scale];
	s16 step = string_start_step(string_id) + 3;
	// get octave
	u8 oct = step / scale_steps;
	step %= scale_steps;
	// get pitch-in-octave of pad 3
	u16 pitch3 = scale_table[scale][step];
	// get pitch-in-octave of pad 4
	step = (step + 1) % scale_steps;
	u16 pitch4 = scale_table[scale][step];
	if (pitch4 < pitch3)
		pitch4 += PITCH_PER_OCT;
	// return average
	return OCTS_TO_PITCH(oct) + string_root_pitch(string_id) + ((pitch3 + pitch4) >> 1);
}

u8 find_string_for_pitch(u16 pitch, u8 from_string, u8 to_string) {
	// find desired string: center pitch closest to pitch
	u8 desired_string = from_string;
	u16 prev_pitch_dist = UINT16_MAX;
	for (u8 string_id = from_string; string_id < to_string; string_id++) {
		u16 pitch_dist = abs(string_center_pitch(string_id) - pitch);
		if (pitch_dist >= prev_pitch_dist)
			break;
		prev_pitch_dist = pitch_dist;
		desired_string = string_id;
	}

	// find closest non-sounding string
	u8 best_string = 255;
	u8 prev_dist = 255;
	for (u8 string_id = from_string; string_id < to_string; string_id++) {
		u8 dist = abs(string_id - desired_string);
		if (dist >= prev_dist)
			break;
		// log non-sounding strings
		if (!play_strings[string_id].touched && voices[string_id].env1_lvl < 0.001f)
			best_string = string_id;
		prev_dist = dist;
	}

	// return found string, if any
	if (best_string != 255)
		return best_string;

	// find quietest non-touched string
	float min_vol = __FLT_MAX__;
	for (u8 string_id = from_string; string_id < to_string; string_id++) {
		float vol = voices[string_id].env1_lvl;
		if (!play_strings[string_id].touched && vol < min_vol) {
			min_vol = vol;
			best_string = string_id;
		}
	}

	// returns 255 if no non-pressed strings were found
	return best_string;
}

u16 string_position_from_pitch(u8 string_id, u16 pitch) {
	Scale scale = string_scale(string_id);
	u8 scale_steps = steps_in_scale[scale];
	s16 start_step = string_start_step(string_id);
	u16 octs_pitch = OCTS_TO_PITCH(start_step / scale_steps);
	u8 step_in_oct = start_step % scale_steps;
	u16 root_pitch_offset = string_root_pitch(string_id);

	u8 pad = 0;
	while (pad < (PADS_PER_STRIP - 1) && octs_pitch + root_pitch_offset + scale_table[scale][step_in_oct] < pitch) {
		pad++;
		step_in_oct++;

		// rollover
		if (step_in_oct == scale_steps) {
			step_in_oct = 0;
			octs_pitch += PITCH_PER_OCT;
		}
	}

	return (7 - pad) << 8;
}

void clear_latch(void) {
	for (u8 string_id = 0; string_id < NUM_STRINGS; string_id++) {
		write_strings[string_id].latch_touch = (LatchTouch){};
		play_strings[string_id].latch_touch = (LatchTouch){};
	}
}

void clear_synth_string(u8 string_id) {
	write_strings[string_id] = (SynthString){.touch = init_touch};
	play_strings[string_id] = (SynthString){.touch = init_touch};
	Touch* frames = touch_frames[string_id];
	Touch* sorted = touch_sorted[string_id];
	for (u8 frame = 0; frame < NUM_TOUCH_FRAMES; frame++) {
		frames[frame] = init_touch;
		sorted[frame] = init_touch;
	}
}

void clear_synth_strings(void) {
	for (u8 string_id = 0; string_id < NUM_STRINGS; string_id++)
		clear_synth_string(string_id);
}

void set_note_tuning(u8 note_number, u16 pitch) {
	if (note_number < NUM_NOTES && pitch <= MAX_PITCH) {
		MIDI_TUNING_SET_ACTIVE(note_number);
		global_data.midi_tuning_pitch[note_number] = pitch;
		log_ram_edit(SEG_GLOBAL_DATA);
	}
}

void clear_midi_tuning(void) {
	memset(&global_data.midi_tuning_pitch, 0,
	       sizeof(global_data.midi_tuning_pitch) + sizeof(global_data.midi_tuning_active)
	           + sizeof(global_data.midi_tuning_name));
	log_ram_edit(SEG_GLOBAL_DATA);
}

void update_reference_pitch(void) {
	// offset from bottom of the sound engine (G-4) to the first valid note (C-1) is 29 semis
	tuning_offset = SEMIS_TO_PITCH(29) + ref_pitch_offset[sys_params.reference_pitch];
}

// === UNORGANIZED SAMPLER CODE === //

#define MAX_SAMPLE_VOICES 6
#define AVG_GRAINBUF_SAMPLE_SIZE (64 + 4) // 2 extra for interpolation, 2 extra for SPI address at the start
#define GRAINBUF_BUDGET (AVG_GRAINBUF_SAMPLE_SIZE * NUM_GRAINS)

// static float smooth_lpg(ValueSmoother* s, s32 out, float drive, float noise, float env1_lvl) {
// 	s16 n = ((s16*)rndtab)[rand() & 16383];
// 	float cutoff = 1.f - squaref(maxf(0.f, 1.f - env1_lvl * 1.1f));
// 	s->y1 += (out * drive + n * noise - s->y1) * cutoff;
// 	return s->y1;
// }

// start of current (slice) loop - u8 slice_id -> int
#define CALCLOOPSTART(slice_id) ((cur_sample_info.loop & 2) ? 0 : cur_sample_info.splitpoints[slice_id])

// end of current (slice) loop - u8 slice_id -> int
#define CALCLOOPEND(slice_id)                                                                                          \
	((cur_sample_info.loop & 2 || (slice_id) >= 7) ? cur_sample_info.samplelen - 192                                   \
	                                               : cur_sample_info.splitpoints[(slice_id) + 1])

static s16 grain_buf[GRAINBUF_BUDGET];
static s32 grain_pos[NUM_GRAINS];
static s16 grain_buf_end[NUM_GRAINS]; // for each of the 32 grain fetches, where does it end in the grain_buf?

// getters for spi
s16* grain_buf_ptr(void) {
	return grain_buf;
}
u32 grain_address(u8 grain_id) {
	return grain_pos[grain_id] * 2;
}
s16 grain_buf_end_get(u8 grain_id) {
	return grain_buf_end[grain_id];
}

static void apply_sample_lpg_noise(u8 voice_id, Voice* voice, float goal_lpg, float noise_diff, float drive, u32* dst) {
	// sampler parameters
	float timestretch = 1.f;
	float posjit = 0.f;
	float sizejit = 1.f;
	float gsize = 0.125f;
	float grate = 1.f;
	float gratejit = 0.f;
	int smppos = 0;
	if (ui_mode != UI_SAMPLE_EDIT) {
		timestretch = param_val_multi(MP_SMP_STRETCH, voice_id) * (2.f / 65536.f);
		gsize = param_val_multi(MP_GR_SIZE, voice_id) * (1.414f / 65536.f);
		grate = param_val_multi(MP_PLAY_SPD, voice_id) * (2.f / 65536.f);
		smppos = (param_val_multi(MP_SCRUB, voice_id) * cur_sample_info.samplelen) >> 16;
		posjit = param_val_multi(MP_SCRUB_JIT, voice_id) * (1.f / 65536.f);
		sizejit = param_val_multi(MP_GR_SIZE_JIT, voice_id) * (1.f / 65536.f);
		gratejit = param_val_multi(MP_PLAY_SPD_JIT, voice_id) * (1.f / 65536.f);
	}
	const SynthString* s_string = &play_strings[voice_id];
	int trig = s_string->env_trigger;

	int prevsliceidx = voice->slice_id;
	bool gp = ui_mode == UI_SAMPLE_EDIT;
	u16 touch_pos = s_string->touch.pos;

	// decide on the sample for the NEXT frame
	if (trig) { // on trigger frames, we FADE out the old grains! then the next dma fetch will be the new sample and
		// we can fade in again
		goal_lpg = 0.f;
		//		DebugLog("\r\n%d", voice_id);
		int ypos = 0;
		if (cur_sample_info.pitched && !gp) {
			/// / / / ////////////////////// multisample choice
			int best = voice_id;
			int bestdist = 0x7fffffff;
			int mypitch = (voice->osc[1].pitch + voice->osc[2].pitch) / 2 + BOTTOM_PAD_PITCH;
			int mysemi = (mypitch) >> 9;
			static u8 multisampletime[8];
			static u8 trig_count = 0;
			trig_count++;
			for (int i = 0; i < 8; ++i) {
				int dist = abs(mysemi - cur_sample_info.notes[i]) * 256 - (u8)(trig_count - multisampletime[i]);
				if (dist < bestdist) {
					bestdist = dist;
					best = i;
				}
			}
			multisampletime[best] = trig_count; // for round robin
			voice->slice_id = best;
			if (grate < 0.f)
				ypos = 8;
		}
		else {
			voice->slice_id = voice_id;
			ypos = (touch_pos / 256);
			if (gp)
				ypos = 0;
			if (grate < 0.f)
				ypos++;
		}
		voice->touch_pos_start = gp ? 128 : touch_pos;
		// calculate playhead position
		int pos16 = clampi(((voice->slice_id * 8) + ypos) << 10, 0, 65535);
		int i = pos16 >> 13;
		int p0 = cur_sample_info.splitpoints[i];
		int p1 = cur_sample_info.splitpoints[i + 1];
		voice->playhead8 = (p0 << 8) + (((p1 - p0) * (pos16 & 0x1fff)) >> 5);
		if (grate < 0.f) {
			voice->playhead8 -= 192 << 8;
			if (voice->playhead8 < 0)
				voice->playhead8 = 0;
		}
		set_smoother(&voice->touch_pos, 0);
	}
	else { // not trigger - just advance playhead
		float ms2 = (voice->grain_pair[0].multisample_grate
		             + voice->grain_pair[1].multisample_grate); // double multisample rate
		int delta_playhead8 = (int)(grate * ms2 * timestretch * (SAMPLES_PER_TICK * 0.5f * 256.f) + 0.5f);

		int new_playhead = voice->playhead8 + delta_playhead8;

		// if the sample loops and the new playhead has crossed the loop boundary, recalculate new playhead position
		if (cur_sample_info.loop & 1) {
			int loopstart = CALCLOOPSTART(voice->slice_id) << 8;
			int loopend = CALCLOOPEND(voice->slice_id) << 8;
			int looplen = loopend - loopstart;
			if (looplen > 0 && (new_playhead < loopstart || new_playhead >= loopstart + looplen)) {
				new_playhead = (new_playhead - loopstart) % looplen;
				if (new_playhead < 0)
					new_playhead += looplen;
				new_playhead += loopstart;
			}
		}

		voice->playhead8 = new_playhead;

		float gdeadzone = clampf(minf(1.f - posjit, timestretch * 2.f), 0.f,
		                         1.f); // if playing back normally and not jittering, add a deadzone
		float fpos = deadzone(touch_pos - voice->touch_pos_start, gdeadzone * 32.f);
		if (gp)
			fpos = 0.f;
		smooth_value(&voice->touch_pos, fpos, 2048.f);
	}

	float noise;
	for (int osc_id = 0; osc_id < OSCS_PER_VOICE / 2; osc_id++) {
		s16* osc_dst = ((s16*)dst) + (osc_id & 1);
		noise = voice->noise_lvl;
		float y1 = voice->lpg_smoother[osc_id].y1;
		float y2 = voice->lpg_smoother[osc_id].y2;
		int randtabpos = rand() & 16383;
		// mix grains
		GrainPair* g = &voice->grain_pair[osc_id];
		int grainidx = voice_id * 4 + osc_id * 2;
		int g0start = 0;
		if (grainidx)
			g0start = grain_buf_end[grainidx - 1];
		int g1start = grain_buf_end[grainidx];
		int g2start = grain_buf_end[grainidx + 1];

		int64_t posa = g->pos[0];
		int64_t posb = g->pos[1];
		int loopstart = CALCLOOPSTART(prevsliceidx);
		int loopend = CALCLOOPEND(prevsliceidx);
		bool outofrange0 = posa < loopstart || posa >= loopend;
		bool outofrange1 = posb < loopstart || posb >= loopend;
		int gvol24 = g->vol24;
		int dgvol24 = g->dvol24;
		int dpos24 = g->dpos24;
		int fpos24 = g->fpos24;
		float vol = voice->env1_lvl;
		float dvol = (goal_lpg - vol) * (1.f / SAMPLES_PER_TICK);
		outofrange0 |= g1start - g0start <= 2;
		outofrange1 |= g2start - g1start <= 2;
		g->outflags = (outofrange0 ? 1 : 0) + (outofrange1 ? 2 : 0);
		if ((g1start - g0start <= 2 && g2start - g1start <= 2)) {
			// fast mode :) emulate side effects without doing any work
			vol += dvol * SAMPLES_PER_TICK;
			noise += noise_diff * SAMPLES_PER_TICK;
			gvol24 -= dgvol24 * SAMPLES_PER_TICK;
			fpos24 += dpos24 * SAMPLES_PER_TICK;
			int id = fpos24 >> 24;
			g->pos[0] += id;
			g->pos[1] += id;
			fpos24 &= 0xffffff;
		}
		else {
			const s16* src0 = (outofrange0 ? (const s16*)zero : &grain_buf[g0start + 2]) + g->bufadjust;
			const s16* src0_backup = src0;
			const s16* src1 = (outofrange1 ? (const s16*)zero : &grain_buf[g1start + 2]) + g->bufadjust;

			spi_ready_for_sampler(grainidx);

			for (int i = 0; i < SAMPLES_PER_TICK; ++i) {
				int o0, o1;
				u32 ab0 = *(u32*)(src0); // fetch a pair of 16 bit samples to interpolate between
				u32 mix = (fpos24 << (16 - 9)) & 0x7fff0000;
				mix |= 32767 - (mix >> 16); // mix is now the weights for the linear interpolation
				SMUAD(o0, ab0, mix);        // do the interpolation, result is *32768
				o0 >>= 16;

				u32 ab1 = *(u32*)(src1); // fetch a pair for the other grain in the pair
				SMUAD(o1, ab1, mix);     // linear interp by same weights
				o1 >>= 16;

				fpos24 += dpos24; // advance fractional sample pos
				int bigdpos = (fpos24 >> 24);
				fpos24 &= 0xffffff;
				src0 += bigdpos; // advance source pointers by any whole sample increment
				src1 += bigdpos;

				mix = (gvol24 >> 9) & 0x7fff; // blend between the two grain results
				mix |= (32767 - mix) << 16;
				u32 o01 = STEREOPACK(o0, o1);
				int ofinal;
				SMUAD(ofinal, o01, mix);
				gvol24 -= dgvol24;
				if (gvol24 < 0)
					gvol24 = 0;

				s16 n = ((s16*)rndtab)[randtabpos++]; // mix in a white noise source
				noise += noise_diff;                  // volume ramp for noise

				vol += dvol;                                               // volume ramp for grain signal
				float input = (ofinal * drive + n * noise);                // input to filter
				float cutoff = 1.f - squaref(maxf(0.f, 1.f - vol * 1.1f)); // filter cutoff for low pass gate
				y1 += (input - y1) * cutoff;                               // do the lowpass

				int yy = FLOAT2FIXED(y1 * vol, 0);    // for granular, we include an element of straight VCA
				*osc_dst = SATURATE16(*osc_dst + yy); // write to output
				osc_dst += 2;
			}
			int bigposdelta = src0 - src0_backup;
			g->pos[0] += bigposdelta;
			g->pos[1] += bigposdelta;
		} // grain mix
		g->fpos24 = fpos24;
		g->vol24 = gvol24;

		if (gvol24 <= dgvol24 || trig) { // new grain trigger! this is for the *next* frame
			int ph = voice->playhead8 >> 8;
			int slicelen =
			    cur_sample_info.splitpoints[voice->slice_id + 1] - cur_sample_info.splitpoints[voice->slice_id];
			if (ui_mode != UI_SAMPLE_EDIT) {
				ph += ((int)(voice->touch_pos.y2 * slicelen)) >> (10);
				ph += smppos; // scrub input
			}
			g->vol24 = ((1 << 24) - 1);
			int grainsize = ((rand() & 127) * sizejit + 128.f) * (gsize * gsize) + 0.5f;
			grainsize *= SAMPLES_PER_TICK;
			int jitpos = (rand() & 255) * posjit;
			ph += ((grainsize + 8192) * jitpos) >> 8;
			g->dvol24 = g->vol24 / grainsize;

			float grate2 = 1.f + ((rand() & 255) * (gratejit * gratejit)) * (1.f / 256.f);
			if (timestretch < 0.f)
				grate2 = -grate2;
			g->grate_ratio = grate2;
			g->pos[0] = trig ? ph : g->pos[1];
			g->pos[1] = ph;
		}
		voice->lpg_smoother[osc_id].y1 = y1;
		voice->lpg_smoother[osc_id].y2 = y2;

		// save voice pitch
		if (osc_id == 0)
			voice->pitch = voice->osc[2].pitch;
	} // osc loop

	voice->env1_lvl = goal_lpg;
	voice->noise_lvl = noise;

	// update pitch (aka dpos24) for next time!
	for (int gi = 0; gi < 2; ++gi) {
		float multisample_grate;
		if (cur_sample_info.pitched && (ui_mode != UI_SAMPLE_EDIT)) {
			int relpitch = voice->osc[1 + gi].pitch + BOTTOM_PAD_PITCH - cur_sample_info.notes[voice->slice_id] * 512;
			if (relpitch < -512 * 12 * 5) {
				multisample_grate = 0.f;
			}
			else {
				multisample_grate = // exp2f(relpitch / (512.f * 12.f));
				    table_interp(pitches, relpitch + 32768);
			}
		}
		else {
			multisample_grate = 1.f;
		}
		voice->grain_pair[gi].multisample_grate = multisample_grate;
		int dpos24 = (1 << 24) * (grate * voice->grain_pair[gi].grate_ratio * multisample_grate);
		while (dpos24 > (2 << 24))
			dpos24 >>= 1;
		voice->grain_pair[gi].dpos24 = dpos24;
	}
}

// === END OF UNORGANIZED SAMPLER CODE === //

// === MAIN === //

void init_synth(void) {
	update_reference_pitch();
}

// this combines inputs from touchstrips, midi, cv, latch, arp & sequencer and saves the resulting Touch in
// string_touch[string_id]
static void generate_string_touch(u8 string_id) {
	static bool suppress_latch = false;
	static bool prev_latch[NUM_STRINGS];
	SynthString* s_string = &write_strings[string_id];

	bool has_latch = latch_active_on_string(string_id);
	// clear latch on latch disable
	if (prev_latch[string_id] && !has_latch) {
		write_strings[string_id].latch_touch = (LatchTouch){};
		play_strings[string_id].latch_touch = (LatchTouch){};
	}
	prev_latch[string_id] = has_latch;
	LatchTouch* s_latch = &s_string->latch_touch;

	u8 mask = 1 << string_id;
	bool pres_increasing = false;
	// the touch we're processing
	const Touch* touch = get_touch(string_id, 0);
	// we save the resulting touch in here until we write it to s_string
	s16 pressure = TOUCH_MIN_PRES;
	s16 position = TOUCH_MIN_POS;

	// === TOUCH INPUT === //

	// can we read touch input from this string?
	if (
	    // we have local control
	    !sys_params.local_ctrl_off
	    // default ui, exception for the edit-strips
	    && ((ui_mode == UI_DEFAULT && !editing_multi_param() && !(string_id == 0 && editing_param()))
	        // sampler in preview mode when a sample is loaded
	        || (ui_mode == UI_SAMPLE_EDIT && sampler_mode == SM_PREVIEW && USING_SAMPLER))) {
		bool touching = get_strip_touched() & mask;
		bool first_touch_global = false;

		u8 pad_id = 7 - (touch->pos >> 8);

		// disable touches outside valid range
		if (touching) {
			s16 step = string_start_step(string_id) + pad_id;
			if (step < 0 || step > MAX_VALID_SCALE_STEP(string_scale(string_id)))
				touching = false;
		}

		if (touching) {
			// new touch while nothing was touched
			if (!phys_touch_mask)
				first_touch_global = true;
			phys_touch_mask |= mask;
			pressure = touch->pres;
			position = touch->pos;
			pres_increasing = pressure > get_touch(string_id, 1)->pres;
		}
		else
			phys_touch_mask &= ~mask;

		// === LATCH WRITE === //

		if (has_latch && touching && pres_increasing) {
			if (first_touch_global) {
				// start a new latch
				clear_latch();

				// in step record mode, trying to start a new latch temporarily turns off latching
				// trying to start a new latch outside of step record mode turns it on again
				suppress_latch = seq_state() == SEQ_STEP_RECORDING;
			}
			// save latch values
			if (!suppress_latch)
				fill_latch(string_id, s_latch);
		}
	}

	if (has_latch && s_latch->min.pres > 0) {
		// randomize 25% variation around the center position
		s16 avg_pres = (s_latch->min.pres + s_latch->max.pres) / 2;
		s16 pres_range = (s_latch->max.pres - s_latch->min.pres) / 4;
		s16 latch_pres = random_in_range(avg_pres - pres_range, avg_pres + pres_range);
		// use latch if it's larger than the pressure we're holding
		if (latch_pres > pressure) {
			pressure = latch_pres;
			u16 avg_pos = (s_latch->min.pos + s_latch->max.pos) >> 1;
			u16 pos_range = (s_latch->max.pos - s_latch->min.pos) >> 2;
			position = random_in_range(avg_pos - pos_range, avg_pos + pos_range);
			pres_increasing = true;
		}
	}

	// record touch to sequencer
	seq_try_rec_touch(string_id, pressure, position, pres_increasing);

	// retrieve touch from sequencer
	if (pressure <= 0)
		seq_try_get_touch(string_id, &pressure, &position);

	// any available pressure from touch/latch/sequencer overrides midi/cv
	if (pressure > 0)
		s_string->touch_type = PHYS_TOUCH;
	// retrieve touch from cv
	else if (cv_try_get_touch(string_id, &pressure, &position, &s_string->note_number, &s_string->note_offset_pitch,
	                          &s_string->start_velocity))
		s_string->touch_type = CV_TOUCH;
	// retrieve touch from midi
	else if (midi_try_get_touch(string_id, &pressure, &position, &s_string->note_number, &s_string->note_offset_pitch,
	                            &s_string->start_velocity))
		s_string->touch_type = MIDI_TOUCH;

	// === FINISHING UP === //

	Touch* s_touch_frames = touch_frames[string_id];
	Touch prev_touch = s_touch_frames[(write_frame + 7) & 7];

	// no pressure => retain previous frame's position
	if (pressure <= 0)
		position = prev_touch.pos;
	// new finger touch => fill (non-pressed) history with slightly randomized variant of current position
	else if (prev_touch.pres <= 0) {
		Touch* s_touch = s_touch_frames;
		for (u8 frame = 0; frame < NUM_TOUCH_FRAMES; frame++, s_touch++)
			if (frame != write_frame && s_touch->pres <= 0)
				s_touch->pos = position ^ (s_touch->pos & 3);
	}

	// save results to the synth string
	s_touch_frames[write_frame].pres = pressure;
	s_touch_frames[write_frame].pos = position;

	// sort touch frames by position
	sort8((int*)touch_sorted[string_id], (int*)s_touch_frames);
}

// manage generating the string_touch array
void generate_string_touches(void) {
	static u8 no_arp_touch_mask_1back = 0;
	static bool do_second_half = false;
	static u8 phys_string_touch_1back = 0;

	// update half of the strings (0 - 3 / 4 - 7)
	for (u8 string_id = 0; string_id < NUM_STRINGS / 2; ++string_id)
		generate_string_touch(string_id + do_second_half * NUM_STRINGS / 2);
	// end of a full update of all strings
	if (do_second_half) {
		// lift the last finger in step record mode: auto-step forward
		if (seq_state() == SEQ_STEP_RECORDING && !phys_touch_mask && phys_string_touch_1back)
			seq_inc_step();
		phys_string_touch_1back = phys_touch_mask;

		// processing the strings happens at a significantly higher framerate than reading out the touchstrips
		// u8 touch_frame tracks which frame in the touches array is currently being written to
		// u8 strings_write_frame tracks which frame in the string_touch array we are writing to
		// we never want any half-filled frames in the string_touch array, so we only update strings_write_frame after
		// processing a full frame of strings

		// in practice, all internal functionality (arp, sequencer, mod sources, etc) get processed at the higher
		// framerate - whenever touchstrips.h has read out a full frame of touches, strings_write_frame increments to
		// make use of the new touch-data

		if (write_frame != get_touch_frame()) {
			// we read from the frame that was written just before
			play_frame = write_frame;
			// we write to the frame that is currently being processed by the touchstrips
			write_frame = get_touch_frame();
			// update synth_string buffer pointers
			write_strings = synth_string[write_frame & 1];
			play_strings = synth_string[play_frame & 1];
			arp_next_strings_frame_trig();
		}
	}

	// toggle which half we process
	do_second_half = !do_second_half;

	// -- writing data do the synth strings is now complete, we continue with doing some precalc
	// for the strings that will be playing this frame -- //

	// calculate string touches
	no_arp_touch_mask_1back = before_arp_touch_mask;
	before_arp_touch_mask = 0;
	for (u8 string_id = 0; string_id < NUM_STRINGS; ++string_id)
		if (touch_frames[string_id][play_frame].pres > 0)
			before_arp_touch_mask |= 1 << string_id;
	u8 envelope_trigger = before_arp_touch_mask & ~no_arp_touch_mask_1back;

	// new (physical or virtual) touch: restart arp
	if (arp_active_mask() && before_arp_touch_mask && !no_arp_touch_mask_1back) {
		arp_reset();
		// if the sequencer is not playing, reset the clock so the arp gets a trigger
		if (!seq_playing())
			clock_reset();
	}

	// generate final touch mask
	u8 touch_mask = before_arp_touch_mask;
	// replace touch_mask by arp touches, update envelope triggers
	if (arp_active_mask())
		arp_tick(before_arp_touch_mask, &touch_mask, &envelope_trigger);

	// precalc and populate strings for this frame
	for (u8 string_id = 0; string_id < NUM_STRINGS; string_id++) {
		SynthString* s_string = &play_strings[string_id];
		Touch* cur_touch = &s_string->touch;
		u8 mask = 1 << string_id;

		// basic properties
		s_string->touched = !!(touch_mask & mask);
		s_string->env_trigger = !!(envelope_trigger & mask);
		// save frame touch to current touch
		*cur_touch = touch_frames[string_id][play_frame];
		// clear pressure if not touched
		if (!s_string->touched)
			cur_touch->pres = TOUCH_MIN_PRES;
		// generate start velocity for physical touches - needs to be saved in both buffers as this only generates on
		// env_trigger
		if (s_string->touch_type == PHYS_TOUCH && s_string->env_trigger) {
			u8 start_velocity = clampi((cur_touch->pres << 3) / sys_params.midi_out_vel_balance - 1, 0, 127);
			s_string->start_velocity = start_velocity;
			write_strings[string_id].start_velocity = start_velocity;
		}
	}
}

static void apply_synth_lpg_noise(u8 voice_id, Voice* voice, float goal_lpg, float noise_diff, float drive,
                                  float resonance, u32* dst) {
	// two loops handling two oscillators each
	u32 glide_param = param_val_multi(MP_GLIDE, voice_id);
	float glide = lpf_k(glide_param >> 2) * (0.5f / SAMPLES_PER_TICK);
	s32 osc_shape = param_val_multi(MP_SHAPE, voice_id);
	float noise;
	for (u8 osc_id = 0; osc_id < OSCS_PER_VOICE / 2; osc_id++) {
		s16* osc_dst = ((s16*)dst) + (osc_id & 1);
		noise = voice->noise_lvl;
		int rand_table_pos = rand() & 16383;
		float osc_lpg = voice->env1_lvl;
		float osc_lpg_diff = (goal_lpg - osc_lpg) * (1.f / SAMPLES_PER_TICK);

		Osc* osc = &voice->osc[osc_id];
		s32 goal_phase_diff1 = table_interp(pitches, osc[0].pitch + tuning_offset) * (65536.f * 128.f);
		s32 goal_phase_diff2 = table_interp(pitches, osc[2].pitch + tuning_offset) * (65536.f * 128.f);

		u32 flippity = 0;
		if (osc_shape != 0) {
			flippity = ~0;
			u32 avg_phase_diff = (osc[0].phase_diff + osc[2].phase_diff) / 2;
			osc[0].phase_diff = avg_phase_diff;
			osc[2].phase_diff = avg_phase_diff;
			avg_phase_diff = (goal_phase_diff1 + goal_phase_diff2) / 2;
			goal_phase_diff1 = avg_phase_diff;
			goal_phase_diff2 = avg_phase_diff;
			if (osc_shape < 0) {
				s32 phase0_fix =
				    (s32)(osc[2].phase - osc[0].phase - (osc_shape << 16) + (1 << 31)) / (SAMPLES_PER_TICK);
				osc[0].phase_diff += phase0_fix;
				goal_phase_diff1 += phase0_fix;
			}
		}
		// remove unwanted pitch glides if voice is quiet
		if (glide_param == 0 && osc_lpg < 0.001f) {
			osc[0].phase_diff = goal_phase_diff1;
			osc[2].phase_diff = goal_phase_diff2;
		}
		int dd_phase1 = (int)((goal_phase_diff1 - osc->phase_diff) * glide);
		u32 phase1 = osc->phase;
		s32 phase1_diff = osc->phase_diff;
		u32 prev_sample1 = osc->prev_sample;
		osc += 2;
		int dd_phase2 = (int)((goal_phase_diff2 - osc->phase_diff) * glide);
		u32 phase2 = osc->phase;
		s32 phase2_diff = osc->phase_diff;
		u32 prev_sample2 = osc->prev_sample;
		osc -= 2;

		float y1 = voice->lpg_smoother[osc_id].y1;
		float y2 = voice->lpg_smoother[osc_id].y2;

		// == WAVETABLE == //
		if (osc_shape > 0) {
			s32 shift1 = 16 - clz(maxi(phase1_diff, 1 << 22));
			s32 sub_wave = (osc_shape & 4095) << 1;
			sub_wave = sub_wave | ((8191 - sub_wave) << 16);
			u8 table_id = osc_shape >> 12;
			const s16* table1 = wavetable[table_id] + wavetable_octave_offset[shift1];
			for (u8 i = 0; i < SAMPLES_PER_TICK; ++i) {
				u32 i1;
				u32 i2;
				s32 s0;
				s32 s1;
				phase1_diff += dd_phase1;
				i1 = (phase1 += phase1_diff) >> shift1;
				i2 = i1 >> 16;
				s0 = table1[i2];
				s1 = table1[i2 + 1];
				s32 out0 = (s0 << 16) + ((s1 - s0) * (u16)(i1));
				i2 += WAVETABLE_SIZE;
				s0 = table1[i2];
				s1 = table1[i2 + 1];
				s32 out1 = (s0 << 16) + ((s1 - s0) * (u16)(i1));
				u32 packed = STEREOPACK(out1 >> 16, out0 >> 16);
				s32 out;
				SMUAD(out, packed, sub_wave);
				//////////////////////////////////////////////////
				// rest is same as polyblep
				s16 n = ((s16*)rndtab)[rand_table_pos++];
				noise += noise_diff;

				osc_lpg += osc_lpg_diff;
				y1 += (out * drive + n * noise - (y2 - y1) * resonance - y1) * osc_lpg; // drive
				y1 *= 0.999f;
				y2 += (y1 - y2) * osc_lpg;
				y2 *= 0.999f;

				s32 smooth_lpg = FLOAT2FIXED(y2, 0);
				*osc_dst = SATURATE16(*osc_dst + smooth_lpg);
				osc_dst += 2;
			}
		}

		// == SUPERSAW & PULSE WAVE == //

		else {
			for (u8 i = 0; i < SAMPLES_PER_TICK; ++i) {
				phase1_diff += dd_phase1;
				phase1 += phase1_diff;
				u32 newsample1 = phase1;
				if (unlikely(phase1 < (u32)phase1_diff)) {
					// edge! polyblep it.
					u32 fractime = mini(65535, phase1 / (phase1_diff >> 16));
					prev_sample1 -= (fractime * fractime) >> 1;
					fractime = 65535 - fractime;
					newsample1 += (fractime * fractime) >> 1;
				}
				s32 out = (s32)(prev_sample1 >> 4);
				prev_sample1 = newsample1;
				phase2_diff += dd_phase2;
				phase2 += phase2_diff;
				u32 newsample2 = phase2;
				if (unlikely(phase2 < (u32)phase2_diff)) {
					// edge! polyblep it.
					u32 fractime = mini(65535, phase2 / (phase2_diff >> 16));
					prev_sample2 -= (fractime * fractime) >> 1;
					fractime = 65535 - fractime;
					newsample2 += (fractime * fractime) >> 1;
				}
				out += (s32)((prev_sample2 ^ flippity) >> 4) - (2 << (31 - 4));
				prev_sample2 = newsample2;

				s16 n = ((s16*)rndtab)[rand_table_pos++];
				noise += noise_diff;

				osc_lpg += osc_lpg_diff;
				y1 += (out * drive + n * noise - (y2 - y1) * resonance - y1) * osc_lpg; // drive
				y1 *= 0.999f;
				y2 += (y1 - y2) * osc_lpg;
				y2 *= 0.999f;

				s32 smooth_lpg = FLOAT2FIXED(y2, 0);
				*osc_dst = SATURATE16(*osc_dst + smooth_lpg);
				osc_dst += 2;
			} // samples
		}
		osc[0].phase = phase1;
		osc[0].phase_diff = phase1_diff;
		osc[0].prev_sample = prev_sample1;

		osc[2].phase = phase2;
		osc[2].phase_diff = phase2_diff;
		osc[2].prev_sample = prev_sample2;

		// save exact pitch including pitch modulation and portamento
		if (osc_id == 0)
			voice->pitch = log2f((float)phase2_diff * SAMPLE_RATE / (BASE_FREQUENCY * ((u64)1 << 32))) * PITCH_PER_OCT;

		voice->lpg_smoother[osc_id].y1 = y1;
		voice->lpg_smoother[osc_id].y2 = y2;
	} // osc loop

	voice->env1_lvl = goal_lpg;
	voice->noise_lvl = noise;
}

static void run_voice(u8 voice_id, u32* dst) {
	// the synth string we read data from
	SynthString* s_string = &play_strings[voice_id];
	// the voice we write the resulting data to
	Voice* voice = &voices[voice_id];
	s16 pressure = s_string->touch.pres;
	float env_lvl = voice->env1_lvl;
	bool voice_audible = env_lvl > 0.001f;

	// turn off external touch if it has rung out
	if (s_string->touch_type != PHYS_TOUCH && !s_string->touched && !voice_audible)
		s_string->touch_type = PHYS_TOUCH;

	// generate cv gate
	if (s_string->touched)
		cv_gate_out_high = true;

	// == GENERATE OSCILLATOR PITCHES == //

	if (s_string->touched || voice_audible) {
		// precalc some parameters
		s32 pitch_param_pitch = PARAM_VAL_TO_PITCH(param_val_multi(MP_PITCH, voice_id));
		s16 osc_interval_pitch = PARAM_VAL_TO_PITCH(param_val_multi(MP_INTERVAL, voice_id));
		u32 micro_param = param_val_multi(MP_MICROTONE, voice_id);
		TouchType touch_type = s_string->touch_type;

		// the pitch of the base note
		s32 note_pitch = 0;
		// the offset from the base note caused by touch position, pitchbend
		s32 note_offset_pitch = 0;
		// the combined pitch of the four oscillators
		s32 pitch_4x = 0;

		// these only get used by phys touch
		Touch* s_touch_sort = &touch_sorted[voice_id][2]; // we use pitches 2-5, discarding extreme values
		u16 root_pitch;
		s16 string_step_offset = 0;
		s32 arp_oct_pitch = 0;

		// for phys and cv
		Scale scale = string_scale(voice_id);
		u8 scale_steps = steps_in_scale[scale];

		switch (touch_type) {
		case PHYS_TOUCH:
			root_pitch = string_root_pitch(voice_id);
			string_step_offset = string_start_step(voice_id) + param_index_multi(MP_DEGREE, voice_id);
			arp_oct_pitch = OCTS_TO_PITCH(arp_oct_offset);
			break;
		case MIDI_TOUCH:
			note_pitch = pitch_at_note_with_midi_tuning(s_string->note_number);
			break;
		case CV_TOUCH:
			// microtune at 100% effectively turns off all quantization
			CVQuantType cv_quant = micro_param == 65536 ? CVQ_OFF : sys_params.cv_quant;
			u16 start_note_pitch = NOTE_NR_TO_PITCH(s_string->note_number);
			switch (cv_quant) {
			case CVQ_OFF:
				note_pitch = start_note_pitch;
				s_string->pitchbend_pitch = note_offset_pitch;
				break;
			case CVQ_CHROMATIC:
			case CVQ_SCALE:
				u16 cv_pitch = start_note_pitch + s_string->note_offset_pitch;
				u8 note_step = 0;
				note_pitch = cv_quant == CVQ_SCALE ? quant_pitch_to_scale_with_step(cv_pitch, voice_id, &note_step)
				                                   : ROUND_PITCH_TO_SEMIS(cv_pitch);
				bool quantized_up = note_pitch > cv_pitch;
				u16 next_step_dist;

				// To make cv pitch input work smoothly with midi tuning, we find the closest (quantized) note numbers
				// and calculate how much cv_pitch is offset from the closest as a percentage. This becomes the pitch
				// offset factor. Then we apply that factor to the actual pitches (either linear or midi tuning) to
				// smoothly interpolate between the pitches that are actually heard

				// how far are we away from the closest note?
				u16 quant_offset_dist = abs(cv_pitch - note_pitch);
				// how far is the closest note to the next-closest note?
				if (cv_quant == CVQ_SCALE) {
					u8 step_in_scale = note_step % scale_steps;
					u8 next_step_in_scale = (step_in_scale + (quantized_up ? -1 : 1) + scale_steps) % scale_steps;
					next_step_dist =
					    (scale_table[scale][next_step_in_scale] - scale_table[scale][step_in_scale] + PITCH_PER_OCT)
					    % PITCH_PER_OCT;
				}
				else
					next_step_dist = PITCH_PER_SEMI;
				// how far away are we as a u16 percentage?
				u16 pitch_offset_factor = UINT16_MAX * quant_offset_dist / next_step_dist;

				// find that same relative distance between the actual output pitches, also attenuate by micro_param
				if (pitch_offset_factor) {
					u8 note_number = PITCH_TO_NOTE_NR(note_pitch);
					u8 next_note_number = note_number + PITCH_TO_SEMIS(next_step_dist) * (quantized_up ? -1 : 1);
					u16 output_range = abs(pitch_at_note_with_midi_tuning(next_note_number)
					                       - pitch_at_note_with_midi_tuning(note_number));
					s_string->note_offset_pitch =
					    ((u64)pitch_offset_factor * output_range * micro_param >> 32) * (quantized_up ? -1 : 1);
				}

				// pitchbend derived from new exact pitch and start note
				s_string->pitchbend_pitch = note_pitch + s_string->note_offset_pitch - start_note_pitch;
				break;
			default:
				break;
			}
			break;
		}

		// loop through oscillators
		for (u8 osc_id = 0; osc_id < OSCS_PER_VOICE; ++osc_id) {
			u16 osc_pitch = 0;

			switch (touch_type) {
			case PHYS_TOUCH: {
				u16 position = s_touch_sort++->pos;
				// steps from string + steps from pad
				s16 pad_step = clampi(string_step_offset + (7 - (position >> 8)), 0, MAX_VALID_SCALE_STEP(scale));

				// pitch from closest pad
				u16 pad_pitch = pitch_at_step_with_midi_tuning(pad_step, scale, scale_steps);
				note_pitch = root_pitch + pad_pitch + arp_oct_pitch;

				// pitch offset from pad touch
				s8 pos_on_pad = 127 - (position & 255);
				u16 next_pad_pitch =
				    pitch_at_step_with_midi_tuning(pad_step + (pos_on_pad > 0 ? 1 : -1), scale, scale_steps);
				s16 pitch_to_next_pad = next_pad_pitch - pad_pitch;
				note_offset_pitch = (s64)abs(pos_on_pad) * micro_param * pitch_to_next_pad >> 24;

				// generate note number - needs to be saved to both buffers when this only generates on env_trigger
				if (osc_id == 2 && (!sys_params.mpe_out || s_string->env_trigger)) {
					u8 note_number = PITCH_TO_NOTE_NR(clampi(note_pitch, 0, MAX_PITCH));
					s_string->note_number = note_number;
					if (s_string->env_trigger)
						write_strings[voice_id].note_number = note_number;
				}
				break;
			}
			// midi touch: saved offset pitch + generate pitch spread
			case MIDI_TOUCH:
				note_offset_pitch = s_string->note_offset_pitch + ((osc_id - 2) * micro_param >> 10);
				break;
			// cv touch: saved offset pitch + fixed width pitch spread
			case CV_TOUCH:
				note_offset_pitch = s_string->note_offset_pitch + ((osc_id - 2) * 8192 >> 10);
				break;
			}

			// save combined pitch, used for calculating pitchbend below
			osc_pitch = note_pitch + note_offset_pitch;
			pitch_4x += osc_pitch;

			// add pitch modulation
			osc_pitch = clampi(osc_pitch + pitch_param_pitch, 0, MAX_PITCH);

			// add osc interval (if within valid pitch range)
			osc_pitch += (osc_id & 1) == 1 && osc_pitch + osc_interval_pitch <= MAX_PITCH ? osc_interval_pitch : 0;

			// save to voice
			voice->osc[osc_id].pitch = osc_pitch;
		}

		// save the lowest and highest string that are touched
		if (s_string->touched) {
			if (low_string_id == 255)
				low_string_id = voice_id;
			high_string_id = voice_id;
			high_string_note = s_string->note_number;
		}

		// for physical touches, pitchbend is derived from the average oscillator pitch
		if (touch_type == PHYS_TOUCH)
			s_string->pitchbend_pitch = clampi(pitch_4x >> 2, 0, MAX_PITCH) - SEMIS_TO_PITCH(s_string->note_number);
	}

	// == UPDATE ENVELOPE == //

	float env_goal = 0.f;

	// calc goal lpg
	if (s_string->touched) {
		float sens = param_val_multi(MP_ENV_LVL1, voice_id) * (2.f / 65536.f);
		env_goal = pressure * 1.f / TOUCH_FULL_PRES * sens * sens;
		if (env_goal < 0.f)
			env_goal = 0.f;
		env_goal *= env_goal;
		// filter cutoff pitch tracking
		env_goal *= 1.f + ((voice->osc[2].pitch - (43000 + OCTS_TO_PITCH(2))) * (1.f / 65536.f));
	}

	// retrieve envelope params
	bool is_sample_preview = ui_mode == UI_SAMPLE_EDIT;
	const float attack = is_sample_preview ? 0.5f : lpf_k((param_val_multi(MP_ATTACK1, voice_id)));
	const float decay = is_sample_preview ? 1.f : lpf_k((param_val_multi(MP_DECAY1, voice_id)));
	const float sustain = is_sample_preview ? 1.f : squaref(param_val_multi(MP_SUSTAIN1, voice_id) * (1.f / 65536.f));
	const float release = is_sample_preview ? 0.5f : lpf_k((param_val_multi(MP_RELEASE1, voice_id)));

	// retrigger envelope
	if (s_string->env_trigger) {
		env_lvl *= sustain;
		voice->env1_decaying = false;
		voice->env1_peak = env_goal;
		cv_trig_out_high = true; // send cv trigger
	}

	if (env_goal <= 0.f) // no pressure => release phase (aka not decaying)
		voice->env1_decaying = false;
	else if (voice->env1_decaying) // in decay phase => aim for sustain level
		env_goal *= sustain;

	// apply envelope
	float lpg_diff = env_goal - env_lvl;
	lpg_diff *= (lpg_diff > 0.f) ? attack : env_goal ? decay : release;
	env_lvl += lpg_diff;

	// release phase
	if (env_goal <= 0.f)
		voice->env1_norm = voice->env1_peak == 0 ? 0 : env_lvl / voice->env1_peak;
	// decay/sustain phase
	else if (voice->env1_decaying) {
		voice->env1_norm = 1;
		voice->env1_peak = env_lvl;
	}
	// attack phase
	else {
		voice->env1_norm = env_lvl / env_goal;
		if (env_goal > voice->env1_peak)
			voice->env1_peak = env_goal;
	}
	// we hit the peak! time to decay
	if (env_lvl > env_goal * 0.95f)
		voice->env1_decaying = true;
	// constrain to max 1.0
	if (env_lvl > 1.f) {
		env_lvl = 1.f;
		voice->env1_decaying = true;
	}

	// track max pressure
	u16 env_16 = maxi(env_lvl * 2048, 0);
	if (env_16 > max_env_lvl)
		max_env_lvl = env_16;

	// == NOISE, DRIVE, LPG == //

	// pre-calc noise, drive, resonance
	int drive_lvl = param_val_multi(MP_DISTORTION, voice_id) * 2 - 65536;
	float fdrive = table_interp(pitches, ((32768 - 2048) + drive_lvl / 2));
	if (drive_lvl < -65536 + 2048)
		fdrive *= (drive_lvl + 65536) * (1.f / 2048.f); // ensure drive goes right to 0 when full minimum
	float drive = fdrive * (0.75f / 65536.f);
	float goal_noise = param_val_multi(MP_NOISE, voice_id) * (1.f / 65536.f);
	goal_noise *= goal_noise;
	if (drive_lvl > 0)
		goal_noise *= fdrive;
	float noise_diff = (goal_noise - voice->noise_lvl) * (1.f / SAMPLES_PER_TICK);
	int resonancei = 65536 - param_val_multi(MP_RESO, voice_id);
	float resonance = 2.1f - (table_interp(pitches, resonancei) * (2.1f / pitches[1024]));
	drive *= 2.f / (resonance + 2.f);

	if (USING_SAMPLER)
		apply_sample_lpg_noise(voice_id, voice, env_lvl, noise_diff, drive, dst);
	else {
		apply_synth_lpg_noise(voice_id, voice, env_lvl, noise_diff, drive, resonance, dst);
		// make the pitchbend value observe pitch modulation and portamento
		if (sys_params.mpe_out && sys_params.mpe_out_fine_tuning)
			s_string->pitchbend_pitch = voice->pitch - pitch_at_note_with_midi_tuning(s_string->note_number);
	}
}

void handle_synth_voices(u32* dst) {
	string_oct_valid = 0;
	string_scale_valid = 0;
	string_start_step_valid = 0;
	string_root_pitch_valid = 0;

	// clear cv values
	cv_trig_out_high = false;
	cv_gate_out_high = false;
	max_env_lvl = 0;
	low_string_id = 255;
	high_string_id = 255;

	// run all voices
	for (u8 voice_id = 0; voice_id < NUM_VOICES; ++voice_id)
		run_voice(voice_id, dst);

	// send cv out
	send_cv_trigger(cv_trig_out_high);
	send_cv_gate(cv_gate_out_high);
	send_cv_pressure(max_env_lvl << 5);

	// drone the lowest string of the arp
	if (low_string_id == high_string_id && before_arp_touch_mask)
		low_string_id = __builtin_ctz(before_arp_touch_mask);

	if (low_string_id != 255)
		send_cv_pitch(false, voices[low_string_id].pitch);
	if (high_string_id != 255)
		send_cv_pitch(true, voices[high_string_id].pitch);

	if (USING_SAMPLER) {
		// decide on a priority for 8 voices
		int gprio[8];
		u32 sampleaddr = get_sample_address();

		for (int i = 0; i < 8; ++i) {
			GrainPair* g = voices[i].grain_pair;
			int glen0 =
			    ((abs(g[0].dpos24) * (SAMPLES_PER_TICK / 2) + g[0].fpos24 / 2 + 1) >> 23) + 2; // +2 for interpolation
			int glen1 =
			    ((abs(g[1].dpos24) * (SAMPLES_PER_TICK / 2) + g[1].fpos24 / 2 + 1) >> 23) + 2; // +2 for interpolation

			// TODO - if pos at end of next fetch will be out of bounds, negate dpos24 and grate_ratio so we ping
			// pong back for the rest of the grain!
			int glen = maxi(glen0, glen1);
			glen = clampi(glen, 0, AVG_GRAINBUF_SAMPLE_SIZE * 2);
			g[0].bufadjust = (g[0].dpos24 < 0) ? maxi(glen - 2, 0) : 0;
			g[1].bufadjust = (g[1].dpos24 < 0) ? maxi(glen - 2, 0) : 0;
			grain_pos[i * 4 + 0] = (int)(g[0].pos[0]) - g[0].bufadjust + sampleaddr;
			grain_pos[i * 4 + 1] = (int)(g[0].pos[1]) - g[0].bufadjust + sampleaddr;
			grain_pos[i * 4 + 2] = (int)(g[1].pos[0]) - g[1].bufadjust + sampleaddr;
			grain_pos[i * 4 + 3] = (int)(g[1].pos[1]) - g[1].bufadjust + sampleaddr;
			glen += 2; // 2 extra 'samples' for the SPI header
			gprio[i] = ((int)(voices[i].env1_lvl * 65535.f) << 12) + i + (glen << 3);
		}
		sort8(gprio, gprio);
		u8 lengths[8];
		int pos = 0, i;
		for (i = 7; i >= 0; --i) {
			int prio = gprio[i];
			int fi = prio & 7;
			int len = (prio >> 3) & 255;
			// we only budget for LAST_GRAIN_SPI_STATE transfers. so after that, len goes to 0. also helps CPU load
			if (i < 8 - MAX_SAMPLE_VOICES)
				len = 0;
			else if (voices[fi].env1_lvl <= 0.01f && !play_strings[fi].touched)
				len = 0; // if your finger is up and the volume is 0, we can just skip this one.
			lengths[fi] = (pos + len * 4 > GRAINBUF_BUDGET) ? 0 : len;
			pos += len * 4;
		}
		// cumulative sum
		pos = 0;
		for (int i = 0; i < NUM_GRAINS; ++i) {
			pos += lengths[i / 4];
			grain_buf_end[i] = pos;
		}
	}
}

// === VISUALS === //

u8 draw_high_note(void) {
	gfx_text_color = 3;
	if (max_env_lvl > 1 && !(USING_SAMPLER && !cur_sample_info.pitched))
		return fdraw_str(0, 0, F_20_BOLD, "%s", note_name(high_string_note, true));
	else
		return 0;
}

void draw_max_pres(void) {
	vline(126, 32 - (max_env_lvl >> 6), 32, 1);
	vline(127, 32 - (max_env_lvl >> 6), 32, 1);
}

void draw_voices(bool show_latch) {
	const static u8 bar_width = 3;
	const static u8 max_height = 9;

	u8 left_offset = show_latch ? 42 : 46;
	u8 bar_spacing = show_latch ? 6 : 8;
	for (u8 voice_id = 0; voice_id < NUM_VOICES; voice_id++) {
		u8 bar_height = clampi(voices[voice_id].env1_norm * max_height, 0, max_height);
		u8 x = voice_id * bar_spacing + left_offset;
		// top
		fill_rectangle(x, OLED_HEIGHT - bar_height - 1, x + bar_width, OLED_HEIGHT - bar_height + 1);
		// bar
		if (play_strings[voice_id].touched)
			half_rectangle(x, OLED_HEIGHT - bar_height + 1, x + bar_width, OLED_HEIGHT);
		// outline
		hline(x, OLED_HEIGHT - bar_height - 2, x + bar_width, 0);
		vline(x - 1, OLED_HEIGHT - bar_height - 2, OLED_HEIGHT, 0);
		vline(x + bar_width, OLED_HEIGHT - bar_height - 2, OLED_HEIGHT, 0);
	}
}

void draw_sample_playback(SampleInfo* s) {
	static int curofscenter = 0;
	static bool jumpable = false;
	int ofs = curofscenter / 1024;
	gfx_text_color = 3;
	for (int i = 32; i < 128 - 16; ++i) {
		int x = i - 32 + ofs;
		u8 h = get_waveform4(s, x);
		vline(i, 15 - h, 16 + h, 2);
	}
	for (int si = 0; si < 8; ++si) {
		int x = (s->splitpoints[si] / 1024) - ofs + 32;
		u8 h = get_waveform4(s, s->splitpoints[si] / 1024);
		if (x >= 32 && x < 128 - 16) {
			vline(x, 0, 13 - h, 1);
			vline(x, 18 + h, 32, 1);
		}
	}
	s8 gx[8 * 4], gy[8 * 4], gd[8 * 4];
	int numtodraw = 0;
	int min_pos = MAX_SAMPLE_LEN;
	int max_pos = 0;

	min_pos = clampi(min_pos, 0, s->samplelen);
	max_pos = clampi(max_pos, 0, s->samplelen);

	for (int i = 0; i < 8; ++i) {
		GrainPair* gr = voices[i].grain_pair;
		int vvol = (int)(256.f * voices[i].env1_lvl);
		if (vvol > 8)
			for (int g = 0; g < 4; ++g) {
				if (!(gr->outflags & (1 << (g & 1)))) {
					int pos = grain_pos[i * 4 + g] & (MAX_SAMPLE_LEN - 1);
					int vol = gr->vol24 >> (24 - 4);
					if (g & 1)
						vol = 15 - vol;
					int graindir = (gr->dpos24 < 0) ? -1 : 1;
					int disppos = pos + graindir * 1024 * 32;
					min_pos = mini(min_pos, pos);
					max_pos = maxi(max_pos, pos);
					min_pos = mini(min_pos, disppos);
					max_pos = maxi(max_pos, disppos);
					int x = (pos / 1024) - ofs + 32;
					int y = (vol * vvol) >> 8;
					if (g & 2)
						y = 16 + y;
					else
						y = 16 - y;
					if (x >= 32 && x < 128 - 16 && y >= 0 && y < 32) {
						gx[numtodraw] = x;
						gy[numtodraw] = y;
						gd[numtodraw] = graindir;
						numtodraw++;
					}
				}
				if (g & 1)
					gr++;
			}
	}
	for (int i = 0; i < numtodraw; ++i) {
		int x = gx[i], y = gy[i];
		vline(x, y - 2, y + 2, 0);
		vline(x + gd[i], y - 1, y + 2, 0);
	}
	for (int i = 0; i < numtodraw; ++i) {
		int x = gx[i], y = gy[i];
		vline(x, y - 1, y + 1, 1);
		put_pixel(x + gd[i], y, 1);
	}
	if (min_pos >= max_pos)
		jumpable = true;
	else {
		if (jumpable)
			curofscenter = min_pos - 8 * 1024;
		else {
#define GRAIN_SCROLL_SHIFT 3
			if (min_pos < curofscenter)
				curofscenter += (min_pos - curofscenter) >> GRAIN_SCROLL_SHIFT;
			if (max_pos > curofscenter + (128 - 48) * 1024)
				curofscenter += (max_pos - curofscenter - (128 - 48) * 1024) >> GRAIN_SCROLL_SHIFT;
		}
		jumpable = false;
	}
}

void get_root_and_blackout_leds(u8 string_id, u8 root_k[PADS_PER_STRIP]) {
	Scale scale = string_scale(string_id);
	u8 scale_steps = steps_in_scale[scale];
	s16 step = string_start_step(string_id);
	for (u8 y = 0; y < PADS_PER_STRIP; y++, step++) {
		if (step < 0 || step > MAX_VALID_SCALE_STEP(scale))
			root_k[7 - y] = 48;
		else if (step % scale_steps == 0)
			root_k[7 - y] = 96;
	}
}