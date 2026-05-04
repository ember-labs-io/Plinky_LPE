#include "settings_menu.h"
#include "gfx/gfx.h"
#include "hardware/adc_dac.h"
#include "hardware/leds.h"
#include "hardware/memory.h"
#include "hardware/midi.h"
#include "hardware/touchstrips.h"
#include "synth/synth.h"
#include "ui/oled_viz.h"

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
	I_LAYOUT_GLOBAL = S_SYSTEM * 8,
	I_ACCEL_SENS,
	I_ENC_DIR,
	I_REFERENCE_PITCH,
	I_MIDI_TUNING,
	I_LOCAL_CTRL_OFF,
	// midi in
	I_MIDI_IN_CH = S_MIDI_IN * 8,
	I_MPE_IN,
	I_MIDI_IN_VEL_BALANCE,
	I_MIDI_IN_PRES_TYPE,
	I_MIDI_CHANNEL_BEND_RANGE_IN,
	I_MIDI_IN_SCALE_QUANT,
	I_MIDI_IN_CLOCK_MULT,
	I_MIDI_IN_FILTER,
	// midi out
	I_MIDI_OUT_CH = S_MIDI_OUT * 8,
	I_MPE_OUT,
	I_MIDI_OUT_VEL_BALANCE,
	I_MIDI_OUT_PRES_TYPE,
	I_MIDI_TRS_OUT_OFF,
	I_MIDI_SOFT_THRU,
	I_MIDI_OUT_FILTER_1,
	I_MIDI_OUT_FILTER_2,
	// cv
	I_CV_QUANT = S_CV * 8,
	I_CV_GATE_IN_IS_PRESSURE,
	I_CV_PPQN_IN,
	I_CV_PPQN_OUT,
	// actions
	I_REBOOT = S_ACTIONS * 8,
	I_TOUCH_CALIB,
	I_CV_CALIB,
	I_PUSH_PRESET,
	I_OG_PRESETS,
	I_CLEAR_MIDI_TUNING,
	I_MIDI_PANIC,

	NUM_DEFAULT_ITEMS,

	// alternative items

	I_MPE_IN_CHANS = I_MIDI_IN_CH + 64,
	I_MIDI_STRING_BEND_RANGE_IN = I_MIDI_IN_PRES_TYPE + 64,
	I_MPE_OUT_CHANS = I_MIDI_OUT_CH + 64,
	I_MIDI_STRING_BEND_RANGE_OUT = I_MIDI_OUT_PRES_TYPE + 64,
	NUM_MENU_ITEMS,
} Item;

const static SysParam item_to_sys_param[NUM_MENU_ITEMS] = {
    [I_LAYOUT_GLOBAL] = SYS_LAYOUT_GLOBAL,
    [I_ACCEL_SENS] = SYS_ACCEL_SENS,
    [I_ENC_DIR] = SYS_REVERSE_ENCODER,
    [I_REFERENCE_PITCH] = SYS_REFERENCE_PITCH,
    [I_LOCAL_CTRL_OFF] = SYS_LOCAL_CTRL_OFF,
    [I_MIDI_TUNING] = SYS_MIDI_TUNING,
    [I_MPE_IN] = SYS_MPE_IN,
    [I_MIDI_IN_CH] = SYS_MIDI_IN_CHAN,
    [I_MIDI_IN_VEL_BALANCE] = SYS_MIDI_IN_VEL_BALANCE,
    [I_MIDI_IN_PRES_TYPE] = SYS_MIDI_IN_PRES_TYPE,
    [I_MIDI_CHANNEL_BEND_RANGE_IN] = SYS_MIDI_CHANNEL_BEND_RANGE_IN,
    [I_MIDI_IN_SCALE_QUANT] = SYS_MIDI_IN_SCALE_QUANT,
    [I_MIDI_IN_FILTER] = SYS_MIDI_IN_FILTER,
    [I_MIDI_IN_CLOCK_MULT] = SYS_MIDI_IN_CLOCK_MULT,
    [I_MPE_OUT] = SYS_MPE_OUT,
    [I_MIDI_OUT_CH] = SYS_MIDI_OUT_CHAN,
    [I_MIDI_OUT_VEL_BALANCE] = SYS_MIDI_OUT_VEL_BALANCE,
    [I_MIDI_OUT_PRES_TYPE] = SYS_MIDI_OUT_PRES_TYPE,
    [I_MIDI_SOFT_THRU] = SYS_MIDI_SOFT_THRU,
    [I_MIDI_OUT_FILTER_1] = SYS_MIDI_OUT_FILTER_1,
    [I_MIDI_OUT_FILTER_2] = SYS_MIDI_OUT_FILTER_2,
    [I_MIDI_TRS_OUT_OFF] = SYS_MIDI_TRS_OUT_OFF,
    [I_CV_QUANT] = SYS_CV_QUANT,
    [I_CV_GATE_IN_IS_PRESSURE] = SYS_CV_GATE_IN_IS_PRESSURE,
    [I_CV_PPQN_IN] = SYS_CV_PPQN_IN,
    [I_CV_PPQN_OUT] = SYS_CV_PPQN_OUT,
    [I_MPE_IN_CHANS] = SYS_MPE_CHANS,
    [I_MIDI_STRING_BEND_RANGE_IN] = SYS_MIDI_STRING_BEND_RANGE_IN,
    [I_MPE_OUT_CHANS] = SYS_MPE_CHANS,
    [I_MIDI_STRING_BEND_RANGE_OUT] = SYS_MIDI_STRING_BEND_RANGE_OUT,
};

const static char* section_name[NUM_SYS_PARAM_SECTS] = {
    [S_SYSTEM] = "System", [S_MIDI_IN] = "Midi in", [S_MIDI_OUT] = "Midi out",
    [S_CV] = "CV in",      [S_ACTIONS] = "Actions",
};

const static char* item_name[NUM_MENU_ITEMS] = {
    [I_LAYOUT_GLOBAL] = "Layout",
    [I_ACCEL_SENS] = "Acc Sens",
    [I_ENC_DIR] = "Enc dir",
    [I_REFERENCE_PITCH] = "Ref A4 =",
    [I_LOCAL_CTRL_OFF] = "Local Ctrl",
    [I_MIDI_TUNING] = "Midi Tuning",
    [I_MPE_IN] = "MPE",
    [I_MIDI_IN_CH] = "Channel",
    [I_MIDI_IN_VEL_BALANCE] = "Vel/Pres",
    [I_MIDI_IN_PRES_TYPE] = "AfterTch",
    [I_MIDI_CHANNEL_BEND_RANGE_IN] = "Ch Bend",
    [I_MIDI_IN_SCALE_QUANT] = "To Scale",
    [I_MIDI_IN_FILTER] = "Filter",
    [I_MIDI_IN_CLOCK_MULT] = "Clock mult",
    [I_MPE_OUT] = "MPE",
    [I_MIDI_OUT_CH] = "Channel",
    [I_MIDI_OUT_VEL_BALANCE] = "Vel/Pres",
    [I_MIDI_OUT_PRES_TYPE] = "AfterTch",
    [I_MIDI_SOFT_THRU] = "Thru",
    [I_MIDI_OUT_FILTER_1] = "Filter 1",
    [I_MIDI_OUT_FILTER_2] = "Filter 2",
    [I_MIDI_TRS_OUT_OFF] = "TRS out",
    [I_CV_QUANT] = "Quant",
    [I_CV_GATE_IN_IS_PRESSURE] = "Gate",
    [I_CV_PPQN_IN] = "PPQN",
    [I_CV_PPQN_OUT] = "PPQN",
    [I_REBOOT] = "Reboot",
    [I_TOUCH_CALIB] = "Touch Calib",
    [I_CV_CALIB] = "CV Calib",
    [I_PUSH_PRESET] = "Push Preset",
    [I_OG_PRESETS] = "OG Presets",
    [I_CLEAR_MIDI_TUNING] = "Clear Midi Tuning",
    [I_MIDI_PANIC] = "Midi Panic",
    [I_MPE_IN_CHANS] = "Chans",
    [I_MIDI_STRING_BEND_RANGE_IN] = "Vc Bend",
    [I_MPE_OUT_CHANS] = "Chans",
    [I_MIDI_STRING_BEND_RANGE_OUT] = "Vc Bend",
};

static Item cur_item = 0;
static Section display_section; // only holds default sections
static u8 cur_value = 0;
static bool value_selected = false;
static u8 fill_start = OLED_WIDTH;
static bool perform_action = false;

static bool item_exists(u8 x, u8 y) {
	// actions
	if (y == S_ACTIONS && x < 7)
		return true;
	// sys params
	return item_to_sys_param[(y << 3) + x] != 0;
}

static void select_item(u8 x, u8 y) {
	Item item = (y << 3) + x;

	// alternative sections
	if ((sys_params.mpe_in && (item == I_MIDI_IN_CH || item == I_MIDI_IN_PRES_TYPE))
	    || (sys_params.mpe_out && (item == I_MIDI_OUT_CH || item == I_MIDI_OUT_PRES_TYPE)))
		item += 64;

	// no change
	if (item == cur_item)
		return;

	// save
	cur_item = item;
	display_section = (cur_item >> 3) & 7;

	// retrieve value
	cur_value = get_sys_param(item_to_sys_param[cur_item]);
}

static void save_value(s16 value) {
	SysParam param = item_to_sys_param[cur_item];
	if (!param)
		return;

	value = clampi(value, 0, sys_param_range(param) - 1);

	// actions on value save
	switch (cur_item) {
	case I_REFERENCE_PITCH:
		set_sys_param(param, value);
		update_reference_pitch();
		break;
	case I_LOCAL_CTRL_OFF:
		if (set_sys_param(param, value) && value)
			clear_latch();
		break;
	case I_MIDI_IN_CH:
		if (set_sys_param(param, value))
			midi_clear_all();
		break;
	case I_MPE_IN:
		if (set_sys_param(param, value))
			midi_update_zone_boundaries();
		break;
	case I_MIDI_CHANNEL_BEND_RANGE_IN:
	case I_MIDI_STRING_BEND_RANGE_IN:
	case I_MIDI_STRING_BEND_RANGE_OUT:
		set_sys_param(param, value);
		midi_precalc_bends();
		break;
	case I_MIDI_OUT_CH:
		if (set_sys_param(param, value))
			midi_clear_all();
		break;
	case I_MPE_IN_CHANS:
	case I_MPE_OUT_CHANS:
		set_mpe_channels(sys_params.mpe_zone, value + 1);
		break;
	default:
		set_sys_param(param, value);
		break;
	}
	cur_value = value;
}

void open_settings_menu(void) {
	ui_mode = UI_SETTINGS_MENU;
	// update display data
	display_section = (cur_item >> 3) & 7;
	cur_value = get_sys_param(item_to_sys_param[cur_item]);
}

void press_settings_menu_pad(u8 x, u8 y) {
	if (item_exists(x, y))
		select_item(x, y);
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
	case I_PUSH_PRESET:
		midi_push_preset();
		flash_message(F_16_BOLD, "preset", "pushed");
		break;
	case I_OG_PRESETS:
		revert_presets();
		break;
	case I_CLEAR_MIDI_TUNING:
		clear_midi_tuning();
		flash_message(F_16_BOLD, "cleared", "midi tuning");
		break;
	case I_MIDI_PANIC:
		midi_panic();
		break;
	default:
		break;
	}
	fill_start = OLED_WIDTH;
	perform_action = false;
	ui_mode = UI_DEFAULT;
}

void settings_encoder_press(bool pressed, u16 duration) {
	static bool enc_pressed = false;
	if (display_section == S_ACTIONS) {
		fill_start = pressed ? maxi(OLED_WIDTH * (LONG_PRESS_TIME - duration) / LONG_PRESS_TIME, 0) : OLED_WIDTH;
		if (duration >= LONG_PRESS_TIME + POST_PRESS_DELAY)
			perform_action = true;
	}
	else if (pressed && !enc_pressed)
		value_selected = !value_selected;
	enc_pressed = pressed;
}

void edit_settings_from_encoder(s8 enc_diff) {
	// having an action selected reverts to editing item selection
	if (value_selected && (cur_item & 63) >= S_ACTIONS * 8)
		value_selected = false;

	// edit value
	if (value_selected) {
		s16 new_value = cur_value;
		bool is_mpe_chans = cur_item == I_MPE_IN_CHANS || cur_item == I_MPE_OUT_CHANS;
		bool is_vel_balance = cur_item == I_MIDI_IN_VEL_BALANCE || cur_item == I_MIDI_OUT_VEL_BALANCE;
		// switch between lower/upper mpe zone through the mpe channels setting
		if (is_mpe_chans && new_value == 14) {
			if (enc_diff > 0 && sys_params.mpe_zone == 0) {
				set_sys_param(SYS_MPE_ZONE, 1);
				enc_diff--;
			}
			else if (enc_diff < 0 && sys_params.mpe_zone == 1) {
				set_sys_param(SYS_MPE_ZONE, 0);
				enc_diff++;
			}
		}
		if (
		    // avoid encoder glitching while editing its direction
		    (cur_item == I_ENC_DIR && cur_value)
		    // editing balance feels more natural inverted
		    || is_vel_balance
		    // upper zone channels go from high to low
		    || (is_mpe_chans && sys_params.mpe_zone == 1)
		    // inverted params
		    || cur_item == I_LOCAL_CTRL_OFF || cur_item == I_MIDI_TRS_OUT_OFF)
			enc_diff = -enc_diff;
		// update value
		new_value += enc_diff;
		// users should only be able to select 101 out of the 129 possible values
		if (is_vel_balance && (((new_value * 100) & 127) >= 100))
			new_value += enc_diff > 0 ? 1 : -1;
		save_value(new_value);
		return;
	}

	// edit item selection
	u8 x = cur_item & 7;
	u8 y = (cur_item & 63) >> 3;

	while (enc_diff > 0) {
		x++;
		if (x == 8 || !item_exists(x, y)) {
			x = 0;
			y = y == S_ACTIONS ? 0 : y + 1;
		}
		enc_diff--;
	}
	while (enc_diff < 0) {
		if (x == 0) {
			y = y == 0 ? S_ACTIONS : y - 1;
			x = 7;
			while (!item_exists(x, y))
				x--;
		}
		else
			x--;
		enc_diff++;
	}
	select_item(x, y);
}

static const char* get_param_str(Item item, u8 value, char* val_buf) {
	switch (item) {
	case I_LAYOUT_GLOBAL:
		return value ? "Global" : "Preset";
	case I_ACCEL_SENS:
		sprintf(val_buf, "%d%%", 2 * value - 200);
		return val_buf;
	case I_ENC_DIR:
		return value ? "Invert" : "Normal";
	case I_REFERENCE_PITCH:
		sprintf(val_buf, "%dHz", 430 + value);
		return val_buf;
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
	case I_MPE_IN_CHANS:
	case I_MPE_OUT_CHANS:
		// upper
		if (sys_params.mpe_zone)
			sprintf(val_buf, sys_params.mpe_chans == 0 ? "15 [16]" : "%u-15 [16]", 15 - sys_params.mpe_chans);
		// lower
		else
			sprintf(val_buf, sys_params.mpe_chans == 0 ? "2 [ 1 ]" : "2-%u [ 1 ]", sys_params.mpe_chans + 2);
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
		}
	case I_MPE_IN:
	case I_MPE_OUT:
	case I_MIDI_IN_SCALE_QUANT:
	case I_MIDI_TUNING:
		return value ? "On" : "Off";
	case I_LOCAL_CTRL_OFF:
	case I_MIDI_TRS_OUT_OFF:
		return value ? "Off" : "On";
	case I_MIDI_SOFT_THRU:
		return value ? "Consume" : "Off";
	case I_CV_QUANT:
		return value == CVQ_OFF ? "Off" : value == CVQ_CHROMATIC ? "Chrom" : "Scale";
	case I_CV_GATE_IN_IS_PRESSURE:
		return value ? "Press" : "Gate";
	// ppqns
	case I_CV_PPQN_IN:
	case I_CV_PPQN_OUT:
		sprintf(val_buf, "%d", ppqn_values[value]);
		return val_buf;
	// bend ranges
	case I_MIDI_CHANNEL_BEND_RANGE_IN:
	case I_MIDI_STRING_BEND_RANGE_IN:
	case I_MIDI_STRING_BEND_RANGE_OUT:
		sprintf(val_buf, "%d%ssemi", bend_ranges[value], value >= 5 ? "" : " ");
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
	// section name
	switch (cur_item) {
	case I_CV_PPQN_OUT:
		draw_str(1, 0, F_16_BOLD, "CV out");
		break;
	default:
		draw_str(1, 0, F_16_BOLD, section_name[display_section]);
		break;
	}
	Font font = F_16;
	// actions
	if (display_section == S_ACTIONS) {
		draw_str(1, 16, font, item_name[cur_item]);
		if (cur_item != I_CLEAR_MIDI_TUNING)
			draw_str(OLED_WIDTH - 32, 15, font, I_TOUCH);
		if (fill_start < OLED_WIDTH)
			inverted_rectangle(fill_start, 0, OLED_WIDTH, OLED_HEIGHT);
		return;
	}
	// selection arrow
	const u8 arrow_width = 15;
	draw_str(value_selected ? 113 : 0, 15, font, value_selected ? I_LEFT : I_RIGHT);
	// name
	draw_str(2 + (value_selected ? 0 : arrow_width), 16, font, item_name[cur_item]);

	u8 right_offset = OLED_WIDTH - 1 - (value_selected ? arrow_width : 0);
	switch (cur_item) {
	// icons
	case I_MIDI_IN_FILTER: {
		u8 x = right_offset - 48;
		draw_str(x, 16, font, sys_params.midi_rcv_clock ? I_TEMPO : I_CROSS);
		draw_str(x + 16, 16, font, sys_params.midi_rcv_transport ? I_PLAY : I_CROSS);
		draw_str(x + 32, 16, font, sys_params.midi_rcv_param_ccs ? I_KNOB : I_CROSS);
		if (sys_params.midi_rcv_param_ccs == RP_CC14) {
			x += 35;
			fill_rectangle(x, 16, x + 9, 32);
			inverted_rectangle(x, 16, x + 9, 32);
			x++;
			draw_str(x, 17, F_8, "C");
			draw_str(x, 24, F_8, "1");
			x += 4;
			draw_str(x, 17, F_8, "C");
			draw_str(x, 24, F_8, "4");
		}
		break;
	}
	case I_MIDI_OUT_FILTER_1: {
		u8 x = right_offset - 48;
		draw_str(x, 16, font, sys_params.midi_send_clock ? I_TEMPO : I_CROSS);
		x += 16;
		draw_str(x, 16, font, sys_params.midi_send_transport ? I_PLAY : I_CROSS);
		x += 16;
		draw_str(x, 16, font, sys_params.midi_send_param_ccs ? I_KNOB : I_CROSS);
		if (sys_params.midi_send_param_ccs == SP_NRPN) {
			x += 2;
			fill_rectangle(x, 16, x + 11, 32);
			inverted_rectangle(x, 16, x + 11, 32);
			x++;
			draw_str(x, 17, F_8, "N");
			draw_str(x, 24, F_8, "P");
			x += 5;
			draw_str(x, 17, F_8, "R");
			draw_str(x, 24, F_8, "N");
		}
		break;
	}
	case I_MIDI_OUT_FILTER_2: {
		u8 x = right_offset - 48;
		draw_str(x, 16, font, sys_params.mpe_out_fine_tuning ? I_FORK : I_CROSS);
		if (sys_params.mpe_out_fine_tuning) {
			u8 y = 20;
			fill_rectangle(x, y + 1, x + 16, y + 7);
			inverted_rectangle(x, y + 1, x + 16, y + 7);
			draw_str(x, y, F_8, "mpe");
		}
		x += 16;
		draw_str(x, 16, font, sys_params.midi_send_lfo_cc ? I_ALFO : I_CROSS);
		x += 16;
		if (sys_params.midi_out_yz_control) {
			draw_str(x + 1, 17, F_12, "Y");
			draw_str(x + 8, 21, F_12, "Z");
		}
		else
			draw_str(x, 16, font, I_CROSS);
		break;
	}
	// value
	default: {
		char val_buf[16];
		const char* val_str = get_param_str(cur_item, cur_value, val_buf);
		u8 width = str_width(font, val_str);
		draw_str(right_offset - width, 16, font, val_str);
		break;
	}
	}
}

void settings_menu_leds(u8 pulse) {
	memset(leds, 0, sizeof(leds));
	for (u8 y = 0; y < NUM_SYS_PARAM_SECTS; y++) {
		bool active_sect = y == display_section;
		for (u8 x = 0; x < 8; x++) {
			// highlight selected item
			if ((y << 3) + x == (cur_item & 63))
				leds[x][y] = 255;
			// light up section items
			else if (item_exists(x, y))
				leds[x][y] = led_add_gamma(active_sect ? 64 : 32);
		}
	}
	// pulse settings pad
	leds[5][7] = pulse;
}