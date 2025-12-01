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

FunctionPad function_pressed = FN_NONE;

static u8 action_press_on_strip = 0; // does the strip have an action press? (mask)

// ui & edit mode switching
static u8 press_start_ui_mode = UI_DEFAULT;
static bool keep_ui_open = false;
static bool keep_edit_mode_open = false;

// keep track of (long) presses on the main grid
u8 main_press_pad = 255;
static bool main_press_canceled = false;
static u32 main_press_start = 0;
u32 main_press_ms = 0;

// keep track of (long) presses on the function pads
static u32 function_press_start = 0;
u32 function_press_ms = 0;

// == UTILS == //

// disables current main press from triggering any actions
static void cancel_main_press(void) {
	main_press_canceled = true;
	main_press_ms = 0;
}

// == MAIN == //

static void press_function(FunctionPad new_function) {
	function_pressed = new_function;
	function_press_start = millis();

	if (ui_mode == UI_SAMPLE_EDIT) {
		// record/play buttons have identical behavior
		if (new_function == FN_RECORD || new_function == FN_PLAY) {
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

	// all other modes
	press_start_ui_mode = ui_mode;
	switch (function_pressed) {
	case FN_SHIFT_A:
	case FN_SHIFT_B:
		bool mode_a = function_pressed == FN_SHIFT_A;
		keep_edit_mode_open = try_restore_param(mode_a);
		keep_ui_open = false;
		ui_mode = mode_a ? UI_EDITING_A : UI_EDITING_B;
		break;
	case FN_LOAD:
		cancel_main_press();
		keep_ui_open = ui_mode != UI_LOAD;
		ui_mode = UI_LOAD;
		break;
	case FN_LEFT:
		keep_ui_open = ui_mode != UI_PTN_START;
		ui_mode = UI_PTN_START;
		break;
	case FN_RIGHT:
		keep_ui_open = ui_mode != UI_PTN_END;
		ui_mode = UI_PTN_END;
		break;
	case FN_CLEAR:
		if (ui_mode == UI_LOAD)
			cancel_main_press();
		else {
			// pressing clear stops latched notes playing
			clear_latch();
			keep_ui_open = ui_mode == UI_LOAD;
		}
		break;
	case FN_RECORD:
		keep_ui_open = true;
		break;
	case FN_PLAY:
		seq_press_play();
		keep_ui_open = true;
		break;
	default:
		break;
	}
}

static void release_function(void) {
	bool short_press = millis() - function_press_start < SHORT_PRESS_TIME;
	function_press_ms = 0;

	if (ui_mode == UI_SAMPLE_EDIT) {
		// short presses in sample edit mode
		if (short_press)
			switch (function_pressed) {
			case FN_SHIFT_A:
				sampler_toggle_play_mode();
				break;
			case FN_SHIFT_B:
				sampler_iterate_loop_mode();
				break;
			case FN_LOAD:
			case FN_LEFT:
			case FN_RIGHT:
			case FN_CLEAR:
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

		function_pressed = FN_NONE;
		return;
	}

	// all other modes
	switch (function_pressed) {
	case FN_SHIFT_A:
	case FN_SHIFT_B:
		if (!keep_edit_mode_open)
			close_edit_mode();
		break;
	case FN_LOAD:
		cancel_main_press();
		break;
	case FN_LEFT:
		if (press_start_ui_mode != UI_PTN_START && press_start_ui_mode != UI_PTN_END && short_press) {
			seq_press_left(press_start_ui_mode == UI_DEFAULT);
			keep_ui_open = false;
		}
		break;
	case FN_RIGHT:
		if (press_start_ui_mode != UI_PTN_START && press_start_ui_mode != UI_PTN_END && short_press) {
			seq_press_right();
			keep_ui_open = false;
		}
		break;
	case FN_CLEAR:
		if (ui_mode == UI_LOAD)
			cancel_main_press();
		else
			seq_press_clear();
		break;
	case FN_RECORD:
		if (short_press)
			seq_press_rec();
		break;
	case FN_PLAY:
		seq_release_play(short_press);
		break;
	default:
		break;
	}

	if (!keep_ui_open)
		ui_mode = UI_DEFAULT;
	function_pressed = FN_NONE;
}

static void hold_function(void) {
	function_press_ms = millis() - function_press_start;

	switch (ui_mode) {
	case UI_LOAD:
		// hold ui_load open after a short press
		if (function_pressed == FN_LOAD && !keep_ui_open && function_press_ms >= SHORT_PRESS_TIME)
			keep_ui_open = true;
		break;
	case UI_SAMPLE_EDIT:
		// long-pressing record or play in sampler preview mode records a new sample
		if (sampler_mode == SM_PREVIEW && (function_pressed == FN_RECORD || function_pressed == FN_PLAY)
		    && function_press_ms >= PRESS_DELAY + LONG_PRESS_TIME + POST_PRESS_DELAY) {
			start_erasing_sample_buffer();
		}
		break;
	default:
		break;
	}
}

static bool validate_function_change(FunctionPad new_function, Touch* touch) {
	if (new_function == function_pressed)
		return false;

	// going from one function to another, apply hysteresis - dist from old pad should be larger than 192
	if (function_pressed != FN_NONE && abs((function_pressed * 256 + 128) - touch->pos) < 192)
		return false;

	// accidental presses: rule out function presses if the surrounding synth pads are being pressed
	if (hw_version == HW_PLINKY) {
		Touch* strip_above = get_touch_prev(new_function, 0);
		Touch* strip_left = get_touch_prev(maxi(0, new_function - 1), 0);
		Touch* strip_right = get_touch_prev(mini(7, new_function + 1), 0);
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

	// function pads
	if (strip_id == 8) {
		if (valid_touch && validate_function_change(pad_y, touch))
			press_function(pad_y);
		else if (touch->pres <= 0 && function_pressed != FN_NONE)
			release_function();
		// any function pressed => hold
		if (function_pressed != FN_NONE)
			hold_function();
		return;
	}

	// main grid
	if (valid_touch) {
		// track main press
		if (!action_press_on_strip) {
			main_press_pad = pad_id;
			main_press_start = millis();
			main_press_ms = 0;
			main_press_canceled = false;
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
			if (is_press_start && function_pressed == FN_NONE)
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
	if (!main_press_canceled)
		main_press_ms = millis() - main_press_start;
	// actions on long press
	if (ui_mode == UI_LOAD && main_press_ms >= PRESS_DELAY + LONG_PRESS_TIME + POST_PRESS_DELAY) {
		long_press_mem_item(main_press_pad);
		cancel_main_press();
	}
}

void pad_actions_keep_edit_mode_open(void) {
	keep_edit_mode_open = true;
}

// == VISUALS == //

// returns false during the short delay before the mode becomes active
bool ptn_edit_active(void) {
	return (ui_mode == UI_PTN_START || ui_mode == UI_PTN_END)
	       && (function_pressed == FN_NONE || press_start_ui_mode == UI_PTN_START || press_start_ui_mode == UI_PTN_END
	           || function_press_ms > SHORT_PRESS_TIME);
}

bool mod_action_pressed(void) {
	return action_press_on_strip & 128;
}

// returns whether this produced screen-filling graphics
bool oled_function_visuals(void) {
	switch (ui_mode) {
	case UI_SAMPLE_EDIT:
		if (function_pressed == FN_RECORD && sampler_mode == SM_PREVIEW && function_press_ms >= PRESS_DELAY) {
			draw_str(0, 0, F_32, "record?");
			draw_load_bar(function_press_ms - PRESS_DELAY, LONG_PRESS_TIME);
			return true;
		}
		break;
	default:
		switch (function_pressed) {
		case FN_CLEAR:
			if (ui_mode != UI_LOAD) {
				draw_str(0, 0, F_32_BOLD, I_CROSS "clear");
				return true;
			}
			break;
		case FN_RECORD:
			draw_str(0, 4, F_20_BOLD, seq_recording() ? I_RECORD "record >off" : I_RECORD "record >on");
			return true;
		case FN_PLAY:
			draw_str(0, 0, F_32_BOLD, I_PLAY "play");
			return true;
		default:
			break;
		}
		break;
	}

	return false;
}

u8 ui_load_long_press_led(u8 x, u8 y, u8 pulse) {
	if ((action_press_on_strip & (1 << x)) && main_press_pad == x * 8 + y && !main_press_canceled)
		return maxi(pulse, 1);
	return 0;
}