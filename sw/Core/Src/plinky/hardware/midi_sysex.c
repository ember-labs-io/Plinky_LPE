#include "midi_sysex.h"
#include "gfx/gfx.h"
#include "memory.h"
#include "synth/params.h"
#include "synth/synth.h"
#include "ui/oled_viz.h"

typedef enum {
	SUB_ID_BULK = 1,
	SUB_ID_SINGLE = 2,
	SUB_ID_OCTAVE_1BYTE = 8,
	SUB_ID_OCTAVE_2BYTE = 9,
} SysexSubId;

static u16 sysex_status = 0;
static u8 sysex_manuf_id = 0;
static u8 sysex_sub_id[2] = {};

void init_sysex(void) {
	sysex_status = 0;
}

static void sysex_progress(bool valid) {
	if (valid)
		sysex_status++;
	else
		sysex_status = UINT16_MAX;
}

static void process_bulk_tuning_byte(u8 byte) {
	// tuning program number (ignored)
	if (sysex_status == 4) {
		sysex_status++;
		return;
	}
	// 16 char tuning name
	if (sysex_status < 21) {
		global_data.midi_tuning_name[sysex_status - 6] = byte;
		sysex_status++;
		log_ram_edit(SEG_GLOBAL_DATA);
		return;
	}
	// note data, 128 x 3 bytes
	if (sysex_status < 405) {
		static u8 save_byte[2];
		u8 save_id = (sysex_status - 21) % 3;
		// first two bytes: save
		if (save_id < 2)
			save_byte[save_id] = byte;
		// third byte, set note tuning ("no change" message excluded)
		else if (save_byte[0] + save_byte[1] + byte != 381)
			set_note_tuning((sysex_status - 21) / 3,
			                NOTE_NR_TO_PITCH(save_byte[0]) + (((save_byte[1] << 7) | byte) >> 5));
		if (sysex_status == 404)
			flash_message(F_16_BOLD, "midi tuning", "received");
		sysex_status++;
		return;
	}
	// checksum is ignored
	sysex_status = UINT16_MAX;
}

static void process_single_tuning_byte(u8 byte) {
	static u8 num_changes = 0;
	// tuning program number (ignored)
	if (sysex_status == 4) {
		sysex_status++;
		return;
	}
	if (sysex_status == 5) {
		num_changes = byte;
		sysex_status++;
		return;
	}
	// note data, num_changes x 4 bytes
	if (sysex_status < 6 + (num_changes << 2)) {
		static u8 save_byte[3] = {};
		u8 save_id = (sysex_status - 6) % 4;
		// first three bytes: save
		if (save_id < 3)
			save_byte[save_id] = byte;
		// fourth byte: set note tuning ("no change" message excluded)
		else if (save_byte[1] + save_byte[2] + byte != 381)
			set_note_tuning(save_byte[0], NOTE_NR_TO_PITCH(save_byte[1]) + (((save_byte[2] << 7) | byte) >> 5));
		if (sysex_status == 5 + (num_changes << 2))
			flash_message(F_12, "midi tuning", "received");
		sysex_status++;
		return;
	}
	// checksum is ignored
	sysex_status = UINT16_MAX;
}

static void process_scale_octave_1byte_byte(u8 byte) {
	// ignore ff, gg, hh channel bytes (tuning always applied globally)
	if (sysex_status < 7) {
		sysex_status++;
		return;
	}
	// note pitch offset, 12 x 1 byte
	if (sysex_status < 19) {
		u8 note_class = sysex_status - 7;
		s16 pitch_offset = 512 * (byte - 64) / 100;
		for (u8 note = note_class; note < NUM_NOTES; note += 12)
			set_note_tuning(note, NOTE_NR_TO_PITCH(note) + pitch_offset);
		if (sysex_status == 18)
			flash_message(F_12, "midi tuning", "received");
		sysex_status++;
		return;
	}
	// checksum is ignored
	sysex_status = UINT16_MAX;
}

static void process_scale_octave_2byte_byte(u8 byte) {
	// ignore ff, gg, hh channel bytes (tuning always applied globally)
	if (sysex_status < 7) {
		sysex_status++;
		return;
	}
	// note pitch offset, 12 x 2 bytes
	if (sysex_status < 31) {
		static u8 save_byte;
		u8 save_id = (sysex_status - 7) % 2;
		if (save_id == 0)
			save_byte = byte;
		else {
			u8 note_class = (sysex_status - 7) / 2;
			s16 pitch_offset = ((s32)((save_byte << 7) | byte) - UINT14_HALF) >> 4;
			for (u8 note = note_class; note < NUM_NOTES; note += 12)
				set_note_tuning(note, NOTE_NR_TO_PITCH(note) + pitch_offset);
		}
		if (sysex_status == 30)
			flash_message(F_12, "midi tuning", "received");
		sysex_status++;
		return;
	}
	// checksum is ignored
	sysex_status = UINT16_MAX;
}

void process_sysex_byte(u8 byte) {
	switch (sysex_status) {
	// manufacturer
	case 0:
		sysex_manuf_id = byte;
		// we only recognize universal realtime and non-realtime headers
		sysex_progress(sysex_manuf_id == 0x7E || sysex_manuf_id == 0x7F);
		return;
	// device id (ignored)
	case 1:
		sysex_status++;
		return;
	// sub-id
	case 2:
		sysex_sub_id[0] = byte;
		// midi tuning
		sysex_progress(byte == 8);
		return;
	case 3:
		sysex_sub_id[1] = byte;
		sysex_progress(byte == SUB_ID_BULK || byte == SUB_ID_SINGLE || byte == SUB_ID_OCTAVE_1BYTE
		               || byte == SUB_ID_OCTAVE_2BYTE);
		return;
	}
	if (sysex_manuf_id == 0x7E && sysex_sub_id[1] == SUB_ID_BULK)
		process_bulk_tuning_byte(byte);
	else if (sysex_manuf_id == 0x7F && sysex_sub_id[1] == SUB_ID_SINGLE)
		process_single_tuning_byte(byte);
	// we accept both realtime and non-realtime headers for scale/octave mode and handle both as realtime
	else if (sysex_sub_id[1] == SUB_ID_OCTAVE_1BYTE)
		process_scale_octave_1byte_byte(byte);
	else if (sysex_sub_id[1] == SUB_ID_OCTAVE_2BYTE)
		process_scale_octave_2byte_byte(byte);
}

bool draw_midi_tuning_name(void) {
	if (global_data.midi_tuning_name[0] == '\0')
		return false;

	gfx_text_color = 2;
	draw_str_ctr(0, F_8, global_data.midi_tuning_name);
	return true;
}