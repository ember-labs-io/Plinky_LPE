#include "touchstrips.h"
#include "gfx/gfx.h"
#include "leds.h"
#include "memory.h"
#include "sensor_defs.h"
#include "synth/audio.h"
#include "ui/pad_actions.h"

extern TSC_HandleTypeDef htsc;

#define TOUCH_THRESHOLD 1000

u8 touch_frame = 0; // frame counter for touch reading loop
u16 strip_touched = 0;

static TouchCalibData touch_calib_data[NUM_TOUCH_READINGS];
static Touch touches[NUM_TOUCHSTRIPS][NUM_TOUCH_FRAMES]; // the touches
static u16 sensor_val[2 * NUM_TOUCH_READINGS];           // current value (range 0 - 65027)
static u16 sensor_min[2 * NUM_TOUCH_READINGS];           // lifetime low
static u16 sensor_max[2 * NUM_TOUCH_READINGS];           // lifetime high

static bool tsc_started = false;
static u16 read_this_frame = 0; // has touch been read this touch_frame? bitmask

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

#define GET_TOUCH(touch_id, frames_back)                                                                               \
	(&touches[touch_id]                                                                                                \
	         [(touch_frame - (read_this_frame & (1 << touch_id) ? 0 : 1) - frames_back + NUM_TOUCH_FRAMES) & 7])

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

const Touch* get_touch(u8 touch_id, u8 frames_back) {
	return GET_TOUCH(touch_id, frames_back);
}

// == MAIN == //

static bool touch_stable(u8 touch_id, s16 pressure, u16 position) {
	static const u8 STABLE_POS_RANGE = 32;
	Touch* touch_1back = GET_TOUCH(touch_id, 1);
	Touch* touch_2back = GET_TOUCH(touch_id, 2);
	// check stable position over three frames
	if (abs(touch_1back->pos - position) > STABLE_POS_RANGE || abs(touch_2back->pos - position) > STABLE_POS_RANGE)
		return false;
	// check unchanged pad id over three frames
	u8 pad_y = position >> 8;
	if (pad_y != touch_1back->pos >> 8 || pad_y != touch_2back->pos >> 8)
		return false;
	return true;
}

static void process_reading(u8 reading_id) {
	// raw values
	s16 raw_pos = sensor_reading_position(reading_id);
	u16 raw_pres = sensor_reading_pressure(reading_id);

	// touch
	u8 touch_id = reading_id % NUM_TOUCHSTRIPS;
	Touch* cur_touch = &touches[touch_id][touch_frame];
	Touch* prev_touch = &touches[touch_id][(touch_frame + 7) & 7];

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

	// noise filtering

	u16 mask = 1 << touch_id;
	bool was_touching = (strip_touched & mask) != 0;
	// pressure hysteresis
	bool is_touching = calib_pres > (was_touching ? 0 : 100);
	// check stability on new touches
	bool is_valid_touch = is_touching && (was_touching || touch_stable(touch_id, calib_pres, calib_pos));
	// save touch validities
	if (is_valid_touch != was_touching) {
		if (is_valid_touch)
			strip_touched |= mask;
		else
			strip_touched &= ~mask;
	}

	// not touching or quickly lifting finger => retain previous frame's position
	if (!is_touching || calib_pres < prev_touch->pres - 128)
		calib_pos = prev_touch->pos;

	// save results to touch
	cur_touch->pres = calib_pres;
	cur_touch->pos = calib_pos;

	// sensor value has been read
	read_this_frame |= 1 << touch_id;

	// don't further process the touches during cv-calib
	if (calib_mode == CALIB_CV)
		return;

	// at this point the touchstrip has fully been processed to be used by the synth, which runs on its own time
	// next, the touchstrip gets handled in the context of parameters and other actions
	handle_pad_actions(touch_id);
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

TouchCalibData* touch_calib_ptr(void) {
	return touch_calib_data;
}

void touch_calib(FlashCalibType flash_calib_type) {
	calib_mode = CALIB_TOUCH;

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
			}
		}

		// update the leds
		u8 pulse = triangle(millis());
		for (u8 strip = 0; strip < NUM_TOUCHSTRIPS; ++strip) {
			bool ready = (ready_mask & (1 << strip)) > 0;
			for (u8 pad = 0; pad < PADS_PER_STRIP; ++pad) {
				u8 k = 0;
				if (pad == cur_pad[strip])
					k = ready ? pulse : (255 - reading_calib[strip].weight[pad] * 12.f);
				leds[strip][pad] = led_add_gamma(k);
			}
		}
	} while (readings_done < NUM_TOUCH_READINGS);

	// save results
	flash_write_calib(flash_calib_type);
	HAL_Delay(500);
	calib_mode = CALIB_NONE;
}