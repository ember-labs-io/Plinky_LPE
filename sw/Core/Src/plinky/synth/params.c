#include "params.h"
#include "audio.h"
#include "data/tables.h"
#include "gfx/gfx.h"
#include "hardware/accelerometer.h"
#include "hardware/adc_dac.h"
#include "hardware/encoder.h"
#include "hardware/leds.h"
#include "hardware/memory.h"
#include "lfos.h"
#include "param_defs.h"
#include "sampler.h"
#include "sequencer.h"
#include "strings.h"
#include "synth.h"
#include "time.h"
#include "ui/oled_viz.h"
#include "ui/pad_actions.h"

#define EDITING_PARAM (selected_param < NUM_PARAMS)
#define SELECT_PARAM(param_id)                                                                                         \
	do {                                                                                                               \
		selected_param = (param_id);                                                                                   \
		selected_mod_src = SRC_BASE;                                                                                   \
	} while (0)

// There are three ranges of parameters:
// Raw:
//  - saved parameters
//  - s16 in range -1024 to 1024
// Value:
//  - high resolution
//  - used for granular control such as envelope parameters
//  - are displayed on screen as -100.0 to 100.0
//  - s32 in range -65536 to 65536 (effectively raw << 6)
// Index:
//  - whole numbers
//  - used for discrete values such as octave offset and sequencer clock division
//  - s8 scaled and clamped to their own range
//  - mapping to index follows a simple (value * index_range / full_range) formula
//  - mapping from index will snap to the value closest to 0 that truncates to the given index:
//    index (range = 3)   |........-2|........-1|.........0.........|1.........|2.........|
//    raw            -1024|......-683|......-342|.........0.........|342.......|683.......|1024

static Param selected_param = 255;
static ModSource selected_mod_src = SRC_BASE;

// stable snapshots for drawing oled and led visuals
static Param param_snap;
static ModSource src_snap;
// enables clear modulation message
static u32 clear_mods_duration = 0;

// modulation values
static s32 param_with_lfo[NUM_PARAMS];
static u16 max_env_global = 0;
static u32 max_pres_global = 0;
static u16 sample_hold_global = {8 << 12};
static u16 sample_hold_poly[NUM_STRINGS] = {0, 1 << 12, 2 << 12, 3 << 12, 4 << 12, 5 << 12, 6 << 12, 7 << 12};

// editing params
static Param mem_param = 255; // remembers previous selected_param, used by encoder and A/B shift-presses
static s16 left_strip_start = 0;
static ValueSmoother left_strip_smooth;

static Touch* touch_pointer[NUM_STRINGS];

// == UTILS == //

static s32 SATURATE17(s32 a) {
	int tmp;
	asm("ssat %0, %1, %2" : "=r"(tmp) : "I"(17), "r"(a));
	return tmp;
}

static s8 value_to_index(s32 value, u8 range) {
	return (clampi(value, -65535, 65535) * range + (value < 0 ? 65535 : 0)) >> 16;
}

static s8 raw_to_index(s16 raw, u8 range) {
	return value_to_index(raw << 6, range);
}

static u8 param_range(Param param_id) {
	return param_info[range_type[param_id]] & RANGE_MASK;
}

static u8 param_is_index(Param param_id, ModSource mod_src, s16 raw) {
	if (mod_src != SRC_BASE)
		return false;
	if (param_range(param_id) == 0)
		return false;
	if ((range_type[param_id] == R_DLYCLK || range_type[param_id] == R_DUACLK) && raw < 0)
		return false;
	return true;
}

static bool param_signed_or_mod(Param param_id, ModSource mod_src) {
	return param_signed(param_id) || mod_src != SRC_BASE;
}

static Param get_recent_param(void) {
	return EDITING_PARAM ? selected_param : mem_param;
}

const Preset* init_params_ptr() {
	return &init_params;
}

bool param_signed(Param param_id) {
	return param_info[range_type[param_id]] & SIGNED;
}

// will this strip produce a press for the synth?
bool strip_available_for_synth(u8 strip_id) {
	// never for the function strip
	if (strip_id == 8)
		return false;
	// yes in the default ui, but not the left-most strip when a parameter is being edited
	if (ui_mode == UI_DEFAULT && !(strip_id == 0 && EDITING_PARAM))
		return true;
	// yes in the preview mode of the sample editor with a sample loaded
	if (ui_mode == UI_SAMPLE_EDIT && sampler_mode == SM_PREVIEW && USING_SAMPLER)
		return true;
	// in all other situations: no
	return false;
}

// is the arp actively being executed?
bool arp_active(void) {
	return param_index(P_ARP_TGL) && ui_mode != UI_SAMPLE_EDIT && seq_state() != SEQ_STEP_RECORDING;
}

// to prevent redundant calls to get_string_touch(), we save our own list of pointers to the relevant Touch*
// elements every time strings_frame increments
void params_update_touch_pointers(void) {
	for (u8 string_id = 0; string_id < NUM_STRINGS; string_id++)
		touch_pointer[string_id] = get_string_touch(string_id);
}

// == MAIN == //

// parameter ranges in the og firmware
static u8 get_og_range(Param param_id) {
	u8 lpe_range = param_range(param_id);
	switch (param_id) {
	// slight range changes because of new index scaling
	case P_DEGREE:
	case P_OCT:
	case P_STEP_OFFSET:
		return lpe_range - 1;
	case P_ARP_EUC_LEN:
	case P_SEQ_EUC_LEN:
		return lpe_range + 1;
	// more time sync divisions were added
	case P_ARP_CLK_DIV:
	case P_SEQ_CLK_DIV:
		return lpe_range - 6;
	default:
		return lpe_range;
	}
}

bool update_preset(Preset* preset) {
	// clear volume mod sources
	for (ModSource src = SRC_ENV2; src < NUM_MOD_SOURCES; ++src)
		preset->params[P_VOLUME][src] = 0;
	switch (preset->version) {
	case LPE_PRESET_VERSION:
		// correct!
		break;
	case 0:
		// add mix width, switch value with (what used to be) accel sensitivity
		for (u8 mod_id = SRC_BASE; mod_id < NUM_MOD_SOURCES; ++mod_id) {
			s16 temp = preset->params[P_MIX_WIDTH][mod_id];
			preset->params[P_MIX_WIDTH][mod_id] = preset->params[P_MIX_UNUSED3][mod_id];
			preset->params[P_MIX_UNUSED3][mod_id] = temp;
		}
		// set default
		preset->params[P_MIX_WIDTH][SRC_BASE] = RAW_HALF;
		preset->version = 1;
		// fall through for further upgrading
	case 1:
		// add lfo saw shape
		for (u8 lfo_id = 0; lfo_id < NUM_LFOS; ++lfo_id) {
			s16* data = preset->params[P_A_SHAPE + lfo_id * 6];
			*data = (*data * (NUM_LFO_SHAPES - 1)) / (NUM_LFO_SHAPES); // rescale to add extra enum entry
			if (*data >= (LFO_SAW * RAW_SIZE) / NUM_LFO_SHAPES)        // and shift high numbers up
				*data += (1 * RAW_SIZE) / NUM_LFO_SHAPES;
		}
		preset->version = 2;
		// fall through for further upgrading
	case OG_PRESET_VERSION:
		// upgrade to first LPE preset type
		for (u8 param_id = 0; param_id < NUM_PARAMS; param_id++) {
			s16 og_raw = preset->params[param_id][SRC_BASE];
			s16 lpe_raw = og_raw;
			switch (param_id) {
			// map full bipolar to unipolar range
			case P_DISTORTION:
			case P_SYN_WET_DRY:
			case P_IN_WET_DRY:
			case P_MIX_WIDTH:
				lpe_raw = (og_raw + RAW_SIZE + 1) >> 1;
				break;
			// restrict to unipolar range
			case P_ARP_EUC_LEN:
			case P_SEQ_EUC_LEN:
				lpe_raw = INDEX_TO_RAW((abs(og_raw) * get_og_range(param_id)) >> 10, param_range(param_id));
				break;
			// arp & latch, take from what used to be "flags"
			case P_ARP_TGL:
				lpe_raw = (preset->pad & 0b01) << 9;
				break;
			case P_LATCH_TGL:
				lpe_raw = (preset->pad & 0b10) << 8;
				break;
			// synced lfos added, remap lfo rate
			case P_A_RATE:
			case P_B_RATE:
			case P_X_RATE:
			case P_Y_RATE:
				lpe_raw = ((og_raw + RAW_SIZE + 1) >> 1) - RAW_SIZE;
				break;
			// delay time - invert polarity
			case P_DLY_TIME:
				og_raw = -og_raw;
				lpe_raw = og_raw;
				// free timing - the first 44 values didn't do anything
				if (lpe_raw < 0)
					lpe_raw = map_s16(mini(lpe_raw, -45), -45, -1024, -1, -1024);
				// fall through
			default:
				// indeces - map more equally over raw range
				if (param_is_index(param_id, SRC_BASE, og_raw))
					lpe_raw = INDEX_TO_RAW((og_raw * get_og_range(param_id)) >> 10, param_range(param_id));
				break;
			}
			preset->params[param_id][SRC_BASE] = lpe_raw;
		} // param loop
		preset->version = LPE_PRESET_VERSION;
		return true;
	}
	return false;
}

void revert_preset(Preset* preset) {
	for (u8 param_id = 0; param_id < NUM_PARAMS; param_id++) {
		s16 lpe_raw = preset->params[param_id][SRC_BASE];
		s16 og_raw = lpe_raw;
		bool is_index = param_is_index(param_id, SRC_BASE, lpe_raw);
		switch (param_id) {
		// map unipolar to full bipolar range
		case P_DISTORTION:
		case P_SYN_WET_DRY:
		case P_IN_WET_DRY:
		case P_MIX_WIDTH:
			og_raw = (lpe_raw << 1) - RAW_SIZE;
			break;
		// restrict to unipolar range
		case P_ARP_CHANCE:
			og_raw = abs(lpe_raw);
			break;
		// arp & latch, save to "flags"
		case P_ARP_TGL:
			if (og_raw >= 512)
				preset->pad |= 0b01;
			else
				preset->pad &= ~0b01;
			break;
		case P_LATCH_TGL:
			if (og_raw >= 512)
				preset->pad |= 0b10;
			else
				preset->pad &= ~0b10;
			break;
		// delay time - invert polarity
		case P_DLY_TIME:
			// free timing - insert 44 at the start
			if (lpe_raw < 0)
				lpe_raw = map_s16(lpe_raw, -1, -1024, -45, -1024);
			og_raw = -lpe_raw;
			break;
		// no synced lfos
		case P_A_RATE:
		case P_B_RATE:
		case P_X_RATE:
		case P_Y_RATE:
			og_raw = (-abs(lpe_raw) << 1) + RAW_SIZE;
			break;
		default:
			// indeces - map to center of range
			if (is_index)
				og_raw = ((raw_to_index(lpe_raw, param_range(param_id)) << 10) + RAW_HALF) / get_og_range(param_id);
			break;
		}
		preset->params[param_id][SRC_BASE] = og_raw;
	}
	preset->version = OG_PRESET_VERSION;
}

static void apply_lfo_mods(Param param_id) {
	s16* param = cur_preset.params[param_id];
	s32 new_val = param[SRC_BASE] << 16;
	for (u8 lfo_id = 0; lfo_id < NUM_LFOS; lfo_id++)
		new_val += lfo_cur[lfo_id] * param[SRC_LFO_A + lfo_id];
	param_with_lfo[param_id] = new_val;
}

void params_tick(void) {
	// envelope 2
	for (Param param_id = P_ENV_LVL2; param_id <= P_RELEASE2; param_id++)
		apply_lfo_mods(param_id);
	max_pres_global = 0;
	max_env_global = 0;
	for (u8 string_id = 0; string_id < NUM_STRINGS; ++string_id) {
		// update envelope 2
		u8 mask = 1 << string_id;
		Voice* v = &voices[string_id];
		// reset envelope on new touch
		if (env_trig_mask & mask) {
			v->env2_lvl = 0.f;
			v->env2_decaying = false;
		}
		bool touching = string_touched & mask;
		// set lvl_goal
		float lvl_goal = touching
		                     // touching the string
		                     ? (v->env2_decaying)
		                           // decay stage: 2 times sustain parameter
		                           ? 2.f * (param_val_poly(P_SUSTAIN2, string_id) * (1.f / 65536.f))
		                           // attack stage: we aim for 2.2, the actual peak is at 2.0
		                           : 2.2f
		                     // not touching, release stage: 0
		                     : 0.f;
		float lvl_diff = lvl_goal - v->env2_lvl;
		// get multiplier size (scaled exponentially)
		float k = lpf_k(param_val_poly((lvl_diff > 0.f)
		                                   // positive difference => moving up => attack param
		                                   ? P_ATTACK2
		                                   : (v->env2_decaying && touching)
		                                         // negative difference and decaying => decay param
		                                         ? P_DECAY2
		                                         // negative difference and not decaying => release param
		                                         : P_RELEASE2,
		                               string_id));
		// change env level by fraction of difference
		v->env2_lvl += lvl_diff * k;
		// if we went past the peak during the attack stage, start the decay stage
		if (v->env2_lvl >= 2.f && touching)
			v->env2_decaying = true;
		// scale the envelope from a roughly [0, 2] float, to a u16 range scaled by the envelope level parameter
		v->env2_lvl16 = SATURATE17(v->env2_lvl * param_val_poly(P_ENV_LVL2, string_id));

		// collect range pressure
		max_pres_global = maxi(max_pres_global, touch_pointer[string_id]->pres);
		// collect range envelope
		max_env_global = maxf(max_env_global, voices[string_id].env2_lvl16);
		// generate polyphonic sample & hold random value on new touch
		if (env_trig_mask & mask)
			sample_hold_poly[string_id] += 4813;
	}
	// scale range pressure to u16 range
	max_pres_global *= 32;
	// generate global sample & hold random value on new touch
	if (env_trig_mask)
		sample_hold_global += 4813;

	accel_tick();

	adc_update_inputs();

	// lfos
	update_lfo_scope();
	for (u8 lfo_id = 0; lfo_id < NUM_LFOS; lfo_id++) {
		u8 lfo_row_offset = lfo_id * 6;
		// apply lfo modulation to the parameters of the lfo itself
		for (Param param_id = P_A_SCALE; param_id <= P_A_SYM; param_id++)
			apply_lfo_mods(param_id + lfo_row_offset);
		update_lfo(lfo_id);
	}

	// apply lfo modulation to al other params
	for (Param param_id = 0; param_id < NUM_PARAMS; ++param_id) {
		// we already did envelope 2 above
		if (param_id == P_ENV_LVL2) {
			param_id += 5;
			continue;
		}
		// we already did the lfos above
		if (param_id == P_A_SCALE) {
			param_id += 23;
			continue;
		}
		apply_lfo_mods(param_id);
	}
}

// == RETRIEVAL == //

// raw parameter value, range -1024 to 1024
static s16 param_val_raw(Param param_id, ModSource mod_src) {
	if (param_id == P_VOLUME && mod_src == SRC_BASE)
		return (sys_params.volume_msb << 8) + sys_params.volume_lsb;
	return cur_preset.params[param_id][mod_src];
}

// modulated parameter value, range -65536 to 65536
static s32 param_val_mod(Param param_id, u16 rnd, u16 env, u16 pres) {
	s16* param = cur_preset.params[param_id];

	// pre-modulated with lfos, has 16 precision bits
	s32 mod_val = param_with_lfo[param_id];

	// apply envelope modulation
	mod_val += env * param[SRC_ENV2];

	// apply pressure modulation
	mod_val += pres * param[SRC_PRES];

	// apply sample & hold modulation
	if (param[SRC_RND]) {
		u16 rnd_id = (u16)(rnd + param_id);
		// positive => uniform distribution
		if (param[SRC_RND] > 0)
			mod_val += (rndtab[rnd_id] * param[SRC_RND]) << 8;
		// negative => triangular distribution
		else {
			rnd_id += rnd_id;
			mod_val += ((rndtab[rnd_id] - rndtab[rnd_id - 1]) * param[SRC_RND]) << 8;
		}
	}

	// the shape parameter can only modulate within the same oscillator shape type
	if (param_id == P_SHAPE) {
		s16 raw = param_val_raw(param_id, SRC_BASE);
		// wavetable
		if (raw > 0)
			mod_val = maxi(1 << 17, mod_val); // 0.1%
		// pulsewave
		else if (raw < 0)
			mod_val = mini(mod_val, -(1 << 16)); // -0.1%
		// supersaw
		else
			mod_val = 0;
	}

	// all 7 mod sources have now been applied, scale and clamp to 16 bit
	return clampi(mod_val >> 10, param_signed(param_id) ? -65536 : 0, 65536);
}

// param value range +/- 65536

s32 param_val(Param param_id) {
	return param_val_mod(param_id, sample_hold_global, max_env_global, max_pres_global);
}

s32 param_val_poly(Param param_id, u8 string_id) {
	return param_val_mod(param_id, sample_hold_poly[string_id], voices[string_id].env2_lvl16,
	                     clampi(touch_pointer[string_id]->pres << 5, 0, 65535));
}

// index value is scaled to its appropriate range

s8 param_index(Param param_id) {
	u8 range = param_range(param_id);
	s8 index = value_to_index(param_val(param_id), range);
	// revert from being stored 1-based
	if (param_id == P_SAMPLE)
		index = (index - 1 + range) % range;
	return index;
}

s8 param_index_poly(Param param_id, u8 string_id) {
	return value_to_index(param_val_poly(param_id, string_id), param_range(param_id));
}

s8 param_index_unmod(Param param_id) {
	return raw_to_index(cur_preset.params[param_id][SRC_BASE], param_range(param_id));
}

// == SAVING == //

void save_param_raw(Param param_id, ModSource mod_src, s16 data) {
	// special cases
	switch (param_id) {
	case P_VOLUME:
		set_sys_param(SYS_VOLUME, data);
		return;
	default:
		break;
	}
	// don't save if no change
	if (data == cur_preset.params[param_id][mod_src])
		return;
	// save
	cur_preset.params[param_id][mod_src] = data;
	apply_lfo_mods(param_id);
	log_ram_edit(SEG_PRESET);
}

void save_param_index(Param param_id, s8 index) {
	u8 range = param_range(param_id);
	// save 1-based
	if (param_id == P_SAMPLE)
		index = (index + 1) % range;
	index = clampi(index, param_signed(param_id) ? -(range - 1) : 0, range - 1);
	save_param_raw(param_id, SRC_BASE, INDEX_TO_RAW(index, range));
}

// == PAD ACTIONS == //

// returns whether we ended up editing a param
bool try_restore_param(bool mode_a) {
	// restore from memory
	if (!EDITING_PARAM && mem_param < NUM_PARAMS) {
		u8 new_param = mem_param;
		if ((new_param % 12 < 6) != mode_a) {
			new_param += mode_a ? -6 : 6;
			if (range_type[new_param] == R_UNUSED)
				return false;
		}
		SELECT_PARAM(new_param);
		return true;
	}
	// restore other param on same pad
	if (EDITING_PARAM && (selected_param % 12 < 6) != mode_a) {
		u8 new_param = selected_param + (mode_a ? -6 : 6);
		if (range_type[new_param] == R_UNUSED)
			flash_message(F_20_BOLD, I_CROSS "No Param", 0);
		else
			SELECT_PARAM(new_param);
		return true;
	}
	// nothing to restore
	return false;
}

void close_edit_mode(void) {
	mem_param = selected_param;
	clear_last_encoder_use();
	SELECT_PARAM(NUM_PARAMS);
}

static void reset_left_strip(void) {
	left_strip_start = param_val_raw(selected_param, selected_mod_src);
	set_smoother(&left_strip_smooth, left_strip_start);
}

void touch_edit_strip(u16 position, bool is_press_start) {
	static const u16 STRIP_DEADZONE = 256;
	static const float HALF_CENTER_DEADZONE = 32.f;

	if (!EDITING_PARAM)
		return;

	if (is_press_start)
		reset_left_strip();

	// scale the press position to a param size value
	float press_value =
	    clampf((TOUCH_MAX_POS - STRIP_DEADZONE - position) * (RAW_SIZE / (TOUCH_MAX_POS - 2.f * STRIP_DEADZONE)), 0.f,
	           RAW_SIZE);
	bool is_signed = param_signed_or_mod(selected_param, selected_mod_src);
	if (is_signed)
		press_value = press_value * 2 - RAW_SIZE;
	// smooth the pressed value
	smooth_value(&left_strip_smooth, press_value, 256);
	float smoothed_value = clampf(left_strip_smooth.y2, (is_signed) ? -RAW_SIZE - 0.1f : 0.f, RAW_SIZE + 0.1f);
	// value stops exactly at +/- 100%
	bool notch_at_50 = (selected_param == P_PLAY_SPD || selected_param == P_SMP_STRETCH);
	if (notch_at_50) {
		if (smoothed_value < RAW_HALF && left_strip_start > RAW_HALF)
			smoothed_value = RAW_HALF;
		if (smoothed_value > RAW_HALF && left_strip_start < RAW_HALF)
			smoothed_value = RAW_HALF;
		if (smoothed_value < -RAW_HALF && left_strip_start > -RAW_HALF)
			smoothed_value = -RAW_HALF;
		if (smoothed_value > -RAW_HALF && left_strip_start < -RAW_HALF)
			smoothed_value = -RAW_HALF;
	}

	s16 raw = smoothed_value + (smoothed_value > 0 ? 0.5f : -0.5f);
	// snap to index value
	if (param_is_index(selected_param, selected_mod_src, raw)) {
		u8 range = param_range(selected_param);
		raw = INDEX_TO_RAW(raw_to_index(raw, range), range);
	}
	// apply center deadzone to non-index, signed params (notched params only first half)
	else if (is_signed && !(notch_at_50 && fabs(smoothed_value) >= RAW_HALF)) {
		float scale_range = notch_at_50 ? RAW_HALF : RAW_SIZE;
		float scale_factor = scale_range / (scale_range - HALF_CENTER_DEADZONE);
		if (smoothed_value < -HALF_CENTER_DEADZONE)
			smoothed_value = (smoothed_value + HALF_CENTER_DEADZONE) * scale_factor;
		else if (smoothed_value > HALF_CENTER_DEADZONE)
			smoothed_value = (smoothed_value - HALF_CENTER_DEADZONE) * scale_factor;
		else
			smoothed_value = 0;
		raw = smoothed_value + (smoothed_value > 0 ? 0.5f : -0.5f);
	}
	// save to parameter
	save_param_raw(selected_param, selected_mod_src, raw);
}

void press_param_pad(u8 pad_id, bool is_press_start) {
	u8 prev_param = selected_param;
	SELECT_PARAM((pad_id & 7) * 12 + ((pad_id >> 3) - 1) + (ui_mode == UI_EDITING_B ? 6 : 0));
	// pressed an unused param
	if (range_type[selected_param] == R_UNUSED) {
		SELECT_PARAM(prev_param);
		flash_message(F_20_BOLD, I_CROSS "No Param", 0);
	}
	// parameters that do something the moment they are pressed
	else if (is_press_start) {
		// toggle binary params
		if (range_type[selected_param] == R_BINARY)
			save_param_index(selected_param, !(cur_preset.params[selected_param][SRC_BASE] >= RAW_HALF));
		// tap tempo
		if (selected_param == P_TEMPO)
			trigger_tap_tempo();
	}
	if (selected_param != prev_param)
		reset_left_strip();
}

void press_mod_pad(u8 pad_y) {
	if (selected_param == P_VOLUME) {
		flash_message(F_20_BOLD, I_CROSS "No Mod", 0);
		return;
	}
	selected_mod_src = pad_y;
}

// == ENCODER == //

void edit_param_from_encoder(s8 enc_diff, float enc_acc) {
	Param param_id = get_recent_param();
	if (param_id >= NUM_PARAMS)
		return;

	// if this is a precision-edit, keep the param selected
	if (function_pressed == FN_SHIFT_A || function_pressed == FN_SHIFT_B)
		pad_actions_keep_edit_mode_open();

	s16 raw = param_val_raw(param_id, selected_mod_src);
	u8 range = param_range(param_id);

	// negative values of delay/dual clock are unranged
	if ((range_type[param_id] == R_DLYCLK || range_type[param_id] == R_DUACLK) && raw < 0)
		range = 0;

	// indeces: just add/subtract 1 per encoder tick
	if (range && selected_mod_src == SRC_BASE) {
		s16 index = raw_to_index(raw, range) + enc_diff;
		raw = INDEX_TO_RAW(clampi(index, param_signed(param_id) ? -(range - 1) : 0, range - 1), range);
		// smooth transition between synced and free timing
		if ((range_type[param_id] == R_DLYCLK || range_type[param_id] == R_DUACLK) && index < 0)
			raw = -1;
		save_param_raw(param_id, SRC_BASE, raw);
		return;
	}

	// holding shift disables acceleration
	enc_acc = function_pressed == FN_SHIFT_A || function_pressed == FN_SHIFT_B ? 1.f : maxf(1.f, enc_acc * enc_acc);
	raw += floorf(enc_diff * enc_acc + 0.5f);
	switch (param_id) {
	case P_PITCH:
	case P_INTERVAL:
	case P_DLY_TIME:
	case P_TEMPO:
	case P_PLAY_SPD:
	case P_SMP_STRETCH:
	case P_A_SCALE:
	case P_B_SCALE:
	case P_X_SCALE:
	case P_Y_SCALE:
	case P_A_RATE:
	case P_B_RATE:
	case P_X_RATE:
	case P_Y_RATE:
		// these params are on a larger than 100.0 scale, every encoder tick (before acceleration) affects one raw
		// step
		break;
	default:
		// these params are on a (+/-) 100.0 scale, every encoder tick (before acceleration) changes 0.1 exactly
		s8 pos = raw & 127;
		if (pos == 1 || pos == 43 || pos == 86 || pos == -42 || pos == -85 || pos == -127)
			raw += enc_diff > 0 ? 1 : -1;
		break;
	}
	raw = clampi(raw, param_signed_or_mod(param_id, selected_mod_src) ? -RAW_SIZE : 0, RAW_SIZE);
	save_param_raw(param_id, selected_mod_src, raw);
}

void params_toggle_default_value(void) {
	static u16 param_hash = NUM_PARAMS * NUM_MOD_SOURCES;
	static s16 saved_val = INT16_MAX;

	Param param_id = get_recent_param();
	if (param_id >= NUM_PARAMS)
		return;

	// clear saved value when we're seeing a new parameter
	u16 new_hash = param_id * NUM_MOD_SOURCES + selected_mod_src;
	if (new_hash != param_hash) {
		saved_val = INT16_MAX;
		param_hash = new_hash;
	}

	s16 cur_val = param_val_raw(param_id, selected_mod_src);
	s16 init_val = selected_mod_src ? 0 : init_params.params[param_id][0];
	// first press: save current value and set init value
	if (cur_val != init_val || saved_val == INT16_MAX) {
		saved_val = param_val_raw(param_id, selected_mod_src);
		save_param_raw(param_id, selected_mod_src, init_val);
	}
	// second press: restore saved value
	else
		save_param_raw(param_id, selected_mod_src, saved_val);
}

void hold_encoder_for_params(u16 duration) {
	static bool press_used_up = false;
	static u32 last_seen_duration = 0;

	clear_mods_duration = 0;

	// no param selected
	if (!EDITING_PARAM)
		return;

	// new press
	if (duration < last_seen_duration)
		press_used_up = false;
	last_seen_duration = duration;

	if (press_used_up)
		return;

	// draw warning
	if (duration >= SHORT_PRESS_TIME)
		clear_mods_duration = duration - SHORT_PRESS_TIME;

	// execute clearing mods
	if (duration >= LONG_PRESS_TIME + SHORT_PRESS_TIME + POST_PRESS_DELAY) {
		for (ModSource mod_src = SRC_ENV2; mod_src < NUM_MOD_SOURCES; ++mod_src)
			save_param_raw(selected_param, mod_src, 0);
		flash_message(F_16_BOLD, "all modulation", "Cleared");
		press_used_up = true;
	}
}

// == VISUALS == //

bool mod_clear_visuals(void) {
	if (clear_mods_duration) {
		draw_str_ctr(1, F_12, "Clear");
		draw_str_ctr(13, F_16_BOLD, "all modulation?");
		draw_load_bar(clear_mods_duration, LONG_PRESS_TIME);
		return true;
	}
	return false;
}

void take_param_snapshots(void) {
	param_snap = selected_param;
	src_snap = selected_mod_src;
}

static const char* get_val_str(s32 val, u8 num_decimals, char* val_buf, char* unit, bool force_sign) {
	const char* sign = "";
	if (val < 0) {
		sign = "-";
		val = -val;
	}
	else if (force_sign && val > 0) {
		sign = "+";
	}
	num_decimals = mini(num_decimals, 2);
	switch (num_decimals) {
	case 0:
		sprintf(val_buf, "%s%d%s", sign, (u16)(val), unit);
		break;
	case 1:
		sprintf(val_buf, "%s%d.%d%s", sign, (u16)(val / 10), (u16)(val % 10), unit);
		break;
	case 2:
		sprintf(val_buf, "%s%d.%02d%s", sign, (u16)(val / 100), (u16)(val % 100), unit);
		break;
	}
	return val_buf;
}

static const char* get_param_str(Param param_id, ModSource mod_src, s16 raw, char* val_buf) {
	s16 disp_val_10x;

	// mod amounts
	if (mod_src != SRC_BASE)
		return get_val_str(raw * 1000 >> 10, 1, val_buf, "", true);

	u8 range = param_range(param_id);
	if ((range_type[param_id] == R_DLYCLK || range_type[param_id] == R_DUACLK) && raw < 0)
		range = 0;

	// indeces
	if (range) {
		s8 index = raw_to_index(raw, range);
		switch (param_id) {
		case P_SCALE:
			return scale_name[index];
		case P_ARP_ORDER:
			return arm_mode_name[index];
		case P_SEQ_ORDER:
			return seq_mode_name[index];
		case P_SEQ_CLK_DIV:
			if (index == NUM_SYNC_DIVS) {
				sprintf(val_buf, "(CV Gate)");
				return val_buf;
			}
			break;
		// 1-based params
		case P_ARP_OCTAVES:
		case P_PATTERN:
			sprintf(val_buf, "%d", index + 1);
			return val_buf;
		case P_SAMPLE:
			if (index == 0) {
				sprintf(val_buf, "None");
				return val_buf;
			}
			break;
		default:
			break;
		}
		switch (range_type[param_id]) {
		// clock sync
		case R_DLYCLK:
		case R_SEQCLK:
		case R_DUACLK: {
			u16 num_32nds = sync_divs_32nds[index];
			u16 gcd = num_32nds;
			u8 n = 32;
			while (n) {
				u16 temp = n;
				n = gcd % n;
				gcd = temp;
			}
			u8 numerator = num_32nds / gcd;
			u8 denominator = 32 / gcd;
			char postfix[3];

			if (denominator == 1)
				sprintf(val_buf, "%d %s%s", numerator, "bar", numerator > 1 ? "s" : "");
			else {
				switch (denominator) {
				case 2:
				case 32:
					strcpy(postfix, "nd");
					break;
				case 3:
					strcpy(postfix, "rd");
					break;
				case 4:
				case 8:
				case 16:
					strcpy(postfix, "th");
					break;
				}
				sprintf(val_buf, "%d/%d%s", numerator, denominator, postfix);
			}
			return val_buf;
		}
		case R_BINARY:
			sprintf(val_buf, "%s", index ? "On" : "Off");
			return val_buf;
		case R_EUCLEN:
			if (index == 0) {
				sprintf(val_buf, "rnd");
				return val_buf;
			}
			sprintf(val_buf, "%d", index + 1);
			return val_buf;
		case R_LFOSHP:
			return lfo_shape_name[index];
		default:
			break;
		}
		switch (param_id) {
		case P_OCT:
		case P_DEGREE:
		case P_STEP_OFFSET:
			// force a plus sign for offset-based params
			return get_val_str(index, 0, val_buf, "", true);
		default:
			// default: no plus sign on positive values
			return get_val_str(index, 0, val_buf, "", false);
		}
	}

	// values
	u16 disp_range_10x = 1000;
	switch (param_id) {
	// an octave with 2 decimals
	case P_PITCH:
	case P_INTERVAL:
		return get_val_str(raw * 1200 >> 10, 2, val_buf, "", true);
	// free time durations - drawn with a minus sign because they're on the negative range of the param
	case P_DLY_TIME:
		// in ms with one decimal (delay runs at half sample rate)
		static const float DELAY_TIME_FACTOR = 10000.f / (SAMPLE_RATE >> 1);
		u32 delay_time = delay_samples_from_param(-raw << 6) * DELAY_TIME_FACTOR + 0.5f;
		// smaller than 120ms, one decimal
		if (delay_time < 1200)
			return get_val_str(-delay_time, 1, val_buf, "ms", false);
		// no decimal
		delay_time /= 10;
		return get_val_str(-delay_time, 0, val_buf, "ms", false);
	case P_ARP_CLK_DIV:
		// in ms with one decimal
		static const float STEP_LENGTH_FACTOR = (10 << 7) * TICK_LENGTH_MS;
		u32 step_length = STEP_LENGTH_FACTOR / table_interp(pitches, (raw << 4) + 49152) + 0.5f;
		return get_val_str(-step_length, 1, val_buf, "ms", false);
	case P_A_RATE:
	case P_B_RATE:
	case P_X_RATE:
	case P_Y_RATE:
		// in ms with one decimal
		static const float CYCLE_TIME_FACTOR = (10 << 8) * TICK_LENGTH_MS;
		u32 cycle_time = CYCLE_TIME_FACTOR / table_interp(pitches, (raw + 1025) << 6) + 0.5f;
		// smaller than 140ms, 1 decimal
		if (cycle_time < 1400)
			return get_val_str(-cycle_time, 1, val_buf, "ms", false);
		// smaller than 1.4s, 0 decimals
		if (cycle_time < 14000) {
			cycle_time /= 10;
			return get_val_str(-cycle_time, 0, val_buf, "ms", false);
		}
		// smaller than 14s, seconds with 2 decimals
		cycle_time /= 100;
		if (cycle_time < 1400)
			return get_val_str(-cycle_time, 2, val_buf, "s", false);
		// seconds with 1 decimal
		cycle_time /= 10;
		return get_val_str(-cycle_time, 1, val_buf, "s", false);
	// on a (+/-) 200 scale
	case P_PLAY_SPD:
	case P_SMP_STRETCH:
	case P_A_SCALE:
	case P_B_SCALE:
	case P_X_SCALE:
	case P_Y_SCALE:
		disp_range_10x = 2000;
		break;
	default:
		break;
	}
	disp_val_10x = raw * disp_range_10x >> 10;
	// on a different calculation altogether
	switch (param_id) {
	case P_PING_PONG:
		disp_val_10x = disp_val_10x - 1000;
		break;
	case P_TEMPO:
		disp_val_10x = clock_type == CLK_INTERNAL ? maxi((raw + RAW_SIZE) * 1200 >> 10, MIN_BPM_10X) : bpm_10x;
		break;
	default:
		break;
	}
	switch (param_id) {
	case P_A_OFFSET:
	case P_B_OFFSET:
	case P_X_OFFSET:
	case P_Y_OFFSET:
	case P_A_SYM:
	case P_B_SYM:
	case P_X_SYM:
	case P_Y_SYM:
		// force a plus sign for offset-based params
		return get_val_str(disp_val_10x, 1, val_buf, "", true);
	default:
		// default: no plus sign on positive values
		return get_val_str(disp_val_10x, 1, val_buf, "", false);
	}
}

static bool has_modulation(Param param_id) {
	for (u8 mod_src = SRC_ENV2; mod_src <= SRC_RND; mod_src++)
		if (cur_preset.params[param_id][mod_src])
			return true;
	return false;
}

bool params_want_to_draw(void) {
	switch (ui_mode) {
	case UI_DEFAULT:
		if (param_snap < NUM_PARAMS || (mem_param < NUM_PARAMS && enc_recently_used()))
			return true;
		break;
	case UI_EDITING_A:
	case UI_EDITING_B:
		return true;
	default:
		break;
	}
	return false;
}

// this assumes params_want_to_draw() has already been checked
void draw_cur_param(void) {
	if ((ui_mode == UI_EDITING_A || ui_mode == UI_EDITING_B) && param_snap >= NUM_PARAMS) {
		// not editing => ask for param
		draw_str(0, 8, F_16_BOLD, "select parameter");
		return;
	}

	Param draw_param;
	if (param_snap < NUM_PARAMS)
		// standard param editing
		draw_param = param_snap;
	else
		// no valid snap param => this must be an encoder edit
		draw_param = mem_param;

	// value data
	Font font;
	char val_buf[32];
	u8 width = 0;
	u8 x_center = 0;
	u8 x;
	s16 raw = param_val_raw(draw_param, src_snap);

	gfx_text_color = 3;
	// draw section name
	const char* sect_str;
	if (src_snap == SRC_BASE) {
		sect_str = param_row_name[draw_param / 6];

		// manual section name overrides
		switch (draw_param) {
		case P_PITCH:
		case P_OCT:
		case P_DEGREE:
		case P_SCALE:
		case P_MICROTONE:
		case P_COLUMN:
			sect_str = I_TOUCH "Pads";
			break;
		case P_TEMPO:
		case P_SWING:
			sect_str = I_TEMPO "Clock";
			break;
		case P_LATCH_TGL:
			sect_str = I_TOUCH "Latch";
			break;
		case P_PATTERN:
		case P_STEP_OFFSET:
			sect_str = I_NOTES "Seq";
			break;
		case P_A_SCALE:
		case P_A_OFFSET:
			sect_str = I_A "Mod A";
			break;
		case P_B_SCALE:
		case P_B_OFFSET:
			sect_str = I_B "Mod B";
			break;
		case P_X_SCALE:
		case P_X_OFFSET:
			sect_str = I_X "Mod X";
			break;
		case P_Y_SCALE:
		case P_Y_OFFSET:
			sect_str = I_Y "Mod Y";
			break;
		default:
			break;
		}
	}
	// mod sources
	else {
		if (src_snap == SRC_RND && raw < 0)
			sect_str = I_TILT "Rand >>";
		else
			sect_str = mod_src_name[src_snap];
	}

	const u8 text_x = 19;
	const u8 text_right_x = OLED_WIDTH - 17;
	u8 text_y;
	u8 icon_y = sect_str[0] == I_NOTES[0] ? 1 : 0;
	char icon_str[2] = {sect_str[0], '\0'};
	draw_str(0, icon_y, F_12_BOLD, icon_str);
	draw_str(text_x, 3, F_12_BOLD, sect_str + 1);
	u8 sect_end_x = text_x + str_width(F_12_BOLD, sect_str + 1);

	// modulated value
	if (src_snap != SRC_BASE || has_modulation(draw_param)) {
		s16 raw_mod = param_val(draw_param) >> 6;
		switch (draw_param) {
		case P_SCALE:
		case P_ARP_ORDER:
		case P_SEQ_ORDER:
		case P_A_SHAPE:
		case P_B_SHAPE:
		case P_X_SHAPE:
		case P_Y_SHAPE:
			// display the modulated param as the main param
			if (src_snap == SRC_BASE)
				raw = raw_mod;
			break;
		default:
			const char* mod_val_str = get_param_str(draw_param, SRC_BASE, raw_mod, val_buf);
			font = F_12_BOLD;
			width = str_width(font, mod_val_str);
			x = text_right_x - width;
			draw_str(x, 21, font, mod_val_str);
			break;
		}
	}

	// main value
	font = F_20_BOLD;
	const char* val_str = get_param_str(draw_param, src_snap, raw, val_buf);
	strcpy(val_buf, val_str);

	if (src_snap == SRC_BASE) {
		switch (draw_param) {
		case P_SCALE:
			font = F_12_BOLD;
			x_center = 82;
			break;
		case P_ARP_ORDER:
			font = F_12_BOLD;
			x_center = 81;
			break;
		case P_SEQ_ORDER:
			font = F_12_BOLD;
			x_center = 80;
			break;
		default:
			break;
		}
		switch (range_type[draw_param]) {
		case R_DUACLK:
		case R_DLYCLK:
			if (raw < 0)
				font = F_16_BOLD;
			break;
		case R_LFOSHP:
			font = F_12_BOLD;
			x_center = 85;
			break;
		default:
			break;
		}
	}

	char* newline_pos = strchr(val_buf, '\n');
	if (newline_pos) {
		// draw second line
		char* val_buf2 = newline_pos + 1;
		width = str_width(font, val_buf2);
		x = x_center == 0 ? text_right_x - width : x_center - width / 2;
		draw_str(x, 18, font, val_buf2);
		*newline_pos = '\0';
	}
	// draw first line
	width = str_width(font, val_buf);
	x = x_center == 0 ? text_right_x - width : x_center - width / 2;
	if (x < sect_end_x) {
		font--;
		width = str_width(font, val_buf);
		x = x_center == 0 ? text_right_x - width : x_center - width / 2;
	}
	draw_str(x, font_y_offset[font], font, val_buf);

	// draw param name

	//  special negative ranges
	s16 base_raw = param_val_raw(draw_param, SRC_BASE);
	if (base_raw < 0) {
		switch (draw_param) {
		case P_SHAPE:
			draw_str(0, 18, F_12_BOLD, I_SHAPE);
			draw_str(text_x - 1, 20, F_12_BOLD, "PulseWidth");
			return;
		case P_SWING:
			draw_str(0, 18, F_12_BOLD, I_TILT);
			draw_str(text_x, 18, F_12_BOLD, "Swing 16th");
			return;
		case P_ARP_CHANCE:
		case P_SEQ_CHANCE:
			draw_str(0, 18, F_12_BOLD, I_PERCENT);
			draw_str(text_x, 20, F_12_BOLD, "Chance (W)");
			return;
		default:
			break;
		}
		switch (range_type[draw_param]) {
		case R_DLYCLK:
		case R_DUACLK:
			draw_str(0, 18, F_12_BOLD, I_TIME);
			draw_str(text_x, 20, F_12_BOLD, "Rate");
			return;
		default:
			break;
		}
	}

	// special incidental  cases
	u8 range = param_range(draw_param);
	s8 index = raw_to_index(base_raw, range);
	if (draw_param == P_SEQ_CLK_DIV && index == range - 1) {
		draw_str(0, 18, F_12_BOLD, I_JACK);
		draw_str(text_x, 18, F_12_BOLD, "Trigger");
		return;
	}
	if (draw_param == P_SHAPE && base_raw == 0) {
		draw_str(0, 18, F_12_BOLD, I_SHAPE);
		draw_str(text_x, 20, F_12_BOLD, "SuperSaw");
		return;
	}

	// default
	const char* p_name = param_name[draw_param];
	switch (p_name[0]) {
	case '\x83': // I_DISTORT
	case '\x81': // I_SEND
	case '\x8d': // I_REVERB
	case '\xab': // I_INTERVAL
		icon_y = 17;
		break;
	default:
		icon_y = 18;
		break;
	}
	icon_str[0] = p_name[0];
	draw_str(0, icon_y, F_12_BOLD, icon_str);

	switch (draw_param) {
	// make sure param name descenders don't bump into voice bars
	case P_ENV_LVL1:
	case P_DECAY1:
	case P_DECAY2:
	case P_SWING:
	case P_PLAY_SPD:
	case P_PLAY_SPD_JIT:
	case P_A_SYM:
	case P_B_SYM:
	case P_X_SYM:
	case P_Y_SYM:
	case P_SYN_WET_DRY:
	case P_IN_WET_DRY:
		text_y = 18;
		break;
	default:
		text_y = 20;
		break;
	}
	draw_str(text_x, text_y, F_12_BOLD, p_name + 1);
	return;
}

void draw_arp_flag(void) {
	gfx_text_color = 0;
	if (arp_active()) {
		fill_rectangle(128 - 32, 0, 128 - 17, 8);
		draw_str(-(128 - 17), -1, F_8, "arp");
	}
}

void draw_latch_flag(void) {
	gfx_text_color = 0;
	if (param_index(P_LATCH_TGL)) {
		fill_rectangle(128 - 38, 32 - 8, 128 - 17, 32);
		draw_str(-(128 - 17), 32 - 7, F_8, "latch");
		if (seq_state() == SEQ_STEP_RECORDING)
			inverted_rectangle(128 - 38, 32 - 8, 128 - 17, 32);
	}
}

bool is_snap_param(u8 x, u8 y) {
	u8 pA = x - 1 + y * 12;
	return param_snap < NUM_PARAMS && x > 0 && x < 7 && (param_snap == pA || param_snap == pA + 6);
}

static u8 col_led(float brightness) {
	const static u8 bg = 48;
	return clampf(brightness, 0.f, 1.f) * (255 - bg) + bg;
}

s16 value_editor_column_led(u8 y) {
	if (param_snap >= NUM_PARAMS)
		return -1;
	bool is_signed = param_signed_or_mod(param_snap, src_snap);
	s16 raw = param_val_raw(param_snap, src_snap);
	u8 pad_id = 7 - y;
	u8 range = src_snap == SRC_BASE ? param_range(param_snap) : 0;
	float pad_pos = raw * 7 / (range ? (float)INDEX_TO_RAW(range - 1, range) : 1024.f);
	// small ranges light up discrete leds
	if (range > 0 && (is_signed ? 2 * range - 1 : range) <= 9)
		pad_pos = truncf(pad_pos);
	if (is_signed) {
		// absolute center
		if (raw == 0 && (y == 3 || y == 4))
			return col_led(1);
		// mapped
		pad_pos = fabs(pad_pos) / 2.f - 0.5f;
		switch (pad_id) {
		// negative
		case 0:
		case 1:
		case 2:
			if (raw > 0)
				return col_led(0);
			if (pad_id >= 3 - (u8)pad_pos)
				return col_led(1);
			if (pad_id == 2 - (u8)pad_pos)
				return col_led(fmod(pad_pos, 1));
			break;
		// center
		case 3:
		case 4:
			if ((raw < 0) ^ (pad_id == 4))
				return col_led(1);
			if (pad_pos < 0)
				return col_led(-2 * pad_pos);
			break;
		// positive
		case 5:
		case 6:
		case 7:
			if (raw < 0)
				return col_led(0);
			if (pad_id <= (u8)pad_pos + 4)
				return col_led(1);
			if (pad_id == (u8)pad_pos + 5)
				return col_led(fmod(pad_pos, 1));
			break;
		}
	}
	else {
		if (pad_id <= (u8)pad_pos)
			return col_led(1);
		if (pad_id == (u8)pad_pos + 1)
			return col_led(fmod(pad_pos, 1));
	}
	return col_led(0);
}

u8 ui_editing_led(u8 x, u8 y, u8 pulse) {
	// no leds if no param is selected
	if (param_snap >= NUM_PARAMS)
		return 0;

	u8 k = 0;
	if (x == 0)
		// edit strip
		k = value_editor_column_led(y);
	else if (x < 7) {
		u8 pAorB = x - 1 + y * 12 + (ui_mode == UI_EDITING_B ? 6 : 0);
		// holding down a mod source => light up params that are modulated by it
		if (mod_action_pressed() && src_snap != SRC_BASE && param_val_raw(pAorB, src_snap))
			k = 255;
		// pulse selected param
		if (pAorB == param_snap)
			k = pulse;
	}
	else {
		// pulse active mod source
		if (y == selected_mod_src)
			k = pulse;
		// light up mod sources that modulate current param
		else
			k = (y && param_val_raw(param_snap, y)) ? 255 : 0;
	}
	return k;
}

void param_function_leds(u8 pulse) {
	leds[8][FN_SHIFT_A] = (ui_mode == UI_EDITING_A)                                                     ? 255
	                      : (ui_mode == UI_DEFAULT && param_snap < NUM_PARAMS && (param_snap % 12) < 6) ? pulse
	                                                                                                    : 0;
	leds[8][FN_SHIFT_B] = (ui_mode == UI_EDITING_B)                                                      ? 255
	                      : (ui_mode == UI_DEFAULT && param_snap < NUM_PARAMS && (param_snap % 12) >= 6) ? pulse
	                                                                                                     : 0;
}