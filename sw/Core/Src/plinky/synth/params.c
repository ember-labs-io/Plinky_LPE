#include "params.h"
#include "audio.h"
#include "gfx/gfx.h"
#include "hardware/accelerometer.h"
#include "hardware/encoder.h"
#include "hardware/leds.h"
#include "hardware/memory.h"
#include "hardware/midi.h"
#include "hardware/midi_sysex.h"
#include "lfos.h"
#include "param_defs.h"
#include "sampler.h"
#include "sequencer.h"
#include "synth.h"
#include "time.h"
#include "ui/oled_viz.h"
#include "ui/pad_actions.h"

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

typedef struct Envelope {
	float level;
	u16 level16;
	bool decaying;
} Envelope;

#define EDITING_PARAM (selected_param < NUM_PARAMS)

// editing params
static Param selected_param = 255;
static ModSource selected_mod_src = SRC_BASE;
static Param mem_param = 255; // remembers previous selected_param, used by encoder and A/B shift-presses
static s16 edit_strip_start_pos = 0;
static ValueSmoother edit_strip_pos[NUM_STRINGS] = {};
static u8 selected_edit_strip = 0;

// mod sources
static Envelope envelope2[NUM_STRINGS] = {};
static u16 max_envelope2 = 0;
static u32 max_pres_global = 0;
static s32 multi_param_lfo_offset[NUM_MULTI_PARAMS] = {};
static u16 sample_hold[NUM_STRINGS] = {0, 1 << 12, 2 << 12, 3 << 12, 4 << 12, 5 << 12, 6 << 12, 7 << 12};
static u16 sample_hold_global = {8 << 12};

// precalc
static u8 arp_toggle = 0;
static u8 latch_toggle = 0;

// visuals
static Param param_snap;            // stable snapshot
static ModSource src_snap;          // stable snapshot
static bool mod_action_snap;        // stable snapshot
static u32 clear_mods_duration = 0; // enables clear modulation message

// == UTILS == //

static s32 SATURATE17(s32 a) {
	int tmp;
	asm("ssat %0, %1, %2" : "=r"(tmp) : "I"(17), "r"(a));
	return tmp;
}

// we force the bitshift to the positive domain to avoid integer rounding differences
#define VALUE_TO_INDEX(value, range) ((mini(abs(value), 65535) * (range) >> 16) * ((value) >= 0 ? 1 : -1))

#define RAW_TO_INDEX(raw, range) (VALUE_TO_INDEX((raw) << 6, range))

#define PARAM_RANGE(param_id) (param_info[range_type[param_id]] & RANGE_MASK)

static u8 param_is_index(Param param_id, ModSource mod_src, s16 raw) {
	if (mod_src != SRC_BASE)
		return false;
	if (PARAM_RANGE(param_id) == 0)
		return false;
	if ((range_type[param_id] == R_DLYCLK || range_type[param_id] == R_DUACLK) && raw < 0)
		return false;
	return true;
}

// "modulatable"
#define PARAM_MODDABLE(param_id) (range_type[param_id] != R_UNUSED && param_id != P_VOLUME)

#define PARAM_SIGNED(param_id) (param_info[range_type[param_id]] & SIGNED)

static inline s16 cc_to_raw(u8 cc, Param param_id) {
	bool bipolar = PARAM_SIGNED(param_id);
	return (bipolar ? 0 : 512) + (cc - 64) * (bipolar ? 1024 : 512) / (cc < 64 ? 64 : 63);
}

static inline u8 raw_to_cc(s16 raw, Param param_id) {
	bool bipolar = PARAM_SIGNED(param_id);
	u16 center = bipolar ? 0 : 512;
	bool upper = raw > center;
	return 64 + ((raw - center) * (63 + !upper) + upper * (bipolar ? 512 : 256)) / (bipolar ? 1024 : 512);
}

static inline s16 u14_to_raw(u16 val, Param param_id, ModSource mod_src) {
	bool bipolar = mod_src != SRC_BASE || PARAM_SIGNED(param_id);
	bool upper = val > 8192;
	return (bipolar ? 0 : 512) + ((val - 8192) * (bipolar ? 1024 : 512) + upper * 4095) / (upper ? 8191 : 8192);
}

static inline u16 raw_to_u14(s16 raw, Param param_id, ModSource mod_src) {
	bool bipolar = mod_src != SRC_BASE || PARAM_SIGNED(param_id);
	s16 center = bipolar ? 0 : 512;
	return 8192 + (raw - center) * (raw < center ? 8192 : 8191) / (bipolar ? 1024 : 512);
}

#define RECENT_PARAM (EDITING_PARAM ? selected_param : mem_param)

static void select_param(Param param_id) {
	selected_param = param_id;
	selected_mod_src = SRC_BASE;
}

const Preset* init_params_ptr(void) {
	return &init_params;
}

bool editing_param(void) {
	return EDITING_PARAM;
}

static bool edit_param_multi(Param param_id, ModSource mod_src) {
	if (mod_src != SRC_BASE || !PARAM_IS_MULTI_TIMBRAL(param_id))
		return false;

	MultiParam mp_id = multi_param_from_param[param_id];
	return (global_data.edit_multi_timbral[mp_id >> 3] >> (mp_id % 8)) & 1;
}

bool editing_multi_param(void) {
	return EDITING_PARAM && edit_param_multi(selected_param, selected_mod_src);
}

void dec_selected_edit_strip(void) {
	selected_edit_strip = (selected_edit_strip - 1 + NUM_STRINGS) % NUM_STRINGS;
}

void inc_selected_edit_strip(void) {
	selected_edit_strip = (selected_edit_strip + 1) % NUM_STRINGS;
}

// is the arp actively being executed? (per-string bitmask)
u8 arp_active_mask(void) {
	return arp_toggle;
}

bool latch_active(void) {
	return latch_toggle;
}

bool latch_active_on_string(u8 string_id) {
	return latch_toggle & (1 << string_id);
}

s16 value_to_index(Param param_id, s32 value) {
	return VALUE_TO_INDEX(value, PARAM_RANGE(param_id));
}

void set_param_from_nrpn(Param param_id, u14 value, bool multi, u8 string_id) {
	// user tries to multi-timbrally set a non multi-timbral param
	if (multi && !PARAM_IS_MULTI_TIMBRAL(param_id))
		multi = false;

	// scale 14 bit value to raw
	s16 raw = u14_to_raw(value.value, param_id, SRC_BASE);

	// save
	if (multi)
		save_multi_param_raw(param_id, string_id, raw);
	else
		save_param_raw(param_id, SRC_BASE, raw);
}

void set_mod_from_nrpn(Param param_id, u14 value, ModSource mod_src) {
	if (PARAM_MODDABLE(param_id))
		save_param_raw(param_id, mod_src, u14_to_raw(value.value, param_id, mod_src));
}

void params_rcv_cc(u8 data1, u8 data2, bool multi, u8 string_id) {
	static u14 cc14_values[NUM_14BIT_CCS][NUM_STRINGS] = {};

	// global parameters live on string 0
	if (!multi)
		string_id = 0;

	// define param id
	bool is_14bit = sys_params.midi_rcv_param_ccs == RP_CC14 && data1 < 2 * NUM_14BIT_CCS;
	Param param_id = midi_cc_table[is_14bit ? data1 % NUM_14BIT_CCS : data1];
	if (param_id >= NUM_PARAMS)
		return;

	// make sure we don't treat global params as multi-timbral
	if (!PARAM_IS_MULTI_TIMBRAL(param_id)) {
		multi = false;
		string_id = 0;
	}

	s16 raw;
	// 14 bit CCs
	if (is_14bit) {
		u14* cc14 = &cc14_values[data1 % NUM_14BIT_CCS][string_id];
		if (data1 < NUM_14BIT_CCS)
			cc14->msb = data2;
		else
			cc14->lsb = data2;
		raw = u14_to_raw(cc14->value, param_id, SRC_BASE);
	}
	// 7 bit CCs
	else
		raw = cc_to_raw(data2, param_id);

	// save
	if (multi)
		save_multi_param_raw(param_id, string_id, raw);
	else
		save_param_raw(param_id, SRC_BASE, raw);
}

// == MAIN == //

// parameter ranges in the og firmware
static u8 get_og_range(Param param_id) {
	u8 lpe_range = PARAM_RANGE(param_id);
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
			preset->params[P_MIX_WIDTH][mod_id] = preset->params[P_SETTINGS2][mod_id];
			preset->params[P_SETTINGS2][mod_id] = temp;
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
				lpe_raw = INDEX_TO_RAW((abs(og_raw) * get_og_range(param_id)) >> 10, PARAM_RANGE(param_id));
				break;
			// arp & latch, take from what used to be "flags"
			case P_ARP_TGL:
				lpe_raw = (preset->pad & 0b01) << 9;
				break;
			case P_LATCH_TGL:
				lpe_raw = (preset->pad & 0b10) << 8;
				break;
			// synced lfos added, remap lfo rate from (-1024, 1024) to (-1024, -1)
			case P_A_RATE:
			case P_B_RATE:
			case P_X_RATE:
			case P_Y_RATE:
				lpe_raw = -RAW_SIZE + ((og_raw + RAW_SIZE) * 1023 + 1024) / 2048;
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
					lpe_raw = INDEX_TO_RAW((og_raw * get_og_range(param_id)) >> 10, PARAM_RANGE(param_id));
				break;
			}
			preset->params[param_id][SRC_BASE] = lpe_raw;
		} // param loop
		preset->version = 16;
		// fall through for further upgrading
	case 16:
		// new parameter
		memset(preset->params[P_ROOT], 0, NUM_MOD_SOURCES * sizeof(s16));
		// align multi-timbral params
		for (MultiParam mp_id = 0; mp_id < NUM_MULTI_PARAMS; mp_id++) {
			s16* val = preset->multi_params[mp_id];
			val[6] = val[5] = val[4] = val[3] = val[2] = val[1] = val[0] =
			    preset->params[param_from_multi_param[mp_id]][SRC_BASE];
		}
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
		// no synced lfos, remap lfo rate from (-1024, -1) to (-1024, 1024)
		case P_A_RATE:
		case P_B_RATE:
		case P_X_RATE:
		case P_Y_RATE:
			og_raw = -RAW_SIZE + ((lpe_raw + RAW_SIZE) * 2048 + 511) / 1023;
			break;
		default:
			// indeces - map to center of range
			if (is_index)
				og_raw = ((RAW_TO_INDEX(lpe_raw, PARAM_RANGE(param_id)) << 10) + RAW_HALF) / get_og_range(param_id);
			break;
		}
		preset->params[param_id][SRC_BASE] = og_raw;
	}
	preset->version = OG_PRESET_VERSION;
}

static void precalc_lfo_offset(MultiParam mp_id) {
	s32 offset = 0;
	s16* param = &cur_preset.params[param_from_multi_param[mp_id]][SRC_LFO_A];
	for (u8 lfo_id = 0; lfo_id < NUM_LFOS; lfo_id++)
		offset += lfo_cur[lfo_id] * param[lfo_id];
	multi_param_lfo_offset[mp_id] = offset;
}

void params_tick(void) {
	// envelope 2
	for (MultiParam mp_id = MP_ENV_LVL2; mp_id <= MP_RELEASE2; mp_id++)
		precalc_lfo_offset(mp_id);
	bool any_envelope_triggered = false;
	max_pres_global = 0;
	max_envelope2 = 0;
	for (u8 string_id = 0; string_id < NUM_STRINGS; ++string_id) {
		const SynthString* s_string = get_synth_string(string_id);
		// update envelope 2
		Envelope* env = &envelope2[string_id];
		if (s_string->env_trigger) {
			// reset envelope
			env->level = 0.f;
			env->decaying = false;
			// generate new sample & hold id
			sample_hold[string_id] += 4813;
			any_envelope_triggered = true;
		}
		bool touching = s_string->touched;
		// touching the string
		float lvl_goal = touching ? (env->decaying)
		                                // decay stage: 2 times sustain parameter
		                                ? 2.f * (param_val_multi(MP_SUSTAIN2, string_id) * (1.f / 65536.f))
		                                // attack stage: we aim for 2.2, the actual peak is at 2.0
		                                : 2.2f
		                          // not touching, release stage: aim for 0
		                          : 0.f;
		float lvl_diff = lvl_goal - env->level;
		// get multiplier size (scaled exponentially)
		float k = lpf_k(param_val_multi((lvl_diff > 0.f)
		                                    // positive difference => moving up => attack param
		                                    ? MP_ATTACK2
		                                    : (env->decaying && touching)
		                                          // negative difference and decaying => decay param
		                                          ? MP_DECAY2
		                                          // negative difference and not decaying => release param
		                                          : MP_RELEASE2,
		                                string_id));
		// change env level by fraction of difference
		env->level += lvl_diff * k;
		// if we went past the peak during the attack stage, start the decay stage
		if (env->level >= 2.f && touching)
			env->decaying = true;
		// scale the envelope from a roughly [0, 2] float, to a u16 range scaled by the envelope level parameter
		env->level16 = SATURATE17(env->level * param_val_multi(MP_ENV_LVL2, string_id));

		// collect max pressure
		max_pres_global = maxi(max_pres_global, s_string->touch.pres);
		// collect max envelope
		max_envelope2 = maxf(max_envelope2, env->level16);
	}
	// scale range pressure to u16 range
	max_pres_global <<= 5;
	// generate global sample & hold random value on new touch
	if (any_envelope_triggered)
		sample_hold_global += 4813;

	accel_tick();

	lfos_tick();

	// apply lfo modulation to multi params
	for (MultiParam mp_id = 0; mp_id < NUM_MULTI_PARAMS; ++mp_id) {
		// we already did envelope 2 above
		if (mp_id == MP_ENV_LVL2)
			mp_id = MP_SCRUB;
		precalc_lfo_offset(mp_id);
	}

	// precalc
	arp_toggle = 0;
	if (ui_mode != UI_SAMPLE_EDIT && seq_state() != SEQ_STEP_RECORDING) {
		for (u8 i = 0; i < NUM_STRINGS; i++)
			if (param_index_multi(MP_ARP_TGL, i))
				arp_toggle |= 1 << i;
	}
	latch_toggle = 0;
	for (u8 i = 0; i < NUM_STRINGS; i++)
		if (param_index_multi(MP_LATCH_TGL, i))
			latch_toggle |= 1 << i;
}

// == RETRIEVAL == //

// raw parameter value, range -1024 to 1024
static s16 param_val_raw(Param param_id, ModSource mod_src) {
	return param_id == P_VOLUME ? (sys_params.volume_msb << 8) + sys_params.volume_lsb
	                            : (IS_GLOBAL_LAYOUT(param_id) && mod_src == SRC_BASE
	                                   ? global_data.layout_params[layout_param_from_param[param_id]][0]
	                                   : cur_preset.params[param_id][mod_src]);
}

static s16 param_val_raw_multi(Param param_id, u8 string_id) {
	return IS_GLOBAL_LAYOUT(param_id)
	           ? global_data.layout_params[layout_param_from_param[param_id]][string_id]
	           : (string_id == 0 ? cur_preset.params[param_id][SRC_BASE]
	                             : cur_preset.multi_params[multi_param_from_param[param_id]][string_id - 1]);
}

// param value range +/- 65536

s32 param_val(Param param_id) {
	s16* param = cur_preset.params[param_id];

	// add 16 precision bits to the raw value
	s32 mod_val = param[SRC_BASE] << 16;

	// apply envelope 2 modulation
	mod_val += max_envelope2 * param[SRC_ENV2];

	// apply pressure modulation
	mod_val += max_pres_global * param[SRC_PRES];

	// apply lfo modulation
	for (u8 lfo_id = 0; lfo_id < NUM_LFOS; lfo_id++)
		mod_val += lfo_cur[lfo_id] * param[SRC_LFO_A + lfo_id];

	// apply sample & hold modulation
	if (param[SRC_RND]) {
		u16 rnd_id = (u16)(sample_hold_global + param_id);
		// positive => uniform distribution
		if (param[SRC_RND] > 0)
			mod_val += (rndtab[rnd_id] * param[SRC_RND]) << 8;
		// negative => triangular distribution
		else {
			rnd_id += rnd_id;
			mod_val += ((rndtab[rnd_id] - rndtab[rnd_id - 1]) * param[SRC_RND]) << 8;
		}
	}

	// all 7 mod sources have now been applied, scale and clamp to 16 bit
	return clampi(mod_val >> 10, PARAM_SIGNED(param_id) ? -65536 : 0, 65536);
}

s32 param_val_multi(MultiParam mp_id, u8 string_id) {
	Param param_id = param_from_multi_param[mp_id];
	s16* param = cur_preset.params[param_id];

	// add 16 precision bits to the raw value
	s16 raw = param_val_raw_multi(param_id, string_id);
	s32 mod_val = raw << 16;

	// apply envelope 2 modulation
	mod_val += envelope2[string_id].level16 * param[SRC_ENV2];

	// apply pressure modulation
	mod_val += clampi(get_synth_string(string_id)->touch.pres << 5, 0, 65535) * param[SRC_PRES];

	// apply lfo modulation
	mod_val += multi_param_lfo_offset[mp_id];

	// apply sample & hold modulation
	if (param[SRC_RND]) {
		u16 rnd_id = (u16)(sample_hold[string_id] + param_id);
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
	return clampi(mod_val >> 10, PARAM_SIGNED(param_id) ? -65536 : 0, 65536);
}

// index value is scaled to its appropriate range

s8 param_index(Param param_id) {
	u8 range = PARAM_RANGE(param_id);
	s8 index = VALUE_TO_INDEX(param_val(param_id), range);
	// revert from being stored 1-based
	if (param_id == P_SAMPLE)
		index = (index - 1 + range) % range;
	return index;
}

s8 param_index_multi(MultiParam mp_id, u8 string_id) {
	return VALUE_TO_INDEX(param_val_multi(mp_id, string_id), PARAM_RANGE(param_from_multi_param[mp_id]));
}

s8 param_index_unmod(Param param_id) {
	return RAW_TO_INDEX(cur_preset.params[param_id][SRC_BASE], PARAM_RANGE(param_id));
}

u8 param_cc_value(Param param_id) {
	return raw_to_cc(param_val_raw(param_id, SRC_BASE), param_id);
}

bool get_param_nrpn_value(Param param_id, ModSource mod_src, bool allow_global_layout, u14* nrpn_value) {
	if (range_type[param_id] == R_UNUSED || param_id == P_VOLUME)
		return false;

	nrpn_value->value = raw_to_u14(allow_global_layout && mod_src == SRC_BASE && IS_GLOBAL_LAYOUT(param_id)
	                                   ? global_data.layout_params[layout_param_from_param[param_id]][0]
	                                   : cur_preset.params[param_id][mod_src],
	                               param_id, mod_src);
	return true;
}

bool get_param_nrpn_value_multi(Param param_id, u8 string_id, bool allow_global_layout, u14* nrpn_value) {
	nrpn_value->value =
	    raw_to_u14(allow_global_layout && IS_GLOBAL_LAYOUT(param_id)
	                   ? global_data.layout_params[layout_param_from_param[param_id]][string_id]
	                   : (string_id == 0 ? cur_preset.params[param_id][SRC_BASE]
	                                     : cur_preset.multi_params[multi_param_from_param[param_id]][string_id - 1]),
	               param_id, SRC_BASE);
	return true;
}

// == SAVING == //

void save_param_raw(Param param_id, ModSource mod_src, s16 data) {
	// special case
	if (param_id == P_VOLUME) {
		set_sys_param(SYS_VOLUME, data);
		return;
	}

	bool using_global_layout = IS_GLOBAL_LAYOUT(param_id) && mod_src == SRC_BASE;
	s16* target = using_global_layout ? &global_data.layout_params[layout_param_from_param[param_id]][0]
	                                  : &cur_preset.params[param_id][mod_src];
	// don't save if no change
	if (data == *target)
		return;

	// CCs
	if ((sys_params.midi_send_param_ccs == SP_CC && raw_to_cc(data, param_id) != raw_to_cc(*target, param_id))
	    // NRPNs
	    || (sys_params.midi_send_param_ccs == SP_NRPN
	        && raw_to_u14(data, param_id, mod_src) != raw_to_u14(*target, param_id, mod_src)))
		// send to midi
		midi_send_param(param_id, mod_src);

	// save base value
	*target = data;
	// save to all strings
	if (PARAM_IS_MULTI_TIMBRAL(param_id) && mod_src == SRC_BASE) {
		if (using_global_layout) {
			s16* val = global_data.layout_params[layout_param_from_param[param_id]];
			val[7] = val[6] = val[5] = val[4] = val[3] = val[2] = val[1] = val[0];
		}
		else {
			s16* val = cur_preset.multi_params[multi_param_from_param[param_id]];
			val[6] = val[5] = val[4] = val[3] = val[2] = val[1] = val[0] = cur_preset.params[param_id][SRC_BASE];
		}
	}

	log_ram_edit(using_global_layout ? SEG_GLOBAL_DATA : SEG_PRESET);
}

void save_multi_param_raw(Param param_id, u8 string_id, s16 data) {
	bool using_global_layout = IS_GLOBAL_LAYOUT(param_id);
	s16* target = using_global_layout ? &global_data.layout_params[layout_param_from_param[param_id]][string_id]
	              : string_id > 0     ? &cur_preset.multi_params[multi_param_from_param[param_id]][string_id - 1]
	                                  : &cur_preset.params[param_id][SRC_BASE];

	// don't save if no change
	if (data == *target)
		return;

	// CCs (only track string 0)
	if (sys_params.midi_send_param_ccs == SP_CC && string_id == 0
	    && raw_to_cc(data, param_id) != raw_to_cc(*target, param_id))
		midi_send_param(param_id, SRC_BASE);
	// NRPNs
	else if (sys_params.midi_send_param_ccs == SP_NRPN
	         && raw_to_u14(data, param_id, SRC_BASE) != raw_to_u14(*target, param_id, SRC_BASE))
		midi_send_multi_param(multi_param_from_param[param_id], 1 << string_id);

	// save
	*target = data;

	log_ram_edit(using_global_layout ? SEG_GLOBAL_DATA : SEG_PRESET);
}

void save_param_index(Param param_id, s8 index) {
	u8 range = PARAM_RANGE(param_id);
	// save 1-based
	if (param_id == P_SAMPLE)
		index = (index + 1) % range;
	index = clampi(index, PARAM_SIGNED(param_id) ? -(range - 1) : 0, range - 1);
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
		select_param(new_param);
		return true;
	}
	// restore other param on same pad
	if (EDITING_PARAM && (selected_param % 12 < 6) != mode_a) {
		u8 new_param = selected_param + (mode_a ? -6 : 6);
		if (range_type[new_param] == R_UNUSED)
			flash_message(F_20_BOLD, I_CROSS "No Param", 0);
		else
			select_param(new_param);
		return true;
	}
	// nothing to restore
	return false;
}

void close_edit_mode(void) {
	mem_param = selected_param;
	selected_param = NUM_PARAMS;
	clear_last_encoder_use();
}

static void reset_edit_strip_pos(u8 strip_id) {
	edit_strip_start_pos = edit_param_multi(selected_param, selected_mod_src)
	                           ? param_val_raw_multi(selected_param, strip_id)
	                           : param_val_raw(selected_param, selected_mod_src);
	set_smoother(&edit_strip_pos[strip_id], edit_strip_start_pos);
}

void touch_edit_strip(u8 strip_id, u16 position, bool is_press_start) {
	static const u16 STRIP_DEADZONE = 256;
	static const float HALF_CENTER_DEADZONE = 32.f;

	if (!EDITING_PARAM)
		return;

	selected_edit_strip = strip_id;

	if (is_press_start)
		reset_edit_strip_pos(strip_id);

	// scale the press position to a param size value
	float press_value =
	    clampf((TOUCH_MAX_POS - STRIP_DEADZONE - position) * (RAW_SIZE / (TOUCH_MAX_POS - 2.f * STRIP_DEADZONE)), 0.f,
	           RAW_SIZE);
	bool is_signed = PARAM_SIGNED(selected_param) || selected_mod_src != SRC_BASE;
	if (is_signed)
		press_value = press_value * 2 - RAW_SIZE;
	// smooth the pressed value
	smooth_value(&edit_strip_pos[strip_id], press_value, 256);
	float smoothed_value = clampf(edit_strip_pos[strip_id].y2, is_signed ? -RAW_SIZE - 0.1f : 0.f, RAW_SIZE + 0.1f);
	// value stops exactly at +/- 100%
	bool notch_at_50 =
	    selected_mod_src == SRC_BASE && (selected_param == P_PLAY_SPD || selected_param == P_SMP_STRETCH);
	if (notch_at_50) {
		if (smoothed_value < RAW_HALF && edit_strip_start_pos > RAW_HALF)
			smoothed_value = RAW_HALF;
		if (smoothed_value > RAW_HALF && edit_strip_start_pos < RAW_HALF)
			smoothed_value = RAW_HALF;
		if (smoothed_value < -RAW_HALF && edit_strip_start_pos > -RAW_HALF)
			smoothed_value = -RAW_HALF;
		if (smoothed_value > -RAW_HALF && edit_strip_start_pos < -RAW_HALF)
			smoothed_value = -RAW_HALF;
	}

	s16 raw = smoothed_value + (smoothed_value > 0 ? 0.5f : -0.5f);
	// snap to index value
	if (param_is_index(selected_param, selected_mod_src, raw)) {
		u8 range = PARAM_RANGE(selected_param);
		raw = INDEX_TO_RAW(RAW_TO_INDEX(raw, range), range);
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
	if (edit_param_multi(selected_param, SRC_BASE))
		save_multi_param_raw(selected_param, strip_id, raw);
	else
		save_param_raw(selected_param, selected_mod_src, raw);
}

void press_param_pad(u8 pad_id, bool is_press_start) {
	u8 prev_param = selected_param;
	select_param((pad_id & 7) * 12 + ((pad_id >> 3) - 1) + (ui_mode == UI_EDITING_B ? 6 : 0));
	// pressed an unused param
	if (range_type[selected_param] == R_UNUSED) {
		select_param(prev_param);
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
	if (selected_param != prev_param) {
		for (u8 strip_id = 0; strip_id < NUM_STRINGS; strip_id++)
			reset_edit_strip_pos(strip_id);
	}
}

void press_mod_pad(u8 pad_y) {
	if (selected_param == P_VOLUME) {
		flash_message(F_20_BOLD, I_CROSS "No Mod", 0);
		return;
	}
	selected_mod_src = pad_y;
}

void toggle_multi_edit(Param param_id) {
	MultiParam mp_id = multi_param_from_param[param_id];
	u8 bank = mp_id >> 3;
	u8 mask = 1 << (mp_id % 8);
	global_data.edit_multi_timbral[bank] ^= mask;
	log_ram_edit(SEG_GLOBAL_DATA);
	selected_edit_strip = 0;
	const char* name;
	switch (param_id) {
	case P_SHAPE:
		name = "Shape";
		break;
	case P_ENV_LVL2:
		name = "Env Level 2";
		break;
	case P_ARP_TGL:
		name = "Arp Enable";
		break;
	case P_LATCH_TGL:
		name = "Latch Enable";
		break;
	default:
		name = param_name[param_id] + 1;
		break;
	}

	char name_buf[16];
	snprintf(name_buf, sizeof(name_buf), "%s", name);
	if (param_id >= P_ATTACK1 && param_id <= P_RELEASE1)
		strncat(name_buf, " 1", sizeof(name_buf) - strlen(name_buf) - 1);
	else if (param_id >= P_ATTACK2 && param_id <= P_RELEASE2)
		strncat(name_buf, " 2", sizeof(name_buf) - strlen(name_buf) - 1);

	flash_message(F_16_BOLD, "%s", name_buf,
	              global_data.edit_multi_timbral[bank] & mask ? "Multi Editing On" : "Multi Editing Off");
}

// == ENCODER == //

void edit_param_from_encoder(s8 enc_diff, float enc_acc) {
	Param param_id = RECENT_PARAM;
	if (param_id >= NUM_PARAMS)
		return;

	// if this is a precision-edit, keep the param selected
	if (function_pressed == FN_SHIFT_A || function_pressed == FN_SHIFT_B)
		pad_actions_keep_edit_mode_open();

	bool edit_multi = edit_param_multi(param_id, selected_mod_src);
	s16 raw =
	    edit_multi ? param_val_raw_multi(param_id, selected_edit_strip) : param_val_raw(param_id, selected_mod_src);
	u8 range = PARAM_RANGE(param_id);

	// negative values of delay/dual clock are unranged
	if ((range_type[param_id] == R_DLYCLK || range_type[param_id] == R_DUACLK) && raw < 0)
		range = 0;

	// indeces: just add/subtract 1 per encoder tick
	if (range && selected_mod_src == SRC_BASE) {
		s16 index = RAW_TO_INDEX(raw, range) + enc_diff;
		raw = INDEX_TO_RAW(clampi(index, PARAM_SIGNED(param_id) ? -(range - 1) : 0, range - 1), range);
		// smooth transition between synced and free timing
		if ((range_type[param_id] == R_DLYCLK || range_type[param_id] == R_DUACLK) && index < 0)
			raw = -1;
		if (edit_multi)
			save_multi_param_raw(param_id, selected_edit_strip, raw);
		else
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
	raw = clampi(raw, (PARAM_SIGNED(param_id) || selected_mod_src != SRC_BASE) ? -RAW_SIZE : 0, RAW_SIZE);
	if (edit_multi)
		save_multi_param_raw(param_id, selected_edit_strip, raw);
	else
		save_param_raw(param_id, selected_mod_src, raw);
}

void params_toggle_default_value(void) {
	static u16 param_hash = NUM_PARAMS << 6;
	static s16 saved_raw = INT16_MAX;

	Param param_id = RECENT_PARAM;
	if (param_id >= NUM_PARAMS)
		return;

	bool edit_multi = edit_param_multi(param_id, selected_mod_src);

	// clear saved value when we're seeing a new parameter
	u16 new_hash = (param_id << 6) + (selected_mod_src << 3) + (edit_multi ? selected_edit_strip : 0);
	if (new_hash != param_hash) {
		saved_raw = INT16_MAX;
		param_hash = new_hash;
	}

	s16 cur_raw =
	    edit_multi ? param_val_raw_multi(param_id, selected_edit_strip) : param_val_raw(param_id, selected_mod_src);
	s16 init_raw = edit_multi && selected_edit_strip != 0
	                   ? init_params.multi_params[multi_param_from_param[param_id]][selected_edit_strip - 1]
	                   : init_params.params[param_id][selected_mod_src];
	// first press: save current value and set init value
	if (cur_raw != init_raw || saved_raw == INT16_MAX) {
		saved_raw = cur_raw;
		if (edit_multi)
			save_multi_param_raw(param_id, selected_edit_strip, init_raw);
		else
			save_param_raw(param_id, selected_mod_src, init_raw);
	}
	// second press: restore saved value
	else {
		if (edit_multi)
			save_multi_param_raw(param_id, selected_edit_strip, saved_raw);
		else
			save_param_raw(param_id, selected_mod_src, saved_raw);
	}
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
	mod_action_snap = mod_action_pressed();
}

void draw_preset_info(void) {
	// top-left, priority: cued preset, last pressed note, current preset
	u8 xtab = draw_cued_preset_id();
	if (!xtab)
		xtab = draw_high_note();
	if (!xtab)
		xtab = draw_preset_id();
	// step recording fills the center of the screen
	if (seq_state() != SEQ_STEP_RECORDING) {
		// tuning name replaces preset name
		if (!sys_params.midi_tuning || !draw_midi_tuning_name())
			draw_preset_name(xtab);
	}
	// bottom left priority: cued pattern, current pattern
	xtab = draw_cued_pattern_id(arp_toggle);
	if (!xtab)
		draw_pattern_id(arp_toggle);
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

	u8 range = PARAM_RANGE(param_id);
	if ((range_type[param_id] == R_DLYCLK || range_type[param_id] == R_DUACLK) && raw < 0)
		range = 0;

	// indeces
	if (range) {
		s8 index = RAW_TO_INDEX(raw, range);
		switch (param_id) {
		case P_SCALE:
			return scale_name[index];
		case P_ARP_ORDER:
			return arp_mode_name[index];
		case P_SEQ_ORDER:
			return seq_mode_name[index];
		case P_SEQ_CLK_DIV:
			if (index == NUM_SYNC_DIVS) {
				sprintf(val_buf, "Gate cv");
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
		case R_ROOT:
			sprintf(val_buf, "%s", note_name((index), false));
			return val_buf;
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
	for (u8 mod_src = SRC_ENV2; mod_src < NUM_MOD_SOURCES; mod_src++)
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
	ModSource draw_src;
	// standard param editing
	if (param_snap < NUM_PARAMS) {
		draw_param = param_snap;
		draw_src = src_snap;
	}
	// no valid snap param => this must be an encoder edit
	else {
		draw_param = mem_param;
		draw_src = selected_mod_src;
	}

	// value data
	Font font;
	char val_buf[32];
	u8 width = 0;
	u8 x_center = 0;
	u8 x;

	bool edit_multi = edit_param_multi(draw_param, draw_src);
	s16 raw = edit_multi ? param_val_raw_multi(draw_param, selected_edit_strip) : param_val_raw(draw_param, draw_src);

	gfx_text_color = 3;
	// draw section name
	const char* sect_str;
	if (draw_src == SRC_BASE) {
		sect_str = param_row_name[draw_param / 6];

		// manual section name overrides
		switch (draw_param) {
		case P_OCT:
		case P_SCALE:
		case P_COLUMN:
		case P_ROOT:
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
		if (draw_src == SRC_RND && raw < 0)
			sect_str = I_TILT "Rand >>";
		else
			sect_str = mod_src_name[draw_src];
	}

	u8 text_x = 17;
	u8 text_right_x = OLED_WIDTH - 16;
	// draw section icon
	draw_str(0, sect_str[0] == I_NOTES[0] ? 1 : 0, F_12, (char[]){sect_str[0], '\0'});
	// draw section name
	u8 multi_id_x = draw_str(text_x, 3, F_12, sect_str + 1) + (draw_param == P_SCALE ? 0 : 1);
	// draw multi param string number
	if (edit_multi) {
		fill_rectangle(multi_id_x, 2, multi_id_x + 8, 13);
		gfx_text_color = 0;
		fdraw_str(multi_id_x
		              + (selected_edit_strip == 0   ? 3
		                 : selected_edit_strip == 3 ? 2
		                                            : 1),
		          3, F_12, "%u", selected_edit_strip + 1);
		gfx_text_color = 1;
	}

	// modulated value
	if (draw_src != SRC_BASE || has_modulation(draw_param)) {
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
			if (draw_src == SRC_BASE)
				raw = raw_mod;
			break;
		default:
			const char* mod_val_str = get_param_str(draw_param, SRC_BASE, raw_mod, val_buf);
			font = F_12;
			width = str_width(font, mod_val_str);
			x = text_right_x - width;
			draw_str(x, 21, font, mod_val_str);
			break;
		}
	}

	// main value
	font = F_20_BOLD;
	const char* val_str = get_param_str(draw_param, draw_src, raw, val_buf);
	strcpy(val_buf, val_str);

	if (draw_src == SRC_BASE) {
		switch (draw_param) {
		case P_SCALE:
			font = F_12;
			x_center = 83;
			break;
		case P_ARP_ORDER:
			font = F_12;
			x_center = 81;
			break;
		case P_SEQ_ORDER:
			font = F_12;
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
			font = F_12;
			x_center = 85;
			break;
		default:
			break;
		}
	}

	// draw second line
	char* newline_pos = strchr(val_buf, '\n');
	if (newline_pos) {
		char* val_buf2 = newline_pos + 1;
		width = str_width(font, val_buf2);
		x = x_center == 0 ? text_right_x - width : x_center - width / 2;
		draw_str(x, 18, font, val_buf2);
		*newline_pos = '\0';
	}
	// draw first line
	width = str_width(font, val_buf);
	x = x_center == 0 ? text_right_x - width : x_center - width / 2;
	if (text_x + str_width(F_12, sect_str + 1) > 58 && +val_buf[0] == '-' && strlen(val_buf) == 6) {
		font--;
		width = str_width(font, val_buf);
		x = x_center == 0 ? text_right_x - width : x_center - width / 2;
	}
	u8 y;
	switch (font) {
	case F_12:
		y = 3;
		break;
	case F_16_BOLD:
		y = 1;
		break;
	default:
		y = 0;
		break;
	}
	draw_str(x, y, font, val_buf);

	// draw param name

	char icon_str[2] = {' ', '\0'};
	char name_str[16];
	u8 icon_y = 0;
	u8 text_y = 0;

	//  special negative ranges
	if (raw < 0) {
		switch (draw_param) {
		case P_SHAPE:
			icon_str[0] = *I_SHAPE;
			strcpy(name_str, "PulseWidth");
			icon_y = 18;
			text_y = 20;
			break;
		case P_SWING:
			icon_str[0] = *I_TILT;
			strcpy(name_str, "Swing 16th");
			icon_y = 18;
			text_y = 18;
			break;
		case P_ARP_CHANCE:
		case P_SEQ_CHANCE:
			icon_str[0] = *I_PERCENT;
			strcpy(name_str, "Chance (W)");
			icon_y = 18;
			text_y = 20;
			break;
		default:
			break;
		}
		switch (range_type[draw_param]) {
		case R_DLYCLK:
		case R_DUACLK:
			icon_str[0] = *I_TIME;
			strcpy(name_str, "Rate");
			icon_y = 18;
			text_y = 20;
			break;
		default:
			break;
		}
	}

	// special incidental  cases
	u8 range = PARAM_RANGE(draw_param);
	s8 index = RAW_TO_INDEX(raw, range);
	if (draw_param == P_SEQ_CLK_DIV && index == range - 1) {
		icon_str[0] = *I_JACK;
		strcpy(name_str, "Trigger");
		icon_y = 18;
		text_y = 18;
	}
	if (draw_param == P_SHAPE && raw == 0) {
		icon_str[0] = *I_SHAPE;
		strcpy(name_str, "SuperSaw");
		icon_y = 18;
		text_y = 20;
	}

	const char* p_name = param_name[draw_param];

	// icon default
	if (icon_y == 0) {
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
	}

	// draw icon
	draw_str(0, icon_y, F_12, icon_str);

	// name default
	if (text_y == 0) {
		// y
		switch (draw_param) {
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
		// name
		strcpy(name_str, p_name + 1);
	}

	// draw name
	multi_id_x = draw_str(text_x, text_y, F_12, name_str);

	// multi-timbral param indicator
	if ((ui_mode == UI_EDITING_A || ui_mode == UI_EDITING_B) && PARAM_IS_MULTI_TIMBRAL(draw_param))
		fdraw_str(multi_id_x, text_y - 1, F_8, "M");
}

void draw_arp_flag(void) {
	gfx_text_color = 0;
	if (arp_active_mask()) {
		fill_rectangle(128 - 32, 0, 128 - 17, 8);
		draw_str(-(128 - 17), -1, F_8, "arp");
	}
}

void draw_latch_flag(void) {
	gfx_text_color = 0;
	if (latch_toggle) {
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

static u8 col_led(float brightness, bool selected) {
	const float dim_mult = 0.4f;
	u8 bg = selected ? 48 : 32;
	return (clampf(brightness, 0.f, 1.f) * (selected ? 1.f : dim_mult) * (255 - bg) + bg);
}

u8 value_editor_column_led(u8 x, u8 y) {
	// no param selected
	if (param_snap >= NUM_PARAMS)
		return 0;

	// non-zero strip when not multi editing
	bool edit_multi = editing_multi_param();
	if (x > 0 && !edit_multi)
		return 0;

	bool selected = !edit_multi || x == selected_edit_strip;
	bool is_signed = PARAM_SIGNED(param_snap) || src_snap != SRC_BASE;
	s16 raw = edit_multi ? param_val_raw_multi(param_snap, x) : param_val_raw(param_snap, src_snap);
	u8 pad_id = 7 - y;
	u8 range = src_snap == SRC_BASE ? PARAM_RANGE(param_snap) : 0;
	float pad_pos = raw * 7 / (range ? (float)INDEX_TO_RAW(range - 1, range) : 1024.f);
	// small ranges light up discrete leds
	if (range > 0 && (is_signed ? 2 * range - 1 : range) <= 9)
		pad_pos = truncf(pad_pos);
	if (is_signed) {
		// absolute center
		if (raw == 0 && (y == 3 || y == 4))
			return col_led(1, selected);
		// mapped
		pad_pos = fabs(pad_pos) / 2.f - 0.5f;
		switch (pad_id) {
		// negative
		case 0:
		case 1:
		case 2:
			if (raw > 0)
				return col_led(0, selected);
			if (pad_id >= 3 - (u8)pad_pos)
				return col_led(1, selected);
			if (pad_id == 2 - (u8)pad_pos)
				return col_led(fmod(pad_pos, 1), selected);
			break;
		// center
		case 3:
		case 4:
			if ((raw < 0) ^ (pad_id == 4))
				return col_led(1, selected);
			if (pad_pos < 0)
				return col_led(-2 * pad_pos, selected);
			break;
		// positive
		case 5:
		case 6:
		case 7:
			if (raw < 0)
				return col_led(0, selected);
			if (pad_id <= (u8)pad_pos + 4)
				return col_led(1, selected);
			if (pad_id == (u8)pad_pos + 5)
				return col_led(fmod(pad_pos, 1), selected);
			break;
		}
	}
	else {
		if (pad_id <= (u8)pad_pos)
			return col_led(1, selected);
		if (pad_id == (u8)pad_pos + 1)
			return col_led(fmod(pad_pos, 1), selected);
	}
	return col_led(0, selected);
}

u8 ui_editing_led(u8 x, u8 y, u8 pulse) {
	bool param_selected = param_snap < NUM_PARAMS;
	bool show_mod_targets = mod_action_snap && src_snap != SRC_BASE;
	u8 k = 0;
	// edit strip
	if (x == 0) {
		if (param_selected && !show_mod_targets)
			k = value_editor_column_led(x, y);
	}
	// parameters
	else if (x < 7) {
		u8 pAorB = x - 1 + y * 12 + (ui_mode == UI_EDITING_B ? 6 : 0);
		// pulse selected param
		if (!show_mod_targets && pAorB == param_snap)
			k = pulse;
		// holding down a mod source => light up params that are modulated by it
		if (show_mod_targets && pAorB != P_VOLUME && param_val_raw(pAorB, src_snap))
			k = 255;
	}
	// mod sources
	else {
		// light up held mod source
		if (show_mod_targets)
			k = y == src_snap ? 255 : 0;
		// pulse active mod source
		else if (y == src_snap)
			k = pulse;
		// light up mod sources that modulate current param
		else if (param_selected)
			k = (y && param_val_raw(param_snap, y) && param_snap != P_VOLUME) ? 255 : 0;
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