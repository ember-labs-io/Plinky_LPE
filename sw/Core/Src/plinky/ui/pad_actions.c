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

// in ms
#define PRESS_DELAY 50
#define SHORT_PRESS_TIME 300
#define LONG_PRESS_TIME 1000

ShiftState shift_state = SS_NONE;

static u8 action_press_on_strip = 0; // does the strip have an action press? (mask)

// ui & edit mode switching
static u8 press_start_ui_mode = UI_DEFAULT;
static bool keep_ui_open = false;
static bool keep_edit_mode_open = false;

// keep track of (long) presses on the main grid
static u8 main_press_pad = 255;
static u32 main_press_start = 0;
static u32 main_press_ms = 0;

// keep track of (long) presses on the shift pads
static bool shift_press_used_up = false;
static u32 shift_press_start = 0;
static u32 shift_press_ms = 0;

// == UTILS == //

static void start_main_press(void) {
	main_press_start = millis();
	main_press_ms = 0;
}

static void use_shift_press(void) {
	shift_press_ms = 0;
	shift_press_used_up = true;
}

// == MAIN == //

static void shift_set_state(ShiftState new_state) {
	shift_state = new_state;
	shift_press_start = millis();
	shift_press_used_up = false;

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
	press_start_ui_mode = ui_mode;
	switch (shift_state) {
	case SS_SHIFT_A:
	case SS_SHIFT_B:
		bool mode_a = shift_state == SS_SHIFT_A;
		keep_edit_mode_open = try_restore_param(mode_a);
		keep_ui_open = false;
		ui_mode = mode_a ? UI_EDITING_A : UI_EDITING_B;
		break;
	case SS_LOAD:
		// switching from ss_load unpressed to pressed restarts the long-press
		start_main_press();
		keep_ui_open = ui_mode != UI_LOAD;
		ui_mode = UI_LOAD;
		break;
	case SS_LEFT:
		keep_ui_open = ui_mode != UI_PTN_START;
		ui_mode = UI_PTN_START;
		break;
	case SS_RIGHT:
		keep_ui_open = ui_mode != UI_PTN_END;
		ui_mode = UI_PTN_END;
		break;
	case SS_CLEAR:
		// pressing clear stops latched notes playing
		clear_latch();
		keep_ui_open = ui_mode == UI_LOAD;
		break;
	case SS_RECORD:
		keep_ui_open = true;
		break;
	case SS_PLAY:
		seq_press_play();
		keep_ui_open = true;
		break;
	default:
		break;
	}
}

static void shift_release_state(void) {
	bool short_press = millis() - shift_press_start < SHORT_PRESS_TIME;
	shift_press_ms = 0;

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
		if (!keep_edit_mode_open)
			close_edit_mode();
		break;
	case SS_LOAD:
		// switching from ss_load pressed to unpressed restarts the long-press
		start_main_press();
		break;
	case SS_LEFT:
		if (press_start_ui_mode != UI_PTN_START && press_start_ui_mode != UI_PTN_END && short_press) {
			seq_press_left(press_start_ui_mode == UI_DEFAULT);
			keep_ui_open = false;
		}
		break;
	case SS_RIGHT:
		if (press_start_ui_mode != UI_PTN_START && press_start_ui_mode != UI_PTN_END && short_press) {
			seq_press_right();
			keep_ui_open = false;
		}
		break;
	case SS_CLEAR:
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

	if (!keep_ui_open)
		ui_mode = UI_DEFAULT;
	shift_state = SS_NONE;
}

static void shift_hold_state(void) {
	if (!shift_press_used_up)
		shift_press_ms = millis() - shift_press_start;

	switch (ui_mode) {
	case UI_LOAD:
		switch (shift_state) {
		case SS_LOAD:
			// hold ui_load open after a short press
			if (!keep_ui_open && shift_press_ms >= SHORT_PRESS_TIME)
				keep_ui_open = true;
			break;
		case SS_CLEAR:
			// clear item
			if (shift_press_ms >= PRESS_DELAY + LONG_PRESS_TIME) {
				clear_mem_item();
				use_shift_press();
			}
			break;
		default:
			break;
		}
		break;
	case UI_SAMPLE_EDIT:
		// long-pressing record or play in sampler preview mode records a new sample
		if (sampler_mode == SM_PREVIEW && (shift_state == SS_RECORD || shift_state == SS_PLAY)
		    && shift_press_ms >= PRESS_DELAY + LONG_PRESS_TIME) {
			start_erasing_sample_buffer();
		}
		break;
	default:
		break;
	}
}

static bool is_valid_shift_state_change(u8 new_state, Touch* touch) {
	if (new_state == shift_state)
		return false;

	// going from one state to another, apply hysteresis - dist from old state should be larger than 192
	if (shift_state != SS_NONE && abs((shift_state * 256 + 128) - touch->pos) < 192)
		return false;

	// accidental presses: rule out shift state presses if the surrounding synth pads are being pressed
	if (hw_version == HW_PLINKY) {
		Touch* strip_above = get_touch_prev(new_state, 0);
		Touch* strip_left = get_touch_prev(maxi(0, new_state - 1), 0);
		Touch* strip_right = get_touch_prev(mini(7, new_state + 1), 0);
		if ((strip_above->pres > 256 && strip_above->pos >= 6 * 256)    // ~ bottom two pads of strip above
		    || (strip_left->pres > 256 && strip_left->pos >= 7 * 256)   // ~ bottom pad of strip left
		    || (strip_right->pres > 256 && strip_right->pos >= 7 * 256) // ~ bottom pad of strip right
		)
			return false;
	}

	return true;
}

void handle_pad_actions(u8 strip_id) {
	static const u8 STABLE_PRESS_RANGE = 60;
	static const u8 STABLE_POS_RANGE = 32;
	static const u16 PRESS_TRESH = 400;

	static u16 prev_valid_touch;

	u16 strip_mask = 1 << strip_id;
	bool is_press_start = false;
	Touch* touch = get_touch_prev(strip_id, 0);
	Touch* touch_1back = get_touch_prev(strip_id, 1);
	Touch* touch_2back = get_touch_prev(strip_id, 2);

	u8 pad_y = touch->pos >> 8;       // local pad (on strip, 0 - 7)
	u8 pad_id = strip_id * 8 + pad_y; // global pad (on plate, 0 - 71)

	bool touching = prev_valid_touch & strip_mask;
	bool valid_touch =
	    // pad not used for synth
	    !strip_available_for_synth(strip_id)
	    // pressing the same pad for the third frame
	    && pad_y == touch_1back->pos >> 8
	    && pad_y == touch_2back->pos >> 8
	    // enough pressure (with hysteresis)
	    && touch->pres > (touching ? 0 : PRESS_TRESH)
	    // stable position for three frames (only on touch start)
	    && (touching
	        || (abs(touch_1back->pos - touch->pos) <= STABLE_POS_RANGE
	            && abs(touch_2back->pos - touch->pos) <= STABLE_POS_RANGE));

	if (valid_touch)
		prev_valid_touch |= strip_mask;
	else
		prev_valid_touch &= ~strip_mask;

	// shift pads
	if (strip_id == 8) {
		if (valid_touch && is_valid_shift_state_change(pad_y, touch))
			shift_set_state(pad_y);
		// no press on strip but we were in a state => release
		else if (touch->pres <= 0 && shift_state != SS_NONE)
			shift_release_state();
		// in any shift state => hold
		if (shift_state != SS_NONE)
			shift_hold_state();
		return;
	}

	// main grid
	if (valid_touch) {
		// track main press
		if (!action_press_on_strip) {
			main_press_pad = pad_id;
			start_main_press();
		}
		// track press start
		if (!(action_press_on_strip & strip_mask)) {
			is_press_start = true;
			action_press_on_strip |= strip_mask;
		}
	}
	else
		action_press_on_strip &= ~strip_mask;

	// actions
	if (action_press_on_strip & strip_mask) {
		switch (ui_mode) {
		case UI_DEFAULT:
		case UI_EDITING_A:
		case UI_EDITING_B:
			// settings menu
			if (pad_id == 47) {
				open_settings_menu();
				keep_ui_open = true;
				break;
			}
			// left strip => touched edit strip
			if (strip_id == 0) {
				// pressure stable
				if (abs(touch_1back->pres - touch->pres) < STABLE_PRESS_RANGE
				    && abs(touch_2back->pres - touch->pres) < STABLE_PRESS_RANGE)
					touch_edit_strip(touch->pos, is_press_start);
			}
			// center six strips => pressed a parameter
			else if (strip_id < 7) {
				keep_edit_mode_open = true;
				press_param_pad(pad_id, is_press_start);
			}
			// right-most strip => pressed a mod source
			else {
				keep_edit_mode_open = true;
				press_mod_pad(pad_y);
			}
			break;
		case UI_PTN_START:
			if (is_press_start)
				seq_cue_start_step(pad_y * 8 + strip_id);
			keep_ui_open = false;
			break;
		case UI_PTN_END:
			if (is_press_start)
				seq_set_end_step(pad_y * 8 + strip_id);
			keep_ui_open = false;
			break;
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
				sampler_adjust_slice_point_from_touch(strip_id, touch->pos, is_press_start);
			if (sampler_mode == SM_RECORDING && main_press_ms > SHORT_PRESS_TIME)
				try_stop_recording_sample();
			break;
		case UI_SETTINGS_MENU:
			if (is_press_start)
				select_settings_item(strip_id, pad_y);
			break;
		default:
			break;
		}
	}
}

void pad_actions_frame(void) {
	// handle long presses - we're looking for exactly one strip being pressed
	if (main_press_pad == 255 || !action_press_on_strip || !ispow2(action_press_on_strip)) {
		main_press_ms = 0;
		return;
	}
	main_press_ms = millis() - main_press_start;
	// actions on long press
	if (ui_mode == UI_LOAD && main_press_ms >= PRESS_DELAY + LONG_PRESS_TIME) {
		long_press_load_item(main_press_pad);
		main_press_pad = 255;
	}
}

void pad_actions_keep_edit_mode_open(void) {
	keep_edit_mode_open = true;
}

// == VISUALS == //

// returns false during the short delay before the mode becomes active
bool ptn_edit_active(void) {
	return (ui_mode == UI_PTN_START || ui_mode == UI_PTN_END)
	       && (shift_state == SS_NONE || press_start_ui_mode == UI_PTN_START || press_start_ui_mode == UI_PTN_END
	           || shift_press_ms > SHORT_PRESS_TIME);
}

bool mod_action_pressed(void) {
	return action_press_on_strip & 128;
}

// returns whether this produced screen-filling graphics
bool pad_actions_oled_visuals(void) {
	if (ui_mode == UI_LOAD && main_press_ms > 0) {
		draw_save_load_item(main_press_pad);
		draw_load_bar(main_press_ms, LONG_PRESS_TIME);
		return true;
	}
	return false;
}

// returns whether this produced screen-filling graphics
bool shift_states_oled_visuals(void) {
	switch (ui_mode) {
	case UI_SAMPLE_EDIT:
		if (shift_state == SS_RECORD && sampler_mode == SM_PREVIEW && shift_press_ms >= PRESS_DELAY) {
			draw_str(0, 0, F_32, "record?");
			draw_load_bar(shift_press_ms, PRESS_DELAY + LONG_PRESS_TIME);
			return true;
		}
		break;
	case UI_LOAD:
		if (shift_state == SS_CLEAR && shift_press_ms >= PRESS_DELAY) {
			draw_clear_item();
			draw_load_bar(shift_press_ms, PRESS_DELAY + LONG_PRESS_TIME);
			return true;
		}
		// fall thru
	default:
		switch (shift_state) {
		case SS_CLEAR:
			if (ui_mode != UI_LOAD) {
				draw_str(0, 0, F_32_BOLD, I_CROSS "clear");
				return true;
			}
			break;
		case SS_RECORD:
			draw_str(0, 4, F_20_BOLD, seq_recording() ? I_RECORD "record >off" : I_RECORD "record >on");
			return true;
		case SS_PLAY:
			draw_str(0, 0, F_32_BOLD, I_PLAY "play");
			return true;
		default:
			break;
		}
		break;
	}

	return false;
}

u8 ui_load_long_press_led(u8 x, u8 y, u8 pulse_8x) {
	if ((action_press_on_strip & (1 << x)) && main_press_pad == x * 8 + y)
		return maxi(pulse_8x, 1);
	return 0;
}