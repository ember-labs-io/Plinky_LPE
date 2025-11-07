#include "pad_actions.h"
#include "gfx/gfx.h"
#include "hardware/memory.h"
#include "hardware/touchstrips.h"
#include "settings_menu.h"
#include "synth/params.h"
#include "synth/sampler.h"
#include "synth/sequencer.h"
#include "synth/strings.h"
#include "synth/time.h"

#define LONGPRESS_THRESH 132 // full read cycles
#define SHORT_PRESS_TIME 250 // ms

u8 long_press_pad = 0;
ShiftState shift_state = SS_NONE;

static u16 long_press_frames = 0;
static u8 strip_holds_valid_action = 0; // mask
static u8 strip_is_action_pressed = 0;  // mask
static u8 prev_ui_mode = UI_DEFAULT;
static u32 shift_last_press_time = 0;
static bool action_pressed_during_shift = false;
static u32 shift_state_frames = 0;

static bool shift_short_pressed(void) {
	return (shift_state == SS_NONE) || ((synth_tick - shift_last_press_time) < SHORT_PRESS_TIME);
}

void clear_long_press(void) {
	long_press_frames = 0;
}

void press_action_during_shift(void) {
	action_pressed_during_shift = true;
}

void shift_set_state(ShiftState new_state) {
	shift_state = new_state;
	shift_last_press_time = synth_tick;
	shift_state_frames = 0;

	if (ui_mode == UI_SAMPLE_EDIT) {
		// record/play buttons have identical behavior
		if (new_state == SS_RECORD || new_state == SS_PLAY) {
			switch (sampler_mode) {
			// pre-armed moves to armed
			case SM_PRE_ARMED:
				sampler_mode = SM_ARMED;
				break;
			// armed starts recoding
			case SM_ARMED:
				start_recording_sample();
				break;
			// recording stops recording
			case SM_RECORDING:
				stop_recording_sample();
				break;
			default:
				break;
			}
		}
		return;
	}

	// == Overview of the system == //
	//
	// Shift A / Shift B:
	// - pressing shift pad sets ui_mode to UI_EDITING_A / UI_EDITING_B
	// - releasing shift pad immediately reverts to UI_DEFAULT
	// - the state of touched_main_area and open_edit_mode decide whether a parameter stays selected for editing, or
	// gets cleared, when the shift pad is released
	//
	// Preset / Left / Right
	// - ui_mode UI_LOAD / UI_PTN_START / UI_PTN_END gets set when pad is pressed
	// - mode gets reverted to UI_DEFAULT on release of the pad, depending on these circumstances:
	// 		- a non-shift pad was pressed while the shift button was held ("quick edit", shift + pad)
	//		- no change in ui_mode happened when the shift pad was pressed (the pad was pressed while in its own mode)
	// - Left/Right also revert to UI_DEFAULT at the end of a short press, as that indicates a sequencer step action
	//
	// Clear / Record / Play
	// - do not change ui_mode at all
	// - press and release actions each have their own sequencer-related actions

	// all other modes
	prev_ui_mode = ui_mode;
	action_pressed_during_shift = false;
	switch (shift_state) {
	case SS_SHIFT_A:
	case SS_SHIFT_B:
		bool mode_a = shift_state == SS_SHIFT_A;
		try_enter_edit_mode(mode_a);
		ui_mode = mode_a ? UI_EDITING_A : UI_EDITING_B;
		break;
	case SS_LOAD:
		// activate preset load screen
		ui_mode = UI_LOAD;
		clear_long_press();
		break;
	case SS_LEFT:
		// edit start of sequencer pattern
		ui_mode = UI_PTN_START;
		break;
	case SS_RIGHT:
		// activate set pattern end screen
		ui_mode = UI_PTN_END;
		break;
	case SS_CLEAR:
		// pressing Clear stops latched notes playing
		clear_latch();
		// exit settings menu
		if (ui_mode == UI_SETTINGS_MENU)
			ui_mode = UI_DEFAULT;
		break;
	case SS_PLAY:
		seq_press_play();
		break;
	default:
		break;
	}
}

void shift_release_state(void) {
	bool short_press = shift_short_pressed();
	shift_state_frames = 0;

	if (ui_mode == UI_SAMPLE_EDIT) {
		// short presses in sample edit mode
		if (short_press)
			switch (shift_state) {
			case SS_SHIFT_A:
				sampler_toggle_play_mode();
				break;
			case SS_SHIFT_B:
				sampler_iterate_loop_mode();
				break;
			case SS_LOAD:
			case SS_LEFT:
			case SS_RIGHT:
			case SS_CLEAR:
				// middle four buttons => general canceling command
				switch (sampler_mode) {
				case SM_PREVIEW:
					// when in default (preview) mode => exit sampler
					ui_mode = UI_DEFAULT;
					break;
				case SM_RECORDING:
					// when recording => stop recording
					stop_recording_sample();
					break;
				default:
					// when in any of the other sampler modes => move to default (preview) mode
					sampler_mode = SM_PREVIEW;
					break;
				}
				break;
			default:
				break;
			}
		// we're no longer in a shift state
		shift_state = SS_NONE;
		return; // exit
	}

	// all other modes
	switch (shift_state) {
	case SS_SHIFT_A:
	case SS_SHIFT_B:
		try_exit_edit_mode(action_pressed_during_shift);
		if (ui_mode == UI_SETTINGS_MENU && action_pressed_during_shift)
			break;
		ui_mode = UI_DEFAULT;
		break;
	case SS_LOAD:
		if (prev_ui_mode == ui_mode && !action_pressed_during_shift)
			ui_mode = UI_DEFAULT;
		clear_long_press();
		break;
	case SS_LEFT:
		if (!action_pressed_during_shift && short_press) {
			seq_press_left(prev_ui_mode == UI_DEFAULT);
			ui_mode = UI_DEFAULT;
		}
		if (action_pressed_during_shift || prev_ui_mode == ui_mode)
			ui_mode = UI_DEFAULT;
		break;
	case SS_RIGHT:
		if (!action_pressed_during_shift && short_press) {
			seq_press_right();
			ui_mode = UI_DEFAULT;
		}
		if (action_pressed_during_shift || prev_ui_mode == ui_mode)
			ui_mode = UI_DEFAULT;
		break;
	case SS_CLEAR:
		if (ui_mode == UI_DEFAULT)
			seq_press_clear();
		break;
	case SS_RECORD:
		if (short_press)
			seq_press_rec();
		break;
	case SS_PLAY:
		seq_release_play(short_press);
		break;
	default:
		break;
	}

	// we're no longer in a shift state
	shift_state = SS_NONE;
}

void shift_hold_state(void) {
	switch (ui_mode) {
	case UI_LOAD:
		// an ss_load hold keeps ui_load open (simulate an action press)
		if (shift_state == SS_LOAD && shift_state_frames == 32)
			action_pressed_during_shift = true;
		// after a long clear press, clear the last touched memory item
		if ((shift_state == SS_CLEAR) && (shift_state_frames == 64 + 4))
			clear_mem_item();
		break;
	case UI_SAMPLE_EDIT:
		// long-pressing record or play in sampler preview mode records a new sample
		if (sampler_mode == SM_PREVIEW && (shift_state == SS_RECORD || shift_state == SS_PLAY)
		    && shift_state_frames > 64)
			start_erasing_sample_buffer();
		break;
	default:
		break;
	}

	// increase hold duration
	shift_state_frames++;
}

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

	else {                                                                  // pressure under 100,
		strip_holds_valid_action &= ~strip_mask;                            // clear valid action flag
		if ((strip_is_action_pressed & strip_mask) && strip_cur->pres <= 0) // pressure under 0,
			strip_is_action_pressed &= ~strip_mask;                         // clear pressed flag
	}

	// == executing actions == //

	if ((strip_is_action_pressed & strip_mask) && (strip_holds_valid_action & strip_mask)) {
		if (shift_state != SS_NONE)
			press_action_during_shift();
		switch (ui_mode) {
		case UI_DEFAULT:
		case UI_EDITING_A:
		case UI_EDITING_B: {
			// settings menu
			if (pad_id == 47) {
				open_settings_menu();
				break;
			}
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
				seq_cue_start_step(pad_y * 8 + strip_id);
			break;
		case UI_PTN_END:
			if (is_press_start)
				seq_set_end_step(pad_y * 8 + strip_id);
			break;
		case UI_SETTINGS_MENU:
			select_settings_item(strip_id, pad_y);
			break;
		default:
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
	if (ui_mode == UI_LOAD && long_press_frames == LONGPRESS_THRESH)
		long_press_load_item(pad_id);
}

// == VISUALS == //

bool mod_action_pressed(void) {
	return (strip_holds_valid_action & 128) && (strip_is_action_pressed & 128);
}

// returns whether this produced screen-filling graphics
bool pad_actions_oled_visuals(void) {
	if (ui_mode == UI_LOAD && long_press_frames > 0) {
		draw_save_load_item(long_press_pad, long_press_frames >= LONGPRESS_THRESH);
		inverted_rectangle(0, 0, long_press_frames, 32);
		return true;
	}
	return false;
}

// returns whether this produced screen-filling graphics
bool shift_states_oled_visuals(void) {
	switch (shift_state) {
	case SS_CLEAR:
		switch (ui_mode) {
		case UI_SAMPLE_EDIT:
			return false;
		case UI_LOAD:
			if (shift_state_frames > 4) {
				draw_clear_item(shift_state_frames - 4 > 64);
				inverted_rectangle(0, 0, shift_state_frames * 2 - 4, 32);
			}
			break;
		default:
			draw_str(0, 0, F_32_BOLD, I_CROSS "clear");
			break;
		}
		return true;
	case SS_RECORD:
		if (ui_mode == UI_SAMPLE_EDIT) {
			if (sampler_mode == SM_PREVIEW && shift_state_frames > 4) {
				draw_str(0, 0, F_32, "record?");
				inverted_rectangle(0, 0, shift_state_frames * 2, 32);
				return true;
			}
		}
		else if (shift_short_pressed()) {
			draw_str(0, 4, F_20_BOLD, seq_recording() ? I_RECORD "record >off" : I_RECORD "record >on");
			return true;
		}
		break;
	case SS_PLAY:
		if (ui_mode != UI_SAMPLE_EDIT) {
			draw_str(0, 0, F_32_BOLD, I_PLAY "play");
			return true;
		}
		break;
	default:
		break;
	}

	return false;
}