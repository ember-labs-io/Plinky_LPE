#include "adc_dac.h"
#include "encoder.h"
#include "gfx/gfx.h"
#include "leds.h"
#include "memory.h"
#include "synth/params.h"
#include "synth/synth.h"
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

#define CV_GATE_THRESH 6000
#define CV_PRES_THRESH 24000
#define CV_THRESH_HYST 600

typedef struct CvTouch {
	bool touched;
	u8 string_id;
	u8 start_velocity;
	u8 note_number;
	s32 note_offset_pitch;
	u16 position;
	u16 pressure;
} CvTouch;

ADC_DAC_Calib adc_dac_calib[NUM_ADC_DAC_ITEMS] = {
    // cv inputs
    {52100.f, 1.f / -9334.833333f}, // pitch (roughly -2.7V to +5.3V)
    {31716.f, 0.2f / -6548.1f},     // gate (roughly +/- 6V)
    {31665.f, 0.2f / -6548.1f},     // X (+/- 5V)
    {31666.f, 0.2f / -6548.1f},     // Y (+/- 5V)
    {31041.f, 0.2f / -6548.1f},     // A (+/- 5V)
    {31712.f, 0.2f / -6548.1f},     // B (+/- 5V)

    // potentiometers seem to skew towards 0 slightly
    {32000.f, 1.05f / -32768.f}, // B knob
    {32000.f, 1.05f / -32768.f}, // A knob

    // cv outputs, volt/octave: 2048 per semitone
    {42490.f, (26620 - 42490) * (1.f / (2048.f * 12.f * 2.f))}, // pitch lo (-3V to 5.5V)
    {42511.f, (26634 - 42511) * (1.f / (2048.f * 12.f * 2.f))}, // pitch hi (-3V to 5.5V)
};

ADC_DAC_Calib* adc_dac_calib_ptr(void) {
	return adc_dac_calib;
}

static u16 adc_buffer[ADC_CHANS * ADC_SAMPLES];

static ValueSmoother adc_smoother[ADC_CHANS];

static CvTouch cv_touch = {};

void init_adc_dac(void) {
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

static bool cv_gate_present(void) {
	return HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_8) == GPIO_PIN_RESET;
}

static float adc_get_calib(ADC_DAC_Index index) {
	return (adc_get_raw(index) - adc_dac_calib[index].bias) * adc_dac_calib[index].scale;
}

float adc_get_smooth(ADCSmoothIndex index) {
	// make sure the knobs reach the full 100%, apply deadzone
	if (index == ADC_S_A_KNOB || index == ADC_S_B_KNOB)
		return deadzone_scaled(clampf(adc_smoother[index].y2 * 1.0001f, -1.f, 1.f), 0.01f);
	return adc_smoother[index].y2;
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

void adc_dac_tick(void) {
	adc_smooth_value(adc_smoother + ADC_S_A_CV, clampf(adc_get_calib(ADC_A_CV), -1.f, 1.f));
	adc_smooth_value(adc_smoother + ADC_S_B_CV, clampf(adc_get_calib(ADC_B_CV), -1.f, 1.f));
	adc_smooth_value(adc_smoother + ADC_S_X_CV, clampf(adc_get_calib(ADC_X_CV), -1.f, 1.f));
	adc_smooth_value(adc_smoother + ADC_S_Y_CV, clampf(adc_get_calib(ADC_Y_CV), -1.f, 1.f));
	smooth_value(adc_smoother + ADC_S_A_KNOB, clampf(adc_get_calib(ADC_A_KNOB), -1.f, 1.f), 1.f);
	smooth_value(adc_smoother + ADC_S_B_KNOB, clampf(adc_get_calib(ADC_B_KNOB), -1.f, 1.f), 1.f);
	adc_smooth_value(adc_smoother + ADC_S_PITCH, cv_pitch_present() ? adc_get_calib(ADC_PITCH) : 0.f);
	adc_smooth_value(adc_smoother + ADC_S_GATE,
	                 cv_gate_present() ? clampf(adc_get_calib(ADC_GATE) * 1.15f - 0.05f, 0.f, 1.f) : 0.f);

	// cv touch
	u16 gate_thresh =
	    (sys_params.cv_gate_in_is_pressure ? CV_PRES_THRESH : CV_GATE_THRESH) + (cv_touch.touched ? CV_THRESH_HYST : 0);
	bool gate_high = adc_get_raw(ADC_GATE) < gate_thresh;
	bool new_touch = false;

	// 1V/octave, add three octaves to map 0V to C2
	u16 cv_pitch = clampi((adc_get_smooth(ADC_S_PITCH) + 3.f) * PITCH_PER_OCT, 0, MAX_PITCH);

	static u8 prev_map_string = 255;
	// new touch
	if (!cv_touch.touched && gate_high) {
		u8 map_string = find_string_for_pitch(cv_pitch, 0, NUM_STRINGS);
		// found a string to map to => valid new touch
		if (map_string != 255 && map_string == prev_map_string) {
			cv_touch.string_id = map_string;
			cv_touch.touched = true;
			cv_touch.start_velocity =
			    maxi((sys_params.cv_gate_in_is_pressure ? adc_get_smooth(ADC_S_GATE) : 1) * 255, 1);
			new_touch = true;
		}
		prev_map_string = map_string;
	}
	// not a new touch
	else {
		prev_map_string = 255;

		// touch release
		if (cv_touch.touched && !gate_high)
			cv_touch.touched = false;
	}

	// save touch data
	if (cv_touch.touched) {
		// recalculate note number and string position
		if (new_touch || !sys_params.mpe_out) {
			u16 note_pitch = sys_params.cv_quant == CVQ_SCALE       ? quant_pitch_to_scale(cv_pitch, cv_touch.string_id)
			                 : sys_params.cv_quant == CVQ_CHROMATIC ? ROUND_PITCH_TO_SEMIS(cv_pitch)
			                                                        : cv_pitch;
			cv_touch.note_number = PITCH_TO_NOTE_NR(note_pitch);
			cv_touch.position = string_position_from_pitch(cv_touch.string_id, note_pitch);
		}
		// save exact pitch and pressure
		cv_touch.note_offset_pitch = cv_pitch - NOTE_NR_TO_PITCH(cv_touch.note_number);
		cv_touch.pressure = (sys_params.cv_gate_in_is_pressure ? adc_get_smooth(ADC_S_GATE) : 1) * TOUCH_FULL_PRES;
	}
}

static void send_pitch_cv_raw(s32 lo_value, s32 hi_value) {
	HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_L, clampi(lo_value, 0, 65535));
	HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_2, DAC_ALIGN_12B_L, clampi(hi_value, 0, 65535));
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
		send_cv_clock(saw < (16384 + 32768));      // 75% pulsewidth square
		send_cv_trigger(saw < 16384);              // 25% pulsewidth square
		send_cv_gate(saw < 32768);                 // 50% pulsewidth square
		send_pitch_cv_raw(saw, (saw * 2) & 65535); // single & double speed saw
		send_cv_pressure(saw);                     // single speed saw
	} while (encoder_pressed || enc_press_count <= 2);
}

// == CV == //

// can only be called from the sequencer
bool new_seq_cv_gate(void) {
	if (!cv_gate_present())
		return false;

	// apply hysteresis
	static bool prev_gate = true;
	float thresh = prev_gate ? 0.01f : 0.02f;
	bool cur_gate = adc_get_calib(ADC_GATE) > thresh;
	bool new_gate = cur_gate && !prev_gate;
	prev_gate = cur_gate;
	return new_gate;
}

bool cv_try_get_touch(u8 string_id, s16* pressure, s16* position, u8* note_number, s32* note_offset_pitch,
                      u8* start_velocity) {
	// not pressed => exit
	if (!cv_touch.touched || cv_touch.string_id != string_id)
		return false;

	*pressure = cv_touch.pressure;
	// for cv, position is only used to light up the led at the correct pad
	*position = cv_touch.position;
	*note_number = cv_touch.note_number;
	*start_velocity = cv_touch.start_velocity;
	*note_offset_pitch = cv_touch.note_offset_pitch;

	return true;
}

void send_cv_pitch(bool pitch_hi, u16 pitch) {
	// shift three octaves so 0V = C2
	s32 pitch_4x = (pitch - 3 * PITCH_PER_OCT) << 2;

	// apply calibration
	ADC_DAC_Calib* calib = &adc_dac_calib[pitch_hi ? DAC_PITCH_CV_HI : DAC_PITCH_CV_LO];
	s32 cv_pitch_4x = pitch_4x * calib->scale + calib->bias;

	// shift octave to keep pitch within bounds
	u16 octave_size = abs((PITCH_PER_OCT << 2) * calib->scale);
	while (cv_pitch_4x > UINT16_MAX)
		cv_pitch_4x -= octave_size;
	while (cv_pitch_4x < 0)
		cv_pitch_4x += octave_size;

	HAL_DAC_SetValue(&hdac1, pitch_hi ? DAC_CHANNEL_2 : DAC_CHANNEL_1, DAC_ALIGN_12B_L, cv_pitch_4x);
}

void cv_calib(void) {
	calib_mode = CALIB_CV;
	const char* top_line = "Unplug all inputs. Use left 4 columns to adjust pitch cv outputs. Plug pitch lo output to "
	                       "pitch input when done.";
	const char* const bottom_lines[5] = {"touch column 1-4", "pitch lo = 0V/C2", "pitch lo = 2V/C4", "pitch hi = 0V/C2",
	                                     "pitch hi = 2V/C4"};
	// text scrolling
	s16 top_line_pos = 128;
	s16 top_line_width = str_width(F_16, top_line);
	u8 cur_frame = get_touch_frame();
	;
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
	send_pitch_cv_raw((s32)cv_out[cur_lo_column], (s32)cv_out[cur_hi_column]);

	// adapt pitch hi/lo output voltage with touch columns
	send_cv_gate(true);
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
		while (get_touch_frame() == cur_frame)
			__asm__ volatile("" ::: "memory");
		cur_frame = get_touch_frame();

		// use touch to change cv_out values
		for (u8 column = 0; column < 4; ++column) {
			const Touch* touch = get_touch(column, 0);
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
		send_pitch_cv_raw((s32)cv_out[cur_lo_column], (s32)cv_out[cur_hi_column]);

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
	send_cv_gate(false);

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
	draw_str(0, 4, F_12, "Checking for pitch");
	draw_str(0, 16, F_12, "loopback cable");
	oled_flip();
	HAL_Delay(1500);

	u32 totals[2] = {0};
	do {
		for (u8 i = 0; i < 2; ++i) {
			send_pitch_cv_raw((s32)cv_out[i], (s32)cv_out[i + 2]);
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
	draw_str(0, 4, F_12, "Auto-calibrating");
	draw_str(0, 16, F_12, "pitch cv input...");
	oled_flip();
	HAL_Delay(1000); // give the user some time to fully plug in the jack

	for (u8 i = 0; i < 2; i++) {
		// in reality, pitch in calib works from both pitch lo and pitch hi outputs
		send_pitch_cv_raw((s32)cv_out[i], (s32)cv_out[i + 2]);

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
	draw_str(0, 4, F_12, "Calibration complete!");
	draw_str(0, 16, F_12, "Unplug pitch cable");
	oled_flip();

	// wait for unplug
	while (cv_pitch_present())
		HAL_Delay(1);
	HAL_Delay(500);
	calib_mode = CALIB_NONE;
}