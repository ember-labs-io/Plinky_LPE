#include "web_editor.h"
#include "hardware/memory.h"
#include "tusb.h"

typedef struct WebUSBHeader {
	u8 magic[4];
	u8 cmd; // 0 = request to send, 1 = request to save
	u8 idx; // id of the preset
	union {
		struct {
			u16 offset_16; // offset inside preset (points to a parameter)
			u16 len_16;    // amount of data to send/expect
		};
		struct {
			u32 offset_32;
			u32 len_32;
		};
	};
} __attribute__((packed)) WebUSBHeader;

typedef enum EditorState {
	MAGIC0,
	MAGIC1,
	MAGIC2,
	MAGIC3,
	RCV_HEADER,
	RCV_DATA,
	SEND_HEADER,
	SEND_DATA,
} EditorState;

#define HEADER_IS_32BIT (header.magic[3] == magic_32[3])
#define DATA_SIZE (HEADER_IS_32BIT ? header.len_32 : header.len_16)
#define DATA_OFFSET (HEADER_IS_32BIT ? header.offset_32 : header.offset_16)

const static u8 magic_16[4] = {0xf3, 0x0f, 0xab, 0xca};
const static u8 magic_32[4] = {0xf3, 0x0f, 0xab, 0xcb};

static EditorState state = MAGIC0;  // current state
static WebUSBHeader header;         // header of current command
static u8* data_buf = (u8*)&header; // buffer where we are reading/writing atm
static u32 remaining_bytes = 1;     // how much left to read/write before state transition
static u32 state_start = 0;         // usb timeout timer

u8 receiving_web_preset = false;

static void set_state(EditorState new_state) {
	state = new_state;
	// start transfer timeout
	if (state != MAGIC0)
		state_start = millis();
	// set up new state => set data_buf pointer and how many bytes to process
	switch (state) {
	case MAGIC0:
	case MAGIC1:
	case MAGIC2:
	case MAGIC3:
		data_buf = header.magic + state;
		remaining_bytes = 1;
		break;
	case RCV_HEADER:
		// data_buf is already at correct position
		remaining_bytes = HEADER_IS_32BIT ? 10 : 6;
		break;
	case RCV_DATA:
		load_preset(header.idx, false);
		receiving_web_preset = true;
		data_buf = (u8*)&cur_preset + DATA_OFFSET;
		remaining_bytes = DATA_SIZE;
		break;
	case SEND_HEADER:
		header.cmd = 1;
		if (DATA_SIZE == 0) {
			header.len_16 = sizeof(Preset) - DATA_OFFSET;
			header.offset_16 = DATA_OFFSET;
			header.magic[3] = magic_16[3];
		}
		data_buf = (u8*)&header;
		remaining_bytes = HEADER_IS_32BIT ? 14 : 10;
		break;
	case SEND_DATA:
		data_buf = preset_flash_ptr(header.idx) + DATA_OFFSET;
		remaining_bytes = DATA_SIZE;
		break;
	}
}

void web_editor_reset(void) {
	state_start = 0;
	receiving_web_preset = false;
	set_state(MAGIC0);
}

void web_editor_frame(void) {
	// transfer timeout
	if (state_start != 0 && millis() - state_start > 5000)
		web_editor_reset();
	// run 1ms of web editor processing
	u32 start_time = micros();
	while (micros() - start_time < 1000) {
		tud_task();

		// first block: handle bytes in the current state

		u32 handled_bytes = 0;
		switch (state) {
		// send bytes
		case SEND_HEADER:
		case SEND_DATA:
			handled_bytes = tud_vendor_write(data_buf, remaining_bytes);
			break;
		// read bytes
		case RCV_DATA:
			// wait until flash has loaded the correct preset
			if (preset_outdated())
				continue;
			// fall thru
		default:
			handled_bytes = tud_vendor_read(data_buf, mini(remaining_bytes, CFG_TUD_VENDOR_RX_BUFSIZE));
			break;
		}
		// nothing read or sent: buffer full or no data => try again
		if (handled_bytes == 0)
			continue;

		// progress
		remaining_bytes = remaining_bytes - handled_bytes;
		data_buf += handled_bytes;

		// more bytes to process
		if (remaining_bytes)
			continue;

		// second block: all bytes are handled, transfer to new state

		switch (state) {
		// magic bytes
		case MAGIC0:
		case MAGIC1:
		case MAGIC2:
		case MAGIC3: {
			u8 m = header.magic[state];
			// incorrect magic byte
			if (m != magic_16[state] && m != magic_32[state]) {
				// received magic 0, move to magic 1
				if (m == magic_16[0])
					set_state(MAGIC1);
				// other magic byte, reset to init state
				else
					web_editor_reset();
				break;
			}
			// correct magic byte, progress
			set_state(state + 1);
			break;
		}
		// finished receiving header
		case RCV_HEADER:
			// only accept presets
			if (header.idx >= NUM_PRESETS) {
				web_editor_reset();
				break;
			}
			switch (header.cmd) {
			// request to send
			case 0:
				set_state(SEND_HEADER);
				break;
			// request to save
			case 1:
				set_state(RCV_DATA);
				break;
			}
			break;
		// finished receiving data
		case RCV_DATA:
			log_ram_edit(SEG_PRESET);
			save_preset();
			web_editor_reset();
			break;
		// finished sending header
		case SEND_HEADER:
			set_state(SEND_DATA);
			break;
		// finished sending data
		case SEND_DATA:
			web_editor_reset();
			break;
		default:
			break;
		}
	}
}