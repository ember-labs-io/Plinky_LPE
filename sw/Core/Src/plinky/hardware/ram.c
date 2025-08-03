#include "ram.h"
#include "flash.h"
#include "gfx/gfx.h"
#include "synth/params.h"

// cleanup
#include "hardware/codec.h"
#include "synth/sequencer.h"
#include "synth/strings.h"
// -- cleanup

#define NUM_RAM_ITEMS (NUM_PRESETS + NUM_PATTERNS + NUM_SAMPLES)

typedef enum RamItemType {
	RAM_PRESET,
	RAM_PATTERN,
	RAM_SAMPLE,
	NUM_ITEM_TYPES,
} RamItemType;

typedef enum PresetFlag {
	FLAG_ARP = 1,
	FLAG_LATCH = 2,
} PresetFlag;

SysParams sys_params;

// item we are (or want to be) editing
// u8 cur_preset_id ?
static u8 cur_pattern_id = 0;
u8 cur_sample_id = 0;

// item actually in ram
static u8 ram_preset_id = 255;
static u8 ram_pattern_id = 255;
static u8 ram_sample_id = 255;

// ram item contents
Preset cur_preset;
static PatternQuarter cur_ptn_quarter[4];
SampleInfo cur_sample_info;

// item to change to
static u8 cued_preset_id = 255;
static u8 cued_pattern_id = 255;
static u8 cued_sample_id = 255;

// history of above, used to check double press on the same item
static u8 prev_cued_preset_id = 255;
static u8 prev_cued_pattern_id = 255;
static u8 prev_cued_sample_id = 255;

// item to copy from
static u8 copy_preset_id = 0;
static u8 copy_pattern_id = 0;
static u8 copy_sample_id = 0;

static u8 edit_item_id = 255;   // ram item to edit | msb unset => copy or toggle, msb set => clear
static u8 recent_load_item = 0; // the most recently touched load item

static u32 last_ram_write[NUM_RAM_SEGMENTS];
static u32 last_flash_write[NUM_RAM_SEGMENTS];

// == UTILS == //

static RamItemType get_item_type(u8 item_id) {
	return item_id < PATTERNS_START  ? RAM_PRESET
	       : item_id < SAMPLES_START ? RAM_PATTERN
	       : item_id < NUM_RAM_ITEMS ? RAM_SAMPLE
	                                 : NUM_RAM_ITEMS;
}

// when the current item is not equal to the item in ram, this indicates we're still writing the old ram item to flash
static bool preset_outdated(void) {
	return sys_params.curpreset != ram_preset_id;
}
bool pattern_outdated(void) {
	return cur_pattern_id != ram_pattern_id;
}
static bool sample_outdated(void) {
	return cur_sample_id != ram_sample_id;
}
// is the arp turned on in the preset?
bool arp_on(void) {
	return cur_preset.flags & FLAG_ARP;
}
// is the arp actively being executed?
bool arp_active(void) {
	return arp_on() && ui_mode != UI_SAMPLE_EDIT && seq_state() != SEQ_STEP_RECORDING;
}

void save_arp(bool on) {
	if (on)
		cur_preset.flags |= FLAG_ARP;
	else
		cur_preset.flags &= ~FLAG_ARP;
}
bool latch_on(void) {
	return cur_preset.flags & FLAG_LATCH;
}
void save_latch(bool on) {
	if (on)
		cur_preset.flags |= FLAG_LATCH;
	else
		cur_preset.flags &= ~FLAG_LATCH;
}

// == MAIN == //

// only_filled returns 0 if the step doesn't hold any pressure data
PatternStringStep* string_step_ptr(u8 string_id, bool only_filled, u8 seq_step) {
	if (preset_outdated() && only_filled)
		return 0;
	PatternStringStep* step = &cur_ptn_quarter[(seq_step >> 4) & 3].steps[seq_step & 15][string_id];
	if (!only_filled)
		return step;
	// return pointer if any of its substeps hold pressure
	for (u8 substep_id = 0; substep_id < 8; substep_id++)
		if (step->pres[substep_id])
			return step;
	// otherwise return 0
	return 0;
}

void init_ram(void) {
	cued_preset_id = -1;
	cued_pattern_id = -1;
	cued_sample_id = -1;
	ram_pattern_id = -1;
	ram_sample_id = -1;
	ram_preset_id = -1;
	// relocate the first preset and pattern into ram
	edit_item_id = 255;
	for (u8 i = 0; i < NUM_RAM_SEGMENTS; ++i) {
		last_ram_write[i] = 0;
		last_flash_write[i] = 0;
	}
	codec_set_volume(sys_params.headphonevol + 45);
	recent_load_item = sys_params.curpreset;
}

static bool need_flash_write(RamSegment seg, u32 now) {
	// segment up to date => no write
	if (last_ram_write[seg] == last_flash_write[seg])
		return false;

	// a ram item (preset, pattern, sample) being outdated means the user has requested to load a different one, but
	// that load has not happened yet because the current item hasn't finished writing to flash - we need to write it to
	// flash immediately so the new item can be loaded
	switch (seg) {
	case SEG_PRESET:
		if (preset_outdated())
			return true;
		break;
	case SEG_PAT0:
	case SEG_PAT1:
	case SEG_PAT2:
	case SEG_PAT3:
		if (pattern_outdated())
			return true;
		break;
	case SEG_SAMPLE:
		if (sample_outdated())
			return true;
		break;
	default:
		break;
	}

	// if our ram item was not outdated, that means we changed something small (a parameter, the contents of a step,
	// etc) we try to wait for at least 5 seconds after the most recent edit before we write them to flash
	if (now - last_ram_write[seg] > 5000)
		return true;
	// but if we are out of date for a minute or more, we write immediately
	return last_ram_write[seg] - last_flash_write[seg] > 60000;
}

void ram_frame(void) {

	u32 now = millis();

	// handle requested edits
	if (edit_item_id != 255) {
		RamItemType item_type = get_item_type(edit_item_id & 63);
		// msb set => clear
		if (edit_item_id & 128) {
			edit_item_id &= 63;
			switch (item_type) {
			case RAM_PRESET:
				memcpy(&cur_preset, init_params_ptr(), sizeof(cur_preset));
				last_ram_write[SEG_PRESET] = now;
				break;
			case RAM_PATTERN:
				memset(&cur_ptn_quarter, 0, sizeof(cur_ptn_quarter));
				last_ram_write[SEG_PAT0] = now;
				last_ram_write[SEG_PAT1] = now;
				last_ram_write[SEG_PAT2] = now;
				last_ram_write[SEG_PAT3] = now;
				break;
			case RAM_SAMPLE:
				memset(&cur_sample_info, 0, sizeof(SampleInfo));
				last_ram_write[SEG_SAMPLE] = now;
				break;
			default:
				break;
			}
		}
		// msb not set, preset tries to copy to itself => toggle
		else if (edit_item_id == copy_preset_id) {
			// flush any writes
			last_flash_write[SEG_SYS] = last_ram_write[SEG_SYS];
			last_flash_write[SEG_PRESET] = last_ram_write[SEG_PRESET];
			flash_write_page(&cur_preset, sizeof(Preset), ram_preset_id);
			// -- flush any writes
			flash_toggle_preset(copy_preset_id);
			memcpy(&cur_preset, preset_flash_ptr(sys_params.curpreset), sizeof(cur_preset));
			load_preset(edit_item_id, true);
		}
		// msb not set, not a toggle => copy
		else {
			switch (item_type) {
			case RAM_PRESET:
				flash_write_page(preset_flash_ptr(copy_preset_id), sizeof(Preset), edit_item_id);
				load_preset(edit_item_id, true);
				break;
			case RAM_PATTERN: {
				u8 src_page = 4 * copy_pattern_id;
				u8 dst_page = 4 * (edit_item_id - PATTERNS_START) + PATTERNS_START;
				for (u8 qtr = 0; qtr < 4; ++qtr)
					flash_write_page(ptn_quarter_flash_ptr(src_page + qtr), sizeof(PatternQuarter), dst_page + qtr);
				save_param(P_PATTERN, SRC_BASE, edit_item_id - PATTERNS_START);
			} break;
			default:
				// samples don't copy
				break;
			}
		}
		edit_item_id = 255;   // clear edit item
		ui_mode = UI_DEFAULT; // always return to default ui after an edit action
	}

	// write ram items to flash (auto-save)

	for (u8 qtr = 0; qtr < 4; ++qtr) {
		if (need_flash_write(SEG_PAT0 + qtr, now)) {
			last_flash_write[SEG_SYS] = last_ram_write[SEG_SYS];
			last_flash_write[SEG_PAT0 + qtr] = last_ram_write[SEG_PAT0 + qtr];
			flash_write_page(&cur_ptn_quarter[qtr], sizeof(PatternQuarter), PATTERNS_START + 4 * ram_pattern_id + qtr);
		}
	}
	if (need_flash_write(SEG_SAMPLE, now)) {
		last_flash_write[SEG_SYS] = last_ram_write[SEG_SYS];
		last_flash_write[SEG_SAMPLE] = last_ram_write[SEG_SAMPLE];
		if (ram_sample_id < NUM_SAMPLES)
			flash_write_page(&cur_sample_info, sizeof(SampleInfo), F_SAMPLES_START + ram_sample_id);
	}
	if (need_flash_write(SEG_PRESET, now) || need_flash_write(SEG_SYS, now)) {
		last_flash_write[SEG_SYS] = last_ram_write[SEG_SYS];
		last_flash_write[SEG_PRESET] = last_ram_write[SEG_PRESET];
		flash_write_page(&cur_preset, sizeof(Preset), ram_preset_id);
	}
}

// == UPDATE RAM == //

static bool segment_outdated(RamSegment seg) {
	return last_ram_write[seg] != last_flash_write[seg];
}

void log_ram_edit(RamSegment segment) {
	last_ram_write[segment] = millis();
}

// put cur_preset_id in here
bool update_preset_ram(bool force) {
	// already up to date
	if (!preset_outdated() && !force)
		return true;
	// flash is not ready
	if (flash_busy || segment_outdated(SEG_PRESET))
		return false;
	// retrieve preset from flash
	memcpy(&cur_preset, preset_flash_ptr(sys_params.curpreset), sizeof(cur_preset));
	ram_preset_id = sys_params.curpreset;

	// clear volume mod sources
	for (u8 m = 1; m < NUM_MOD_SOURCES; ++m)
		cur_preset.params[P_VOLUME][m] = 0;

	// upgrade preset to CUR_PRESET_VERSION
	switch (cur_preset.version) {
	case CUR_PRESET_VERSION:
		// correct!
		break;
	case 0:
		// add mix width, switch value with accel sensitivity
		for (u8 mod_id = 0; mod_id < NUM_MOD_SOURCES; ++mod_id) {
			s16 temp = cur_preset.params[P_MIX_WIDTH][mod_id];
			cur_preset.params[P_MIX_WIDTH][mod_id] = cur_preset.params[P_ACCEL_SENS][mod_id];
			cur_preset.params[P_ACCEL_SENS][mod_id] = temp;
		}
		// set default
		cur_preset.params[P_MIX_WIDTH][0] = HALF_PARAM_SIZE;
		cur_preset.version = 1;
		// fall through for further upgrading
	case 1:
		// add lfo saw shape
		for (u8 lfo_id = 0; lfo_id < NUM_LFOS; ++lfo_id) {
			s16* data = cur_preset.params[P_A_SHAPE + lfo_id * 6];
			*data = (*data * (NUM_LFO_SHAPES - 1)) / (NUM_LFO_SHAPES); // rescale to add extra enum entry
			if (*data >= (LFO_SAW * PARAM_SIZE) / NUM_LFO_SHAPES)      // and shift high numbers up
				*data += (1 * PARAM_SIZE) / NUM_LFO_SHAPES;
		}
		cur_preset.version = 2;
		// fall through for further upgrading
	}
	return true;
}

void update_pattern_ram(bool force) {
	cur_pattern_id = param_val(P_PATTERN);
	// already up to date
	if (!pattern_outdated() && !force)
		return;
	// flash is not ready
	if (flash_busy || segment_outdated(SEG_PAT0) || segment_outdated(SEG_PAT1) || segment_outdated(SEG_PAT2)
	    || segment_outdated(SEG_PAT3))
		return;
	// retrieve pattern from flash
	for (u8 qtr = 0; qtr < 4; ++qtr)
		memcpy(&cur_ptn_quarter[qtr], ptn_quarter_flash_ptr(4 * cur_pattern_id + qtr), sizeof(cur_ptn_quarter[0]));
	ram_pattern_id = cur_pattern_id;
}

void update_sample_ram(bool force) {
	cur_sample_id = param_val(P_SAMPLE);
	// already up to date
	if (!sample_outdated() && !force)
		return;
	// flash is not ready
	if (flash_busy || segment_outdated(SEG_SAMPLE))
		return;
	// retrieve sample info from flash
	if (cur_sample_id < NUM_SAMPLES)
		memcpy(&cur_sample_info, sample_info_flash_ptr(cur_sample_id), sizeof(SampleInfo));
	else
		memset(&cur_sample_info, 0, sizeof(SampleInfo));
	ram_sample_id = cur_sample_id;
}

// == SAVE / LOAD == //

void load_preset(u8 preset_id, bool force) {
	if (preset_id == sys_params.curpreset && !force)
		return;
	sys_params.curpreset = preset_id;
	log_ram_edit(SEG_SYS);
	update_preset_ram(force);
	clear_latch();
}

// rj: this function is exclusively used by open_sampler, we might want to look at using the regular loading
// implementation instead of this for open_sampler as well
void load_sample(u8 sample_id) {
	memcpy(&cur_sample_info, sample_info_flash_ptr(sample_id), sizeof(SampleInfo));
	cur_sample_id = sample_id;
	ram_sample_id = sample_id;
	cued_sample_id = 255;
}

// register the most recently touched ram item
void touch_load_item(u8 item_id) {
	recent_load_item = item_id;
}

// line up recent_load_item to be cleared during the next tick
void clear_load_item(void) {
	edit_item_id = recent_load_item | 128;
}

// line up item_id to be copied during the next tick
void copy_load_item(u8 item_id) {
	edit_item_id = item_id;
}

// returns true if there's a chance this made changes to the sequencer
bool apply_cued_load_items(void) {
	bool possible_seq_changes = false;
	if (cued_preset_id != 255) {
		load_preset(cued_preset_id, false);
		cued_preset_id = 255;
		possible_seq_changes = true;
	}
	if (cued_pattern_id != 255) {
		save_param(P_PATTERN, SRC_BASE, cued_pattern_id);
		cued_pattern_id = 255;
		possible_seq_changes = true;
	}
	if (cued_sample_id != cur_sample_id && cued_sample_id != 255) {
		save_param(P_SAMPLE, SRC_BASE, cued_sample_id);
		cued_sample_id = 255;
	}
	return possible_seq_changes;
}

void cue_ram_item(u8 item_id, u8 long_press_pad) {
	// triggered on touch start:
	// - save currently active item as source for copying
	// - save what was cued into prev_cued, if they end up the same this is a double press
	// - save touched pad as cued item
	RamItemType item_type = get_item_type(item_id);
	if (item_type == get_item_type(long_press_pad)) {
		switch (item_type) {
		case RAM_PRESET:
			copy_preset_id = sys_params.curpreset;
			prev_cued_preset_id = cued_preset_id;
			cued_preset_id = item_id;
			break;
		case RAM_PATTERN: {
			copy_pattern_id = cur_pattern_id;
			prev_cued_pattern_id = cued_pattern_id;
			cued_pattern_id = item_id - PATTERNS_START;
			break;
		}
		case RAM_SAMPLE: {
			copy_sample_id = cur_sample_id;
			prev_cued_sample_id = cued_sample_id;
			// pressing the current sample cues turning it off
			u8 sample_id = item_id - SAMPLES_START;
			cued_sample_id = (sample_id == cur_sample_id) ? NUM_SAMPLES : sample_id;
			break;
		}
		default:
			break;
		}
	}
}

void try_apply_cued_ram_item(u8 item_id) {
	// triggered on pad release, conditionals:
	// 1. is a ram item cued?
	// 2a. is the sequencer not playing? (save to make changes)
	// 2b. or, was the same ram item pressed twice in a row? (force change while sequencer plays)
	switch (get_item_type(item_id)) {
	case RAM_PRESET:
		if (cued_preset_id != 255 && (!seq_playing() || cued_preset_id == prev_cued_preset_id)) {
			load_preset(cued_preset_id, false);
			cued_preset_id = 255;
		}
		break;
	case RAM_PATTERN:
		if (cued_pattern_id != 255 && (!seq_playing() || cued_pattern_id == prev_cued_pattern_id)) {
			save_param(P_PATTERN, SRC_BASE, cued_pattern_id);
			cued_pattern_id = 255;
		}
		break;
	case RAM_SAMPLE:
		if (cued_sample_id != 255 && (!seq_playing() || cued_sample_id == prev_cued_sample_id)) {
			save_param(P_SAMPLE, SRC_BASE, cued_sample_id);
			cued_sample_id = 255;
		}
		break;
	default:
		break;
	}
}

u8 draw_cued_preset_id(void) {
	if (cued_preset_id != 255 && cued_preset_id != sys_params.curpreset)
		return fdraw_str(0, 0, F_20_BOLD, "%c%d->%d", I_PRESET[0], sys_params.curpreset + 1, cued_preset_id + 1);
	else
		return 0;
}

u8 draw_preset_id(void) {
	return fdraw_str(0, 0, F_20_BOLD, I_PRESET "%d", sys_params.curpreset + 1);
}

void draw_preset_name(u8 xtab) {
	char preset_name[9];
	memcpy(preset_name, cur_preset.name, 8);
	preset_name[8] = 0;
	xtab += 2;
	draw_str(xtab, 0, F_8_BOLD, preset_name);
	// category
	if (cur_preset.category > 0 && cur_preset.category < NUM_PST_CATS)
		draw_str(xtab, 8, F_8, preset_category_name[cur_preset.category]);
}

u8 draw_cued_pattern_id(bool with_arp_icon) {
	if (cued_pattern_id != 255 && cued_pattern_id != cur_pattern_id)
		return fdraw_str(0, 16, F_20_BOLD, "%c%d->%d", with_arp_icon ? I_NOTES[0] : I_SEQ[0], cur_pattern_id + 1,
		                 cued_pattern_id + 1);
	else
		return 0;
}

void draw_pattern_id(bool with_arp_icon) {
	fdraw_str(0, 16, F_20_BOLD, "%c%d", with_arp_icon ? I_NOTES[0] : I_SEQ[0], cur_pattern_id + 1, cur_pattern_id + 1);
}

void draw_sample_id(void) {
	fdraw_str(-128, 16, F_20_BOLD, cur_sample_id < NUM_SAMPLES ? I_WAVE "%d" : I_WAVE "Off", cur_sample_id + 1);
}

void draw_arp_flag(void) {
	gfx_text_color = 0;
	if (arp_on()) {
		fill_rectangle(128 - 32, 0, 128 - 17, 8);
		draw_str(-(128 - 17), -1, F_8, "arp");
		if (!arp_active())
			inverted_rectangle(128 - 32, 0, 128 - 17, 8);
	}
}

void draw_latch_flag(void) {
	gfx_text_color = 0;
	if (latch_on()) {
		fill_rectangle(128 - 38, 32 - 8, 128 - 17, 32);
		draw_str(-(128 - 17), 32 - 7, F_8, "latch");
		if (seq_state() == SEQ_STEP_RECORDING)
			inverted_rectangle(128 - 38, 32 - 8, 128 - 17, 32);
	}
}

void draw_select_load_item(u8 item_id, bool done) {
	switch (get_item_type(item_id)) {
	case RAM_PRESET:
		if (item_id == copy_preset_id)
			fdraw_str(0, 0, F_16_BOLD, done ? "toggled\n" I_PRESET "Preset %d" : "toggle\n" I_PRESET "Preset %d?",
			          item_id + 1);
		else
			fdraw_str(0, 0, F_16_BOLD, done ? "copied to " I_PRESET "%d" : "copy over\n" I_PRESET "Preset %d?",
			          item_id + 1);
		break;
	case RAM_PATTERN:
		fdraw_str(0, 0, F_16_BOLD, done ? "copied to " I_SEQ "%d" : "copy over\n" I_SEQ "Pat %d?",
		          item_id - PATTERNS_START + 1);
		break;
	case RAM_SAMPLE:
		fdraw_str(0, 0, F_16_BOLD, done ? "ok!" : "Edit\n" I_WAVE "Sample %d?", item_id - SAMPLES_START + 1);
		break;
	default:
		break;
	}
}

void draw_clear_load_item(u8 item_id, bool done) {
	switch (get_item_type(item_id)) {
	case RAM_PRESET:
		fdraw_str(0, 0, F_16_BOLD, done ? "cleared\n" I_PRESET "Preset %d" : "initialize\n" I_PRESET "Preset %d?",
		          recent_load_item + 1);
		break;
	case RAM_PATTERN:
		fdraw_str(0, 0, F_16_BOLD, done ? "cleared\n" I_SEQ "Pattern %d." : "Clear\n" I_SEQ "Pattern %d?",
		          recent_load_item - PATTERNS_START + 1);
		break;
	case RAM_SAMPLE:
		fdraw_str(0, 0, F_16_BOLD, done ? "cleared\n" I_WAVE "Sample %d." : "Clear\n" I_WAVE "Sample %d?",
		          recent_load_item - SAMPLES_START + 1);
		break;
	default:
		break;
	}
}

u8 ui_load_led(u8 x, u8 y, u8 pulse) {
	u8 item_id = x * 8 + y;

	// all patterns low brightness
	u8 k = get_item_type(item_id) == RAM_PATTERN ? 64 : 0;

	// pulse cued load item
	if (item_id == cued_preset_id)
		k = pulse;
	if (item_id == PATTERNS_START + cued_pattern_id)
		k = pulse;
	if (item_id == SAMPLES_START + cued_sample_id)
		k = pulse;

	// full selected load item
	if (item_id == sys_params.curpreset)
		k = 255;
	if (item_id == PATTERNS_START + cur_pattern_id)
		k = 255;
	if (item_id == SAMPLES_START + cur_sample_id)
		k = 255;

	return k;
}