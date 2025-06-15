#include "pad_actions.h"
#include "gfx/data/names.h"
#include "gfx/gfx.h"
#include "hardware/ram.h"
#include "hardware/touchstrips.h"
#include "shift_states.h"
#include "synth/params.h"
#include "synth/sampler.h"
#include "synth/time.h"

#define LONGPRESS_THRESH 160 // full read cycles

u8 long_press_pad = 0;

static u16 long_press_frames = 0;
static u8 strip_holds_valid_action = 0; // mask
static u8 strip_is_action_pressed = 0;  // mask

void handle_pad_actions(u8 strip_id, Touch* strip_cur) {
	Touch* strip_2back = get_touch_prev(strip_id, 2);
	u8 strip_mask = 1 << strip_id;
	u8 pad_y = strip_cur->pos >> 8;   // local pad (on strip, 0 - 7)
	u8 pad_id = strip_id * 8 + pad_y; // global pad (on plate, 0 - 63)

	// == pad presses == //

	bool pres_stable = abs(strip_2back->pres - strip_cur->pres) < 200;
	bool pos_stable = abs(strip_2back->pos - strip_cur->pos) < 32;
	bool is_press_start = false;

	//  touch + pressure over 100
	if (strip_cur->pres > 100) {
		if (!strip_available_for_synth(strip_id))
			strip_holds_valid_action |= strip_mask; // set valid action flag
		// we're stable
		if (pos_stable && pres_stable) {
			if (!strip_is_action_pressed)
				long_press_pad = pad_id; // first press gets tracked as long press
			if (!(strip_is_action_pressed & strip_mask)) {
				is_press_start = true;
				strip_is_action_pressed |= strip_mask; // set pressed flag
			}
		}
	}

	else {                                                                    // pressure under 100,
		strip_holds_valid_action &= ~strip_mask;                              // clear valid action flag
		if ((strip_is_action_pressed & strip_mask) && strip_cur->pres <= 0) { // pressure under 0,
			strip_is_action_pressed &= ~strip_mask;                           // clear pressed flag
			// release in load preset mode
			if (ui_mode == UI_LOAD)
				try_apply_cued_ram_item(long_press_pad);
		}
	}

	// == executing actions == //

	if ((strip_is_action_pressed & strip_mask) && (strip_holds_valid_action & strip_mask)) {
		if (shift_state != SS_NONE)
			press_action_during_shift();
		switch (ui_mode) {
		case UI_DEFAULT:
		case UI_EDITING_A:
		case UI_EDITING_B: {
			// do we need to reset the left strip?
			bool left_strip_reset = false;
			// left-most strip used to edit param value
			if (strip_id == 0) {
				if (is_press_start)
					left_strip_reset = true;
				if (pres_stable)
					try_left_strip_for_params(strip_cur->pos, is_press_start);
			}
			// center six strips => pressed a parameter
			else if (strip_id < 7) {
				if (press_param(pad_y, strip_id, is_press_start))
					left_strip_reset = true;
			}
			// right-most strip => pressed a mod source
			else
				select_mod_src(pad_y);
			if (left_strip_reset)
				reset_left_strip();
		} break;
		case UI_SAMPLE_EDIT:
			if (is_press_start && shift_state == SS_NONE)
				switch (sampler_mode) {
				case SM_PRE_ARMED:
					// press while pre-armed arms the recording
					sampler_mode = SM_ARMED;
					break;
				case SM_ARMED:
					// press while armed starts the recording
					start_recording_sample();
					break;
				case SM_RECORDING:
					// press while recording registers a slice point
					sampler_record_slice_point();
					break;
				default:
					break;
				}
			if (sampler_mode == SM_PREVIEW)
				sampler_adjust_slice_point_from_touch(strip_id, strip_cur->pos, is_press_start);
			if (sampler_mode == SM_RECORDING && long_press_frames > 32)
				try_stop_recording_sample();
			break;
		case UI_PTN_START:
			if (is_press_start)
				seq_try_set_start(pad_y * 8 + strip_id);
			break;
		case UI_PTN_END:
			if (is_press_start)
				seq_set_end(pad_y * 8 + strip_id);
			break;
		case UI_LOAD:
			if (is_press_start) {
				touch_load_item(pad_id);
				cue_ram_item(pad_id);
			}
			break;
		} // mode
	}
}

void handle_pad_action_long_presses(void) {
	// long-press is only used in load and sample edit modes
	if (ui_mode != UI_LOAD && ui_mode != UI_SAMPLE_EDIT)
		return;
	// we're looking for exactly one strip being pressed for an action
	if (!strip_is_action_pressed || !ispow2(strip_is_action_pressed)) {
		long_press_frames = 0;
		return;
	}
	// find the single pressed strip
	u8 strip_id = 0;
	for (; strip_id < 8; ++strip_id)
		if (strip_is_action_pressed & (1 << strip_id))
			break;
	// get the pressed pad
	u8 pad_id = 8 * strip_id + (get_touch_prev(strip_id, 1)->pos >> 8);
	// only relevant if the pressed pad is the long_press_pad
	if (pad_id != long_press_pad) {
		long_press_frames = 0;
		return;
	}
	// increase counter
	long_press_frames += 2;
	// actions on long press
	if (ui_mode == UI_LOAD && long_press_frames == LONGPRESS_THRESH) {
		// sample pad (strip 7), load sample and enter sample edit mode (belongs in sampler)
		if (strip_id == 7)
			open_sampler(pad_id & 7);
		// patch or pattern, request copy
		else
			copy_load_item(pad_id);
	}
}

// == VISUALS == //

bool mod_action_pressed(void) {
	return (strip_holds_valid_action & 128) && (strip_is_action_pressed & 128);
}

// returns whether this produced screen-filling graphics
bool pad_actions_oled_visuals(void) {
	if (ui_mode == UI_LOAD && long_press_frames >= 32) {
		draw_select_load_item(long_press_pad, long_press_frames - 32 > 128);
		inverted_rectangle(0, 0, long_press_frames - 32, 32);
		return true;
	}
	return false;
}