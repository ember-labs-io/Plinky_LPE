#include "synth.h"
#include "arp.h"
#include "audio_tools.h"
#include "data/tables.h"
#include "gfx/gfx.h"
#include "hardware/adc_dac.h"
#include "hardware/ram.h"
#include "pitch_tools.h"
#include "sampler.h"
#include "strings.h"

Voice voices[NUM_VOICES];

static bool cv_trig_high = false;   // should cv trigger be high?
static s32 cv_gate_value;           // cv gate value
static bool got_high_pitch = false; // did we save a high pitch?
static s32 high_string_pitch = 0;   // pitch on highest touched string
static bool got_low_pitch = false;  // did we save a low pitch?
static s32 low_string_pitch = 0;    // pitch on lowest touched string
static u16 synth_max_pres = 0;      // highest pressure seen

static void osc_generation_init(void) {
	cv_trig_high = false;
	got_high_pitch = false;
	cv_gate_value = 0;
	got_low_pitch = false;
	synth_max_pres = 0;
}

// take string values, calculate osc pitches and write them to voice
void generate_oscs(u8 string_id, Voice* voice) {
	float pres_scaled = get_string_touch(string_id)->pres * 1.f / TOUCH_MAX_POS;

	// rj: cv_gate_value is in practice another expression of the maximum pressure over all strings, which goes against
	// eurorack conventions (gates are generally high/low, 5V/0V) and I'm also not sure what the added value of this is,
	// since we already have an expression of the max pressure on the pressure CV out - should we make gate out binary?
	cv_gate_value = maxi(cv_gate_value, (int)(pres_scaled * 65536.f));

	// only recalculate the oscillator pitches if the string is touched
	if (pres_scaled < 0.001f)
		return;

	u8 mask = 1 << string_id;
	s32 note_pitch = 0;   // pitch offset caused by the played note
	s32 fine_pitch = 0;   // pitch offset caused by micro_tone / spread
	s32 osc_pitch = 0;    // resulting pitch of current oscillator
	s32 summed_pitch = 0; // summed pitch of four oscillators

	// these only get used by touch
	Scale scale = NUM_SCALES;
	s32 cv_pitch_offset = 0;
	s8 cv_step_offset = 0;

	// saving string values that are the same for all oscillators

	s32 base_pitch = 12 *
	                 // pitch from arp and the octave parameter
	                 (((arp_oct_offset + param_index_poly(P_OCT, string_id)) << 9)
	                  // pitch from the pitch parameter
	                  + (param_val_poly(P_PITCH, string_id) >> 7));
	// pitch from interval parameter
	s32 osc_interval_pitch = (param_val_poly(P_INTERVAL, string_id) * 12) >> 7;
	// scale step from rotate parameter
	s8 string_step_offset = param_index_poly(P_DEGREE, string_id);

	// for midi
	if ((midi_pitch_override & mask) && !(midi_suppress & mask)) {
		note_pitch = midi_note_to_pitch_offset(midi_note[string_id], midi_channel[string_id]);
	}
	// for touch
	else {
		scale = param_index_poly(P_SCALE, string_id);
		if (scale >= NUM_SCALES)
			scale = 0;

		// add step offset caused by scale & column (stride) parameters
		string_step_offset += scale_steps_at_string(scale, string_id);

		// cv
		s32 cv_pitch = adc_get_smooth(ADC_S_PITCH);
		if (param_index(P_CV_QUANT) == CVQ_SCALE)
			cv_step_offset = pitch_to_scale_steps(cv_pitch, scale); // quantized cv
		else
			cv_pitch_offset = cv_pitch; // unquantized cv
	}

	// we discard the two highest and lowest positions and use elements 2 through 5 to generate our oscillator pitches
	Touch* s_touch_sort = sorted_string_touch_ptr(string_id) + 2;

	// loop through oscillators
	for (u8 osc_id = 0; osc_id < OSCS_PER_VOICE; ++osc_id) {
		// for midi
		if ((midi_pitch_override & mask) && !(midi_suppress & mask)) {
			// generate pitch spread
			fine_pitch = (osc_id - 2) * 64 * param_val_poly(P_MICROTONE, string_id) >> 16;
		}
		// for touch
		else {
			u16 position = s_touch_sort->pos; // touch position
			u8 pad_y = 7 - (position >> 8);   // pad on string
			// pitch at step + cv
			note_pitch = pitch_at_step(scale, string_step_offset + pad_y + cv_step_offset) + cv_pitch_offset;
			// detuning scaled by microtune param
			s16 fine_pos = 127 - (position & 255); // offset from pad center
			s32 micro_tune = 64 + param_val_poly(P_MICROTONE, string_id);
			fine_pitch = (fine_pos * micro_tune) >> 14;
		}

		// calculate resulting pitch
		osc_pitch =
		    // octave and pitch parameters
		    base_pitch +
		    // pitch from scale step / midi note
		    note_pitch +
		    // pitch from osc interval parameter
		    ((osc_id & 1) ? osc_interval_pitch : 0) +
		    // pitch from micro_tone / pitch spread
		    fine_pitch;

		// save values
		summed_pitch += osc_pitch;
		voice->osc[osc_id].pitch = osc_pitch;
		voice->osc[osc_id].goal_phase_diff =
		    maxi(65536, (s32)(table_interp(pitches, osc_pitch + PITCH_BASE) * (65536.f * 128.f)));
		++s_touch_sort;
	}

	// these are saving respectively the lowest and highest string that are pressed
	if (!got_low_pitch) {
		low_string_pitch = summed_pitch;
		got_low_pitch = true;
	}
	high_string_pitch = summed_pitch;
	got_high_pitch = true;
	// the outgoing midi note is generated from oscillator pitch
	set_midi_goal_note(string_id, quad_pitch_to_midi_note(summed_pitch));
}

static float update_envelope(u8 voice_id, Voice* voice) {
	u8 mask = 1 << voice_id;
	float goal_lpg = 0.f;

	// calc goal lpg
	if (string_touched & mask) {
		float sens = param_val_poly(P_ENV_LVL1, voice_id) * (2.f / 65536.f);
		goal_lpg = get_string_touch(voice_id)->pres * 1.f / TOUCH_MAX_POS * sens * sens;
		if (goal_lpg < 0.f)
			goal_lpg = 0.f;
		goal_lpg *= 1.f + ((voice->osc[2].pitch - 43000) * (1.f / 65536.f)); // pitch compensation
	}

	// retrieve envelope
	bool is_sample_preview = ui_mode == UI_SAMPLE_EDIT;
	const float attack = is_sample_preview ? 0.5f : lpf_k((param_val_poly(P_ATTACK1, voice_id)));
	const float decay = is_sample_preview ? 1.f : lpf_k((param_val_poly(P_DECAY1, voice_id)));
	const float sustain = is_sample_preview ? 1.f : squaref(param_val_poly(P_SUSTAIN1, voice_id) * (1.f / 65536.f));
	const float release = is_sample_preview ? 0.5f : lpf_k((param_val_poly(P_RELEASE1, voice_id)));

	float env_lvl = voice->env1_lvl;

	// new touch: start new envelope
	if (env_trig_mask & mask) {
		env_lvl *= sustain;
		voice->env1_decaying = false;
		voice->env1_peak = goal_lpg;
		cv_trig_high = true; // send cv trigger
	}

	if (goal_lpg <= 0.f) // no pressure => release phase (aka not decaying)
		voice->env1_decaying = false;
	else if (voice->env1_decaying) // in decay phase => aim for sustain level
		goal_lpg *= sustain;

	// apply envelope
	float lpg_diff = goal_lpg - env_lvl;
	lpg_diff *= (lpg_diff > 0.f) ? attack : goal_lpg ? decay : release;
	env_lvl += lpg_diff;

	// release phase
	if (goal_lpg <= 0.f)
		voice->env1_norm = voice->env1_peak == 0 ? 0 : env_lvl / voice->env1_peak;
	// decay/sustain phase
	else if (voice->env1_decaying) {
		voice->env1_norm = 1;
		voice->env1_peak = env_lvl;
	}
	// attack phase
	else {
		voice->env1_norm = env_lvl / goal_lpg;
		if (goal_lpg > voice->env1_peak)
			voice->env1_peak = goal_lpg;
	}
	// we hit the peak! time to decay
	if (env_lvl > goal_lpg * 0.95f)
		voice->env1_decaying = true;
	// constrain to max 1.0
	if (env_lvl > 1.f) {
		env_lvl = 1.f;
		voice->env1_decaying = true;
	}
	return env_lvl;
}

static void apply_subtractive_lpg_noise(u8 voice_id, Voice* voice, float goal_lpg, float noise_diff, float drive,
                                        float resonance, u32* dst) {
	float glide = lpf_k(param_val_poly(P_GLIDE, voice_id) >> 2) * (0.5f / SAMPLES_PER_TICK);

	// oscillator shape
	s32 osc_shape_raw = cur_preset.params[P_SHAPE][0]; // unmodulated
	s32 osc_shape = param_val_poly(P_SHAPE, voice_id);
	// raw from -8 to 7, snap to 0 => supersaw
	if (osc_shape_raw >= -8 && osc_shape_raw < 8)
		osc_shape = 0;
	// raw above 7, clamp to positive => wavetable
	else if (osc_shape_raw > 0)
		osc_shape = clampi(osc_shape, 1, 65535);
	// raw below -8, clamp to negative => pulse wave
	else
		osc_shape = clampi(osc_shape, -65535, -1);

	// two loops handling two oscillators each
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
			{
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
	u8 mask = 1 << voice_id;
	Voice* voice = &voices[voice_id];
	Touch* s_touch = get_string_touch(voice_id);

	// track max pressure
	if (s_touch->pres > synth_max_pres)
		synth_max_pres = s_touch->pres;

	// midi note is released but still playing
	// and it's being suppressed by touch/latch/seq or its release phase has rung out
	if (((midi_pitch_override & mask) && !(midi_pressure_override & mask))
	    && ((midi_suppress & mask) || (voice->env1_lvl < 0.001f)))
		// disable pitch override, this truly turns off the note
		midi_pitch_override &= ~mask;

	// generate oscillators
	generate_oscs(voice_id, voice);

	// apply envelope
	float goal_lpg = update_envelope(voice_id, voice);

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

	// apply low pass gate and noise
	if (using_sampler())
		apply_sample_lpg_noise(voice_id, voice, goal_lpg, noise_diff, drive, dst);
	else
		apply_subtractive_lpg_noise(voice_id, voice, goal_lpg, noise_diff, drive, resonance, dst);
}

// send cv values resulting from oscillator generation
static void send_cvs(void) {
	send_cv_trigger(cv_trig_high);
	if (got_high_pitch)
		send_cv_pitch(true, high_string_pitch, true);
	send_cv_gate(mini(cv_gate_value, 65535));
	if (got_low_pitch)
		send_cv_pitch(false, low_string_pitch, true);
	send_cv_pressure(synth_max_pres * 8);
}

void handle_synth_voices(u32* dst) {
	osc_generation_init();
	for (u8 voice_id = 0; voice_id < NUM_VOICES; ++voice_id)
		run_voice(voice_id, dst);
	send_cvs();
	if (using_sampler())
		sampler_playing_tick();
}

u8 draw_high_note(void) {
	if (synth_max_pres > 1 && !(using_sampler() && !cur_sample_info.pitched))
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