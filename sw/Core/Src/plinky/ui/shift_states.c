#include "shift_states.h"
#include "gfx/data/icons.h"
#include "gfx/gfx.h"
#include "hardware/adc_dac.h"
#include "hardware/flash.h"
#include "hardware/ram.h"
#include "hardware/touchstrips.h"
#include "pad_actions.h"
#include "synth/params.h"
#include "synth/sampler.h"
#include "synth/sequencer.h"
#include "synth/strings.h"
#include "synth/time.h"

#define SHORT_PRESS_TIME 250 // ms

ShiftState shift_state = SS_NONE;

static u8 prev_ui_mode = UI_DEFAULT;
static u32 shift_last_press_time = 0;
static bool action_pressed_during_shift = false;
static u32 shift_state_frames = 0;

static bool shift_short_pressed(void) {
	return (shift_state == SS_NONE) || ((synth_tick - shift_last_press_time) < SHORT_PRESS_TIME);
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
	// - the state of touched_main_area and param_from_mem decide whether a parameter stays selected for editing, or
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
		ui_mode = mode_a ? UI_EDITING_A : UI_EDITING_B;
		enter_param_edit_mode(mode_a);
		break;
	case SS_LOAD:
		// activate preset load screen
		ui_mode = UI_LOAD;
		touch_load_item(sys_params.curpreset);
		break;
	case SS_LEFT:
		// if playing, jump to start of pattern
		if (seq_playing()) {
			seq_play();
			cue_clock_reset();
		}
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
		break;
	case SS_PLAY:
		// cued to stop? => stop immediately
		if (seq_flags.stop_at_next_step)
			seq_stop();
		// playing but not cued to stop? => cue to stop
		else if (seq_playing())
			seq_cue_to_stop();
		// not playing? => initiate preview
		else {
			seq_start_previewing();
			cue_clock_reset();
		}
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
		ui_mode = UI_DEFAULT;
		try_exit_param_edit_mode(action_pressed_during_shift);
		break;
	case SS_LOAD:
		if (action_pressed_during_shift || prev_ui_mode == ui_mode)
			ui_mode = UI_DEFAULT;
		break;
	case SS_LEFT:
		if (!action_pressed_during_shift && short_press) {
			// a short press left only steps left in the sequencer when it's not playing
			if (!seq_playing()) {
				seq_dec_step();
				seq_force_play_step();
				cue_clock_reset();
			}
			ui_mode = UI_DEFAULT;
		}
		if (action_pressed_during_shift || prev_ui_mode == ui_mode)
			ui_mode = UI_DEFAULT;
		break;
	case SS_RIGHT:
		// short press right steps the sequencer right one step
		if (!action_pressed_during_shift && short_press) {
			seq_inc_step();
			seq_force_play_step();
			cue_clock_reset();
			ui_mode = UI_DEFAULT;
		}
		if (action_pressed_during_shift || prev_ui_mode == ui_mode)
			ui_mode = UI_DEFAULT;
		break;
	case SS_CLEAR:
		// pressing clear in step-record mode clears sequencer step
		if (ui_mode == UI_DEFAULT && seq_state() == SEQ_STEP_RECORDING) {
			seq_clear_step();
			// move to next step after clearing
			seq_inc_step();
		}
		break;
	case SS_RECORD:
		if (short_press)
			seq_toggle_rec();
		break;
	case SS_PLAY:
		// - a short press ends previewing and resumes playing normally
		// - a long press means this is the end of a preview and we should stop playing
		if (seq_flags.previewing)
			short_press ? seq_end_previewing() : seq_stop();
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
		// after a delay, clear the last touched load item
		if ((shift_state == SS_CLEAR) && (shift_state_frames > 64 + 4))
			clear_load_item();
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

// returns whether this produced screen-filling graphics
bool shift_states_oled_visuals(void) {
	switch (shift_state) {
	case SS_CLEAR:
		switch (ui_mode) {
		case UI_SAMPLE_EDIT:
			return false;
		case UI_LOAD:
			if (shift_state_frames > 4) {
				draw_clear_load_item(long_press_pad, shift_state_frames - 4 > 64);
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