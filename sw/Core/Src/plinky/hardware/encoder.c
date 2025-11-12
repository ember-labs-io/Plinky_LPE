#include "encoder.h"
#include "hardware/memory.h"
#include "synth/params.h"
#include "synth/sampler.h"
#include "ui/settings_menu.h"

volatile bool encoder_pressed = false;
static volatile s8 encoder_value = 0;

static u8 prev_hardware_state;
static float encoder_acc;
static u32 encoder_press_start;
static u16 encoder_press_duration;
static u32 last_encoder_use = 0;

bool enc_recently_used(void) {
	return last_encoder_use > millis() - 1000;
}

void clear_last_encoder_use(void) {
	last_encoder_use = 0;
}

void init_encoder(void) {
	prev_hardware_state = (GPIOC->IDR >> 14) & 3;
	encoder_value = 2;
}

void encoder_irq(void) {
	static const s8 enc_deltas[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
	static s16 prev_encoder_value;

	// press
	encoder_pressed = !((GPIOC->IDR >> 13) & 1);

	// value
	u8 hardware_state = (GPIOC->IDR >> 14) & 3;
	u8 cur_state = prev_hardware_state | (hardware_state << 2);
	prev_hardware_state = hardware_state;
	prev_encoder_value = encoder_value;
	if (hardware_state == 0b11)
		encoder_value = (encoder_value & ~0b11) | 2; // snap to the middle of a detent (value = 4x + 2)
	else
		encoder_value += enc_deltas[cur_state] * (sys_params.reverse_encoder ? -1 : 1);

	// acceleration
	encoder_acc *= 0.998f;
	encoder_acc += abs(prev_encoder_value - encoder_value) * 0.125f;
}

void encoder_tick(void) {
	static bool prev_encoder_pressed;

	// extract and consume difference since last tick
	s8 enc_diff = encoder_value >> 2;
	if (enc_diff || encoder_pressed || prev_encoder_pressed) {
		encoder_value -= enc_diff << 2;
		last_encoder_use = millis(); // log usage
	}

	// update timer
	if (encoder_pressed && !prev_encoder_pressed)
		encoder_press_start = millis();
	if (encoder_pressed)
		encoder_press_duration = millis() - encoder_press_start;

	// execute actions
	switch (ui_mode) {
	case UI_DEFAULT:
	case UI_EDITING_A:
	case UI_EDITING_B:
		if (enc_diff)
			edit_param_from_encoder(enc_diff, encoder_acc);
		// release of a short encoder press
		else if (!encoder_pressed && prev_encoder_pressed && encoder_press_duration <= SHORT_PRESS_TIME)
			params_toggle_default_value();
		hold_encoder_for_params(encoder_press_duration);
		break;
	case UI_SAMPLE_EDIT:
		if (enc_diff) {
			if (cur_sample_info.pitched)
				sampler_adjust_cur_slice_pitch(enc_diff);
			else
				sampler_adjust_cur_slice_point(enc_diff * 512);
		}
		break;
	case UI_SETTINGS_MENU:
		settings_encoder_press(encoder_pressed, encoder_press_duration);
		if (enc_diff)
			edit_settings_from_encoder(enc_diff);
		break;
	default:
		break;
	}

	if (!encoder_pressed && prev_encoder_pressed)
		encoder_press_duration = 0;
	prev_encoder_pressed = encoder_pressed;
}