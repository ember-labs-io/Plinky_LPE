#include "synth.h"
#include "arp.h"
#include "audio_tools.h"
#include "data/tables.h"
#include "gfx/gfx.h"
#include "hardware/adc_dac.h"
#include "hardware/memory.h"
#include "hardware/midi.h"
#include "params.h"
#include "pitch_tools.h"
#include "sampler.h"
#include "strings.h"

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

Voice voices[NUM_VOICES];

static bool cv_trig_high = false;   // should cv trigger be high?
static s32 cv_gate_value;           // cv gate value
static bool got_high_pitch = false; // did we save a high pitch?
static s32 high_string_pitch = 0;   // pitch on highest touched string
static bool got_low_pitch = false;  // did we save a low pitch?
static s32 low_string_pitch = 0;    // pitch on lowest touched string
static u16 synth_max_pres = 0;      // highest pressure seen

static void run_voice(u8 voice_id, u32* dst, s16 pressure) {
	Voice* voice = &voices[voice_id];
	u8 mask = 1 << voice_id;
	float env_lvl = voice->env1_lvl;

	// track max pressure
	if (pressure > synth_max_pres)
		synth_max_pres = pressure;

	// turn off midi note on this voice when needed
	midi_try_end_note(voice_id);

	// rj: cv_gate_value is in practice another expression of the maximum pressure over all strings, which goes
	// against eurorack conventions (gates are generally high/low, 5V/0V) and I'm also not sure what the added value
	// of this is, since we already have an expression of the max pressure on the pressure CV out - should we make
	// gate out binary?

	// generate cv gate
	cv_gate_value = maxi(cv_gate_value, (s32)(pressure * 65536.f / TOUCH_FULL_PRES));

	// == GENERATE OSCILLATOR PITCHES == //

	if ((string_touched & mask) || env_lvl > 0.001f) {
		s32 note_pitch = 0;   // pitch offset caused by the played note
		s32 fine_pitch = 0;   // pitch offset caused by micro_tone / spread
		s32 osc_pitch = 0;    // resulting pitch of current oscillator
		s32 summed_pitch = 0; // summed pitch of four oscillators

		// these only get used by touch
		Scale scale = NUM_SCALES;
		s32 cv_pitch_offset = 0;
		s8 cv_step_offset = 0;
		s16 string_step_offset = 0;

		// saving string values that are the same for all oscillators

		s32 base_pitch = 12 *
		                 // pitch from arp and the octave parameter
		                 (((arp_oct_offset + param_index_poly(P_OCT, voice_id)) << 9)
		                  // pitch from the pitch parameter
		                  + (param_val_poly(P_PITCH, voice_id) >> 7));
		// pitch from interval parameter
		s32 osc_interval_pitch = (param_val_poly(P_INTERVAL, voice_id) * 12) >> 7;

		bool using_midi = midi_string_used(voice_id);

		// for midi
		if (using_midi)
			note_pitch = midi_get_pitch(voice_id);
		// for touch
		else {
			scale = param_index_poly(P_SCALE, voice_id);
			if (scale >= NUM_SCALES)
				scale = 0;

			// cv
			s32 cv_pitch = adc_get_smooth(ADC_S_PITCH);
			if (sys_params.cv_quant == CVQ_SCALE)
				cv_step_offset = (((PITCH_TO_SEMIS(cv_pitch)) * scale_table[scale][0] + 1) / 12); // quantized cv
			else
				cv_pitch_offset = cv_pitch; // unquantized cv
			string_step_offset = step_at_string(voice_id, scale);
		}

		// we discard the two highest and lowest positions and use elements 2 through 5 to generate our oscillator
		// pitches
		const Touch* s_touch_sort = sorted_string_touch_ptr(voice_id) + 2;

		// loop through oscillators
		for (u8 osc_id = 0; osc_id < OSCS_PER_VOICE; ++osc_id) {
			// for midi
			if (using_midi)
				// generate pitch spread
				fine_pitch = (osc_id - 2) * 64 * param_val_poly(P_MICROTONE, voice_id) >> 16;
			// for touch
			else {
				u16 position = s_touch_sort++->pos; // touch position
				u8 pad_y = 7 - (position >> 8);     // pad on string
				// pitch at step + cv
				note_pitch = pitch_at_step(string_step_offset + pad_y + cv_step_offset, scale) + cv_pitch_offset;

				// detuning scaled by microtune param
				s16 fine_pos = 127 - (position & 255); // offset from pad center
				u16 pitch_to_next_pad =
				    abs(pitch_at_step(string_step_offset + pad_y + cv_step_offset + (fine_pos > 0 ? 1 : -1), scale)
				        + cv_pitch_offset - note_pitch);
				s32 micro_tune = ((64 + param_val_poly(P_MICROTONE, voice_id)) * pitch_to_next_pad) >> 10;
				fine_pitch = (fine_pos * micro_tune) >> 14;
			}

			// send osc 0 pitch to midi
			if (osc_id == 0)
				midi_set_goal_note(
				    voice_id, clampi((base_pitch + note_pitch + (PITCH_PER_SEMI >> 1)) / PITCH_PER_SEMI + 24, 0, 127));

			s32 used_interval_pitch = (osc_id & 1) ? osc_interval_pitch : 0;

			// calculate resulting pitch
			osc_pitch =
			    // octave and pitch parameters
			    base_pitch +
			    // pitch from scale step / midi note
			    note_pitch +
			    // pitch from osc interval parameter
			    used_interval_pitch +
			    // pitch from micro_tone / pitch spread
			    fine_pitch;

			// save values
			summed_pitch += osc_pitch - used_interval_pitch;
			voice->osc[osc_id].pitch = osc_pitch;
			voice->osc[osc_id].goal_phase_diff =
			    maxi(65536, (s32)(table_interp(pitches, osc_pitch + PITCH_BASE) * (65536.f * 128.f)));
		}

		// these are saving respectively the lowest and highest string that are pressed
		if (!got_low_pitch) {
			low_string_pitch = summed_pitch;
			got_low_pitch = true;
		}
		high_string_pitch = summed_pitch;
		got_high_pitch = true;
	}

	// == UPDATE ENVELOPE == //

	float env_goal = 0.f;

	// calc goal lpg
	if (string_touched & mask) {
		float sens = param_val_poly(P_ENV_LVL1, voice_id) * (2.f / 65536.f);
		env_goal = pressure * 1.f / TOUCH_MAX_POS * sens * sens;
		if (env_goal < 0.f)
			env_goal = 0.f;
		env_goal *= env_goal;
		env_goal *= 1.f + ((voice->osc[2].pitch - 43000) * (1.f / 65536.f)); // pitch compensation
	}

	// retrieve envelope
	bool is_sample_preview = ui_mode == UI_SAMPLE_EDIT;
	const float attack = is_sample_preview ? 0.5f : lpf_k((param_val_poly(P_ATTACK1, voice_id)));
	const float decay = is_sample_preview ? 1.f : lpf_k((param_val_poly(P_DECAY1, voice_id)));
	const float sustain = is_sample_preview ? 1.f : squaref(param_val_poly(P_SUSTAIN1, voice_id) * (1.f / 65536.f));
	const float release = is_sample_preview ? 0.5f : lpf_k((param_val_poly(P_RELEASE1, voice_id)));

	// new touch: start new envelope
	if (envelope_trigger & mask) {
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

	env_goal = env_lvl;

	// == NOISE, DRIVE, LPG == //

	// pre-calc noise, drive, resonance
	int drive_lvl = param_val_poly(P_DISTORTION, voice_id) * 2 - 65536;
	float fdrive = table_interp(pitches, ((32768 - 2048) + drive_lvl / 2));
	if (drive_lvl < -65536 + 2048)
		fdrive *= (drive_lvl + 65536) * (1.f / 2048.f); // ensure drive goes right to 0 when full minimum
	float drive = fdrive * (0.75f / 65536.f);
	float goal_noise = param_val_poly(P_NOISE, voice_id) * (1.f / 65536.f);
	goal_noise *= goal_noise;
	if (drive_lvl > 0)
		goal_noise *= fdrive;
	float noise_diff = (goal_noise - voice->noise_lvl) * (1.f / SAMPLES_PER_TICK);
	int resonancei = 65536 - param_val_poly(P_RESO, voice_id);
	float resonance = 2.1f - (table_interp(pitches, resonancei) * (2.1f / pitches[1024]));
	drive *= 2.f / (resonance + 2.f);

	// send sampler voice to sampler module
	if (USING_SAMPLER) {
		apply_sample_lpg_noise(voice_id, voice, env_goal, noise_diff, drive, dst);
		return;
	}

	// two loops handling two oscillators each
	float glide = lpf_k(param_val_poly(P_GLIDE, voice_id) >> 2) * (0.5f / SAMPLES_PER_TICK);
	s32 osc_shape = param_val_poly(P_SHAPE, voice_id);
	float noise;
	for (u8 osc_id = 0; osc_id < OSCS_PER_VOICE / 2; osc_id++) {
		s16* osc_dst = ((s16*)dst) + (osc_id & 1);
		noise = voice->noise_lvl;
		int rand_table_pos = rand() & 16383;
		float osc_lpg = voice->env1_lvl;
		float osc_lpg_diff = (env_goal - osc_lpg) * (1.f / SAMPLES_PER_TICK);

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

	voice->env1_lvl = env_goal;
	voice->noise_lvl = noise;
}

void handle_synth_voices(u32* dst) {
	// clear cv values
	cv_trig_high = false;
	got_high_pitch = false;
	cv_gate_value = 0;
	got_low_pitch = false;
	synth_max_pres = 0;

	// run all voices
	const s16* string_pressures = get_string_pressures();
	for (u8 voice_id = 0; voice_id < NUM_VOICES; ++voice_id)
		run_voice(voice_id, dst, string_pressures[voice_id]);

	// send cv values
	send_cv_trigger(cv_trig_high);
	if (got_high_pitch)
		send_cv_pitch(true, high_string_pitch, true);
	send_cv_gate(mini(cv_gate_value, 65535));
	if (got_low_pitch)
		send_cv_pitch(false, low_string_pitch, true);
	send_cv_pressure(synth_max_pres * 8);

	// send to sampler
	if (USING_SAMPLER)
		sampler_playing_tick();
}

u8 draw_high_note(void) {
	if (synth_max_pres > 1 && !(USING_SAMPLER && !cur_sample_info.pitched))
		return fdraw_str(0, 0, F_20_BOLD, "%s", note_name((high_string_pitch + 1024) / 2048));
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
		if (string_touched & (1 << voice_id))
			half_rectangle(x, OLED_HEIGHT - bar_height + 1, x + bar_width, OLED_HEIGHT);
		// outline
		hline(x, OLED_HEIGHT - bar_height - 2, x + bar_width, 0);
		vline(x - 1, OLED_HEIGHT - bar_height - 2, OLED_HEIGHT, 0);
		vline(x + bar_width, OLED_HEIGHT - bar_height - 2, OLED_HEIGHT, 0);
	}
}