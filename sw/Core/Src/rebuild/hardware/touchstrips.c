#include "touchstrips.h"
#include "sensor_defs.h"

#define TOUCH_THRESHOLD 1000

// cleanup
extern CalibResult calibresults[18];
extern void update_finger(u8 touch_id);
// -- cleanup

// stm setup

TSC_HandleTypeDef htsc;

void Error_Handler(void);

void MX_TSC_Init(void) {
	// Configure the TSC peripheral
	htsc.Instance = TSC;
	htsc.Init.CTPulseHighLength = TSC_CTPH_7CYCLES;
	htsc.Init.CTPulseLowLength = TSC_CTPL_7CYCLES;
	htsc.Init.SpreadSpectrum = DISABLE;
	htsc.Init.SpreadSpectrumDeviation = 32;
	htsc.Init.SpreadSpectrumPrescaler = TSC_SS_PRESC_DIV1;
	htsc.Init.PulseGeneratorPrescaler = TSC_PG_PRESC_DIV2;
	htsc.Init.MaxCountValue = TSC_MCV_16383;
	htsc.Init.IODefaultMode = TSC_IODEF_OUT_PP_LOW;
	htsc.Init.SynchroPinPolarity = TSC_SYNC_POLARITY_FALLING;
	htsc.Init.AcquisitionMode = TSC_ACQ_MODE_NORMAL;
	htsc.Init.MaxCountInterrupt = DISABLE;
	htsc.Init.ChannelIOs = TSC_GROUP1_IO2 | TSC_GROUP1_IO3 | TSC_GROUP1_IO4 | TSC_GROUP2_IO2 | TSC_GROUP2_IO3
	                       | TSC_GROUP2_IO4 | TSC_GROUP3_IO3 | TSC_GROUP3_IO4 | TSC_GROUP4_IO2 | TSC_GROUP4_IO3
	                       | TSC_GROUP4_IO4 | TSC_GROUP5_IO2 | TSC_GROUP5_IO3 | TSC_GROUP5_IO4 | TSC_GROUP6_IO2
	                       | TSC_GROUP6_IO3 | TSC_GROUP7_IO2 | TSC_GROUP7_IO3;
	htsc.Init.ShieldIOs = 0;
	htsc.Init.SamplingIOs = TSC_GROUP1_IO1 | TSC_GROUP2_IO1 | TSC_GROUP3_IO2 | TSC_GROUP4_IO1 | TSC_GROUP5_IO1
	                        | TSC_GROUP6_IO1 | TSC_GROUP7_IO1;
	if (HAL_TSC_Init(&htsc) != HAL_OK) {
		Error_Handler();
	}

	// set up tsc for first reading
	TSC_IOConfigTypeDef config = {0};
	config.ChannelIOs = channels_io[0];
	config.SamplingIOs = sample_io[0];
	HAL_TSC_IOConfig(&htsc, &config);
	HAL_TSC_IODischarge(&htsc, ENABLE);
}

// touch_frame is volatile because calib.h has a blocking "while (touch_frame == ff);" loop
volatile u8 touch_frame = 0; // frame counter for touch reading loop

u16 sensor_min[2 * NUM_TOUCH_READINGS]; // lifetime low
u16 sensor_max[2 * NUM_TOUCH_READINGS]; // lifetime high

static u16 sensor_val[2 * NUM_TOUCH_READINGS];       // current value (range 0 - 65027)
static Touch touches[NUM_TOUCHES][NUM_TOUCH_FRAMES]; // the touches

static u8 read_this_frame = 0; // has touch (0 - 7) been read this touch_frame? bitmask

// sensor macros

#define A_VAL(reading_id) (sensor_val[reading_id * 2])
#define B_VAL(reading_id) (sensor_val[reading_id * 2 + 1])
#define A_MIN(reading_id) (sensor_min[reading_id * 2])
#define B_MIN(reading_id) (sensor_min[reading_id * 2 + 1])
#define A_DIFF(reading_id) (A_VAL(reading_id) - A_MIN(reading_id))
#define B_DIFF(reading_id) (B_VAL(reading_id) - B_MIN(reading_id))
#define IS_TOUCH(reading_id) (A_DIFF(reading_id) + B_DIFF(reading_id) > TOUCH_THRESHOLD)

// == GET TOUCH INFO == //

bool touch_read_this_frame(u8 strip_id) {
	return read_this_frame & (1 << strip_id);
}

Touch* get_touch(u8 touch_id) {
	return &touches[touch_id][touch_frame];
}

Touch* get_touch_prev(u8 touch_id, u8 frames_back) {
	return &touches[touch_id][(touch_frame - frames_back + NUM_TOUCH_FRAMES) & 7];
}

// == MAIN == //

static void process_reading(u8 reading_id) {
	// raw values
	s16 raw_pos = sensor_reading_position(reading_id);
	u16 raw_pres = sensor_reading_pressure(reading_id);

	// touch
	u8 touch_id = reading_id % NUM_TOUCHES;
	Touch* cur_touch = get_touch(touch_id);
	Touch* prev_touch = get_touch_prev(touch_id, 1);

	// calibration
	u16 calib_pos;
	s16 calib_pres;
	const CalibResult* c = &calibresults[reading_id];

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

	// the touch has been read
	if (touch_id < 8)
		read_this_frame |= 1 << touch_id;

	// pass back to update_finger
	update_finger(touch_id);
}

// 1. Make sure TSC is set up to read the current phase
// 2. For each TSC group we are reading, exit if TSC is not done reading yet
// 3. Read and store the value, keep track of lifetime min/max values
// 4. Reading and updating is done by read phase, see below

void read_touchstrips(void) {
	static u8 reading_id = 0;
	static u8 group_id = 0;
	static u8 sensor_id = 0;
	static u8 read_phase = 0;
	static u16 phase_read_mask = 0xffff; // fill min_value array on first loop
	static bool tsc_started = false;

	if (!tsc_started) {
		HAL_TSC_Start(&htsc);
		tsc_started = true;
		return; // give TSC a tick to catch up
	}

	// loop to read all sensor values for this phase
	do {
		// check whether current group is ready for reading
		if (HAL_TSC_GroupGetStatus(&htsc, group_id) != TSC_GROUP_COMPLETED)
			return;
		// if so, save sensor value (resulting range 0 - 65027)
		u16 value = sensor_val[sensor_id] = (1 << 23) / maxi(129, HAL_TSC_GroupGetValue(&htsc, group_id));
		// keep track of lifetime min/max values
		if (value > sensor_max[sensor_id])
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
		process_reading(NUM_TOUCHES + 8);
		break;
	default: // phase 3 through 10: second pass of individual fingers 0 through 7
		process_reading(NUM_TOUCHES + read_phase - 3);
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
		group_id = reading_group[0];
		sensor_id = reading_sensor[0];
		phase_read_mask = 0b111; // the first three phases are always executed
	}

	// catch up with any phases we might have missed
	if (read_phase > 0 && reading_id < max_readings_in_phase[read_phase - 1]) {
		// set reading id to the start-id of this phase
		reading_id = max_readings_in_phase[read_phase - 1];
		group_id = reading_group[reading_id];
		sensor_id = reading_sensor[reading_id];
	}

	// set up the tsc for the next read phase
	TSC_IOConfigTypeDef config = {0};
	config.ChannelIOs = channels_io[read_phase];
	config.SamplingIOs = sample_io[read_phase];
	HAL_TSC_IOConfig(&htsc, &config);
	HAL_TSC_IODischarge(&htsc, ENABLE);
	tsc_started = false;
}

void reset_touches(void) {
	memset(sensor_val, 0, sizeof(sensor_val));
	memset(sensor_min, -1, sizeof(sensor_min));
	memset(sensor_max, 0, sizeof(sensor_max));
	memset(calibresults, 0, sizeof(calibresults));
}

// == FOR CALIB == //

// sensor position: ratio between a and b values mapped to [-4096 .. 4095]
s16 sensor_reading_position(u8 reading_id) {
	return ((B_VAL(reading_id) - A_VAL(reading_id)) << 12) / (A_VAL(reading_id) + B_VAL(reading_id) + 1);
}

// sensor pressure: sensor values added (normalized for noise floor)
u16 sensor_reading_pressure(u8 reading_id) {
	return clampi(A_DIFF(reading_id) + B_DIFF(reading_id), 0, 65536);
}
