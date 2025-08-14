#include "encoder.h"
#include "hardware/ram.h"
#include "synth/params.h"
#include "synth/sampler.h"

volatile s8 encoder_value = 0;
volatile bool encoder_pressed = false;

static u8 prev_hardware_state; // should live in encoder_irq() if encoder_init() is removed
static float encoder_acc;
static u32 last_encoder_use = 0;

bool enc_recently_used(void) {
	return last_encoder_use > millis() - 2000;
}

void encoder_init(void) {
	// rj: I've taken this from main.c, not quite sure why it is necessary - one inaccurate tick of the encoder on
	// startup can't hurt anything can it?
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
		encoder_value += enc_deltas[cur_state];

	// acceleration
	encoder_acc *= 0.998f;
	encoder_acc += abs(prev_encoder_value - encoder_value) * 0.125f;
}

void encoder_tick(void) {
	static u16 encoder_press_duration = 0;
	static bool prev_encoder_pressed;

	s8 enc_diff = encoder_value >> 2;

	// hold time
	if (encoder_pressed)
		encoder_press_duration++;

	if (encoder_press_duration > 250)
		plinky_reboot_sequence(encoder_press_duration);

	if ((enc_diff || encoder_pressed || prev_encoder_pressed)) {
		// log time
		last_encoder_use = millis();
		encoder_value -= enc_diff << 2;
	}

	switch (ui_mode) {
	case UI_DEFAULT:
	case UI_EDITING_A:
	case UI_EDITING_B:
		// get current or last edited param
		Param param_id = get_recent_param();
		// exit if no valid param
		if (param_id >= NUM_PARAMS)
			break;
		// encoder turned
		if (enc_diff)
			edit_param_from_encoder(enc_diff, encoder_acc);
		// start of an encoder press
		if (encoder_pressed && !prev_encoder_pressed)
			check_param_toggles(param_id);
		// release of a short encoder press
		else if (!encoder_pressed && prev_encoder_pressed && encoder_press_duration <= 50)
			params_toggle_default_value(param_id);
		// hold encoder (not during reboot sequence)
		if (encoder_press_duration <= 250)
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
	default:
		break;
	}

	if (!encoder_pressed)
		encoder_press_duration = 0;
	prev_encoder_pressed = encoder_pressed;
}