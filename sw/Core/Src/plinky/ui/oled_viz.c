#include "oled_viz.h"
#include "gfx/data/logo.h"
#include "gfx/data/names.h"
#include "hardware/ram.h"
#include "synth/sampler.h"
#include "ui/pad_actions.h"
#include "ui/shift_states.h"

// == TOOLS == //

const char* note_name(int note) {
	note += 12;
	if (note < 0 || note > 8 * 12)
		return "";
	static char buf[4];
	int octave = note / 12;
	note -= octave * 12;
	buf[0] = "CCDDEFFGGAAB"[note];
	buf[1] = " + +  + + + "[note];
	buf[2] = '0' + octave;
	return buf;
}

#define RND(y) dither[(i & 3) + ((i / 128 + y) & 3) * 4]

static void gfx_dither_logo(u8 frame) {
	const static u8 dither[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
	const u8* l = get_logo() - 1;
	u8* v = oled_buffer() - 1;
	u8 k = frame / 2;
	for (u16 i = 0; i < 32 * 128 / 8; ++i) {
		u8 mask = 0;
		if (RND(0) < k)
			mask |= 1;
		if (RND(1) < k)
			mask |= 2;
		if (RND(2) < k)
			mask |= 4;
		if (RND(3) < k)
			mask |= 8;
		if (RND(4) < k)
			mask |= 16;
		if (RND(5) < k)
			mask |= 32;
		if (RND(6) < k)
			mask |= 64;
		if (RND(7) < k)
			mask |= 128;
		*v = (*v & mask) | (*l & ~mask);
		v++;
		l++;
	}
}

#undef RND

// == MESSAGE == //

static const char* message = 0;
static const char* submessage = 0;
static u8 message_font;
static u32 message_time = 0;

void flash_message(Font fnt, const char* msg, const char* submsg) {
	message = msg;
	submessage = submsg;
	message_font = fnt;
	message_time = millis() + 500;
}

void flash_parameter(u8 param_id) {
	flash_message(F_20_BOLD, param_names[param_id], param_page_names[param_id / 6]);
}

static bool draw_message(void) {
	if (!message)
		return false;
	if (millis() > message_time) {
		message = 0;
		return false;
	}
	u8 y = 0;
	if (submessage) {
		draw_str(0, 0, F_12, submessage);
		y = 12;
	}
	draw_str(0, y, message_font, message);
	return true;
}

// == SCOPE == //

static u32 scope[OLED_WIDTH];

void clear_scope_pixel(u8 x) {
	scope[x] = 0;
}

void put_scope_pixel(u8 x, u8 y) {
	if (y >= 32)
		return;
	scope[x] |= (1 << y);
}

static void draw_scope(void) {
	u8* oled_buf = oled_buffer();
	for (u8 x = 0; x < OLED_WIDTH; ++x) {
		u32 m = scope[x];
		oled_buf[0] = m;
		oled_buf[128] = m >> 8;
		oled_buf[256] = m >> 16;
		oled_buf[384] = m >> 24;
		oled_buf++;
	}
}

// == MAIN == //

static char __attribute__((section(".endsection"))) version_tail[] = FIRMWARE_VERSION;

static void draw_startup_visuals(void) {
	static u8 frame = 3;
	if (frame == 255)
		return;
	if (frame < 36)
		gfx_dither_logo(frame);
	// draw version number
	gfx_text_color = 3;
	u8 y = maxi(frame - 255 + 32, 20);
	fdraw_str(47, y, F_12, latch_on() ? "v" FIRMWARE_VERSION : "LPE v" FIRMWARE_VERSION, version_tail);
	frame += 4;
	// draw plinky plus
	if (hw_version == HW_PLINKY_PLUS)
		fdraw_str(32, y - 12, F_12, "PLINKY+");
}

static void draw_preset_info(void) {
	// top-left, priority: cued preset, last pressed note, current preset
	u8 xtab = draw_cued_preset_id();
	if (!xtab)
		xtab = draw_high_note();
	if (!xtab)
		xtab = draw_preset_id();
	// always draw preset name, to the right of the top-left item
	draw_preset_name(xtab);
	// bottom left priority: cued pattern, current pattern
	xtab = draw_cued_pattern_id(arp_on());
	if (!xtab)
		draw_pattern_id(arp_on());
}

static void draw_visuals(void) {
	gfx_text_color = 1;

	// There is a number of situations where a visual of just one or two fdraw_str() commands temporarily overrides the
	// regular visuals. We handle these first, and exit the function early so the regular visuals don't get rendered.
	// The order in which these are programmed defines their priority

	if (draw_message())
		return;
	if (shift_states_oled_visuals())
		return;
	if (pad_actions_oled_visuals())
		return;

	// build up the regular visuals, per ui mode

	switch (ui_mode) {
	case UI_DEFAULT:
		if (using_sampler())
			draw_sample_playback(&cur_sample_info);
		else
			draw_scope();
		draw_voices();
		draw_flags();
		draw_lfos();
		draw_max_pres();
		if (draw_cur_param())
			// if the param is drawn, we skip drawing the preset info
			return;
		draw_preset_info();
		break;
	case UI_EDITING_A:
	case UI_EDITING_B:
		draw_cur_param();
		break;
	case UI_PTN_START:
		seq_ptn_start_visuals();
		break;
	case UI_PTN_END:
		seq_ptn_end_visuals();
		break;
	case UI_LOAD:
		u8 xtab = draw_preset_id();
		draw_preset_name(xtab);
		draw_pattern_id(false);
		draw_sample_id();
		break;
	case UI_SAMPLE_EDIT:
		sampler_oled_visuals();
		break;
	}

	// startup visuals are drawn on top of the regular visuals
	draw_startup_visuals();
}

void draw_oled_visuals(void) {
	oled_clear();
	draw_visuals();
	oled_flip();
}