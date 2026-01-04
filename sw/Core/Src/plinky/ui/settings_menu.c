#include "settings_menu.h"
#include "gfx/gfx.h"
#include "hardware/adc_dac.h"
#include "hardware/leds.h"
#include "hardware/memory.h"
#include "hardware/midi.h"
#include "hardware/touchstrips.h"
#include "synth/synth.h"

#define NUM_ITEMS 64

typedef enum Section {
	S_SYSTEM,
	S_MIDI_IN,
	S_MIDI_OUT,
	S_CV,
	S_ACTIONS,
	NUM_SYS_PARAM_SECTS,
} Section;

typedef enum Item {
	// system
	I_ACCEL_SENS = S_SYSTEM * 8,
	I_ENC_DIR,
	I_LOCAL_CONTROL,
	// midi in
	I_MIDI_IN_CH = S_MIDI_IN * 8,
	I_MIDI_IN_VEL_BALANCE,
	I_MIDI_IN_PRES_TYPE,
	I_MIDI_IN_CLOCK_MULT,
	I_MIDI_CHANNEL_BEND_RANGE_IN,
	I_MIDI_STRING_BEND_RANGE_IN,
	// midi out
	I_MIDI_OUT_CH = S_MIDI_OUT * 8,
	I_MIDI_OUT_VEL_BALANCE,
	I_MIDI_OUT_PRES_TYPE,
	I_MIDI_OUT_CCS,
	I_MIDI_OUT_LFOS,
	I_MIDI_OUT_PARAMS,
	I_MIDI_SOFT_THRU,
	I_MIDI_STRING_BEND_RANGE_OUT,
	// cv
	I_CV_QUANT = S_CV * 8,
	I_CV_PPQN_IN,
	I_CV_PPQN_OUT,
	// actions
	I_REBOOT = S_ACTIONS * 8,
	I_TOUCH_CALIB,
	I_CV_CALIB,
	I_OG_PRESETS,
	I_MIDI_PANIC,

	MAX_ITEM,
} Item;

const static u8 num_options[NUM_ITEMS] = {
    [I_ACCEL_SENS] = 201,
    [I_ENC_DIR] = 2,
    [I_LOCAL_CONTROL] = 2,
    [I_MIDI_IN_CH] = 16,
    [I_MIDI_OUT_CH] = 16,
    [I_MIDI_IN_CLOCK_MULT] = 3,
    [I_MIDI_IN_VEL_BALANCE] = 129,
    [I_MIDI_OUT_VEL_BALANCE] = 129,
    [I_MIDI_CHANNEL_BEND_RANGE_IN] = NUM_BEND_RANGES,
    [I_MIDI_STRING_BEND_RANGE_IN] = NUM_BEND_RANGES,
    [I_MIDI_IN_PRES_TYPE] = NUM_MIDI_PRESSURE_TYPES,
    [I_MIDI_OUT_PRES_TYPE] = NUM_MIDI_PRESSURE_TYPES,
    [I_MIDI_OUT_CCS] = 3,
    [I_MIDI_OUT_LFOS] = 2,
    [I_MIDI_OUT_PARAMS] = 2,
    [I_MIDI_SOFT_THRU] = 2,
    [I_MIDI_STRING_BEND_RANGE_OUT] = NUM_BEND_RANGES,
    [I_CV_QUANT] = NUM_CV_QUANT_TYPES,
    [I_CV_PPQN_IN] = NUM_PPQN_VALUES,
    [I_CV_PPQN_OUT] = NUM_PPQN_VALUES,
    [I_REBOOT] = 1,
    [I_TOUCH_CALIB] = 1,
    [I_CV_CALIB] = 1,
    [I_OG_PRESETS] = 1,
    [I_MIDI_PANIC] = 1,
};

const static char* section_name[NUM_SYS_PARAM_SECTS] = {
    [S_SYSTEM] = "System", [S_MIDI_IN] = "Midi in", [S_MIDI_OUT] = "Midi out", [S_CV] = "CV", [S_ACTIONS] = "Actions",
};

const static char* item_name[NUM_ITEMS] = {
    [I_ACCEL_SENS] = "Acc sens",
    [I_ENC_DIR] = "Enc dir",
    [I_LOCAL_CONTROL] = "Local Ctrl",
    [I_MIDI_IN_CH] = "Channel",
    [I_MIDI_IN_VEL_BALANCE] = "Vel/Pres",
    [I_MIDI_IN_PRES_TYPE] = "AfterTch",
    [I_MIDI_IN_CLOCK_MULT] = "Clock mult",
    [I_MIDI_CHANNEL_BEND_RANGE_IN] = "Bend",
    [I_MIDI_STRING_BEND_RANGE_IN] = "Ch bend",
    [I_MIDI_OUT_CH] = "Channel",
    [I_MIDI_OUT_VEL_BALANCE] = "Vel/Pres",
    [I_MIDI_OUT_PRES_TYPE] = "AfterTch",
    [I_MIDI_OUT_CCS] = "Touch CCs",
    [I_MIDI_OUT_LFOS] = "LFO CCs",
    [I_MIDI_OUT_PARAMS] = "Param CCs",
    [I_MIDI_SOFT_THRU] = "Thru",
    [I_MIDI_STRING_BEND_RANGE_OUT] = "Ch bend",
    [I_CV_QUANT] = "Quant",
    [I_CV_PPQN_IN] = "PPQN in",
    [I_CV_PPQN_OUT] = "PPQN out",
    [I_REBOOT] = "Reboot",
    [I_TOUCH_CALIB] = "Touch Calib",
    [I_CV_CALIB] = "CV Calib",
    [I_OG_PRESETS] = "OG Presets",
    [I_MIDI_PANIC] = "Midi Panic",
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
	case I_LOCAL_CONTROL:
		cur_value = sys_params.local_on;
		break;
	case I_MIDI_IN_CH:
		cur_value = sys_params.midi_in_chan;
		break;
	case I_MIDI_IN_VEL_BALANCE:
		cur_value = sys_params.midi_in_vel_balance;
		break;
	case I_MIDI_IN_PRES_TYPE:
		cur_value = sys_params.midi_in_pres_type;
		break;
	case I_MIDI_IN_CLOCK_MULT:
		cur_value = sys_params.midi_in_clock_mult;
		break;
	case I_MIDI_CHANNEL_BEND_RANGE_IN:
		cur_value = sys_params.midi_channel_bend_range_in;
		break;
	case I_MIDI_STRING_BEND_RANGE_IN:
		cur_value = sys_params.midi_string_bend_range_in;
		break;
	case I_MIDI_OUT_CH:
		cur_value = sys_params.midi_out_chan;
		break;
	case I_MIDI_OUT_VEL_BALANCE:
		cur_value = sys_params.midi_out_vel_balance;
		break;
	case I_MIDI_OUT_PRES_TYPE:
		cur_value = sys_params.midi_out_pres_type;
		break;
	case I_MIDI_OUT_CCS:
		cur_value = sys_params.midi_out_ccs;
		break;
	case I_MIDI_OUT_LFOS:
		cur_value = sys_params.midi_out_lfos;
		break;
	case I_MIDI_OUT_PARAMS:
		cur_value = sys_params.midi_out_params;
		break;
	case I_MIDI_SOFT_THRU:
		cur_value = sys_params.midi_soft_thru;
		break;
	case I_MIDI_STRING_BEND_RANGE_OUT:
		cur_value = sys_params.midi_string_bend_range_out;
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

static void save_value(s16 value) {
	value = clampi(value, 0, num_options[cur_item] - 1);
	switch (cur_item) {
	case I_ACCEL_SENS:
		set_sys_param(SYS_ACCEL_SENS, value);
		break;
	case I_ENC_DIR:
		set_sys_param(SYS_REVERSE_ENCODER, value);
		break;
	case I_LOCAL_CONTROL:
		if (set_sys_param(SYS_LOCAL_ON, value) && !value)
			clear_latch();
		break;
	case I_MIDI_IN_CH:
		if (set_sys_param(SYS_MIDI_IN_CHAN, value))
			midi_clear_all();
		break;
	case I_MIDI_IN_VEL_BALANCE:
		set_sys_param(SYS_MIDI_IN_VEL_BALANCE, value);
		break;
	case I_MIDI_IN_PRES_TYPE:
		set_sys_param(SYS_MIDI_IN_PRES_TYPE, value);
		break;
	case I_MIDI_IN_CLOCK_MULT:
		set_sys_param(SYS_MIDI_IN_CLOCK_MULT, value);
		break;
	case I_MIDI_CHANNEL_BEND_RANGE_IN:
		set_sys_param(SYS_MIDI_CHANNEL_BEND_RANGE_IN, value);
		midi_precalc_bends();
		break;
	case I_MIDI_STRING_BEND_RANGE_IN:
		set_sys_param(SYS_MIDI_STRING_BEND_RANGE_IN, value);
		midi_precalc_bends();
		break;
	case I_MIDI_OUT_CH:
		if (set_sys_param(SYS_MIDI_OUT_CHAN, value))
			midi_clear_all();
		break;
	case I_MIDI_OUT_VEL_BALANCE:
		set_sys_param(SYS_MIDI_OUT_VEL_BALANCE, value);
		break;
	case I_MIDI_OUT_PRES_TYPE:
		set_sys_param(SYS_MIDI_OUT_PRES_TYPE, value);
		break;
	case I_MIDI_OUT_CCS:
		set_sys_param(SYS_MIDI_OUT_CCS, value);
		break;
	case I_MIDI_OUT_LFOS:
		set_sys_param(SYS_MIDI_OUT_LFOS, value);
		break;
	case I_MIDI_OUT_PARAMS:
		set_sys_param(SYS_MIDI_OUT_PARAMS, value);
		break;
	case I_MIDI_SOFT_THRU:
		set_sys_param(SYS_MIDI_SOFT_THRU, value);
		break;
	case I_MIDI_STRING_BEND_RANGE_OUT:
		set_sys_param(SYS_MIDI_STRING_BEND_RANGE_OUT, value);
		midi_precalc_bends();
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
	case I_MIDI_PANIC:
		midi_panic();
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
		s16 new_value = cur_value;
		if (
		    // avoid encoder glitching while editing its direction
		    (cur_item == I_ENC_DIR && cur_value)
		    // editing balance feels more natural inverted
		    || cur_item == I_MIDI_IN_VEL_BALANCE || cur_item == I_MIDI_OUT_VEL_BALANCE)
			enc_diff = -enc_diff;
		// update value
		new_value += enc_diff;
		// users should only be able to select 101 out of the 129 possible values
		if ((cur_item == I_MIDI_IN_VEL_BALANCE || cur_item == I_MIDI_OUT_VEL_BALANCE)
		    && (((new_value * 100) & 127) >= 100))
			new_value += enc_diff > 0 ? 1 : -1;
		save_value(new_value);
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
	// shown as a percentage
	case I_MIDI_IN_VEL_BALANCE:
	case I_MIDI_OUT_VEL_BALANCE:
		value = value * 100 >> 7;
		sprintf(val_buf, "%d/%d", value, 100 - value);
		return val_buf;
	// 1-based
	case I_MIDI_IN_CH:
	case I_MIDI_OUT_CH:
		sprintf(val_buf, "%d", value + 1);
		return val_buf;
	case I_MIDI_IN_CLOCK_MULT:
		switch (value) {
		case 0:
			return "x1/2";
		case 1:
			return "x1";
		case 2:
			return "x2";
		}
		return val_buf;
	case I_MIDI_IN_PRES_TYPE:
	case I_MIDI_OUT_PRES_TYPE:
		switch (value) {
		case 0:
			return "Off";
		case 1:
			return "Mono";
		case 2:
			return "Poly";
		case 3:
			return "MPE";
		}
	case I_LOCAL_CONTROL:
	case I_MIDI_OUT_CCS:
	case I_MIDI_OUT_LFOS:
	case I_MIDI_OUT_PARAMS:
		return value ? "On" : "Off";
	case I_MIDI_SOFT_THRU:
		return value ? "Consume" : "Off";
	case I_CV_QUANT:
		return cv_quant_name[value];
	// ppqns
	case I_CV_PPQN_IN:
	case I_CV_PPQN_OUT:
		sprintf(val_buf, "%d", ppqn_values[value]);
		return val_buf;
	// bend ranges
	case I_MIDI_CHANNEL_BEND_RANGE_IN:
	case I_MIDI_STRING_BEND_RANGE_IN:
	case I_MIDI_STRING_BEND_RANGE_OUT:
		sprintf(val_buf, "%d%ssemi", bend_ranges[value],
		        (item == I_MIDI_STRING_BEND_RANGE_IN || item == I_MIDI_STRING_BEND_RANGE_OUT) && value >= 5 ? "" : " ");
		return val_buf;
	default:
		sprintf(val_buf, "%d", value);
		return val_buf;
	}
}

void draw_settings_menu(void) {
	// "settings"
	draw_str(79, 1, F_8, "SETTINGS");
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