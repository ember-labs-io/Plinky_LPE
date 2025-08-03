#include "web_editor.h"
#include "hardware/flash.h"
#include "hardware/ram.h"
#include "tusb.h"

/* webusb wire format. 10 byte header, then data.
u32 magic = 0xf30fabca
u8 cmd // 0 = get, 1=set
u8 idx // 0
u8 idx2 // 0
u8 idx3 // 0
u16 datalen // in bytes, not including this header, 0 for get, 1552 for set */

typedef struct WebUSBHeader { // 10 byte header
	u8 magic[4];
	u8 cmd;
	u8 idx;
	union {
		struct {
			u16 offset_16;
			u16 len_16;
		};
		struct { // these are valid if magic3 is 0xcc
			u32 offset_32;
			u32 len_32;
		};
	};
} __attribute__((packed)) WebUSBHeader;

// state machine ticks thru these in order, more or less
typedef enum WuState {
	WU_MAGIC0,
	WU_MAGIC1,
	WU_MAGIC2,
	WU_MAGIC3,
	WU_RCV_HDR,
	WU_RCV_DATA,
	WU_SND_HDR,
	WU_SND_DATA,
} WuState;

extern bool web_serial_connected; // tinyusb/src/usbmidi.c

#define WEB_USB_TIMEOUT 500

const static u8 magic[4] = {0xf3, 0x0f, 0xab, 0xca};
const static u8 magic_32[4] = {0xf3, 0x0f, 0xab, 0xcb}; // 32 bit version

static WuState state = WU_MAGIC0;   // current state
static WebUSBHeader header;         // header of current command
static u8* data_buf = (u8*)&header; // buffer where we are reading/writing atm
static s32 bytes_remaining = 1;     // how much left to read/write before state transition
static u32 last_event_time;         // for timeout detection

static inline bool is_wu_hdr_32bit(void) {
	return header.magic[3] == magic_32[3];
}

static inline u32 wu_hdr_len(void) {
	return is_wu_hdr_32bit() ? header.len_32 : header.len_16;
}

static inline u32 wu_hdr_offset(void) {
	return is_wu_hdr_32bit() ? header.offset_32 : header.offset_16;
}

static void set_state(u8 new_state, u8* data, s32 len) {
	state = new_state;
	data_buf = data;
	bytes_remaining = len;
}

void web_editor_frame(void) {
	if (!web_serial_connected) {
		set_state(WU_MAGIC0, header.magic, 1);
		tud_task();
		return;
	}

	u32 start_time = millis();
	// run for max 1 ms
	while (millis() == start_time) {
		if (millis() > last_event_time + WEB_USB_TIMEOUT)
			set_state(WU_MAGIC0, header.magic, 1);
		tud_task();

		// handle remaining bytes
		u32 handle_bytes = 0;
		switch (state) {
		// sending
		case WU_SND_HDR:
		case WU_SND_DATA:
			if (bytes_remaining > 0) {
				// how much can we write?
				handle_bytes = mini(tud_vendor_write_available(), bytes_remaining);
				// none available or none to send => try again
				if (handle_bytes == 0)
					continue;
				// save how much was written
				handle_bytes = tud_vendor_write(data_buf, handle_bytes);
			}
			break;
		// receiving
		default:
			// save how much was sent
			handle_bytes = tud_vendor_read(data_buf, mini(bytes_remaining, CFG_TUD_VENDOR_RX_BUFSIZE));
			break;
		}
		// nothing read or sent => try again
		if (handle_bytes == 0)
			continue;

		// log event
		last_event_time = millis();
		bytes_remaining -= handle_bytes;
		data_buf += handle_bytes;

		if (bytes_remaining > 0)
			continue;

		// start of new state
		switch (state) {
		case WU_MAGIC0:
		case WU_MAGIC1:
		case WU_MAGIC2:
		case WU_MAGIC3: {
			u8 m = header.magic[state];
			if (m != magic[state] && m != magic_32[state]) {
				// this resyncs to the incoming message!
				header.magic[0] = m;
				bytes_remaining = 1;
				// if we got the first byte, we can tick into next state!
				state = (m == magic[0]) ? WU_MAGIC1 : WU_MAGIC0;
				data_buf = header.magic + state;
				continue;
			}
			state++;
			bytes_remaining = 1;
			// time to get rest of header
			if (state == WU_RCV_HDR)
				bytes_remaining = is_wu_hdr_32bit() ? 10 : 6;
			continue;
			break;
		}
		// handle a received header
		case WU_RCV_HDR:
			// only accept valid presets
			if (header.idx >= NUM_PRESETS)
				break;
			switch (header.cmd) {
			// request to send
			case 0:
				header.cmd = 1;
				if (wu_hdr_len() == 0) {
					u32 offset = wu_hdr_offset();
					header.len_16 = sizeof(Preset) - offset;
					header.offset_16 = offset;
					header.magic[3] = magic[3]; // 16 bit mode
				}
				set_state(WU_SND_HDR, (u8*)&header, is_wu_hdr_32bit() ? 14 : 10);
				break;
			// request to save
			case 1:
				load_preset(header.idx, false);
				set_state(WU_RCV_DATA, ((u8*)&cur_preset) + wu_hdr_offset(), wu_hdr_len());
				break;
			}
			break;
		// finished receiving data
		case WU_RCV_DATA:
			if (header.cmd == 1 && header.idx < NUM_PRESETS)
				log_ram_edit(SEG_PRESET);
			break;
		// we sent the header, now send the data
		case WU_SND_HDR:
			u8* data = header.idx == sys_params.curpreset ? (u8*)&cur_preset : (u8*)preset_flash_ptr(header.idx);
			set_state(WU_SND_DATA, data + wu_hdr_offset(), wu_hdr_len());
			break;
		// done sending data, return to start state
		case WU_SND_DATA:
			set_state(WU_MAGIC0, header.magic, 1);
			break;
		default:
			break;
		}
	}
}