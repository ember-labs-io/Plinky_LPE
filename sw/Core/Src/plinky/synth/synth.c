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
	s32 goal_phase_diff;
	s32 pitch;
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

// Alex notes:
//
// pitch table is (64*8) steps per semitone, ie 512 per semitone
// so heres my maths, this comes out at 435
// 8887421 comes from the value of pitch when playing a C
// the pitch of middle c in plinky as written is (4.0/(65536.0*65536.0/8887421.0/31250.0f))
// which is 1.0114729530400526 too low
// which is 0.19749290999 semitones flat
// *512 = 101. so I need to add 101 to pitch_base

#define PITCH_BASE ((32768 - (12 << 9)) + 512 + 101) // pitch value for C4

static Voice voices[NUM_VOICES];
static SynthString synth_string[NUM_STRINGS];

static u8 synth_write_frame;
static u8 write_frame_mask;
static u8 synth_read_frame;
static u8 read_frame_mask;

static u8 phys_touch_mask = 0;
static bool cv_trig_high = false;    // should cv trigger be high?
static s32 cv_gate_value;            // cv gate value
static bool got_high_pitch = false;  // did we save a high pitch?
static s32 high_string_pitch_4x = 0; // pitch on highest touched string
static s16 high_string_note = 0;     // note on highest touched string
static bool got_low_pitch = false;   // did we save a low pitch?
static s32 low_string_pitch_4x = 0;  // pitch on lowest touched string
static u16 synth_max_pres = 0;       // highest pressure seen

const SynthString* get_synth_string(u8 string_id) {
	return &synth_string[string_id];
}

// === UTILS === //

static s32 pitch_at_step(s8 step, Scale scale) {
	u8 steps_in_scale = scale_table[scale][0];
	s16 oct = step / steps_in_scale;
	step -= oct * steps_in_scale;
	if (step < 0) {
		step += steps_in_scale;
		oct--;
	}
	return OCTS_TO_PITCH(oct) + scale_table[scale][step + 1];
}

s16 step_at_string(u8 string_id, Scale scale) {
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

static s32 string_center_pitch(u8 string_id, Scale scale) {
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
	return ((pitch3 + pitch4) >> 1) + OCTS_TO_PITCH(oct);
}

u8 find_string_for_pitch(s32 pitch) {
	// find desired string: center pitch closest to pitch
	u8 desired_string = 0;
	u32 prev_pitch_dist = __UINT32_MAX__;
	for (u8 string_id = 0; string_id < NUM_STRINGS; string_id++) {
		u32 pitch_dist = abs(string_center_pitch(string_id, param_index_poly(PP_SCALE, string_id)) - pitch);
		if (pitch_dist >= prev_pitch_dist)
			break;
		prev_pitch_dist = pitch_dist;
		desired_string = string_id;
	}

	// find closest non-sounding string
	u8 best_string = 255;
	u8 prev_dist = 255;
	for (u8 string_id = 0; string_id < NUM_STRINGS; string_id++) {
		u8 dist = abs(string_id - desired_string);
		if (dist >= prev_dist)
			break;
		// log non-sounding strings
		if (!synth_string[string_id].touched && voices[string_id].env1_lvl < 0.001f)
			best_string = string_id;
		prev_dist = dist;
	}

	// return found string, if any
	if (best_string != 255)
		return best_string;

	// find quietest non-touched string
	float min_vol = __FLT_MAX__;
	for (u8 string_id = 0; string_id < NUM_STRINGS; string_id++) {
		float vol = voices[string_id].env1_lvl;
		if (!synth_string[string_id].touched && vol < min_vol) {
			min_vol = vol;
			best_string = string_id;
		}
	}

	// returns 255 if no non-pressed strings were found
	return best_string;
}

u16 string_position_from_pitch(u8 string_id, s32 pitch) {
	Scale scale = param_index_poly(PP_SCALE, string_id);
	s16 string_step_offset = step_at_string(string_id, scale);
	u16 position = 7 << 8;
	for (u8 pad = 7; pad > 0; pad--) {
		if (pitch < pitch_at_step(string_step_offset + pad, scale))
			break;
		position = (7 - pad) << 8;
	}
	return position;
}

void clear_latch(void) {
	for (u8 string_id = 0; string_id < NUM_STRINGS; string_id++) {
		synth_string[string_id].latch_touch.pos = 0;
		synth_string[string_id].latch_touch.pres = 0;
	}
}

void clear_synth_string(u8 string_id) {
	synth_string[string_id] = init_synth_string;
}

void clear_synth_strings(void) {
	for (u8 string_id = 0; string_id < NUM_STRINGS; string_id++)
		synth_string[string_id] = init_synth_string;
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
		timestretch = param_val_poly(PP_SMP_STRETCH, voice_id) * (2.f / 65536.f);
		gsize = param_val_poly(PP_GR_SIZE, voice_id) * (1.414f / 65536.f);
		grate = param_val_poly(PP_PLAY_SPD, voice_id) * (2.f / 65536.f);
		smppos = (param_val_poly(PP_SCRUB, voice_id) * cur_sample_info.samplelen) >> 16;
		posjit = param_val_poly(PP_SCRUB_JIT, voice_id) * (1.f / 65536.f);
		sizejit = param_val_poly(PP_GR_SIZE_JIT, voice_id) * (1.f / 65536.f);
		gratejit = param_val_poly(PP_PLAY_SPD_JIT, voice_id) * (1.f / 65536.f);
	}
	int trig = synth_string[voice_id].env_trigger;

	int prevsliceidx = voice->slice_id;
	bool gp = ui_mode == UI_SAMPLE_EDIT;
	u16 touch_pos = synth_string[voice_id].cur_touch.pos;

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
			int mypitch = (voice->osc[1].pitch + voice->osc[2].pitch) / 2;
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
	} // osc loop

	voice->env1_lvl = goal_lpg;
	voice->noise_lvl = noise;

	// update pitch (aka dpos24) for next time!
	for (int gi = 0; gi < 2; ++gi) {
		float multisample_grate;
		if (cur_sample_info.pitched && (ui_mode != UI_SAMPLE_EDIT)) {
			int relpitch = voice->osc[1 + gi].pitch - cur_sample_info.notes[voice->slice_id] * 512;
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

// this combines inputs from touchstrips, midi, latch, arp & sequencer and saves the resulting Touch in
// string_touch[string_id]
static void generate_string_touch(u8 string_id) {
	static bool suppress_latch = false;
	SynthString* s_string = &synth_string[string_id];
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
	    sys_params.local_on
	    // default ui, exception for the edit-strip
	    && ((ui_mode == UI_DEFAULT && !(string_id == 0 && editing_param()))
	        // sampler in preview mode when a sample is loaded
	        || (ui_mode == UI_SAMPLE_EDIT && sampler_mode == SM_PREVIEW && USING_SAMPLER))) {
		bool touching = strip_touched & mask;
		bool first_touch_global = false;

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

		if (latch_active() && touching && pres_increasing) {
			if (first_touch_global) {
				// start a new latch
				clear_latch();

				// in step record mode, trying to start a new latch temporarily turns off latching
				// trying to start a new latch outside of step record mode turns it on again
				suppress_latch = seq_state() == SEQ_STEP_RECORDING;
			}
			// save latch values
			if (!suppress_latch) {
				s_latch->pres = pres_compress(pressure);
				s_latch->pos = pos_compress(position);
			}
			// RJ: I could not work out a way to work with average values that wasn't
			// sluggish or gave undesired intermediate values - slides and in-between notes
			// Current solution is just saving one value and randomizing when reading it out
			// Result feels great, but good to reconsider when the exact contents of
			// touchstrip and string_touch are more clear

			// Averaging code for reference:
			//
			// u8 max_pos = 0, min_pos = 255, maxpressure = 0;
			// Touch* f = string_touch[string_id];
			// for (int j = 0; j < 8; ++j, ++f) {
			// 	u8 p = clampi((f->pos + 4) / 8, 0, 255);
			// 	min_pos = mini(p, min_pos);
			// 	max_pos = maxi(p, max_pos);
			// 	u8 pr = clampi(f->pres / 12, 0, 255);
			// 	maxpressure = maxi(maxpressure, pr);
			// }
			// latch[string_id].pres = maxpressure;
			// latch[string_id].min_pos = min_pos;
			// latch[string_id].max_pos = max_pos;
		}
	}

	// recall latch if it's larger than the pressure we're holding
	if (latch_active() && s_latch->pres > 0 && s_latch->pres * 24 > pressure) {
		// recall latch values
		pressure = pres_decompress(s_latch->pres);
		position = pos_decompress(s_latch->pos);
		pres_increasing = true;

		// Averaging code for reference:
		//
		// int min_pos = latch[string_id].min_pos * 8 + 2;
		// int max_pos = latch[string_id].max_pos * 8 + 6;
		// int avgpos = (min_pos + max_pos) / 2;
		// int range = (max_pos - min_pos) / 4;
		// pressure = latchpres ? rand_range(latchpres - 12, latchpres) : -1024;
		// position = rand_range(avgpos-range,avgpos+range);
	}

	// record touch to sequencer
	seq_try_rec_touch(string_id, pressure, position, pres_increasing);

	// retrieve touch from sequencer
	if (pressure <= 0)
		seq_try_get_touch(string_id, &pressure, &position);

	// any available pressure from touch/latch/sequencer overrides midi
	if (pressure > 0)
		s_string->using_midi &= ~write_frame_mask;
	// retrieve touch from midi
	else if (midi_try_get_touch(string_id, &pressure, &position, &s_string->note_number, &s_string->start_velocity,
	                            &s_string->pitchbend_pitch))
		s_string->using_midi |= write_frame_mask;

	// === FINISHING UP === //

	Touch prev_touch = s_string->touch_frames[(synth_write_frame + 7) & 7];

	// no pressure => retain previous frame's position
	if (pressure <= 0)
		position = prev_touch.pos;
	// new finger touch => fill (non-pressed) history with slightly randomized variant of current position
	else if (prev_touch.pres <= 0) {
		Touch* s_touch = s_string->touch_frames;
		for (u8 frame = 0; frame < NUM_TOUCH_FRAMES; frame++, s_touch++)
			if (frame != synth_write_frame && s_touch->pres <= 0)
				s_touch->pos = position ^ (s_touch->pos & 3);
	}

	// save results to the synth string
	s_string->touch_frames[synth_write_frame].pres = pressure;
	s_string->touch_frames[synth_write_frame].pos = position;

	// sort touch frames by position
	sort8((int*)s_string->touch_sorted, (int*)s_string->touch_frames);
}

// manage generating the string_touch array
void generate_string_touches(void) {
	static u8 no_arp_touch_mask = 0;
	static u8 no_arp_touch_mask_1back = 0;
	static bool do_second_half = false;
	static u8 phys_string_touch_1back = 0;
	static bool prev_latch = false;

	// end latch when necessary
	bool cur_latch = latch_active();
	if (prev_latch && !cur_latch)
		clear_latch();
	prev_latch = cur_latch;

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

		if (synth_write_frame != touch_frame) {
			// we read from the frame that was written just before
			synth_read_frame = synth_write_frame;
			// we write to the frame that is currently being processed by the touchstrips
			synth_write_frame = touch_frame;
			// utility masks
			read_frame_mask = 1 << synth_read_frame;
			write_frame_mask = 1 << synth_write_frame;
			arp_next_strings_frame_trig();
		}
	}

	// toggle which half we process
	do_second_half = !do_second_half;

	// calculate string touches
	no_arp_touch_mask_1back = no_arp_touch_mask;
	no_arp_touch_mask = 0;
	for (u8 string_id = 0; string_id < NUM_STRINGS; ++string_id)
		if (synth_string[string_id].touch_frames[synth_read_frame].pres > 0)
			no_arp_touch_mask |= 1 << string_id;
	u8 envelope_trigger = no_arp_touch_mask & ~no_arp_touch_mask_1back;

	// new (physical or virtual) touch: restart arp
	if (arp_active() && no_arp_touch_mask && !no_arp_touch_mask_1back) {
		arp_reset();
		// if the sequencer is not playing, reset the clock so the arp gets a trigger
		if (!seq_playing())
			clock_reset();
	}

	// generate final touch mask
	u8 touch_mask = no_arp_touch_mask;
	// replace touch_mask by arp touches, update envelope triggers
	if (arp_active())
		envelope_trigger = arp_tick(no_arp_touch_mask, &touch_mask) ? touch_mask : 0;

	// precalc and populate strings for this frame
	for (u8 string_id = 0; string_id < NUM_STRINGS; string_id++) {
		SynthString* s_string = &synth_string[string_id];
		Touch* c_touch = &s_string->cur_touch;
		u8 mask = 1 << string_id;

		// basic properties
		s_string->touched = !!(touch_mask & mask);
		s_string->env_trigger = !!(envelope_trigger & mask);
		// save frame touch to current touch
		*c_touch = s_string->touch_frames[synth_read_frame];
		// clear pressure if not touched
		if (!s_string->touched)
			c_touch->pres = TOUCH_MIN_PRES;
		// generate start velocity for non-midi touches
		if (!(s_string->using_midi & read_frame_mask) && s_string->env_trigger)
			s_string->start_velocity = clampi((c_touch->pres << 3) / sys_params.midi_out_vel_balance - 1, 0, 127);
	}
}

static void apply_synth_lpg_noise(u8 voice_id, Voice* voice, float goal_lpg, float noise_diff, float drive,
                                  float resonance, u32* dst) {
	// two loops handling two oscillators each
	float glide = lpf_k(param_val_poly(PP_GLIDE, voice_id) >> 2) * (0.5f / SAMPLES_PER_TICK);
	s32 osc_shape = param_val_poly(PP_SHAPE, voice_id);
	float noise;
	for (u8 osc_id = 0; osc_id < OSCS_PER_VOICE / 2; osc_id++) {
		s16* osc_dst = ((s16*)dst) + (osc_id & 1);
		noise = voice->noise_lvl;
		int rand_table_pos = rand() & 16383;
		float osc_lpg = voice->env1_lvl;
		float osc_lpg_diff = (goal_lpg - osc_lpg) * (1.f / SAMPLES_PER_TICK);

		Osc* osc = &voice->osc[osc_id];

		u32 flippity = 0;
		if (osc_shape != 0) {
			flippity = ~0;
			u32 avg_phase_diff = (osc[0].phase_diff + osc[2].phase_diff) / 2;
			osc[0].phase_diff = avg_phase_diff;
			osc[2].phase_diff = avg_phase_diff;
			avg_phase_diff = (osc[0].goal_phase_diff + osc[2].goal_phase_diff) / 2;
			osc[0].goal_phase_diff = avg_phase_diff;
			osc[2].goal_phase_diff = avg_phase_diff;
			if (osc_shape < 0) {
				s32 phase0_fix =
				    (s32)(osc[2].phase - osc[0].phase - (osc_shape << 16) + (1 << 31)) / (SAMPLES_PER_TICK);
				osc[0].phase_diff += phase0_fix;
				osc[0].goal_phase_diff += phase0_fix;
			}
		}
		int dd_phase1 = (int)((osc->goal_phase_diff - osc->phase_diff) * glide);
		u32 phase1 = osc->phase;
		s32 phase1_diff = osc->phase_diff;
		u32 prev_sample1 = osc->prev_sample;
		osc += 2;
		int dd_phase2 = (int)((osc->goal_phase_diff - osc->phase_diff) * glide);
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

		voice->lpg_smoother[osc_id].y1 = y1;
		voice->lpg_smoother[osc_id].y2 = y2;
	} // osc loop

	voice->env1_lvl = goal_lpg;
	voice->noise_lvl = noise;
}

static void run_voice(u8 voice_id, u32* dst) {
	// the synth string we read data from
	SynthString* s_string = &synth_string[voice_id];
	// the voice we write the resulting data to
	Voice* voice = &voices[voice_id];
	s16 pressure = s_string->cur_touch.pres;
	float env_lvl = voice->env1_lvl;
	bool voice_audible = env_lvl > 0.001f;

	// track max pressure
	if (pressure > synth_max_pres)
		synth_max_pres = pressure;

	// turn off midi note if it has rung out
	if ((s_string->using_midi & read_frame_mask) && !s_string->touched && !voice_audible)
		s_string->using_midi &= ~read_frame_mask;

	// rj: cv_gate_value is in practice another expression of the maximum pressure over all strings, which goes
	// against eurorack conventions (gates are generally high/low, 5V/0V) and I'm also not sure what the added value
	// of this is, since we already have an expression of the max pressure on the pressure CV out - should we make
	// gate out binary?

	// generate cv gate
	cv_gate_value = maxi(cv_gate_value, (s32)(pressure * 65536.f / TOUCH_FULL_PRES));

	// == GENERATE OSCILLATOR PITCHES == //

	if (s_string->touched || voice_audible) {
		// precalc some pitches
		s32 oct_pitch_offset = OCTS_TO_PITCH(arp_oct_offset + param_index_poly(PP_OCT, voice_id));
		s32 pitch_param_pitch = PARAM_VAL_TO_PITCH(param_val_poly(PP_PITCH, voice_id));
		s32 osc_interval_pitch = PARAM_VAL_TO_PITCH(param_val_poly(PP_INTERVAL, voice_id));
		s32 micro_param = param_val_poly(PP_MICROTONE, voice_id);
		bool using_midi = !!(s_string->using_midi & read_frame_mask);

		// we're filling these
		s32 note_pitch = 0;
		s32 pitchbend_pitch = 0;

		// for averages
		s32 note_pitch_4x = 0;
		s32 pitchbend_pitch_4x = 0;

		// these only get used by touch
		Touch* s_touch_sort = &s_string->touch_sorted[2]; // we use pitches 2-5, discarding extreme values
		Scale scale = S_MAJOR;
		s16 string_step_offset = 0;

		if (using_midi)
			note_pitch = NOTE_NR_TO_PITCH(s_string->note_number);
		else {
			scale = param_index_poly(PP_SCALE, voice_id);
			string_step_offset = step_at_string(voice_id, scale);
		}

		// loop through oscillators
		for (u8 osc_id = 0; osc_id < OSCS_PER_VOICE; ++osc_id) {
			s32 pitch_param_fine_pitch = 0;
			s32 osc_pitch = 0;

			// for midi
			if (using_midi)
				// generate pitch spread
				pitchbend_pitch = s_string->pitchbend_pitch + ((osc_id - 2) * micro_param >> 10);
			// for touch
			else {
				u16 position = s_touch_sort++->pos; // touch position
				u8 pad_y = 7 - (position >> 8);     // pad on string
				s8 total_step_offset = string_step_offset + pad_y;
				s32 pad_pitch = pitch_at_step(total_step_offset, scale);
				if (abs(pitch_param_pitch) < PITCH_PER_SEMI >> 1)
					pitch_param_fine_pitch = pitch_param_pitch;
				// quantize the pitch offset from P_PITCH to scale to find the correct pad - UPDATE LOOP
				else {
					s32 goal_pitch = pad_pitch + pitch_param_pitch;
					u32 best_distance = UINT32_MAX;
					s16 step_offset_with_pitch_param = total_step_offset;
					if (pitch_param_pitch > 0) {
						while (true) {
							step_offset_with_pitch_param++;
							u32 distance = abs(goal_pitch - pitch_at_step(step_offset_with_pitch_param, scale));
							if (distance >= best_distance)
								break;
							best_distance = distance;
						}
						step_offset_with_pitch_param--;
					}
					else {
						while (true) {
							step_offset_with_pitch_param--;
							u32 distance = abs(goal_pitch - pitch_at_step(step_offset_with_pitch_param, scale));
							if (distance >= best_distance)
								break;
							best_distance = distance;
						}
						step_offset_with_pitch_param++;
					}
					pad_pitch = pitch_at_step(step_offset_with_pitch_param, scale);
					pitch_param_fine_pitch = goal_pitch - pad_pitch;
				}

				// the note we're playing
				note_pitch = pad_pitch + oct_pitch_offset;

				// oscillator 2 defines the note number
				if (osc_id == 2)
					s_string->note_number = PITCH_TO_NOTE_NR(note_pitch);

				// detuning from pad touch: pitchbend
				s16 pos_on_pad = 127 - (position & 255);
				u16 pitch_to_next_pad =
				    abs(pitch_at_step(string_step_offset + pad_y + (pos_on_pad > 0 ? 1 : -1), scale) - note_pitch);
				pitchbend_pitch = (s64)pos_on_pad * micro_param * pitch_to_next_pad >> 24;
			}

			// save for averages
			note_pitch_4x += note_pitch;
			pitchbend_pitch_4x += pitchbend_pitch;

			// add detuning from pitch parameter - doesn't count as pitchbend
			osc_pitch = note_pitch + pitchbend_pitch + pitch_param_fine_pitch;

			// add osc interval
			osc_pitch += (osc_id & 1) == 1 ? osc_interval_pitch : 0;

			// save to voice
			voice->osc[osc_id].pitch = osc_pitch;
			voice->osc[osc_id].goal_phase_diff =
			    maxi(65536, (s32)(table_interp(pitches, osc_pitch + PITCH_BASE) * (65536.f * 128.f)));
		}

		if (!using_midi)
			s_string->pitchbend_pitch = pitchbend_pitch_4x >> 2;

		// save the lowest and highest string that are touched
		if (s_string->touched) {
			if (!got_low_pitch) {
				low_string_pitch_4x = note_pitch_4x + pitchbend_pitch_4x;
				got_low_pitch = true;
			}
			high_string_note = s_string->note_number;
			high_string_pitch_4x = note_pitch_4x + pitchbend_pitch_4x;
			got_high_pitch = true;
		}
	}

	// == UPDATE ENVELOPE == //

	float env_goal = 0.f;

	// calc goal lpg
	if (s_string->touched) {
		float sens = param_val_poly(PP_ENV_LVL1, voice_id) * (2.f / 65536.f);
		env_goal = pressure * 1.f / TOUCH_FULL_PRES * sens * sens;
		if (env_goal < 0.f)
			env_goal = 0.f;
		env_goal *= env_goal;
		env_goal *= 1.f + ((voice->osc[2].pitch - 43000) * (1.f / 65536.f)); // pitch compensation
	}

	// retrieve envelope params
	bool is_sample_preview = ui_mode == UI_SAMPLE_EDIT;
	const float attack = is_sample_preview ? 0.5f : lpf_k((param_val_poly(PP_ATTACK1, voice_id)));
	const float decay = is_sample_preview ? 1.f : lpf_k((param_val_poly(PP_DECAY1, voice_id)));
	const float sustain = is_sample_preview ? 1.f : squaref(param_val_poly(PP_SUSTAIN1, voice_id) * (1.f / 65536.f));
	const float release = is_sample_preview ? 0.5f : lpf_k((param_val_poly(PP_RELEASE1, voice_id)));

	// retrigger envelope
	if (s_string->env_trigger) {
		env_lvl *= sustain;
		voice->env1_decaying = false;
		voice->env1_peak = env_goal;
		cv_trig_high = true; // send cv trigger
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

	// == NOISE, DRIVE, LPG == //

	// pre-calc noise, drive, resonance
	int drive_lvl = param_val_poly(PP_DISTORTION, voice_id) * 2 - 65536;
	float fdrive = table_interp(pitches, ((32768 - 2048) + drive_lvl / 2));
	if (drive_lvl < -65536 + 2048)
		fdrive *= (drive_lvl + 65536) * (1.f / 2048.f); // ensure drive goes right to 0 when full minimum
	float drive = fdrive * (0.75f / 65536.f);
	float goal_noise = param_val_poly(PP_NOISE, voice_id) * (1.f / 65536.f);
	goal_noise *= goal_noise;
	if (drive_lvl > 0)
		goal_noise *= fdrive;
	float noise_diff = (goal_noise - voice->noise_lvl) * (1.f / SAMPLES_PER_TICK);
	int resonancei = 65536 - param_val_poly(PP_RESO, voice_id);
	float resonance = 2.1f - (table_interp(pitches, resonancei) * (2.1f / pitches[1024]));
	drive *= 2.f / (resonance + 2.f);

	if (USING_SAMPLER)
		apply_sample_lpg_noise(voice_id, voice, env_lvl, noise_diff, drive, dst);
	else
		apply_synth_lpg_noise(voice_id, voice, env_lvl, noise_diff, drive, resonance, dst);
}

void handle_synth_voices(u32* dst) {
	// clear cv values
	cv_trig_high = false;
	got_high_pitch = false;
	cv_gate_value = 0;
	got_low_pitch = false;
	synth_max_pres = 0;

	// run all voices
	for (u8 voice_id = 0; voice_id < NUM_VOICES; ++voice_id)
		run_voice(voice_id, dst);

	// send cv values
	send_cv_trigger(cv_trig_high);
	if (got_high_pitch)
		send_cv_pitch(true, high_string_pitch_4x, true);
	send_cv_gate(mini(cv_gate_value, 65535));
	if (got_low_pitch)
		send_cv_pitch(false, low_string_pitch_4x, true);
	send_cv_pressure(synth_max_pres * 8);

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
			else if (voices[fi].env1_lvl <= 0.01f && !synth_string[fi].touched)
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
	if (synth_max_pres > 1 && !(USING_SAMPLER && !cur_sample_info.pitched))
		return fdraw_str(0, 0, F_20_BOLD, "%s", note_name(high_string_note));
	else
		return 0;
}

void draw_max_pres(void) {
	vline(126, 32 - (synth_max_pres >> 6), 32, 1);
	vline(127, 32 - (synth_max_pres >> 6), 32, 1);
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
		if (synth_string[voice_id].touched)
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
