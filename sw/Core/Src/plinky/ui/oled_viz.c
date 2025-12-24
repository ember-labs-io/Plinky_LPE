#include "oled_viz.h"
#include "gfx/gfx.h"
#include "hardware/memory.h"
#include "pad_actions.h"
#include "settings_menu.h"
#include "synth/lfos.h"
#include "synth/params.h"
#include "synth/sampler.h"
#include "synth/sequencer.h"
#include "synth/synth.h"
#include <stdarg.h>

// == TOOLS == //

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

#define MESSAGE_TIME 700 // ms

static char message[16] = {};
static char submessage[16] = {};
static u8 message_font;
static u32 message_time = 0;

void flash_message(Font fnt, const char* msg_fmt, const char* submsg, ...) {
	va_list args;
	va_start(args, submsg);
	vsnprintf(message, sizeof(message), msg_fmt, args);
	va_end(args);

	if (submsg)
		strncpy(submessage, submsg, sizeof(submessage) - 1);
	else
		submessage[0] = 0;
	submessage[sizeof(submessage) - 1] = 0;

	message_font = fnt;
	message_time = millis() + MESSAGE_TIME;
}

// returns whether this drew anything
static bool draw_message(void) {
	if (!message[0])
		return false;
	if (millis() > message_time) {
		message[0] = 0;
		return false;
	}
	if (submessage[0]) {
		draw_str_ctr(1, F_12, submessage);
		draw_str_ctr(13, message_font, message);
		return true;
	}
	draw_str_ctr((OLED_HEIGHT - font_height(message_font)) >> 1, message_font, message);
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
	fdraw_str(47, y, F_12, latch_active() ? "v" FIRMWARE_VERSION : "LPE v" FIRMWARE_VERSION, version_tail);
	frame += 4;
}

#ifdef FPS_WINDOW
static void draw_fps_window(void) {
	Font fps_font = F_12;
	char fps_buf[5];
	u8 fps = debug_time[TIME_LOG_ITEMS - 1] / 10;
	sprintf(fps_buf, "%d.%d", (fps / 10) % 100, fps % 10);
	u8 fps_width = str_width(fps_font, fps_buf);
	u8 fps_height = font_height(fps_font);
	fill_rectangle(0, 0, fps_width + 3, fps_height + 2);
	inverted_rectangle(1, 1, fps_width + 2, fps_height + 1);
	gfx_text_color = 1;
	draw_str(2, 2, fps_font, fps_buf);
}
#endif

static void draw_visuals(void) {
	gfx_text_color = 1;

	// There is a number of situations where a visual of just one or two fdraw_str() commands temporarily overrides the
	// regular visuals. We handle these first, and exit the function early so the regular visuals don't get rendered.
	// The order in which these are programmed defines their priority

	if (draw_message())
		return;
	if (oled_function_visuals())
		return;
	if (mod_clear_visuals())
		return;

	// build up the regular visuals, per ui mode

	switch (ui_mode) {
	case UI_PTN_START:
	case UI_PTN_END:
		if (ptn_edit_active()) {
			if (ui_mode == UI_PTN_START) {
				seq_ptn_start_visuals();
				return;
			}
			if (ui_mode == UI_PTN_END) {
				seq_ptn_end_visuals();
				return;
			}
			break;
		}
		// fall through
	case UI_DEFAULT:
		if (USING_SAMPLER)
			draw_sample_playback(&cur_sample_info);
		else
			draw_scope();
		draw_lfos();
		draw_max_pres();
		if (params_want_to_draw()) {
			draw_voices(false);
			draw_cur_param();
			return; // this fills the rest of the display
		}
		draw_preset_info();
		draw_voices(latch_active());
		draw_latch_flag();
		if (seq_state() == SEQ_STEP_RECORDING) {
			seq_draw_step_recording();
			return;
		}
		draw_arp_flag();
		draw_startup_visuals();
		break;
	case UI_EDITING_A:
	case UI_EDITING_B:
		draw_cur_param();
		break;
	case UI_LOAD:
		draw_ui_load_visuals();
		break;
	case UI_SAMPLE_EDIT:
		sampler_oled_visuals();
		break;
	case UI_SETTINGS_MENU:
		draw_settings_menu();
		break;
	}

#ifdef FPS_WINDOW
	draw_fps_window();
#endif
}

void draw_oled_visuals(void) {
	oled_clear();
	draw_visuals();
	oled_flip();
}