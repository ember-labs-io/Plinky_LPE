#include "led_viz.h"
#include "hardware/adc_dac.h"
#include "hardware/leds.h"
#include "hardware/memory.h"
#include "pad_actions.h"
#include "settings_menu.h"
#include "synth/params.h"
#include "synth/sampler.h"
#include "synth/sequencer.h"
#include "synth/synth.h"

static u8 pulse_eighth;
static u8 pulse_half;
static u8 pulse;
static u8 pulse_2x;
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
			const Touch* c_touch = &get_synth_string(x)->cur_touch;
			if (c_touch->pos >> 8 == y) {
				float pressure = c_touch->pres * (1.f / 2048.f);
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

	switch (ui_mode) {
	case UI_SAMPLE_EDIT:
		sampler_leds(pulse_half, pulse);
		return;
	case UI_SETTINGS_MENU:
		settings_menu_leds(pulse_half);
		return;
	default:
		break;
	}

	float* next_wave;
	precalc_waves(&next_wave);

	// prepare pitch calc
	s32 cv_pitch = adc_get_smooth(ADC_S_PITCH);

	for (u8 x = 0; x < 8; ++x) {
		// prepare sample points
		int sp0 = cur_sample_info.splitpoints[x];
		int sp1 = (x < 7) ? cur_sample_info.splitpoints[x + 1] : cur_sample_info.samplelen;

		Scale scale = param_index_poly(PP_SCALE, x);
		u8 steps_in_scale = scale_table[scale][0];
		s16 start_step = step_at_string(x, scale);
		// quantized cv offset
		if (sys_params.cv_quant == CVQ_SCALE)
			start_step += (PITCH_TO_SEMIS(cv_pitch) * steps_in_scale + 6) / 12;

		// Find first pad that lands on step % steps_in_scale == 0
		s16 first_y = ((-start_step % steps_in_scale) + steps_in_scale) % steps_in_scale;

		// Draw all occurrences within 8-pad range
		u8 root_k[8] = {};
		for (s16 y = first_y; y < 8; y += steps_in_scale)
			root_k[7 - y] = 96;

		for (u8 y = 0; y < 8; ++y) {
			u8 k = root_k[y];

			// draw wave
			k = maxi(k, clampi((int)((next_wave[x + y * 8]) * 64.f) - 20, 0, 128));

			// draw finger press
			const Touch* c_touch = &get_synth_string(x)->cur_touch;
			if (c_touch->pos / 256 == y)
				k = maxi(k, mini(c_touch->pres / 8, 255));

			// draw seq press
			k = maxi(k, seq_press_led(x, y));

			switch (ui_mode) {
			case UI_PTN_START:
			case UI_PTN_END:
				if (ptn_edit_active()) {
					k = seq_led(x, y, sync_pulse, true);
					break;
				}
				// fall thru
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
				if (USING_SAMPLER && !cur_sample_info.pitched) {
					int samp = sp0 + (((sp1 - sp0) * y) >> 3);
					u16 avg_peak = getwaveform4zoom(&cur_sample_info, samp / 1024, 3) & 15;
					k = maxi(k, avg_peak * 6);
				}
				// draw sequencer
				k = maxi(k, seq_led(x, y, sync_pulse, false));
				break;
			case UI_EDITING_A:
			case UI_EDITING_B:
				k = ui_editing_led(x, y, pulse_half);
				break;
			case UI_LOAD:
				k = ui_load_long_press_led(x, y, pulse_2x);
				if (k == 0)
					k = ui_load_led(x, y, pulse, pulse_half);
				break;
			default:
				break;
			}
			k = maxi(k, ext_audio_led(x, y));
			leds[x][y] = led_add_gamma(k);
		}
	}
}

static void draw_function_leds(void) {
	switch (ui_mode) {
	case UI_SAMPLE_EDIT:
		SampleInfo* s = &cur_sample_info;
		// clear
		for (u8 x = 0; x < 8; ++x)
			leds[8][x] = 0;
		// sample flags
		if (sampler_mode == SM_PREVIEW) {
			if (s->pitched)
				leds[8][FN_SHIFT_A] = 255;
			if (s->loop & 1)
				leds[8][FN_SHIFT_B] = 255;
		}
		// record pad
		leds[8][FN_RECORD] = sampler_mode == SM_PREVIEW     ? pulse       //
		                     : sampler_mode == SM_RECORDING ? 255         //
		                                                    : pulse_half; //
		break;
	// case UI_SETTINGS_MENU:
	// 	leds[8][FN_SHIFT_A] = 32 + (pulse_eighth >> 3);
	// 	leds[8][FN_SHIFT_B] = 32 + ((255 - pulse_eighth) >> 3);
	// 	// always light up the active shift state
	// 	if (function_pressed >= 0)
	// 		leds[8][function_pressed] = maxi(leds[8][function_pressed], 128);
	// 	break;
	default:
		param_function_leds(pulse_half); // shift a & b
		leds[8][FN_LOAD] = (ui_mode == UI_LOAD) ? 255 : 0;
		leds[8][FN_LEFT] = (ui_mode == UI_PTN_START) ? 255 : 0;
		leds[8][FN_RIGHT] = (ui_mode == UI_PTN_END) ? 255 : 0;
		leds[8][FN_CLEAR] = 0;
		leds[8][FN_RECORD] = seq_recording() ? 255 : 0;
		leds[8][FN_PLAY] = seq_flags.playing && !seq_flags.stop_at_next_step ? led_add_gamma(sync_pulse) : 0;

		// always light up the pressed function pad
		if (function_pressed > FN_NONE && function_pressed != FN_LOAD)
			leds[8][function_pressed] = maxi(leds[8][function_pressed], 128);
		break;
	}
}

void draw_led_visuals(void) {
	pulse_eighth = triangle(millis() / 8);
	pulse_half = triangle(millis() / 2);
	pulse = triangle(millis());
	pulse_2x = triangle(millis() * 2);
	pulse_8x = triangle(millis() * 8);
	sync_pulse = maxi(96, 255 - seq_substep(256 - 96));

	draw_main_leds();
	draw_function_leds();
}