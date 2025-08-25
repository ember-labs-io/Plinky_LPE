#include "settings_menu.h"
#include "gfx/gfx.h"
#include "hardware/leds.h"
#include "hardware/ram.h"

#define NUM_ITEMS 64

typedef enum Section {
	S_SYSTEM,
	S_MIDI,
	S_CV,
	NUM_SYS_PARAM_SECTS,
} Section;

typedef enum Item {
	// system
	I_ACCEL_SENS = S_SYSTEM * 8,
	// midi
	I_MIDI_IN_CH = S_MIDI * 8,
	I_MIDI_OUT_CH,
	// cv
	I_CV_QUANT = S_CV * 8,

	MAX_ITEM,
} Item;

const static u8 num_options[NUM_ITEMS] = {
    [I_ACCEL_SENS] = 201,
    [I_MIDI_IN_CH] = 16,
    [I_MIDI_OUT_CH] = 16,
    [I_CV_QUANT] = NUM_CV_QUANT_TYPES,
};

const static char* section_name[NUM_SYS_PARAM_SECTS] = {
    [S_SYSTEM] = "System",
    [S_MIDI] = "Midi",
    [S_CV] = "CV",
};

const static char* item_name[NUM_ITEMS] = {
    [I_ACCEL_SENS] = "Acc sens",
    [I_MIDI_IN_CH] = "In channel",
    [I_MIDI_OUT_CH] = "Out channel",
    [I_CV_QUANT] = "Quant",
};

static Item cur_item = 0;
static u8 cur_value = 0;
static bool value_selected = false;

static void select_item(Item item, bool force) {
	if (item == cur_item && !force)
		return;
	cur_item = item;
	switch (cur_item) {
	case I_ACCEL_SENS:
		cur_value = sys_params.accel_sens;
		break;
	case I_MIDI_IN_CH:
		cur_value = sys_params.midi_in_chan;
		break;
	case I_MIDI_OUT_CH:
		cur_value = sys_params.midi_out_chan;
		break;
	case I_CV_QUANT:
		cur_value = sys_params.cv_quant;
		break;
	default:
		break;
	}
}

static void save_value(u8 value) {
	value = clampi(value, 0, num_options[cur_item] - 1);
	u8 saved_value = 0;
	switch (cur_item) {
	case I_ACCEL_SENS:
		saved_value = sys_params.accel_sens;
		break;
	case I_MIDI_IN_CH:
		saved_value = sys_params.midi_in_chan;
		break;
	case I_MIDI_OUT_CH:
		saved_value = sys_params.midi_out_chan;
		break;
	case I_CV_QUANT:
		saved_value = sys_params.cv_quant;
		break;
	default:
		break;
	}
	if (value == saved_value)
		return;
	cur_value = value;
	switch (cur_item) {
	case I_ACCEL_SENS:
		sys_params.accel_sens = cur_value;
		break;
	case I_MIDI_IN_CH:
		sys_params.midi_in_chan = cur_value;
		break;
	case I_MIDI_OUT_CH:
		sys_params.midi_out_chan = cur_value;
		break;
	case I_CV_QUANT:
		sys_params.cv_quant = cur_value;
		break;
	default:
		break;
	}
	log_ram_edit(SEG_SYS);
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

void settings_encoder_press(void) {
	value_selected = !value_selected;
}

void edit_settings_from_encoder(s8 enc_diff) {
	// edit value
	if (value_selected) {
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
	// 1-based
	case I_MIDI_IN_CH:
	case I_MIDI_OUT_CH:
		sprintf(val_buf, "%d", value + 1);
		return val_buf;
	case I_CV_QUANT:
		return cv_quant_name[value];
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
	Section section = cur_item / 8;
	draw_str(0, 0, F_16_BOLD, section_name[section]);
	// selection arrow
	Font font = F_16;
	draw_str(value_selected ? 110 : 0, 16, font, value_selected ? I_LEFT : I_RIGHT);
	// name
	draw_str(value_selected ? 0 : 18, 17, font, item_name[cur_item]);
	// value
	char val_buf[16];
	const char* val_str = get_param_str(cur_item, cur_value, val_buf);
	u8 width = str_width(font, val_str);
	draw_str(OLED_WIDTH - width - (value_selected ? 21 : 2), 17, font, val_str);
}

void settings_menu_leds(void) {
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
}