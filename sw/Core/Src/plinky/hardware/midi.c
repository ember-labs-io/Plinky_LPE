// include tinyusb, undo their bool def
#include "tusb.h"
#undef bool

#include "midi.h"
// regular includes
#include "gfx/gfx.h"
#include "hardware/touchstrips.h"
#include "memory.h"
#include "midi_sysex.h"
#include "synth/audio.h"
#include "synth/lfos.h"
#include "synth/params.h"
#include "synth/synth.h"
#include "synth/time.h"
#include "ui/oled_viz.h"
#include "usb/usb.h"

// midi uart, lives in main.c
extern UART_HandleTypeDef huart3;

typedef enum MidiStringState {
	MS_UNPRESSED,
	MS_PRESSED,
	MS_SUSTAINED,
} MidiStringState;

typedef struct MidiString {
	MidiStringState state;

	// midi values
	u8 note_number;    // note on number
	u14 pitchbend;     // pitchbend value
	u8 start_velocity; // note on value
	u8 pressure;       // poly aftertouch or mpe pressure value
	u14 mod_wheel;     // mod wheel value

	// modified values
	bool sustain_pressed; // CC64 as a bool
	s32 pitchbend_pitch;  // pitchbend expressed as a 512/semi pitch offset
	u16 position;         // position on the string, tries to light up pad led at midi pitch
} MidiString;

typedef struct LastSentString {
	u8 note_number;
	u14 pitchbend;
	u8 pressure;
	u8 pressure_cc;
	u8 position_cc;
} LastSentString;

typedef struct MpeZone {
	u8 num_chans;
	u8 num_strings;
	u8 pressure;
	u14 pitchbend;
	s32 pitchbend_pitch;
} MpeZone;

#define MIDI_BUFFER_SIZE 16
#define THRU_BUFFER_SIZE 16
#define PARAM_BUFFER_SIZE 8
#define MIDI_SEND_BUFFER_FREE (MIDI_BUFFER_SIZE - (midi_send_head - midi_send_tail))
#define SYSEX_TIMEOUT_MILLIS 5000

static const MidiString init_midi_string = {MS_UNPRESSED, 255, {UINT14_HALF}};
static const LastSentString init_last_sent_string = {255, {UINT14_HALF}};

// buffers - double sized to allow linearizing cross-boundary reads/writes
static u8 midi_receive_buffer[2 * MIDI_BUFFER_SIZE];
static u8 midi_send_buffer[2 * MIDI_BUFFER_SIZE];
static u8 midi_send_head;
static u8 midi_send_tail;

// settings
static u16 max_channel_bend_pitch_in;
static u16 max_string_bend_pitch_in;
static u16 max_string_bend_pitch_out;

// midi state
static MidiString midi_string[NUM_STRINGS];
static u8 channel_pressure;
static u14 channel_pitchbend;
static s32 channel_pitchbend_pitch;
static bool mod_wheel_14bit = false;
static MpeZone mpe_zone[2] = {};
static u8 non_mpe_start_string = 0;
static u8 high_mpe_start_string = NUM_STRINGS;
static bool receiving_sysex = false;
static u32 sysex_start_time = 0;
static bool pushing_preset = false;

// NRPNs
static u14 nrpn_id[NUM_STRINGS] = {{UINT14_MAX}, {UINT14_MAX}, {UINT14_MAX}, {UINT14_MAX},
                                   {UINT14_MAX}, {UINT14_MAX}, {UINT14_MAX}, {UINT14_MAX}};
static u14 rpn_id[NUM_STRINGS] = {{UINT14_MAX}, {UINT14_MAX}, {UINT14_MAX}, {UINT14_MAX},
                                  {UINT14_MAX}, {UINT14_MAX}, {UINT14_MAX}, {UINT14_MAX}};
static u14 n_rpn_value[NUM_STRINGS] = {};             // shared value for nrpn and rpn
static bool received_n_rpn_data[NUM_STRINGS][2] = {}; // is the full 14 bit value received?
static u8 rpn_last_received = 0;                      // was rpn or nrpn number last received?

// cue midi out
static u8 midi_out_channel;
static u8 clocks_to_send = 0;
static MidiMessageType send_transport = 0;
static LastSentString last_sent_string[NUM_STRINGS];
static u8 last_channel_pressure = 0;
static u8 last_sent_lfo[NUM_LFOS];

// soft thru
static u8 thru_buffer[THRU_BUFFER_SIZE][3];
static u8 thru_buffer_head = 0;
static u8 thru_buffer_tail = 0;
static u8 thru_buffer_count = 0;
static u32 send_param_val[3] = {};
static Param sending_param_id = 0;
static u8 sending_param_progress = 255;

// == UTILS == //

static u8 num_midi_bytes(u8 status) {
	if (status == MIDI_SYSTEM_EXCLUSIVE || status >= MIDI_TUNE_REQUEST)
		return 1;
	if ((status & MIDI_TYPE_MASK) == MIDI_PROGRAM_CHANGE || (status & MIDI_TYPE_MASK) == MIDI_CHANNEL_PRESSURE
	    || status == MIDI_TIME_CODE || status == MIDI_SONG_SELECT)
		return 2;
	return 3;
}

static u8 midi_out_pressure(u16 string_pres, u8 start_velocity) {
	u8 velo_mult = sys_params.midi_out_vel_balance;
	u8 pres_mult = 128 - velo_mult;
	return velo_mult == 128 ? 0
	                        : clampi(
	                              // scale string pressure to 14 bit
	                              ((string_pres << 3)
	                               // subtract velocity part
	                               - velo_mult * start_velocity
	                               // revert offset
	                               - 128
	                               // integer rounding
	                               + (pres_mult >> 1))
	                                  / pres_mult,
	                              0, 127);
}

static void update_channel_pitchbend(void) {
	channel_pitchbend_pitch = (channel_pitchbend.value - UINT14_HALF) * max_channel_bend_pitch_in >> 13;
}

static void update_zone_pitchbend(u8 zone) {
	zone %= 2;
	mpe_zone[zone].pitchbend_pitch = (mpe_zone[zone].pitchbend.value - UINT14_HALF) * max_channel_bend_pitch_in >> 13;
}

static void update_string_pitchbend(u8 string_id) {
	midi_string[string_id].pitchbend_pitch =
	    (midi_string[string_id].pitchbend.value - UINT14_HALF) * max_string_bend_pitch_in >> 13;
}

static void force_release_string(u8 string_id) {
	midi_string[string_id].sustain_pressed = false;
	midi_string[string_id].state = MS_UNPRESSED;
}

static void apply_sustain(bool new_sustain, u8 string_id) {
	MidiString* m_string = &midi_string[string_id];
	if (new_sustain != m_string->sustain_pressed) {
		m_string->sustain_pressed = new_sustain;
		// release midi note held by the sustain
		if (!new_sustain && m_string->state == MS_SUSTAINED)
			m_string->state = MS_UNPRESSED;
	}
}

static void reset_controls(u8 string_id) {
	force_release_string(string_id);
	MidiString* m_string = &midi_string[string_id];
	m_string->mod_wheel.value = 0;
	m_string->pitchbend.value = UINT14_HALF;
	update_string_pitchbend(string_id);
	m_string->pressure = 0;
	nrpn_id[string_id].value = UINT14_MAX;
	rpn_id[string_id].value = UINT14_MAX;
	received_n_rpn_data[string_id][0] = received_n_rpn_data[string_id][1] = false;
}

static void register_press(u8 string_id, u8 note, u8 velocity, u16 position) {
	MidiString* m_string = &midi_string[string_id];
	m_string->state = MS_PRESSED;
	m_string->note_number = note;
	m_string->start_velocity = velocity;
	m_string->position = position;
}

// send midi msg to uart and usb, returns false if buffer too full
static bool send_midi_msg(u8 status, u8 data1, u8 data2) {
	u8 num_bytes = num_midi_bytes(status);
	// exit if serial buffer full
	if (num_bytes > MIDI_SEND_BUFFER_FREE)
		return false;
	// header packet
	midi_code_index_number_t cin = status < MIDI_SYSTEM_EXCLUSIVE ? status >> 4 : MIDI_CIN_1BYTE_DATA;
	// add midi channel
	if (status < MIDI_SYSTEM_EXCLUSIVE && (status & MIDI_CHANNEL_MASK) == 0)
		status += midi_out_channel;
	// usb midi packet
	u8 buf[4] = {cin, status, data1, data2};

#ifndef DEBUG_LOG

	// serial midi out
	if (!sys_params.midi_trs_out_off) {
		const u8* src = buf + 1;

		// running status
		static u8 running_status = 0;
		// match - skip first byte
		if (status == running_status) {
			num_bytes--;
			src++;
		}
		// channel voice message - save running status
		else if (status < MIDI_SYSTEM_EXCLUSIVE)
			running_status = status;
		// system common message - cancel running status
		else if (status < MIDI_TIMING_CLOCK)
			running_status = 0;

		// send to buffer
		while (num_bytes--)
			midi_send_buffer[(midi_send_head++) & 15] = *src++;
	}

#endif

	// send to usb
	// (we assume this can only ever return false when trs out is disabled, as serial is so much slower than usb midi)
	return tud_midi_packet_write(buf);
}

static bool send_double_midi_msg(u8 status1, u8 data1_1, u8 data1_2, u8 status2, u8 data2_1, u8 data2_2) {
	// exit if not enough space for both messages
	if (num_midi_bytes(status1) + num_midi_bytes(status2) > MIDI_SEND_BUFFER_FREE)
		return false;
	// send both messages
	send_midi_msg(status1, data1_1, data1_2);
	send_midi_msg(status2, data2_1, data2_2);
	return true;
}

static bool send_nrpn(u8 nrpn_msb, u8 nrpn_lsb, u14 value) {
	// don't send identical parameter numbers
	static u14 last_nrpn = {UINT14_MAX};
	bool send_nrpn_msb = nrpn_msb != last_nrpn.msb;
	bool send_nrpn_lsb = nrpn_lsb != last_nrpn.lsb;

	// we need three bytes for each cc message
	if (MIDI_SEND_BUFFER_FREE < (send_nrpn_msb + send_nrpn_lsb + 2) * 3)
		return false;

	// send number
	if (send_nrpn_msb) {
		send_midi_msg(MIDI_CONTROL_CHANGE, CC_NRPN_MSB, nrpn_msb);
		last_nrpn.msb = nrpn_msb;
	}
	if (send_nrpn_lsb) {
		send_midi_msg(MIDI_CONTROL_CHANGE, CC_NRPN_LSB, nrpn_lsb);
		last_nrpn.lsb = nrpn_lsb;
	}

	// send data
	send_midi_msg(MIDI_CONTROL_CHANGE, CC_DATA_MSB, value.msb);
	send_midi_msg(MIDI_CONTROL_CHANGE, CC_DATA_LSB, value.lsb);
	return true;
}

static void forward_midi_msg(u8 status, u8 data1, u8 data2) {
	if (thru_buffer_count >= THRU_BUFFER_SIZE) {
		flash_message(F_16_BOLD, "BUFFER FULL", "MIDI SOFT THRU");
		return;
	}
	thru_buffer[thru_buffer_head][0] = status;
	thru_buffer[thru_buffer_head][1] = data1;
	thru_buffer[thru_buffer_head][2] = data2;
	thru_buffer_head = (thru_buffer_head + 1) % THRU_BUFFER_SIZE;
	thru_buffer_count++;
}

void midi_precalc_bends(void) {
	max_channel_bend_pitch_in = SEMIS_TO_PITCH(bend_ranges[sys_params.midi_channel_bend_range_in]);
	update_channel_pitchbend();
	update_zone_pitchbend(0);
	update_zone_pitchbend(1);
	max_string_bend_pitch_in = SEMIS_TO_PITCH(bend_ranges[sys_params.midi_string_bend_range_in]);
	for (u8 string_id = 0; string_id < NUM_STRINGS; string_id++)
		update_string_pitchbend(string_id);
	max_string_bend_pitch_out = SEMIS_TO_PITCH(bend_ranges[sys_params.midi_string_bend_range_out]);
}

void midi_clear_all(void) {
	memset(&midi_receive_buffer, 0, 2 * MIDI_BUFFER_SIZE);
	memset(&midi_send_buffer, 0, 2 * MIDI_BUFFER_SIZE);
	midi_send_head = 0;
	midi_send_tail = 0;
	channel_pressure = 0;
	channel_pitchbend.value = UINT14_HALF;
	channel_pitchbend_pitch = 0;
	mpe_zone[0].pressure = 0;
	mpe_zone[0].pitchbend.value = UINT14_HALF;
	mpe_zone[0].pitchbend_pitch = 0;
	mpe_zone[1].pressure = 0;
	mpe_zone[1].pitchbend.value = UINT14_HALF;
	mpe_zone[1].pitchbend_pitch = 0;
	for (u8 string_id = 0; string_id < NUM_STRINGS; string_id++) {
		memcpy(&midi_string[string_id], &init_midi_string, sizeof(MidiString));
		memcpy(&last_sent_string[string_id], &init_last_sent_string, sizeof(LastSentString));
	}
	memset(&thru_buffer, 0, sizeof(thru_buffer));
	thru_buffer_head = 0;
	thru_buffer_tail = 0;
	thru_buffer_count = 0;
	memset(send_param_val, 0, sizeof(send_param_val));
}

void midi_panic(void) {
	midi_clear_all();
	forward_midi_msg(MIDI_SYSTEM_RESET, 0, 0);
}

// == MAIN == //

void init_midi(void) {
	midi_clear_all();
	midi_precalc_bends();
	set_mpe_channels(sys_params.mpe_zone, sys_params.mpe_chans + 1);
	HAL_UART_Receive_DMA(&huart3, midi_receive_buffer, MIDI_BUFFER_SIZE);
	receiving_sysex = false;
}

// cue all midi data for one string, returns whether there is still space in the midi buffer
static bool cue_midi_string_out(void) {
	typedef enum MidiOutState {
		MSG_CHAN_PRESSURE,
		MSG_POLY_PRESSURE,
		MSG_PITCHBEND,
		MSG_NOTE,
		MSG_Z_CONTROL,
		MSG_Y_CONTROL,
		MSG_LFOS,
	} MidiOutState;

	static u8 string_id = 0;
	static MidiOutState msg_state = 0;

	bool using_mpe = sys_params.mpe_out;
	if (using_mpe) {
		// early exit for non-mpe strings
		if (string_id >= non_mpe_start_string && string_id < high_mpe_start_string)
			return true;
		midi_out_channel = string_id < non_mpe_start_string
		                       ? string_id + 1
		                       : 15 - mpe_zone[1].num_chans + (string_id - high_mpe_start_string);
	}
	else
		midi_out_channel = sys_params.midi_out_chan;

	const Touch* touch = get_touch(string_id, 0);
	const SynthString* s_string = get_synth_string(string_id);
	u16 string_pres = clampi(s_string->touch.pres, 0, TOUCH_FULL_PRES);
	u8 string_vel = maxi(s_string->start_velocity, 1);
	LastSentString* m_last = &last_sent_string[string_id];

	switch (msg_state) {
	case MSG_CHAN_PRESSURE: {
		static u16 max_string_pressure = 0;
		static u8 max_velocity = 0;
		static u8 chan_pres = 0;
		static u8 min_diff = 5;
		// track max pressure & velocity
		if (string_pres > max_string_pressure) {
			max_string_pressure = string_pres;
			max_velocity = string_vel;
		}
		// all strings have been tracked
		if (string_id == 7) {
			// only update when mono pressure is selected
			if (!using_mpe && sys_params.midi_out_pres_type == MP_CHANNEL_PRESSURE) {
				chan_pres = midi_out_pressure(max_string_pressure, max_velocity);
				// require a difference of 5, unless this is an extreme value
				min_diff = (chan_pres == 0 || chan_pres == 127) ? 1 : 5;
			}
			// send if changed
			if (abs(chan_pres - last_channel_pressure) >= min_diff) {
				if (!send_midi_msg(MIDI_CHANNEL_PRESSURE, chan_pres, 0))
					return false;
				last_channel_pressure = chan_pres;
			}
			// restart tracking
			max_string_pressure = 0;
			max_velocity = 0;
		}
		msg_state++;
		// fall thru
	}
	// this handles MIDI_POLY_KEY_PRESSURE for default midi and MIDI_CHANNEL_PRESSURE for mpe
	case MSG_POLY_PRESSURE: {
		static u8 poly_pres = 0;
		static u8 min_diff = 5;
		// only update for mpe and poly aftertouch
		if (using_mpe || sys_params.midi_out_pres_type == MP_POLY_AFTERTOUCH) {
			poly_pres = midi_out_pressure(string_pres, string_vel);
			// require a difference of 5, unless this is an extreme value
			min_diff = (poly_pres == 0 || poly_pres == 127) ? 1 : 5;
		}
		// send if changed
		if (abs(poly_pres - m_last->pressure) >= min_diff) {
			if (using_mpe ? !send_midi_msg(MIDI_CHANNEL_PRESSURE, poly_pres, 0)
			              : !send_midi_msg(MIDI_POLY_KEY_PRESSURE, m_last->note_number, poly_pres))
				return false;
			m_last->pressure = poly_pres;
		}
		msg_state++;
		// fall thru
	}
	case MSG_PITCHBEND: {
		if (using_mpe) {
			u14 s_pitchbend = {
			    clampi(((s_string->pitchbend_pitch << 13) / max_string_bend_pitch_out) + UINT14_HALF, 0, UINT14_MAX)};
			// require a difference of 5, unless this is an extreme value
			u8 min_diff = (s_pitchbend.value == -UINT14_HALF || s_pitchbend.value == 8191) ? 1 : 5;
			// send if changed
			if (abs(s_pitchbend.value - m_last->pitchbend.value) >= min_diff) {
				if (!send_midi_msg(MIDI_PITCH_BEND, s_pitchbend.lsb, s_pitchbend.msb))
					return false;
				m_last->pitchbend = s_pitchbend;
			}
		}
		msg_state++;
		// fall thru
	}
	case MSG_NOTE: {
		u8 last_note = m_last->note_number;
		s8 string_note = s_string->note_number;
		// string is touching a note we can send over midi
		if (s_string->touched && string_note >= 0) {
			// we last sent a note off => send a note on
			if (last_note == 255) {
				if (!send_midi_msg(MIDI_NOTE_ON, string_note, string_vel))
					return false;
				m_last->note_number = string_note;
			}
			// the string has an envelope retrigger or we last sent a different note => send both a note off and note on
			else if (s_string->env_trigger || last_note != string_note) {
				if (!send_double_midi_msg(MIDI_NOTE_OFF, last_note, 0, MIDI_NOTE_ON, string_note, string_vel))
					return false;
				m_last->note_number = string_note;
			}
		}
		// string is not touched but we last sent a valid note => send note off
		else if (last_note != 255) {
			if (!send_midi_msg(MIDI_NOTE_OFF, last_note, 0))
				return false;
			m_last->note_number = 255;
		}
		msg_state++;
		// fall thru
	}
	case MSG_Z_CONTROL: // cc 40-47
		if (sys_params.midi_out_yz_control) {
			// reports directly from touchstrips
			u8 strip_pressure = clampi(touch->pres, 0, TOUCH_FULL_PRES) >> 4;
			// always send extreme values
			u8 min_diff = (strip_pressure == 0 || strip_pressure == 127) ? 1 : 2;
			if (abs(strip_pressure - m_last->pressure_cc) >= min_diff) {
				if (!send_midi_msg(MIDI_CONTROL_CHANGE, 40 + string_id, strip_pressure))
					return false;
				m_last->pressure_cc = strip_pressure;
			}
		}
		msg_state++;
		// fall thru
	case MSG_Y_CONTROL: // cc 32-39
		if (sys_params.midi_out_yz_control) {
			// reports directly from touchstrips
			u8 strip_position = 127 - (mini(touch->pos, TOUCH_MAX_POS) >> 4);
			// always send extreme values
			u8 min_diff = (strip_position == 0 || strip_position == 127) ? 1 : 2;
			if (abs(strip_position - m_last->position_cc) >= min_diff) {
				if (!send_midi_msg(MIDI_CONTROL_CHANGE, 32 + string_id, strip_position))
					return false;
				m_last->position_cc = strip_position;
			}
		}
		msg_state++;
		// fall thru
	case MSG_LFOS: {
		static u8 lfo_id = 0;
		// only send lfos after the last string
		if (string_id == 7 && sys_params.midi_send_lfo_cc) {
			// handle all four lfos
			do {
				u8 lfo_val = clampi((lfo_cur[lfo_id] + 65536) >> 10, 0, 127);
				if (abs(lfo_val - last_sent_lfo[lfo_id])) {
					if (!send_midi_msg(MIDI_CONTROL_CHANGE, 48 + lfo_id, lfo_val))
						return false;
					last_sent_lfo[lfo_id] = lfo_val;
				}
				lfo_id = (lfo_id + 1) % NUM_LFOS;
			}
			// we break when lfo_id rolls over
			while (lfo_id != 0);
		}

		// done, set up for next string
		string_id = (string_id + 1) & 7;
		msg_state = 0;
		break;
	}
	}
	return true;
}

static void cue_midi_out(void) {
// exit if uart is used for debug logging
#ifdef DEBUG_LOG
	return;
#endif

	// exit if the uart is not ready
	if (huart3.TxXferCount || pushing_preset)
		return;

	// send thru
	while (thru_buffer_count) {
		if (!send_midi_msg(thru_buffer[thru_buffer_tail][0], thru_buffer[thru_buffer_tail][1],
		                   thru_buffer[thru_buffer_tail][2]))
			return;
		thru_buffer_tail = (thru_buffer_tail + 1) % THRU_BUFFER_SIZE;
		thru_buffer_count--;
	}

	// send transport
	if (send_transport != MIDI_NONE) {
		if (!send_midi_msg(send_transport, 0, 0))
			return;
		send_transport = MIDI_NONE;
	}

	// send clock
	while (clocks_to_send) {
		if (!send_midi_msg(MIDI_TIMING_CLOCK, 0, 0))
			return;
		clocks_to_send--;
	}

	// send channel pitchbends
	static u14 last_channel_pitchbend = {};
	static u14 last_zone_pitchbend[2] = {};
	if (last_channel_pitchbend.value != channel_pitchbend.value) {
		midi_out_channel = sys_params.midi_out_chan;
		if (!send_midi_msg(MIDI_PITCH_BEND, channel_pitchbend.lsb, channel_pitchbend.msb))
			return;
		last_channel_pitchbend.value = channel_pitchbend.value;
	}
	if (last_zone_pitchbend[0].value != mpe_zone[0].pitchbend.value) {
		midi_out_channel = 0;
		if (!send_midi_msg(MIDI_PITCH_BEND, mpe_zone[0].pitchbend.lsb, mpe_zone[0].pitchbend.msb))
			return;
		last_zone_pitchbend[0].value = mpe_zone[0].pitchbend.value;
	}
	if (last_zone_pitchbend[1].value != mpe_zone[1].pitchbend.value) {
		midi_out_channel = 15;
		if (!send_midi_msg(MIDI_PITCH_BEND, mpe_zone[1].pitchbend.lsb, mpe_zone[1].pitchbend.msb))
			return;
		last_zone_pitchbend[1].value = mpe_zone[1].pitchbend.value;
	}

	// send values for one param
	if (sys_params.midi_send_param_ccs) {
		Param send_param = NUM_PARAMS;

		// we were still sending a param
		if (sending_param_progress != 255)
			send_param = sending_param_id;
		// find new param to send
		else {
			// start searching beyond the last sent param
			Param start_param = sending_param_id + 1;
			u8 bank = start_param >> 5;
			// mask bits of lower-numbered params
			u32 bank_bits = send_param_val[bank] & ~((1 << (start_param & 31)) - 1);
			if (bank_bits)
				send_param = (bank << 5) + __builtin_ctz(bank_bits);
			// look for set bits in the other two banks
			else if (send_param_val[(bank + 1) % 3]) {
				bank = (bank + 1) % 3;
				send_param = (bank << 5) + __builtin_ctz(send_param_val[bank]);
			}
			else if (send_param_val[(bank + 2) % 3]) {
				bank = (bank + 2) % 3;
				send_param = (bank << 5) + __builtin_ctz(send_param_val[bank]);
			}
			// found a param, kick off sending
			if (send_param != NUM_PARAMS)
				sending_param_progress = 0;
		}

		// we have a param to send
		if (send_param != NUM_PARAMS) {
			sending_param_id = send_param;
			for (u8 zone = 0; zone < (sys_params.mpe_out ? 2 : 1); zone++) {
				// set midi channel
				if (sys_params.mpe_out) {
					// zone without channels is invalid, early exit
					if (mpe_zone[zone].num_chans == 0)
						continue;
					midi_out_channel = zone == 0 ? 0 : 15;
				}
				else
					midi_out_channel = sys_params.midi_out_chan;

				// send param value as cc
				if (sys_params.midi_send_param_ccs == 1) {
					if (!send_midi_msg(MIDI_CONTROL_CHANGE, midi_cc_table_rvs[send_param], param_cc_value(send_param)))
						return;
				}
				// send nrpns
				else {
					// send param and modulation values
					for (ModSource mod_src = sending_param_progress; mod_src < NUM_MOD_SOURCES; mod_src++) {
						u14 nrpn_value;
						get_param_nrpn_value(send_param, mod_src, &nrpn_value);
						if (!send_nrpn(mod_src, send_param, nrpn_value))
							return;
						sending_param_progress++;
					}
					// send poly param values
					if (PARAM_IS_POLY(send_param)) {
						for (u8 i = sending_param_progress; i < 16; i++) {
							u8 string_id = i - 8;
							if (!send_nrpn(string_id + 8, send_param, param_nrpn_poly_value(send_param, string_id)))
								return;
							sending_param_progress++;
						}
					}
				}
			}
			// done
			send_param_val[send_param >> 5] &= ~(1 << (send_param & 31));
			sending_param_progress = 255;
		}
	}

	// send string data, breaks if:
	// - midi out buffer full
	// - any midi was sent for a string (gives space to send realtime data next tick)
	// - all strings have been checked
	bool buffer_free = true;
	u8 initial_send_head = midi_send_head;
	u8 strings_checked = 0;
	do {
		buffer_free = cue_midi_string_out();
		strings_checked++;
	} while (buffer_free && midi_send_head == initial_send_head && strings_checked < NUM_STRINGS);
}

static void try_apply_n_rpn(bool is_rpn, u8 n_rpn_string, bool mpe_member) {
	typedef enum NRPN_Action {
		NA_NONE,
		NA_SET_PARAM,
		NA_SET_MOD,
	} NRPN_Action;

	// value not fully received
	if (!received_n_rpn_data[n_rpn_string][0] || !received_n_rpn_data[n_rpn_string][1])
		return;

	// rpn
	if (is_rpn) {
		// mpe configuration message
		if (rpn_id[n_rpn_string].value == 6) {
			u8 num_chans = n_rpn_value->msb & 15;
			// lower zone
			if (n_rpn_string == 0) {
				set_mpe_channels(0, num_chans);
				// clear strings in mpe zone
				for (u8 string_id = 0; string_id < mpe_zone[0].num_strings; string_id++)
					reset_controls(string_id);
			}
			// upper zone
			else if (n_rpn_string == 15) {
				set_mpe_channels(1, num_chans);
				// clear strings in mpe zone
				for (u8 string_id = high_mpe_start_string; string_id < 8; string_id++)
					reset_controls(string_id);
			}
		}
		return;
	}

	// nrpn: work out the action to take
	NRPN_Action nrpn_action = NA_NONE;
	u8 id_msb = nrpn_id[n_rpn_string].msb;
	u8 string_id;
	bool poly;
	// on a member channel
	if (mpe_member) {
		// poly param set through member channel
		if (id_msb == 0) {
			nrpn_action = NA_SET_PARAM;
			poly = true;
			string_id = n_rpn_string;
		}
		// msb invalid
		else
			return;
	}
	// on the global channel
	else {
		// 0 => global param set through global channel
		if (id_msb == 0) {
			nrpn_action = NA_SET_PARAM;
			poly = false;
			string_id = 0;
		}
		// 1-7 => set modulation
		else if (id_msb < 8)
			nrpn_action = NA_SET_MOD;
		// 8-15 => poly param set through global channel
		else if (id_msb < 16) {
			nrpn_action = NA_SET_PARAM;
			poly = true;
			string_id = id_msb - 8;
		}
		// msb invalid
		else
			return;
	}

	// take action
	u8 param_id = nrpn_id[n_rpn_string].lsb;

	if (param_id >= NUM_PARAMS)
		return;

	switch (nrpn_action) {
	case NA_NONE:
		break;
	case NA_SET_PARAM:
		set_param_from_nrpn(param_id, n_rpn_value[n_rpn_string], poly, string_id);
		break;
	case NA_SET_MOD:
		set_mod_from_nrpn(param_id, n_rpn_value[n_rpn_string], id_msb);
		break;
	}
}

static bool try_handle_n_rpn(u8 cc_number, u8 cc_value, bool mpe_member, u8 string_id) {
	u14* n_id = &nrpn_id[string_id];
	u14* r_id = &rpn_id[string_id];
	u14* value = &n_rpn_value[string_id];
	bool* received = received_n_rpn_data[string_id];
	u8 mask = 1 << string_id;
	bool rpn_last = rpn_last_received & mask;

	switch (cc_number) {
	case CC_DATA_MSB:
		value->msb = cc_value;
		received[0] = true;
		try_apply_n_rpn(rpn_last, string_id, mpe_member);
		return true;
	case CC_DATA_LSB:
		value->lsb = cc_value;
		received[1] = true;
		try_apply_n_rpn(rpn_last, string_id, mpe_member);
		return true;
	case CC_DATA_INC:
		// no valid value
		if (!received[0] || !received[1])
			return true;
		// maxed out
		if (value->value == UINT14_MAX)
			return true;
		// increase
		value->value++;
		try_apply_n_rpn(rpn_last, string_id, mpe_member);
		return true;
	case CC_DATA_DEC:
		// no valid value
		if (!received[0] || !received[1])
			return true;
		// minned out
		if (value->value == 0)
			return true;
		// decrease
		value->value--;
		try_apply_n_rpn(rpn_last, string_id, mpe_member);
		return true;
	case CC_NRPN_LSB:
		n_id->lsb = cc_value;
		received[0] = received[1] = false;
		rpn_last_received &= ~mask;
		return true;
	case CC_NRPN_MSB:
		n_id->msb = cc_value;
		received[0] = received[1] = false;
		rpn_last_received &= ~mask;
		return true;
	case CC_RPN_LSB:
		r_id->lsb = cc_value;
		received[0] = received[1] = false;
		rpn_last_received |= mask;
		return true;
	case CC_RPN_MSB:
		r_id->msb = cc_value;
		received[0] = received[1] = false;
		rpn_last_received |= mask;
		return true;
	default:
		return false;
	}
}

static void start_receiving_sysex(void) {
	init_sysex();
	receiving_sysex = true;
	sysex_start_time = millis();
}

// apply midi messages to plinky
static void process_midi_msg(u8 status, u8 data1, u8 data2) {
	MidiMessageType type = status & MIDI_TYPE_MASK;

	//  == not channel based == //

	if (type == MIDI_SYSTEM_COMMON_MSG) {
		// not used => forward
		if (status <= MIDI_TUNE_REQUEST) {
			if (sys_params.midi_soft_thru)
				forward_midi_msg(status, data1, data2);
		}
		// clock
		else if (status == MIDI_TIMING_CLOCK) {
			if (sys_params.midi_rcv_clock)
				clock_rcv_midi(status);
		}
		// transport
		else if (status <= MIDI_STOP) {
			if (sys_params.midi_rcv_transport)
				clock_rcv_midi(status);
		}
		// midi reset / panic
		else if (status == MIDI_SYSTEM_RESET)
			midi_clear_all();
		// active sense not used => forward
		else if (sys_params.midi_soft_thru)
			forward_midi_msg(status, data1, data2);
		return;
	}

	u8 channel = status & MIDI_CHANNEL_MASK;

	// mpe
	bool is_manager_msg = false;
	bool is_member_msg = false;
	bool mpe_zone_upper;
	bool using_mpe = false;
	if (sys_params.mpe_in) {
		if ((channel == 0 && mpe_zone[0].num_chans > 0) || (channel == 15 && mpe_zone[1].num_chans > 0)) {
			is_manager_msg = true;
			mpe_zone_upper = channel == 15;
		}
		else if (channel > 0 && channel <= mpe_zone[0].num_chans) {
			is_member_msg = true;
			mpe_zone_upper = false;
		}
		else if (channel >= 15 - mpe_zone[1].num_chans && channel < 15) {
			is_member_msg = true;
			mpe_zone_upper = true;
		}
		using_mpe = is_manager_msg || is_member_msg;
	}

	// == unused channels: forward & exit == //

	if ((sys_params.mpe_in && !using_mpe) || (!sys_params.mpe_in && channel != sys_params.midi_in_chan)) {
		// we forward voice messages not on either our in or out channels
		if (sys_params.midi_soft_thru
		    // no mpe, not on regular in channel
		    && ((!sys_params.mpe_out && channel != sys_params.midi_out_chan)
		        // mpe, not in either zone
		        || (sys_params.mpe_out && (mpe_zone[0].num_chans == 0 || channel > mpe_zone[0].num_chans)
		            && (mpe_zone[1].num_chans == 0 || channel < 15 - mpe_zone[1].num_chans))))
			forward_midi_msg(status, data1, data2);
		return;
	}

	// turn silent note ons into note offs
	if (type == MIDI_NOTE_ON && data2 == 0)
		type = MIDI_NOTE_OFF;

	// == global messages == //

	if (!using_mpe || is_manager_msg) {
		switch (type) {
		case MIDI_PROGRAM_CHANGE:
			if (data1 < NUM_PRESETS)
				cue_mem_item(data1);
			return;
		case MIDI_CONTROL_CHANGE:
			switch (data1) {
			case CC_ALL_SOUNDS_OFF:
				for (u8 string_id = 0; string_id < NUM_STRINGS; string_id++)
					force_release_string(string_id);
				clear_synth_strings();
				delay_clear();
				reverb_clear();
				return;
			case CC_RESET_ALL_CTR:
				// global
				channel_pressure = 0;
				channel_pitchbend.value = UINT14_HALF;
				channel_pitchbend_pitch = 0;
				mpe_zone[0].pressure = 0;
				mpe_zone[0].pitchbend.value = UINT14_HALF;
				mpe_zone[0].pitchbend_pitch = 0;
				mpe_zone[1].pressure = 0;
				mpe_zone[1].pitchbend.value = UINT14_HALF;
				mpe_zone[1].pitchbend_pitch = 0;
				// per string
				for (u8 string_id = 0; string_id < NUM_STRINGS; string_id++)
					reset_controls(string_id);
				return;
			case CC_LOCAL_CONTROL: {
				bool off = data2 < 64;
				if (set_sys_param(SYS_LOCAL_CTRL_OFF, off) && off)
					clear_latch();
				return;
			}
			case CC_ALL_NOTES_OFF:
				for (u8 string_id = 0; string_id < NUM_STRINGS; string_id++)
					force_release_string(string_id);
				return;
			default:
				break;
			}
			break;
		default:
			break;
		}
	}

	// == non-mpe == //

	if (!using_mpe) {
		switch (type) {
		case MIDI_NOTE_OFF: {
			bool string_found = false;
			MidiString* m_string;
			// find string pressing this note
			for (u8 string_id = non_mpe_start_string; string_id < high_mpe_start_string; string_id++) {
				m_string = &midi_string[string_id];
				if (m_string->state == MS_PRESSED && m_string->note_number == data1) {
					string_found = true;
					break;
				}
			}
			if (string_found)
				m_string->state = m_string->sustain_pressed ? MS_SUSTAINED : MS_UNPRESSED;
		} break;
		case MIDI_NOTE_ON: {
			// we can't play this note => ignore
			if (data1 >= NUM_NOTES)
				break;

			u8 string_id = 255;

			// find string pressing or sustaining this note
			for (u8 i = non_mpe_start_string; i < high_mpe_start_string; ++i) {
				MidiString* m_string = &midi_string[i];
				if (m_string->state != MS_UNPRESSED && m_string->note_number == data1) {
					string_id = i;
					break;
				}
			}

			// no existing string found => find new string
			s32 midi_pitch = NOTE_NR_TO_PITCH(data1);
			if (string_id == 255)
				string_id = find_string_for_pitch(midi_pitch, non_mpe_start_string, high_mpe_start_string);

			// no space to register a new midi press => exit
			if (string_id == 255)
				break;

			// quantize pitch and note number
			if (sys_params.midi_in_scale_quant) {
				midi_pitch = quant_pitch_to_scale(midi_pitch, string_id);
				data1 = PITCH_TO_NOTE_NR(midi_pitch);
			}

			// save results
			register_press(string_id, data1, data2, string_position_from_pitch(string_id, midi_pitch));
		} break;
		case MIDI_POLY_KEY_PRESSURE:
			// apply to all strings holding this note
			for (u8 string_id = non_mpe_start_string; string_id < high_mpe_start_string; string_id++) {
				MidiString* m_string = &midi_string[string_id];
				if (m_string->note_number == data1)
					m_string->pressure = data2;
			}
			break;
		case MIDI_PITCH_BEND:
			channel_pitchbend.lsb = data1;
			channel_pitchbend.msb = data2;
			update_channel_pitchbend();
			break;
		case MIDI_CHANNEL_PRESSURE:
			channel_pressure = data1;
			break;
		case MIDI_CONTROL_CHANGE:
			if (try_handle_n_rpn(data1, data2, false, 0))
				break;
			switch (data1) {
			// update string mod wheels
			case CC_MOD_WHEEL:
				for (u8 string_id = non_mpe_start_string; string_id < high_mpe_start_string; string_id++)
					midi_string[string_id].mod_wheel.msb = data2;
				break;
			case CC_MOD_WHEEL_LSB:
				for (u8 string_id = non_mpe_start_string; string_id < high_mpe_start_string; string_id++)
					midi_string[string_id].mod_wheel.lsb = data2;
				mod_wheel_14bit = true;
				break;
			// update string sustains
			case CC_SUSTAIN:
				bool new_sustain = data2 >= 64;
				for (u8 string_id = non_mpe_start_string; string_id < high_mpe_start_string; string_id++)
					apply_sustain(new_sustain, string_id);
				break;
			default:
				// update parameters from ccs
				if (sys_params.midi_rcv_param_ccs)
					params_rcv_cc(data1, data2, false, 0);
				break;
			}
			break;
		default:
			break;
		}
		return;
	}

	// == mpe manager channel == //

	if (is_manager_msg) {
		u8 zone_start_string = mpe_zone_upper ? high_mpe_start_string : 0;
		u8 zone_end_string = mpe_zone_upper ? NUM_STRINGS : mpe_zone[0].num_strings;
		switch (type) {
		case MIDI_PITCH_BEND:
			mpe_zone[(u8)mpe_zone_upper].pitchbend.lsb = data1;
			mpe_zone[(u8)mpe_zone_upper].pitchbend.msb = data2;
			update_zone_pitchbend(mpe_zone_upper);
			break;
		case MIDI_CHANNEL_PRESSURE:
			mpe_zone[(u8)mpe_zone_upper].pressure = data1;
			break;
		case MIDI_CONTROL_CHANGE: {
			// (n)rpn handling
			if (try_handle_n_rpn(data1, data2, false, 0))
				return;
			switch (data1) {
			// update string mod wheels
			case CC_MOD_WHEEL:
				for (u8 string_id = zone_start_string; string_id < zone_end_string; string_id++)
					midi_string[string_id].mod_wheel.msb = data2;
				break;
			case CC_MOD_WHEEL_LSB:
				for (u8 string_id = zone_start_string; string_id < zone_end_string; string_id++)
					midi_string[string_id].mod_wheel.lsb = data2;
				mod_wheel_14bit = true;
				break;
			// update string sustains
			case CC_SUSTAIN:
				bool new_sustain = data2 >= 64;
				for (u8 string_id = zone_start_string; string_id < zone_end_string; string_id++)
					apply_sustain(new_sustain, string_id);
				break;
			default:
				// update parameters from ccs
				if (sys_params.midi_rcv_param_ccs) {
					for (u8 string_id = zone_start_string; string_id < zone_end_string; string_id++)
						params_rcv_cc(data1, data2, true, string_id);
				}
				break;
			}
			break;
		}
		default:
			break;
		}
		return;
	}

	// == mpe member channel == //

	MpeZone* zone = &mpe_zone[(u8)mpe_zone_upper];
	u8 string_id = mpe_zone_upper ? ((channel - (15 - zone->num_chans)) % zone->num_strings) + (8 - zone->num_strings)
	                              : (channel - 1) % zone->num_strings;
	MidiString* m_string = &midi_string[string_id];
	switch (type) {
	case MIDI_NOTE_OFF:
		if (m_string->state == MS_PRESSED)
			m_string->state = m_string->sustain_pressed ? MS_SUSTAINED : MS_UNPRESSED;
		break;
	case MIDI_NOTE_ON:
		// we can't play this note => ignore
		if (data1 >= NUM_NOTES)
			break;

		register_press(string_id, data1, data2, string_position_from_pitch(string_id, NOTE_NR_TO_PITCH(data1)));
		break;
	case MIDI_PITCH_BEND:
		m_string->pitchbend.lsb = data1;
		m_string->pitchbend.msb = data2;
		update_string_pitchbend(string_id);
		break;
	case MIDI_CHANNEL_PRESSURE:
		m_string->pressure = data1;
		break;
	case MIDI_CONTROL_CHANGE:
		if (try_handle_n_rpn(data1, data2, true, string_id))
			break;
		switch (data1) {
		case CC_MOD_WHEEL:
			m_string->mod_wheel.msb = data2;
			break;
		case CC_MOD_WHEEL_LSB:
			m_string->mod_wheel.lsb = data2;
			mod_wheel_14bit = true;
			break;
		case CC_SUSTAIN:
			apply_sustain(data2 >= 64, string_id);
			break;
		default:
			// update parameters from ccs
			if (sys_params.midi_rcv_param_ccs)
				params_rcv_cc(data1, data2, true, string_id);
			break;
		}
		break;
	default:
		break;
	}
}

void midi_tick(void) {
	cue_midi_out();

	// send to uart
	if (midi_send_head != midi_send_tail) {
		u8 from = midi_send_tail & 15;
		u8 to = midi_send_head & 15;
		if (to > from) {
			midi_send_tail += (to - from);
			HAL_UART_Transmit_DMA(&huart3, midi_send_buffer + from, to - from);
		}
		else {
			u8 send_len = (MIDI_BUFFER_SIZE - from) + to;
			memcpy(midi_send_buffer + MIDI_BUFFER_SIZE, midi_send_buffer, to);
			midi_send_tail += send_len;
			HAL_UART_Transmit_DMA(&huart3, midi_send_buffer + from, send_len);
		}
	}

	// sysex timeout
	if (receiving_sysex && millis() - sysex_start_time >= SYSEX_TIMEOUT_MILLIS)
		receiving_sysex = false;

	// serial midi in
	static u8 last_read_pos = 0;
	static u8 state = 0;
	static u8 msg[3] = {0};
	u8 read_pos = MIDI_BUFFER_SIZE - __HAL_DMA_GET_COUNTER(huart3.hdmarx);
	if (read_pos != last_read_pos) {
		u8 len;
		if (read_pos > last_read_pos)
			len = read_pos - last_read_pos;
		// linearize read across buffer boundary
		else {
			len = read_pos - last_read_pos + MIDI_BUFFER_SIZE;
			memcpy(midi_receive_buffer + MIDI_BUFFER_SIZE, midi_receive_buffer, read_pos);
		}
		const u8* buf = &midi_receive_buffer[last_read_pos];
		for (; len--;) {
			u8 data = *buf++;
			// not sysex
			if (!receiving_sysex) {
				// status byte
				if (data & MIDI_STATUS_BYTE_MASK) {
					// sysex start
					if (data == MIDI_SYSTEM_EXCLUSIVE)
						start_receiving_sysex();
					// real-time msg
					else if ((data & MIDI_REAL_TIME_MASK) == MIDI_REAL_TIME_MASK)
						// handle immediately
						process_midi_msg(data, 0, 0);
					// channel mode msg
					else if ((data & 0xF0) == 0xF0)
						// cancels running status, no further processing
						msg[0] = 0;
					// channel voice msg, start new
					else {
						msg[0] = data;
						state = 1;
					}
				}
				// data byte
				else {
					// not gathering a channel voice msg, ignore
					if (msg[0] == 0)
						continue;
					// running status
					if (state == 3)
						state = 1;
					// save data
					msg[state++] = data;
					// received enough bytes
					if (state == num_midi_bytes(msg[0]))
						process_midi_msg(msg[0], msg[1], msg[2]);
				}
			}
			// sysex
			else {
				// end sysex
				if (data == MIDI_END_OF_EXCLUSIVE)
					receiving_sysex = false;
				// real-time messages get processed normally
				else if (data >= MIDI_TIMING_CLOCK)
					process_midi_msg(data, 0, 0);
				// sysex data
				else if (data >> 7 == 0)
					process_sysex_byte(data);
			}
		}
		last_read_pos = read_pos;
	}

	// usb midi in
	usb_request_tud_task();
	u8 midi_packet[4];
	// handle incoming packets
	while (tud_midi_available() && tud_midi_packet_read(midi_packet)) {
		midi_code_index_number_t cin = midi_packet[0] & 15;
		// not receiving sysex
		if (!receiving_sysex) {
			// 3 byte sysex start message
			if (cin == MIDI_CIN_SYSEX_START) {
				// byte 1: start sysex
				start_receiving_sysex();
				// byte 2
				if (midi_packet[2] == MIDI_END_OF_EXCLUSIVE)
					receiving_sysex = false;
				else {
					process_sysex_byte(midi_packet[2]);
					// byte 3
					if (midi_packet[3] == MIDI_END_OF_EXCLUSIVE)
						receiving_sysex = false;
					else
						process_sysex_byte(midi_packet[3]);
				}
			}
			// regular midi message
			else
				process_midi_msg(midi_packet[1], midi_packet[2], midi_packet[3]);
		}
		// sysex
		else {
			switch (cin) {
			// 3 byte sysex continue
			case MIDI_CIN_SYSEX_START:
				process_sysex_byte(midi_packet[1]);
				process_sysex_byte(midi_packet[2]);
				process_sysex_byte(midi_packet[3]);
				break;
			// sysex ends
			case MIDI_CIN_SYSEX_END_1BYTE:
				receiving_sysex = false;
				break;
			case MIDI_CIN_SYSEX_END_2BYTE:
				process_sysex_byte(midi_packet[1]);
				receiving_sysex = false;
				break;
			case MIDI_CIN_SYSEX_END_3BYTE:
				process_sysex_byte(midi_packet[1]);
				process_sysex_byte(midi_packet[2]);
				receiving_sysex = false;
				break;
			// real time messages are allowed during sysex
			case MIDI_CIN_1BYTE_DATA:
				if (midi_packet[1] >= MIDI_TIMING_CLOCK)
					process_midi_msg(midi_packet[1], 0, 0);
				break;
			default:
				break;
			}
		}
	}
}

void midi_update_zone_boundaries(void) {
	non_mpe_start_string = sys_params.mpe_in ? mpe_zone[0].num_strings : 0;
	high_mpe_start_string = sys_params.mpe_in ? NUM_STRINGS - mpe_zone[1].num_strings : NUM_STRINGS;
}

void set_mpe_channels(u8 zone, u8 num_chans) {
	// set channels
	mpe_zone[zone].num_chans = num_chans;
	mpe_zone[(u8)!zone].num_chans = mini(mpe_zone[(u8)!zone].num_chans, 15 - num_chans);
	// recalculate num_strings
	bool one_chan_per_string = mpe_zone[0].num_chans + mpe_zone[1].num_chans <= 8;
	mpe_zone[zone].num_strings = one_chan_per_string ? mpe_zone[zone].num_chans : (mpe_zone[zone].num_chans + 1) >> 1;
	mpe_zone[(u8)!zone].num_strings =
	    one_chan_per_string ? mpe_zone[(u8)!zone].num_chans : (mpe_zone[(u8)!zone].num_chans + 1) >> 1;
	midi_update_zone_boundaries();
	// save to sys_params
	set_sys_param(SYS_MPE_ZONE, zone);
	set_sys_param(SYS_MPE_CHANS, num_chans);
}

static void midi_push_cc(u8 data1, u8 data2) {
	// wait for DMA to be ready
	if (!sys_params.midi_trs_out_off)
		while (huart3.TxXferCount)
			;

	// fill buffer
	u8 buffer[4];
	buffer[0] = MIDI_CIN_CONTROL_CHANGE;
	buffer[1] =
	    MIDI_CONTROL_CHANGE | (sys_params.mpe_out ? sys_params.mpe_zone == 0 ? 0 : 15 : sys_params.midi_out_chan);
	buffer[2] = data1;
	buffer[3] = data2;

	// send buffer
	if (!sys_params.midi_trs_out_off)
		HAL_UART_Transmit_DMA(&huart3, &buffer[1], 3);
	// when mounted, keep trying to send until success
	while (tud_ready() && !tud_midi_packet_write(buffer))
		;
}

void midi_push_preset(void) {
	pushing_preset = true;
	// send params set to "off" or ccs => send ccs
	if (sys_params.midi_send_param_ccs < 2) {
		// loop through parameters
		for (Param param_id = 0; param_id < NUM_PARAMS; param_id++) {
			u8 param_cc = midi_cc_table_rvs[param_id];
			// no cc for this param
			if (param_cc == 255)
				continue;
			// push value
			midi_push_cc(param_cc, param_cc_value(param_id));
		}
	}
	// send params set to send nrpns
	else {
		// loop through mod sources
		for (ModSource mod_src = SRC_BASE; mod_src <= SRC_RND; mod_src++) {
			// send nrpn msb (selects main parameter or mod source)
			midi_push_cc(CC_NRPN_MSB, mod_src);

			// loop through parameters
			for (Param param_id = 0; param_id < NUM_PARAMS; param_id++) {
				u14 nrpn_value;
				if (!get_param_nrpn_value(param_id, mod_src, &nrpn_value))
					continue;
				// send nrpn lsb (selects parameter id)
				midi_push_cc(CC_NRPN_LSB, param_id);
				// send value
				midi_push_cc(CC_DATA_MSB, nrpn_value.msb);
				midi_push_cc(CC_DATA_LSB, nrpn_value.lsb);

				// send multi-timbral parameters
				if (PARAM_IS_POLY(param_id)) {
					// loop through strings (string 0 is identical to the global param value)
					for (u8 string_id = 1; string_id < NUM_STRINGS; string_id++) {
						nrpn_value = param_nrpn_poly_value(param_id, string_id);
						// send nrpn msb (selects string_id)
						midi_push_cc(CC_NRPN_MSB, string_id + 8);
						// send nrpn lsb (selects parameter id)
						midi_push_cc(CC_NRPN_LSB, param_id);
						// send value
						midi_push_cc(CC_DATA_MSB, nrpn_value.msb);
						midi_push_cc(CC_DATA_LSB, nrpn_value.lsb);
					}
				}
			}
		}
	}
	pushing_preset = false;
}

bool midi_try_get_touch(u8 string_id, s16* pressure, s16* position, u8* note_number, s32* note_offset_pitch,
                        u8* start_velocity) {
	MidiString* m_string = &midi_string[string_id];
	// not pressed => exit
	if (m_string->state == MS_UNPRESSED)
		return false;

	bool using_mpe = sys_params.mpe_in && (string_id < non_mpe_start_string || string_id >= high_mpe_start_string);
	u8 zone = string_id < non_mpe_start_string ? 0 : 1;

	// synthesize internal pressure from midi velocity and midi pressure
	u16 midi_pressure14 = 0;
	if (using_mpe)
		midi_pressure14 = maxi(m_string->pressure << 7, mpe_zone[zone].pressure);
	else if (sys_params.midi_in_pres_type == MP_POLY_AFTERTOUCH)
		midi_pressure14 = maxi(m_string->pressure << 7, channel_pressure);
	else if (sys_params.midi_in_pres_type == MP_CHANNEL_PRESSURE)
		midi_pressure14 = channel_pressure << 7;

	// apply mod wheel as pressure, map 14-bit mod wheel to 127 << 7 to conform with 7 bit behavior
	midi_pressure14 =
	    maxi(midi_pressure14, mod_wheel_14bit ? m_string->mod_wheel.value * 127 >> 7 : m_string->mod_wheel.msb << 7);

	// synthesize internal pressure based on velocity/pressure balance
	u8 velo_mult = sys_params.midi_in_vel_balance;
	*pressure =
	    // scaled velocity, offset to max out at 1 << 14
	    (velo_mult * ((m_string->start_velocity << 7) + 128)
	     // scaled pressure, offset to max out at 1 << 14
	     + (128 - velo_mult) * (midi_pressure14 + 128)
	     // scale to max out at TOUCH_FULL_PRES
	     + 512)
	    >> 10;
	// for midi, position is only used to light up the led at the correct pad
	*position = m_string->position;
	*note_number = m_string->note_number;
	*start_velocity = m_string->start_velocity;
	if (using_mpe)
		*note_offset_pitch = mpe_zone[zone].pitchbend_pitch + m_string->pitchbend_pitch;
	else
		*note_offset_pitch = channel_pitchbend_pitch + m_string->pitchbend_pitch;
	return true;
}

// == CUE MIDI OUT == //

void midi_send_clock(void) {
	if (sys_params.midi_send_clock)
		clocks_to_send++;
}

void midi_send_transport(MidiMessageType transport_type) {
	if (sys_params.midi_send_transport && transport_type >= MIDI_TIMING_CLOCK)
		send_transport = transport_type;
}

void midi_send_param(Param param_id) {
	u8 bank = param_id / 32;
	u8 position = param_id & 31;
	send_param_val[bank] |= 1 << position;
}

// == VISUALS == //

void draw_sysex_flag(void) {
	if (!receiving_sysex)
		return;

	fill_rectangle(49, 20, 77, 30);
	inverted_rectangle(49, 20, 77, 30);
	fill_rectangle(50, 21, 76, 29);
	gfx_text_color = 0;
	draw_str(51, 20, F_8, "sysex");
}

// == AUX == //

// we debug log through the midi serial
void debug_log(const char* format, ...) {
#ifndef DEBUG_LOG
	return;
#endif

	char buf[128];
	va_list args;
	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);
	for (u16 i = 0; buf[i] != '\0'; i++)
		midi_send_buffer[(midi_send_head++) & 15] = buf[i];
	midi_send_buffer[(midi_send_head++) & 15] = '\n';
}

// from https://community.st.com/s/question/0D50X00009XkflR/haluartirqhandler-bug
// what a trash fire
// USART Error Handler
void HAL_UART_ErrorCallback(UART_HandleTypeDef* huart) {
	__HAL_UART_CLEAR_OREFLAG(huart);
	__HAL_UART_CLEAR_NEFLAG(huart);
	__HAL_UART_CLEAR_FEFLAG(huart);
	/* Disable the UART Error Interrupt: (Frame error, noise error, overrun error) */
	__HAL_UART_DISABLE_IT(huart, UART_IT_ERR);
	// The most important thing when UART framing error occur/any error is restart the RX process
	midi_clear_all();
	HAL_UART_Receive_DMA(&huart3, midi_receive_buffer, MIDI_BUFFER_SIZE);
}
