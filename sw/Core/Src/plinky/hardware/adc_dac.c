#include "adc_dac.h"
#include "encoder.h"
#include "flash.h"
#include "gfx/gfx.h"
#include "leds.h"
#include "synth/params.h"
#include "touchstrips.h"

// these are defined in main.c
extern DAC_HandleTypeDef hdac1;
extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim6;
extern TIM_HandleTypeDef htim3;

#define ADC_CHANS 8
#define ADC_SAMPLES 8

#define ADC_CHANS 8
#define ADC_SAMPLES 8

#define NUM_CV_INS 6

ADC_DAC_Calib adc_dac_calib[NUM_ADC_DAC_ITEMS] = {
    // cv inputs
    {52100.f, 1.f / -9334.833333f}, // pitch
    {31716.f, 0.2f / -6548.1f},     // gate
    {31665.f, 0.2f / -6548.1f},     // X
    {31666.f, 0.2f / -6548.1f},     // Y
    {31041.f, 0.2f / -6548.1f},     // A
    {31712.f, 0.2f / -6548.1f},     // B

    // potentiometers seem to skew towards 0 slightly
    {32000.f, 1.05f / -32768.f}, // B knob
    {32000.f, 1.05f / -32768.f}, // A knob

    // cv outputs, volt/octave: 2048 per semitone
    {42490.f, (26620 - 42490) * (1.f / (2048.f * 12.f * 2.f))}, // pitch lo
    {42511.f, (26634 - 42511) * (1.f / (2048.f * 12.f * 2.f))}, // pitch hi
};

ADC_DAC_Calib* adc_dac_calib_ptr(void) {
	return adc_dac_calib;
}

// only global for calib and testjig - preferred local
u16 adc_buffer[ADC_CHANS * ADC_SAMPLES];

static ValueSmoother adc_smoother[ADC_CHANS];

void adc_dac_init(void) {
	// adc init
	for (s16 i = 0; i < ADC_CHANS * ADC_SAMPLES; ++i)
		adc_buffer[i] = 32768;
	HAL_ADC_Start_DMA(&hadc1, (uint32_t*)&adc_buffer, ADC_CHANS * ADC_SAMPLES);
	// dac init
	HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
	HAL_DAC_Start(&hdac1, DAC_CHANNEL_2);
	HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
	HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
	HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
	HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);
	// start both adc and dac
	HAL_TIM_Base_Start(&htim6);
}

u16 adc_get_raw(ADC_DAC_Index index) {
	u32 raw_value = 0;
	u16* src = adc_buffer + index;
	// gate input: get max value to better respond to short gates
	if (index == ADC_GATE) {
		for (u8 i = 0; i < ADC_SAMPLES; ++i) {
			raw_value = maxi(raw_value, *src);
			src += ADC_CHANS;
		}
		return raw_value;
	}
	// all other inputs: get average
	for (u8 i = 0; i < ADC_SAMPLES; ++i) {
		raw_value += *src;
		src += ADC_CHANS;
	}
	return raw_value / ADC_SAMPLES;
}

float adc_get_calib(ADC_DAC_Index index) { // only one use in arp.h
	return (adc_get_raw(index) - adc_dac_calib[index].bias) * adc_dac_calib[index].scale;
}

float adc_get_smooth(ADCSmoothIndex index) {
	if (index == ADC_S_PITCH) {
		s32 pitch = (s32)(adc_smoother[ADC_S_PITCH].y2 * (512.f * 12.f));
		// quantize pitch according to param
		if (param_index(P_CV_QUANT))
			pitch = (pitch + 256) & (~511);
		return pitch;
	}
	return adc_smoother[index].y2;
}

s32 apply_dac_pitch_calib(bool pitch_hi, s32 pitch_uncalib) {
	return (s32)((pitch_uncalib * adc_dac_calib[pitch_hi ? DAC_PITCH_CV_HI : DAC_PITCH_CV_LO].scale)
	             + adc_dac_calib[pitch_hi ? DAC_PITCH_CV_HI : DAC_PITCH_CV_LO].bias);
}

s32 get_dac_pitch_octave(bool pitch_hi) {
	return abs((s32)(adc_dac_calib[pitch_hi ? DAC_PITCH_CV_HI : DAC_PITCH_CV_LO].scale * (2048.f * 12.f)));
}

// same as general smooth_value(), but with faster constants
static void adc_smooth_value(ValueSmoother* s, float new_val) {
	// inspired by  https ://cytomic.com/files/dsp/DynamicSmoothing.pdf
	const static float sens = 10.f;
	float band = fabsf(s->y2 - s->y1);
	float g = minf(1.f, 0.1f + band * sens);
	s->y1 += (new_val - s->y1) * g;
	s->y2 += (s->y1 - s->y2) * g;
}

void adc_update_inputs(void) {
	// why don't we clamp in the calib stage? and why aren't all calls to value_calib clamped?
	adc_smooth_value(adc_smoother + ADC_S_A_CV, clampf(adc_get_calib(ADC_A_CV), -1.f, 1.f));
	adc_smooth_value(adc_smoother + ADC_S_B_CV, clampf(adc_get_calib(ADC_B_CV), -1.f, 1.f));
	adc_smooth_value(adc_smoother + ADC_S_X_CV, clampf(adc_get_calib(ADC_X_CV), -1.f, 1.f));
	adc_smooth_value(adc_smoother + ADC_S_Y_CV, clampf(adc_get_calib(ADC_Y_CV), -1.f, 1.f));
	// why are the knobs saved in 4/5 and not in ADC_A_KNOB / ADC_B_KNOB ?
	smooth_value(adc_smoother + ADC_S_A_KNOB, clampf(adc_get_calib(ADC_A_KNOB), -1.f, 1.f), 1.f);
	smooth_value(adc_smoother + ADC_S_B_KNOB, clampf(adc_get_calib(ADC_B_KNOB), -1.f, 1.f), 1.f);
	adc_smooth_value(adc_smoother + ADC_S_PITCH, cv_pitch_present() ? adc_get_calib(ADC_PITCH) : 0.f);
	// why do we do another mapping here, instead of incorporating this in cv calib?
	adc_smooth_value(adc_smoother + ADC_S_GATE,
	                 cv_gate_present() ? clampf(adc_get_calib(ADC_GATE) * 1.15f - 0.05f, 0.f, 1.f) : 1.f);
}

static void adc_dac_monitor(void) {
	// light up all leds
	for (u8 strip = 0; strip < 9; ++strip)
		for (u8 pad = 0; pad < 8; ++pad)
			leds[strip][pad] = 255;
	u16 saw = 128;
	s16 enc_press_count = -1;
	do {
		if (encoder_pressed) {
			// avoid registering a press when first entering the function
			if (enc_press_count >= 0)
				enc_press_count++;
		}
		else
			enc_press_count = 0;

		// display raw buffer values
		oled_clear();
		fdraw_str(0, 0, F_12, "A %d", adc_buffer[ADC_A_KNOB] / 256);
		fdraw_str(32, 0, F_12, "B %d", adc_buffer[ADC_B_KNOB] / 256);
		fdraw_str(64, 0, F_12, "G %d", adc_buffer[ADC_GATE] / 256);
		fdraw_str(96, 0, F_12, "P %d", adc_buffer[ADC_PITCH] / 256);
		fdraw_str(0, 16, F_12, "A %d", adc_buffer[ADC_A_CV] / 256);
		fdraw_str(32, 16, F_12, "B %d", adc_buffer[ADC_B_CV] / 256);
		fdraw_str(64, 16, F_12, "X %d", adc_buffer[ADC_X_CV] / 256);
		fdraw_str(96, 16, F_12, "Y %d", adc_buffer[ADC_Y_CV] / 256);
		oled_flip();
		HAL_Delay(20);

		// send test signals over cv
		saw += 256;
		send_cv_clock(saw < (16384 + 32768));          // 75% pulsewidth square
		send_cv_trigger(saw < 16384);                  // 25% pulsewidth square
		send_cv_pitch(true, (saw * 2) & 65535, false); // double speed saw
		send_cv_gate((saw * 2) & 65535);               // double speed saw
		send_cv_pitch(false, saw, false);              // single speed saw
		send_cv_pressure(saw);                         // single speed saw
	} while (encoder_pressed || enc_press_count <= 2);
}

// == CV == //

void send_cv_pitch(bool pitch_hi, s32 data, bool apply_calib) {
	if (apply_calib) {
		data = apply_dac_pitch_calib(pitch_hi, data);
		s32 octave = get_dac_pitch_octave(pitch_hi);
		for (s32 k = 0; k < 3; ++k)
			if (data > 65535)
				data -= octave;
			else
				break;
		for (s32 k = 0; k < 3; ++k)
			if (data < 0)
				data += octave;
			else
				break;
	}
	HAL_DAC_SetValue(&hdac1, pitch_hi ? DAC_CHANNEL_2 : DAC_CHANNEL_1, DAC_ALIGN_12B_L, clampi(data, 0, 65535));
}

void cv_calib(void) {
	const char* top_line = "Unplug all inputs. Use left 4 columns to adjust pitch cv outputs. Plug pitch lo output to "
	                       "pitch input when done.";
	const char* const bottom_lines[5] = {"touch column 1-4", "pitch lo = 0V/C0", "pitch lo = 2V/C2", "pitch hi = 0V/C0",
	                                     "pitch hi = 2V/C2"};
	// text scrolling
	s16 top_line_pos = 128;
	s16 top_line_width = str_width(F_16, top_line);
	u8 cur_frame = touch_frame;
	// track neutral cv in averages
	float adc_avgs[NUM_CV_INS][2];
	for (u8 i = 0; i < NUM_CV_INS; ++i)
		adc_avgs[i][0] = -1.f;
	// track pitch cable inserted
	bool pitch_present = cv_pitch_present();
	bool pitch_present_1back = pitch_present;
	bool pitch_present_2back = pitch_present;
	// edit pitch out with touches from 4 columns
	s8 last_touched_column = -1;
	float touch_start_pos[4] = {};
	float touch_start_cv[4] = {};
	s16 pres_1back[4] = {};
	float cv_out[4] = {
	    adc_dac_calib[DAC_PITCH_CV_LO].bias,
	    adc_dac_calib[DAC_PITCH_CV_LO].bias + adc_dac_calib[DAC_PITCH_CV_LO].scale * 2048.f * 24.f,
	    adc_dac_calib[DAC_PITCH_CV_HI].bias,
	    adc_dac_calib[DAC_PITCH_CV_HI].bias + adc_dac_calib[DAC_PITCH_CV_HI].scale * 2048.f * 24.f,
	};
	u8 cur_lo_column = 0;
	u8 cur_hi_column = 2;
	ValueSmoother cv_smoother;
	set_smoother(&cv_smoother, 0);
	// send neutral cv out
	send_cv_pitch(false, (s32)cv_out[cur_lo_column], false);
	send_cv_pitch(true, (s32)cv_out[cur_hi_column], false);

	// adapt pitch hi/lo output voltage with touch columns
	do {
		// encoder press activates adc/dac monitoring mode
		if (encoder_pressed)
			adc_dac_monitor();

		// show text on display
		top_line_pos--;
		if (top_line_pos < -top_line_width)
			top_line_pos = 128;
		oled_clear();
		drawstr_noright(top_line_pos, 0, F_16, top_line);
		draw_str(0, 20, F_12, bottom_lines[last_touched_column + 1]);
		if (last_touched_column >= 0)
			fdraw_str(-124, 22, F_8, "%d", (s32)cv_out[last_touched_column]);
		oled_flip();

		// wait for touchstrips to update
		while (touch_frame == cur_frame)
			__asm__ volatile("" ::: "memory");
		cur_frame = touch_frame;

		// use touch to change cv_out values
		for (u8 column = 0; column < 4; ++column) {
			Touch* touch = get_touch_prev(column, 0);
			if (touch->pres >= 500) {
				// touch start
				if (pres_1back[column] < 500) {
					touch_start_pos[column] = touch->pos;
					touch_start_cv[column] = cv_out[column];
					set_smoother(&cv_smoother, cv_out[column]);
					last_touched_column = column;
				}
				// only edit one column at a time
				if (column == last_touched_column) {
					// adapt cv out from touch
					float delta_pos = deadzone(touch->pos - touch_start_pos[column], 64.f);
					float goal_cv = clampf(touch_start_cv[column] + delta_pos * 0.25f, 0.f, 65535.f);
					smooth_value(&cv_smoother, goal_cv, 5);
					cv_out[column] = cv_smoother.y2;
					if (column < 2)
						cur_lo_column = column;
					else
						cur_hi_column = column;
				}
			}
			pres_1back[column] = touch->pres;
		}

		// monitor updated cv values
		send_cv_pitch(false, (s32)cv_out[cur_lo_column], false);
		send_cv_pitch(true, (s32)cv_out[cur_hi_column], false);

		// while calibrating pitch out, track average neutral values on the cv inputs
		for (u8 i = 0; i < NUM_CV_INS; ++i) {
			u32 tot = 0;
			for (u8 j = 0; j < ADC_SAMPLES; ++j)
				tot += adc_buffer[j * ADC_CHANS + i];
			tot /= ADC_SAMPLES;
			if (adc_avgs[i][0] < 0)
				adc_avgs[i][0] = adc_avgs[i][1] = tot;
			else {
				adc_avgs[i][0] += (tot - adc_avgs[i][0]) * 0.05f;
				adc_avgs[i][1] += (adc_avgs[i][0] - adc_avgs[i][1]) * 0.05f;
			}
		}

		// update the leds
		for (u8 column = 0; column < 9; ++column) {
			for (u8 y = 0; y < 8; ++y) {
				u8 k = column < 4 ? triangle(y * 64 - (s32)cv_out[column] / 4) / 4 : 128;
				leds[column][y] = led_add_gamma(((column == last_touched_column) ? 255 : 128) - k);
			}
		}

		// pitch cable detection
		pitch_present_2back = pitch_present_1back;
		pitch_present_1back = pitch_present;
		pitch_present = cv_pitch_present();

		// loop until pitch cable is inserted
	} while (!(pitch_present && !pitch_present_2back));

	// save calibrated pitch out values
	adc_dac_calib[DAC_PITCH_CV_LO].bias = cv_out[0];
	adc_dac_calib[DAC_PITCH_CV_LO].scale = (cv_out[1] - cv_out[0]) / (2048.f * 24.f);
	adc_dac_calib[DAC_PITCH_CV_HI].bias = cv_out[2];
	adc_dac_calib[DAC_PITCH_CV_HI].scale = (cv_out[3] - cv_out[2]) / (2048.f * 24.f);

	// save average neutral adc value to all cv inputs
	for (u8 i = 0; i < NUM_CV_INS; ++i)
		adc_dac_calib[i].bias = adc_avgs[i][1];

	// wait for them to plug the other end in
	oled_clear();
	draw_str(0, 4, F_12_BOLD, "Checking for pitch");
	draw_str(0, 16, F_12_BOLD, "loopback cable");
	oled_flip();
	HAL_Delay(1500);

	u32 totals[2] = {0};
	do {
		for (u8 i = 0; i < 2; ++i) {
			send_cv_pitch(false, (s32)cv_out[i], false);
			send_cv_pitch(true, (s32)cv_out[i + 2], false);
			HAL_Delay(50);
			u32 total = 0;
			for (u8 j = 0; j < ADC_SAMPLES; ++j)
				total += adc_buffer[j * ADC_CHANS + ADC_PITCH];
			total /= ADC_SAMPLES;
			totals[i] = total;
		}
	} while (abs(totals[0] - totals[1]) <= 5000);

	// start pitch in auto-calib
	oled_clear();
	draw_str(0, 4, F_12_BOLD, "Auto-calibrating");
	draw_str(0, 16, F_12_BOLD, "pitch cv input...");
	oled_flip();
	HAL_Delay(1000); // give the user some time to fully plug in the jack

	for (u8 i = 0; i < 2; i++) {
		// in reality, pitch in calib works from both pitch lo and pitch hi outputs
		send_cv_pitch(false, (s32)cv_out[i], false);
		send_cv_pitch(true, (s32)cv_out[i + 2], false);
		HAL_Delay(50);
		u32 tot = 0;
		// take 256 measurements
		for (u16 m = 0; m < 256; ++m) {
			for (u8 smp = 0; smp < ADC_SAMPLES; ++smp)
				tot += adc_buffer[smp * ADC_CHANS];
			HAL_Delay(2);
		}
		tot /= ADC_SAMPLES * 256;
		if (i == 0)
			adc_dac_calib[ADC_PITCH].bias = tot;
		else
			adc_dac_calib[ADC_PITCH].scale = 2.f / (minf(-0.00001f, tot - adc_dac_calib[ADC_PITCH].bias));
	}

	// save data
	flash_write_calib(FLASH_CALIB_COMPLETE);

	// done
	oled_clear();
	draw_str(0, 4, F_12_BOLD, "Calibration complete!");
	draw_str(0, 16, F_12_BOLD, "Unplug pitch cable");
	oled_flip();

	// wait for unplug
	while (cv_pitch_present())
		HAL_Delay(1);

	HAL_Delay(500);
	oled_flip_with_buffer(logo_buffer);
	leds_bootswish();
}