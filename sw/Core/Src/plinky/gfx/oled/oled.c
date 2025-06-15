#include "oled.h"
#include "SSD130x.h"

// debug
#define DEBUG_DISPLAY_TIME 1500
static u32 debug_start_time = 0;
static bool debug_view_active = false;
static bool debug_buffer_active = false;

static u8 oled[OLED_BUFFER_SIZE];
static u8 oled_debug[OLED_BUFFER_SIZE];

u8* oled_buffer(void) {
	return (debug_buffer_active ? oled_debug : oled) + 1;
}

void oled_init(void) {
	// ssd130x init settings
	ssd130x_init();
	// first element of the buffer is always 0x40
	oled[0] = 0x40;
	memset(&oled[1], 0, OLED_BUFFER_SIZE - 1);
	oled_debug[0] = 0x40;
	memset(&oled_debug[1], 0, OLED_BUFFER_SIZE - 1);
}

void oled_clear(void) {
	memset(&oled[1], 0, OLED_BUFFER_SIZE - 1);
}

void oled_flip() {
	// debug timeout
	if (debug_view_active && (millis() - debug_start_time >= DEBUG_DISPLAY_TIME))
		debug_view_active = false;
	ssd130x_flip(debug_view_active ? oled_debug : oled);
}

void oled_flip_with_buffer(u8* buffer) {
	ssd130x_flip(buffer);
}

// DEBUG

void oled_open_debug_buffer(u8 row) {
	static const u16 clear_size = (OLED_BUFFER_SIZE - 1) / 2;
	debug_view_active = true;
	debug_buffer_active = true;
	debug_start_time = millis();
	// clear row
	row %= 2;
	memset(oled_debug + 1 + row * clear_size, 0, clear_size);
}

void oled_close_debug_buffer() {
	debug_buffer_active = false;
}