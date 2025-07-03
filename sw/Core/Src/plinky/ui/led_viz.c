#include "led_viz.h"
#include "hardware/adc_dac.h"
#include "hardware/leds.h"
#include "hardware/ram.h"
#include "synth/arp.h"
#include "synth/params.h"
#include "synth/pitch_tools.h"
#include "synth/sampler.h"
#include "synth/sequencer.h"
#include "ui/shift_states.h"

static u8 pulse_half;
static u8 pulse;
static u8 pulse_8x;
static u8 sync_pulse;

// calculate waves spreading out from touches and audio in
static void precalc_waves(float** next_wave_ptr) {
	const static float life_damping = 0.91f;
	const static float life_force = 0.25f;
	const static float life_input_power = 6.f;
	static u8 frame = 0;
	static float surf[2][8][8];

	float* prev_wave = surf[frame & 1][0];
	frame++;
	*next_wave_ptr = surf[frame & 1][0];
	float* next_wave = *next_wave_ptr;

	u8 i = 0;
	for (u8 y = 0; y < 8; ++y) {
		for (u8 x = 0; x < 8; ++x, ++i) {
			Touch* curfinger = get_string_touch(x);
			float corners = 0.f;
			float edges = 0.f;
			if (x > 0) {
				if (y > 0)
					corners += prev_wave[i - 9];
				edges += prev_wave[i - 1];
				if (y < 7)
					corners += prev_wave[i + 7];
			}
			if (y > 0)
				edges += prev_wave[i - 8];
			if (y < 7)
				edges += prev_wave[i + 8];
			if (x < 7) {
				if (y > 0)
					corners += prev_wave[i - 7];
				edges += prev_wave[i + 1];
				if (y < 7)
					corners += prev_wave[i + 9];
			}
			float target = corners * (1.f / 12.f) + edges * (1.f * 2.f / 12.f);
			target *= life_damping;
			if (curfinger->pos >> 8 == y) {
				float pressure = curfinger->pres * (1.f / 2048.f);
				target = lerp(target, life_input_power, clampf(pressure * 2.f, 0.f, 1.f));
			}
			float pos = prev_wave[i];
			float accel = (target - pos) * life_force;
			float vel =
			    (prev_wave[i] - next_wave[i]) * life_damping + accel; // here next_wave is really 'prev prev wave'
			next_wave[i] = pos + vel;
		}
	}
}

static void draw_main_leds(void) {
	update_peak_hist();

	if (ui_mode == UI_SAMPLE_EDIT)
		sampler_leds(pulse_half, pulse);
	else {
		float* next_wave;
		precalc_waves(&next_wave);

		// prepare pitch calc
		int cv_pitch = adc_get_smooth(ADC_S_PITCH);

		for (u8 x = 0; x < 8; ++x) {
			// prepare press
			Touch* s_touch = get_string_touch(x);

			// prepare sample points
			int sp0 = cur_sample_info.splitpoints[x];
			int sp1 = (x < 7) ? cur_sample_info.splitpoints[x + 1] : cur_sample_info.samplelen;

			// root pitch calcs
			s8 root = param_val_poly(P_DEGREE, x);
			u32 scale = param_val_poly(P_SCALE, x);
			if (scale >= NUM_SCALES)
				scale = 0;
			if (param_val(P_CV_QUANT) == CVQ_SCALE) {
				int steps = ((cv_pitch / 512) * scale_table[scale][0] + 1) / 12;
				root += steps;
			}
			root += scale_steps_at_string(scale, x);

			for (u8 y = 0; y < 8; ++y) {
				u8 k = 0;

				// draw wave
				k = clampi((int)((next_wave[x + y * 8]) * 64.f) - 20, 0, 128);

				// draw finger press
				if (s_touch->pos / 256 == y)
					k = maxi(k, mini(s_touch->pres / 8, 255));

				// draw seq press
				k = maxi(k, seq_press_led(x, y));

				switch (ui_mode) {
				case UI_DEFAULT:
					// draw left column value editor
					if (x == 0) {
						s16 edit_k = value_editor_column_led(y);
						if (edit_k >= 0) {
							k = edit_k;
							// done
							break;
						}
					}
					// pulse selected param
					if (is_snap_param(x, y))
						k = pulse_half;
					// map loudness of each 8th of a slice to a pad
					if (using_sampler() && !cur_sample_info.pitched) {
						int samp = sp0 + (((sp1 - sp0) * y) >> 3);
						u16 avg_peak = getwaveform4zoom(&cur_sample_info, samp / 1024, 3) & 15;
						k = maxi(k, avg_peak * 6);
					}
					// draw root notes
					else {
						s32 pitch = (pitch_at_step(scale, (7 - y) + root));
						pitch %= 12 * 512;
						if (pitch < 0)
							pitch += 12 * 512;
						if (pitch < 256)
							k = maxi(k, 96);
					}
					// draw sequencer
					k = maxi(k, seq_led(x, y, sync_pulse));
					break;
				case UI_EDITING_A:
				case UI_EDITING_B:
					k = ui_editing_led(x, y, pulse_half);
					break;
				case UI_PTN_START:
				case UI_PTN_END:
					k = seq_led(x, y, sync_pulse);
					break;
				case UI_LOAD:
					k = ui_load_led(x, y, pulse_8x);
					break;
				default:
					break;
				}
				k = maxi(k, ext_audio_led(x, y));
				leds[x][y] = led_add_gamma(k);
			}
		}
	}
}

static void draw_shift_leds(void) {
	switch (ui_mode) {
	case UI_SAMPLE_EDIT:
		SampleInfo* s = &cur_sample_info;
		// clear
		for (u8 x = 0; x < 8; ++x)
			leds[8][x] = 0;
		// sample flags
		if (sampler_mode == SM_PREVIEW) {
			if (s->pitched)
				leds[8][SS_SHIFT_A] = 255;
			if (s->loop & 1)
				leds[8][SS_SHIFT_B] = 255;
		}
		// record pad
		leds[8][SS_RECORD] = sampler_mode == SM_PREVIEW     ? pulse       //
		                     : sampler_mode == SM_RECORDING ? 255         //
		                                                    : pulse_half; //
		break;
	default:
		param_shift_leds(pulse_half); // shift a & b
		leds[8][SS_LOAD] = (ui_mode == UI_LOAD) ? 255 : 0;
		leds[8][SS_LEFT] = (ui_mode == UI_PTN_START) ? 255 : 0;
		leds[8][SS_RIGHT] = (ui_mode == UI_PTN_END) ? 255 : 0;
		leds[8][SS_CLEAR] = 0;
		leds[8][SS_RECORD] = seq_recording() ? 255 : 0;
		leds[8][SS_PLAY] = seq_flags.playing && !seq_flags.stop_at_next_step ? led_add_gamma(sync_pulse) : 0;

		// always light up the active shift state
		if (shift_state >= 0)
			leds[8][shift_state] = maxi(leds[8][shift_state], 128);
		break;
	}
}

void draw_led_visuals(void) {
	pulse_half = triangle(millis() / 2);
	pulse = triangle(millis());
	pulse_8x = triangle(millis() * 8);
	sync_pulse = maxi(96, 255 - seq_substep(256 - 96));

	draw_main_leds();
	draw_shift_leds();
}