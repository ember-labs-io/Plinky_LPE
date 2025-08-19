#include "touchstrips.h"
#include "flash.h"
#include "gfx/gfx.h"
#include "leds.h"
#include "sensor_defs.h"
#include "synth/audio.h"
#include "ui/pad_actions.h"
#include "ui/shift_states.h"

extern TSC_HandleTypeDef htsc;

#define TOUCH_THRESHOLD 1000

static TouchCalibData touch_calib_data[NUM_TOUCH_READINGS];

TouchCalibData* touch_calib_ptr(void) {
	return touch_calib_data;
}

u8 touch_frame = 0; // frame counter for touch reading loop

static Touch touches[NUM_TOUCHSTRIPS][NUM_TOUCH_FRAMES]; // the touches
static u16 sensor_val[2 * NUM_TOUCH_READINGS];           // current value (range 0 - 65027)
static u16 sensor_min[2 * NUM_TOUCH_READINGS];           // lifetime low
static u16 sensor_max[2 * NUM_TOUCH_READINGS];           // lifetime high

static bool tsc_started = false;
static u8 read_this_frame = 0; // has touch (0 - 7) been read this touch_frame? bitmask

// sensor macros

#define A_VAL(reading_id) (sensor_val[reading_id * 2])
#define B_VAL(reading_id) (sensor_val[reading_id * 2 + 1])
#define A_MIN(reading_id) (sensor_min[reading_id * 2])
#define B_MIN(reading_id) (sensor_min[reading_id * 2 + 1])
#define A_MAX(reading_id) (sensor_max[reading_id * 2])
#define B_MAX(reading_id) (sensor_max[reading_id * 2 + 1])
#define A_DIFF(reading_id) (A_VAL(reading_id) - A_MIN(reading_id))
#define B_DIFF(reading_id) (B_VAL(reading_id) - B_MIN(reading_id))
#define IS_TOUCH(reading_id) (A_DIFF(reading_id) + B_DIFF(reading_id) > TOUCH_THRESHOLD)

static void setup_tsc(u8 read_phase) {
	TSC_IOConfigTypeDef config = {0};
	config.ChannelIOs = channels_io[read_phase];
	config.SamplingIOs = sample_io[read_phase];
	HAL_TSC_IOConfig(&htsc, &config);
	HAL_TSC_IODischarge(&htsc, ENABLE);
	tsc_started = false;
}

void init_touchstrips(void) {
	memset(sensor_val, 0, sizeof(sensor_val));
	memset(sensor_min, -1, sizeof(sensor_min));
	memset(sensor_max, 0, sizeof(sensor_max));
	memset(touch_calib_data, 0, sizeof(touch_calib_data));
	setup_tsc(0);
}

// == GET TOUCH INFO == //

// sensor position: ratio between a and b values mapped to [-4096 .. 4095]
static s16 sensor_reading_position(u8 reading_id) {
	return ((B_VAL(reading_id) - A_VAL(reading_id)) << 12) / (A_VAL(reading_id) + B_VAL(reading_id) + 1);
}

// sensor pressure: sensor values added (normalized for noise floor)
static u16 sensor_reading_pressure(u8 reading_id) {
	return clampi(A_DIFF(reading_id) + B_DIFF(reading_id), 0, 65536);
}

bool touch_read_this_frame(u8 strip_id) {
	return read_this_frame & (1 << strip_id);
}

static Touch* get_touch(u8 touch_id) {
	return &touches[touch_id][touch_frame];
}

Touch* get_touch_prev(u8 touch_id, u8 frames_back) {
	return &touches[touch_id][(touch_frame - frames_back + NUM_TOUCH_FRAMES) & 7];
}

// == MAIN == //

// let's root out some invalid state changes!
static bool validate_shift_state_change(ShiftState new_state) {
	// the new state needs to be different
	if (new_state == shift_state)
		return false;

	// going from one state to another, apply a bit of hysteresis - dist from old state should be larger than 192
	Touch* strip_cur = get_touch(8);
	if ((shift_state != SS_NONE) && (abs((shift_state * 256 + 128) - strip_cur->pos) < 192))
		return false;

	// pressure needs to be high enough and the position somewhat stable
	Touch* strip_2back = get_touch_prev(8, 2);
	if ((strip_2back->pres < 700) || (strip_cur->pres < 700) || (abs(strip_cur->pos - strip_2back->pos) > 60))
		return false;

	// for a new press, the button needs to be held for at least three frames
	Touch* strip_1back = get_touch_prev(8, 1);
	ShiftState state_1back = strip_1back->pos >> 8;
	ShiftState state_2back = strip_2back->pos >> 8;
	if ((shift_state == SS_NONE) && (new_state != state_1back || new_state != state_2back))
		return false;

	// accidental presses: rule out shift state presses if the surrounding synth pads are being pressed
	Touch* strip_above = get_touch(new_state);
	Touch* strip_left = get_touch(maxi(0, new_state - 1));
	Touch* strip_right = get_touch(mini(7, new_state + 1));
	if ((strip_above->pres > 256 && strip_above->pos >= 6 * 256)    // ~ bottom two pads of strip above
	    || (strip_left->pres > 256 && strip_left->pos >= 7 * 256)   // ~ bottom pad of strip left
	    || (strip_right->pres > 256 && strip_right->pos >= 7 * 256) // ~ bottom pad of strip right
	)
		return false;

	// valid!
	return true;
}

static void process_reading(u8 reading_id) {
	// raw values
	s16 raw_pos = sensor_reading_position(reading_id);
	u16 raw_pres = sensor_reading_pressure(reading_id);

	// touch
	u8 touch_id = reading_id % NUM_TOUCHSTRIPS;
	Touch* cur_touch = get_touch(touch_id);
	Touch* prev_touch = get_touch_prev(touch_id, 1);

	// calibration
	u16 calib_pos;
	s16 calib_pres;
	const TouchCalibData* c = &touch_calib_data[reading_id];

	// we have calibration data, let's apply it
	if (c->pres[0] != 0) {
		s16 avg_pres;

		// handle reversed calibration values
		bool reversed = c->pos[7] < c->pos[0];

		// position out of range, negative extreme
		if ((raw_pos < c->pos[0] - (c->pos[1] - c->pos[0])) ^ reversed) {
			calib_pos = TOUCH_MIN_POS;
			avg_pres = c->pres[0];
		}
		// position out of range, positive extreme
		else if ((raw_pos >= c->pos[7] + (c->pos[7] - c->pos[6])) ^ reversed) {
			calib_pos = TOUCH_MAX_POS;
			avg_pres = c->pres[7];
		}
		// position in range
		else {
			// find the correct section
			u8 section = 0;
			s16 upper_pos = c->pos[0];
			while (section < 8 && ((raw_pos >= upper_pos) ^ reversed)) {
				section++;
				if (section == 8)
					upper_pos = c->pos[7] + (c->pos[7] - c->pos[6]); // extrapolated
				else
					upper_pos = c->pos[section];
			}
			s16 lower_pos = (section == 0) ? c->pos[0] - (c->pos[1] - c->pos[0]) : c->pos[section - 1];
			// scale the position between the upper and lower calibrations
			s16 section_size = upper_pos - lower_pos;
			s16 section_pos = (section_size ^ reversed) ? ((raw_pos - lower_pos) * 256) / section_size : 0;
			calib_pos = clampi(section * 256 - 128 + section_pos, TOUCH_MIN_POS, TOUCH_MAX_POS);
			// scale the pressure between the upper and lower calibrations
			s16 lower_pres = c->pres[maxi(0, section - 1)];
			s16 upper_pres = c->pres[mini(7, section)];
			avg_pres = (upper_pres - lower_pres) * section_pos / 256 + lower_pres;
		}
		// scale the pressure around the expected pressure at this point - a raw_pres less than half the expected
		// pressure from calibration results in a negative calib_pres
		calib_pres = (raw_pres << 12) / maxi(avg_pres, 1000) + TOUCH_MIN_PRES;
	}
	// we have no calibration data - it's unlikely that we get any usable result without calibration data, but we
	// can at least map the raw data to acceptable ranges
	else {
		// map raw_pos [-4096, 4095] to [0, TOUCH_MAX_POS]
		calib_pos = (raw_pos + 4096) >> 2;
		// map [0, 32767] such that TOUCH_THRESHOLD maps to 0 and 32767 maps to 2048
		calib_pres = ((raw_pres - TOUCH_THRESHOLD) << 11) / (32767 - TOUCH_THRESHOLD);
	}

	// save pressure to touch
	cur_touch->pres = calib_pres;
	// only save position if touching and not quickly releasing
	if (calib_pres > 0 && calib_pres > prev_touch->pres - 128)
		cur_touch->pos = calib_pos;
	// fast release or no touch, retain the previous position
	else
		cur_touch->pos = prev_touch->pos;

	// don't further process the touches during cv-calib
	if (calib_mode == CALIB_CV)
		return;

	// shift buttons
	if (touch_id == 8) {
		ShiftState new_state = cur_touch->pos >> 8;
		// valid new state? => set
		if (validate_shift_state_change(new_state))
			shift_set_state(new_state);
		// no pressure on strip but we were in a state => release
		else if (cur_touch->pres <= 0 && shift_state != SS_NONE)
			shift_release_state();
		// in any shift state => hold
		if (shift_state != SS_NONE)
			shift_hold_state();
	}
	// main grid
	else {
		// sensor values have been read
		read_this_frame |= 1 << touch_id;

		// at this point the touchstrip has fully been processed to be used by the synth, which runs on its own time
		// next, the touchstrip gets handled in the context of parameters and other actions
		handle_pad_actions(touch_id, get_touch(touch_id));
	}
}

// 1. Make sure TSC is set up to read the current phase
// 2. For each TSC group we are reading, exit if TSC is not done reading yet
// 3. Read and store the value, keep track of lifetime min/max values
// 4. Reading and updating is done by read phase, see below

// returns the current read phase
u8 read_touchstrips(void) {
	static u8 reading_id = 0;
	static u8 group_id = 0;
	static u8 sensor_id = 0;
	static u8 read_phase = 0;
	static u16 phase_read_mask = 0xffff; // fill min_value array on first loop

	if (!tsc_started) {
		HAL_TSC_Start(&htsc);
		tsc_started = true;
		return 255; // give TSC a tick to catch up
	}

	// loop to read all sensor values for this phase
	do {
		// check whether current group is ready for reading
		if (HAL_TSC_GroupGetStatus(&htsc, group_id) != TSC_GROUP_COMPLETED)
			return 255; // give TSC a tick to catch up
		// if so, save sensor value (resulting range 0 - 65027)
		u16 value = sensor_val[sensor_id] = (1 << 23) / maxi(129, HAL_TSC_GroupGetValue(&htsc, group_id));
		// keep track of lifetime min/max values
		if (calib_mode && value > sensor_max[sensor_id])
			sensor_max[sensor_id] = value;
		if (value < sensor_min[sensor_id])
			sensor_min[sensor_id] = value;
		// move to next reading
		reading_id++;
		group_id = reading_group[reading_id];
		sensor_id = reading_sensor[reading_id];
	} while (reading_id < max_readings_in_phase[read_phase]);

	// we have done all readings for this phase
	HAL_TSC_Stop(&htsc);

	// touch-calibration loop is a simplified version of the regular loop
	if (calib_mode == CALIB_TOUCH) {
		read_phase++;
		if (read_phase == READ_PHASES) {
			touch_frame = (touch_frame + 1) & 7;
			read_phase = 0;
			reading_id = 0;
			group_id = reading_group[reading_id];
			sensor_id = reading_sensor[reading_id];
		}
		setup_tsc(read_phase);
		return read_phase; // skip the regular loop
	}

	// read phases
	//
	// the strategy is to first quickly check all strips for touches:
	// - phase 0 checks strips 0, 1, 2, and the first sensor of 8
	// - phase 1 checks strips 3, 4, 5, and the second sensor of 8
	// - plase 2 checks strips 6 and 7
	//
	// in a phase, if there are 0 or 1 touches detected, all checked strips are immediately updated
	// if there are 2 or more touches detected:
	//	- untouched strips are immediately updated
	//	- touched strips are queued to be updated in the second pass
	//
	// phase 3 through 12 constitute the second pass
	// - each phase checks their assigned strip (phase_id - 3) if it was queued during the first pass
	// - strip 8 is checked over two phases (11 & 12) because of a wiring issue

	// rj: while this does some effective checking in the first three frames, the second pass effectively ignores
	// the values read in the first three frames, which means two read phases (including setting up the TSC) are
	// spent for one touch update - does this outperform a simple sequential read of the fingers?
	//
	// additionally, the sensor readings are saved in 36-sized arrays for 18 sensors, but it looks like the double
	// sensor readings are not used anywhere - it might be easier to just store them in 18-sized arrays?

	switch (read_phase) {
	case 0: { // strip 0, 1, 2 and first half of 8
		bool t0 = IS_TOUCH(0);
		bool t1 = IS_TOUCH(1);
		bool t2 = IS_TOUCH(2);
		bool t8 = IS_TOUCH(8);
		u8 num_touches = t0 + t1 + t2 + 2 * t8;
		if (num_touches <= 1) {
			process_reading(0);
			process_reading(1);
			process_reading(2);
			// strip 8 has read one sensor at this point, which means we can't process it yet
		}
		else {
			if (t0)
				phase_read_mask |= 1 << (3 + 0);
			else
				process_reading(0);
			if (t1)
				phase_read_mask |= 1 << (3 + 1);
			else
				process_reading(1);
			if (t2)
				phase_read_mask |= 1 << (3 + 2);
			else
				process_reading(2);
		}
	} break;
	case 1: { // strip 3, 4, 5 and second half of 8
		bool t3 = IS_TOUCH(3);
		bool t4 = IS_TOUCH(4);
		bool t5 = IS_TOUCH(5);
		bool t8 = IS_TOUCH(8);
		u8 num_touches = t3 + t4 + t5 + 2 * t8;
		if (num_touches <= 1) {
			process_reading(3);
			process_reading(4);
			process_reading(5);
			process_reading(8);
		}
		else {
			if (t3)
				phase_read_mask |= 1 << (3 + 3);
			else
				process_reading(3);
			if (t4)
				phase_read_mask |= 1 << (3 + 4);
			else
				process_reading(4);
			if (t5)
				phase_read_mask |= 1 << (3 + 5);
			else
				process_reading(5);
			if (t8)
				phase_read_mask |= (1 << (3 + 8)) + (1 << (3 + 9));
			else
				process_reading(8);
		}
	} break;
	case 2: { // strip 6 and 7
		bool t6 = IS_TOUCH(6);
		bool t7 = IS_TOUCH(7);
		u8 num_touches = t6 + t7;
		if (num_touches <= 1) {
			process_reading(6);
			process_reading(7);
		}
		else {
			// the only option here is that both strips need to be queued
			phase_read_mask |= (1 << (3 + 6)) + (1 << (3 + 7));
		}
	} break;
	case 11: // second pass of first sensor of strip 8
		break;
	case 12: // second pass of second sensor of strip 8
		process_reading(NUM_TOUCHSTRIPS + 8);
		break;
	default: // phase 3 through 10: second pass of individual fingers 0 through 7
		process_reading(NUM_TOUCHSTRIPS + read_phase - 3);
		break;
	}

	// look for another phase in this loop that needs to be executed
	do {
		read_phase++;
	} while (read_phase < READ_PHASES && !(phase_read_mask & (1 << read_phase)));

	// if we have completed all read phases
	if (read_phase == READ_PHASES) {
		touch_frame = (touch_frame + 1) & 7; // move to next frame,
		read_this_frame = 0;                 // where no touches have been read
		read_phase = 0;                      // start back from the top
		reading_id = 0;
		group_id = reading_group[reading_id];
		sensor_id = reading_sensor[reading_id];
		phase_read_mask = 0b111; // the first three phases are always executed
	}

	// catch up with any phases we might have missed
	if (read_phase > 0 && reading_id < max_readings_in_phase[read_phase - 1]) {
		// set reading id to the start-id of this phase
		reading_id = max_readings_in_phase[read_phase - 1];
		group_id = reading_group[reading_id];
		sensor_id = reading_sensor[reading_id];
	}

	// prepare for next phase
	setup_tsc(read_phase);
	return read_phase;
}

// == CALIB == //

void touch_calib(FlashCalibType flash_calib_type) {
	typedef struct ReadingCalib {
		float pos[PADS_PER_STRIP];
		float pres[PADS_PER_STRIP];
		float weight[PADS_PER_STRIP];
	} ReadingCalib;

	ReadingCalib* reading_calib = (ReadingCalib*)delay_ram_buf;
	memset(reading_calib, 0, sizeof(ReadingCalib) * NUM_TOUCH_READINGS);
	s8 cur_pad[NUM_TOUCHSTRIPS];
	memset(cur_pad, PADS_PER_STRIP - 1, sizeof(cur_pad));
	u16 raw_pres_1back[NUM_TOUCH_READINGS] = {};
	u8 cur_frame = touch_frame;
	u8 readings_done = 0;
	u16 errors = 0;
	// display drawing
	char help_text[64] = "slowly/evenly press lit pads\ntake care, be accurate";
	u8 refresh_counter = 0;
	bool blink = false;

	do {
		// show text on display
		if (!refresh_counter) {
			refresh_counter = 16; // update once every 16 frames
			blink = !blink;
			oled_clear();
			fdraw_str(0, 0, F_16, "Calibration%c", blink ? '!' : ' ');
			draw_str(0, 16, F_8, help_text);
			if (errors)
				inverted_rectangle(0, 0, OLED_WIDTH, OLED_HEIGHT);
			oled_flip();
		}
		else
			refresh_counter--;

		// wait for touchstrips to update
		while (touch_frame == cur_frame)
			__asm__ volatile("" ::: "memory");
		cur_frame = touch_frame;

		// update the 18 calibration entries for their respective current steps
		u16 ready_mask = 0;
		readings_done = 0;
		for (u8 read_id = 0; read_id < NUM_TOUCH_READINGS; ++read_id) {
			s8 pad = cur_pad[read_id % NUM_TOUCHSTRIPS];
			// reading done => skip loop
			if (pad < 0) {
				readings_done++;
				continue;
			}
			// pressure
			u16 raw_pres = sensor_reading_pressure(read_id);
			u16 prev_raw_pres = raw_pres_1back[read_id];
			raw_pres_1back[read_id] = raw_pres;
			u16 pres_band = raw_pres / 20;
			// position
			s16 raw_pos = sensor_reading_position(read_id);
			// pressure is quite stable => update calibration state
			if (raw_pres > 1200 && raw_pres > prev_raw_pres - pres_band / 2 && raw_pres < prev_raw_pres + pres_band) {
				float weight = (raw_pres - 1200.f) / 1000.f;
				float change = abs(prev_raw_pres - raw_pres) * (1.f / 250.f);
				weight *= maxf(0.f, 1.f - change);
				weight = minf(weight, 1.f);
				weight *= weight;
				// leakage
				const static float LEAK = 0.90f;
				reading_calib[read_id].weight[pad] *= LEAK;
				reading_calib[read_id].pos[pad] *= LEAK;
				reading_calib[read_id].pres[pad] *= LEAK;
				// add new input
				reading_calib[read_id].weight[pad] += weight;
				reading_calib[read_id].pos[pad] += raw_pos * weight;
				reading_calib[read_id].pres[pad] += raw_pres * weight;
			}
			// second read id of a sensor
			u8 read_id2 = read_id + NUM_TOUCHSTRIPS;
			// don't save calib data if: this is a 2nd reading or either calibration doesn't have enough data yet
			if (read_id >= NUM_TOUCHSTRIPS || reading_calib[read_id].weight[pad] < 4.f
			    || reading_calib[read_id2].weight[pad] < 4.f)
				continue;
			// pressed finger => pulse led
			if (raw_pres > 900)
				ready_mask |= 1 << read_id;
			// lifted finger => save calib data and move to next pad
			else {
				touch_calib_data[read_id].pres[pad] =
				    reading_calib[read_id].pres[pad] / reading_calib[read_id].weight[pad];
				touch_calib_data[read_id].pos[pad] =
				    reading_calib[read_id].pos[pad] / reading_calib[read_id].weight[pad];
				touch_calib_data[read_id2].pres[pad] =
				    reading_calib[read_id2].pres[pad] / reading_calib[read_id2].weight[pad];
				touch_calib_data[read_id2].pos[pad] =
				    reading_calib[read_id2].pos[pad] / reading_calib[read_id2].weight[pad];
				cur_pad[read_id]--;

				// a few pads have been handled, check for invalid value ranges
				if (pad <= 4) {
					errors &= ~(1 << read_id);
					if (A_MAX(read_id) - A_MIN(read_id) < 1000) {
						snprintf(help_text, sizeof(help_text), "!pad %d upper not conn\ncheck soldering", read_id + 1);
						errors |= (1 << read_id);
					}
					else if (B_MAX(read_id) - B_MIN(read_id) < 1000) {
						snprintf(help_text, sizeof(help_text), "!pad %d lower not conn\ncheck soldering", read_id + 1);
						errors |= (1 << read_id);
					}
					else if (abs(touch_calib_data[read_id].pos[pad] - touch_calib_data[read_id].pos[PADS_PER_STRIP - 1])
					         < 300) {
						snprintf(help_text, sizeof(help_text), "!pad %d shorted?\ncheck soldering", read_id + 1);
						errors |= (1 << read_id);
					}
				}
			}
		}

		// update the leds
		u8 pulse = triangle(millis());
		for (u8 strip = 0; strip < NUM_TOUCHSTRIPS; ++strip) {
			bool ready = (ready_mask & (1 << strip)) > 0;
			bool err = (errors & (1 << strip)) > 0;
			for (u8 pad = 0; pad < PADS_PER_STRIP; ++pad) {
				u8 k = 0;
				if (pad == cur_pad[strip])
					k = ready ? pulse : (255 - reading_calib[strip].weight[pad] * 12.f);
				if (err)
					k = maxi(k, pulse / 2);
				leds[strip][pad] = led_add_gamma(k);
			}
		}
	} while (readings_done < NUM_TOUCH_READINGS);

	// save results
	flash_write_calib(flash_calib_type);

	HAL_Delay(500);
	draw_logo();
	leds_bootswish();
}