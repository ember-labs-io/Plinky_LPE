#include "usb.h"
#include "tusb.h"
#include "web_editor.h"

extern bool web_serial_connected; // tinyusb/src/usbmidi.c
static bool web_serial_was_connected;
static atomic_flag tud_task_running = ATOMIC_FLAG_INIT;

void usb_request_tud_task(void) {
	if (atomic_flag_test_and_set(&tud_task_running))
		return;
	tud_task();
	atomic_flag_clear(&tud_task_running);
}

void init_usb(void) {
	tusb_init();
}

void usb_frame(void) {
	if (web_serial_connected != web_serial_was_connected) {
		web_editor_reset();
		web_serial_was_connected = web_serial_connected;
	}

	if (web_serial_connected)
		web_editor_frame();
}