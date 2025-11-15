#include "settings_menu.h"
#include "gfx/gfx.h"
#include "hardware/adc_dac.h"
#include "hardware/leds.h"
#include "hardware/memory.h"
#include "hardware/midi.h"
#include "hardware/touchstrips.h"

#define NUM_ITEMS 64

typedef enum Section {
	S_SYSTEM,
	S_MIDI,
	S_CV,
	S_ACTIONS,
	NUM_SYS_PARAM_SECTS,
} Section;

typedef enum Item {
	// system
	I_ACCEL_SENS = S_SYSTEM * 8,
	I_ENC_DIR,
	// midi
	I_MIDI_IN_CH = S_MIDI * 8,
	I_MIDI_OUT_CH,
	I_MIDI_CLOCK_IN_MULT,
	// cv
	I_CV_QUANT = S_CV * 8,
	I_CV_PPQN_IN,
	I_CV_PPQN_OUT,
	// actions
	I_REBOOT = S_ACTIONS * 8,
	I_TOUCH_CALIB,
	I_CV_CALIB,
	I_OG_PRESETS,

	MAX_ITEM,
} Item;

const static u8 num_options[NUM_ITEMS] = {
    [I_ACCEL_SENS] = 201,
    [I_ENC_DIR] = 2,
    [I_MIDI_IN_CH] = 16,
    [I_MIDI_OUT_CH] = 16,
    [I_MIDI_CLOCK_IN_MULT] = 3,
    [I_CV_QUANT] = NUM_CV_QUANT_TYPES,
    [I_CV_PPQN_IN] = NUM_PPQN_VALUES,
    [I_CV_PPQN_OUT] = NUM_PPQN_VALUES,
    [I_REBOOT] = 1,
    [I_TOUCH_CALIB] = 1,
    [I_CV_CALIB] = 1,
    [I_OG_PRESETS] = 1,
};

const static char* section_name[NUM_SYS_PARAM_SECTS] = {
    [S_SYSTEM] = "System",
    [S_MIDI] = "Midi",
    [S_CV] = "CV",
    [S_ACTIONS] = "Actions",
};

const static char* item_name[NUM_ITEMS] = {
    [I_ACCEL_SENS] = "Acc sens",
    [I_ENC_DIR] = "Enc dir",
    [I_MIDI_IN_CH] = "In channel",
    [I_MIDI_OUT_CH] = "Out channel",
    [I_MIDI_CLOCK_IN_MULT] = "Clock in mult",
    [I_CV_QUANT] = "Quant",
    [I_CV_PPQN_IN] = "PPQN in",
    [I_CV_PPQN_OUT] = "PPQN out",
    [I_REBOOT] = "Reboot",
    [I_TOUCH_CALIB] = "Touch Calib",
    [I_CV_CALIB] = "CV Calib",
    [I_OG_PRESETS] = "OG Presets",
};

static Item cur_item = 0;
static Section cur_section;
static u8 cur_value = 0;
static bool value_selected = false;
static u8 fill_start = OLED_WIDTH;
static bool perform_action = false;

static void select_item(Item item, bool force) {
	if (item == cur_item && !force)
		return;
	cur_item = item;
	cur_section = cur_item / 8;
	switch (cur_item) {
	case I_ACCEL_SENS:
		cur_value = sys_params.accel_sens;
		break;
	case I_ENC_DIR:
		cur_value = sys_params.reverse_encoder;
		break;
	case I_MIDI_IN_CH:
		cur_value = sys_params.midi_in_chan;
		break;
	case I_MIDI_OUT_CH:
		cur_value = sys_params.midi_out_chan;
		break;
	case I_MIDI_CLOCK_IN_MULT:
		cur_value = sys_params.midi_in_clock_mult;
		break;
	case I_CV_QUANT:
		cur_value = sys_params.cv_quant;
		break;
	case I_CV_PPQN_IN:
		cur_value = sys_params.cv_in_ppqn;
		break;
	case I_CV_PPQN_OUT:
		cur_value = sys_params.cv_out_ppqn;
		break;
	default:
		break;
	}
}

static void save_value(u8 value) {
	value = clampi(value, 0, num_options[cur_item] - 1);
	switch (cur_item) {
	case I_ACCEL_SENS:
		set_sys_param(SYS_ACCEL_SENS, value);
		break;
	case I_ENC_DIR:
		set_sys_param(SYS_REVERSE_ENCODER, value);
		break;
	case I_MIDI_IN_CH:
		if (set_sys_param(SYS_MIDI_IN_CHAN, value))
			midi_clear_all();
		break;
	case I_MIDI_OUT_CH:
		if (set_sys_param(SYS_MIDI_OUT_CHAN, value))
			midi_clear_all();
		break;
	case I_MIDI_CLOCK_IN_MULT:
		set_sys_param(SYS_MID_CLOCK_IN_MULT, value);
		break;
	case I_CV_QUANT:
		set_sys_param(SYS_CV_QUANT, value);
		break;
	case I_CV_PPQN_IN:
		set_sys_param(SYS_CV_PPQN_IN, value);
		break;
	case I_CV_PPQN_OUT:
		set_sys_param(SYS_CV_PPQN_OUT, value);
		break;
	default:
		break;
	}
	cur_value = value;
}

void open_settings_menu(void) {
	ui_mode = UI_SETTINGS_MENU;
	// force-load the value of the first item
	select_item(cur_item, true);
}

void select_settings_item(u8 x, u8 y) {
	Item item = 8 * y + x;
	if (item != cur_item && num_options[item])
		select_item(item, false);
}

void settings_menu_actions(void) {
	if (!perform_action)
		return;

	// visuals
	switch (cur_item) {
	case I_REBOOT:
	case I_CV_CALIB:
		oled_clear();
		draw_str_ctr(0, F_16, "release");
		draw_str_ctr(16, F_16, "encoder");
		oled_flip();
		HAL_Delay(1500);
		break;
	default:
		break;
	}

	// actions
	switch (cur_item) {
	case I_REBOOT:
		oled_clear();
		oled_flip();
		HAL_NVIC_SystemReset();
		break;
	case I_TOUCH_CALIB:
		touch_calib(FLASH_CALIB_COMPLETE);
		break;
	case I_CV_CALIB:
		cv_calib();
		break;
	case I_OG_PRESETS:
		revert_presets();
		break;
	default:
		break;
	}
	perform_action = false;
	ui_mode = UI_DEFAULT;
}

void settings_encoder_press(bool pressed, u16 duration) {
	static bool enc_pressed = false;
	if (cur_section == S_ACTIONS) {
		fill_start = pressed ? maxi(OLED_WIDTH * (LONG_PRESS_TIME - duration) / LONG_PRESS_TIME, 0) : OLED_WIDTH;
		if (duration >= LONG_PRESS_TIME + POST_PRESS_DELAY)
			perform_action = true;
	}
	else if (pressed && !enc_pressed)
		value_selected = !value_selected;
	enc_pressed = pressed;
}

void edit_settings_from_encoder(s8 enc_diff) {
	// edit value
	if (value_selected) {
		// avoid encoder glitch while editing
		if (cur_item == I_ENC_DIR && cur_value)
			enc_diff = -enc_diff;
		save_value(maxi(cur_value + enc_diff, 0));
		return;
	}
	// edit item selection
	u8 new_item = cur_item;
	if (enc_diff > 0)
		while (new_item < MAX_ITEM - 1 && enc_diff != 0) {
			new_item++;
			while (!num_options[new_item])
				new_item++;
			enc_diff--;
		}
	else
		while (new_item > 0 && enc_diff != 0) {
			new_item--;
			while (!num_options[new_item])
				new_item--;
			enc_diff++;
		}
	select_item(new_item, false);
}

static const char* get_param_str(Item item, u8 value, char* val_buf) {
	switch (item) {
	case I_ACCEL_SENS:
		sprintf(val_buf, "%d", 2 * value - 200);
		return val_buf;
	case I_ENC_DIR:
		return value ? "Rvrse" : "Normal";
	// 1-based
	case I_MIDI_IN_CH:
	case I_MIDI_OUT_CH:
		sprintf(val_buf, "%d", value + 1);
		return val_buf;
	case I_MIDI_CLOCK_IN_MULT:
		switch (value) {
		case 0:
			return "1/2";
		case 1:
			return "x1";
		case 2:
			return "x2";
		}
		return val_buf;
	case I_CV_QUANT:
		return cv_quant_name[value];
	// ppqns
	case I_CV_PPQN_IN:
	case I_CV_PPQN_OUT:
		sprintf(val_buf, "%d", ppqn_values[value]);
		return val_buf;
	default:
		sprintf(val_buf, "%d", value);
		return val_buf;
	}
}

void draw_settings_menu(void) {
	// "settings"
	draw_str(79, 1, F_8_BOLD, "SETTINGS");
	vline(OLED_WIDTH / 2, 0, 9, 1);
	vline(OLED_WIDTH - 1, 0, 9, 1);
	hline(OLED_WIDTH / 2, 9, OLED_WIDTH, 1);
	// section
	draw_str(1, 0, F_16_BOLD, section_name[cur_section]);
	Font font = F_16;
	// actions
	if (cur_section == S_ACTIONS) {
		draw_str(1, 17, font, item_name[cur_item]);
		draw_str(OLED_WIDTH - 32, 15, font, I_TOUCH);
		if (fill_start < OLED_WIDTH)
			inverted_rectangle(fill_start, 0, OLED_WIDTH, OLED_HEIGHT);
		return;
	}
	// selection arrow
	draw_str(value_selected ? 110 : 0, 16, font, value_selected ? I_LEFT : I_RIGHT);
	// name
	draw_str(value_selected ? 0 : 18, 17, font, item_name[cur_item]);
	// value
	char val_buf[16];
	const char* val_str = get_param_str(cur_item, cur_value, val_buf);
	u8 width = str_width(font, val_str);
	draw_str(OLED_WIDTH - width - (value_selected ? 21 : 2), 17, font, val_str);
}

void settings_menu_leds(u8 pulse) {
	memset(leds, 0, sizeof(leds));
	for (u8 y = 0; y < NUM_SYS_PARAM_SECTS; y++) {
		bool active_sect = y == cur_item / 8;
		for (u8 x = 0; x < 8; x++) {
			Item param_id = 8 * y + x;
			if (param_id == cur_item)
				leds[x][y] = 255;
			else if (num_options[param_id])
				leds[x][y] = led_add_gamma(active_sect ? 64 : 32);
		}
	}
	// pulse settings pad
	leds[5][7] = pulse;
}