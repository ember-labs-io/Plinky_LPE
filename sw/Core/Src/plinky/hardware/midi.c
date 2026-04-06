// include tinyusb, undo their bool def
#include "tusb.h"
#undef bool

#include "midi.h"
// regular includes
#include "hardware/touchstrips.h"
#include "memory.h"
#include "synth/audio.h"
#include "synth/lfos.h"
#include "synth/params.h"
#include "synth/synth.h"
#include "synth/time.h"
#include "ui/oled_viz.h"

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
	bool mpe;             // prevents global midi messages from mapping to mpe strings
} MidiString;

typedef struct LastSentString {
	u8 note_number;
	u8 pressure;
	u8 pressure_cc;
	u8 position_cc;
} LastSentString;

#define MIDI_BUFFER_SIZE 16
#define THRU_BUFFER_SIZE 16
#define PARAM_BUFFER_SIZE 8

static const MidiString init_midi_string = {MS_UNPRESSED, 255, {UINT14_HALF}};
static const LastSentString init_last_sent_string = {255};

// buffers - double sized to allow linearizing cross-boundary reads/writes
static u8 midi_receive_buffer[2 * MIDI_BUFFER_SIZE];
static u8 midi_send_buffer[2 * MIDI_BUFFER_SIZE];
static u8 midi_send_head;
static u8 midi_send_tail;

// settings
static u16 max_channel_bend_pitch_in;
static u16 max_string_bend_pitch_in;
static u16 max_string_bend_pitch_out; // not implemented yet

// midi state
static MidiString midi_string[NUM_STRINGS];
static u8 channel_pressure;
static u14 channel_pitchbend;
static s32 channel_pitchbend_pitch;
static bool mod_wheel_14bit = false;

// mpe
static u8 manager_chan = 0;
static u8 num_member_chans = 8;

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
static Param last_sent_param = 0;

// == UTILS == //

static u8 num_midi_bytes(u8 status) {
	if (status >= MIDI_TUNE_REQUEST)
		return 1;
	if ((status & MIDI_TYPE_MASK) == MIDI_PROGRAM_CHANGE || (status & MIDI_TYPE_MASK) == MIDI_CHANNEL_PRESSURE
	    || status == MIDI_TIME_CODE || status == MIDI_SONG_SELECT)
		return 2;
	return 3;
}

static u8 midi_out_pressure(u8 string_pres, u8 start_velocity) {
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
	params_rcv_cc(CC_NRPN_LSB, 127, true, string_id);
	params_rcv_cc(CC_NRPN_MSB, 127, true, string_id);
	params_rcv_cc(CC_RPN_LSB, 127, true, string_id);
	params_rcv_cc(CC_RPN_MSB, 127, true, string_id);
}

static void register_press(u8 string_id, bool mpe, u8 note, u8 velocity, u16 position) {
	MidiString* m_string = &midi_string[string_id];
	m_string->state = MS_PRESSED;
	m_string->mpe = mpe;
	m_string->note_number = note;
	m_string->start_velocity = velocity;
	m_string->position = position;
}

// send midi msg to uart and usb, returns false if buffer too full
static bool send_midi_msg(u8 status, u8 data1, u8 data2) {
	u8 num_bytes = num_midi_bytes(status);
	// exit if serial buffer full
	if (midi_send_head - midi_send_tail + num_bytes > MIDI_BUFFER_SIZE)
		return false;
	// set midi channel
	if (status < MIDI_SYSTEM_EXCLUSIVE)
		status += midi_out_channel;
	// prepare usb packet
	u8 buf[4] = {status >> 4, status, data1, data2};

#ifndef DEBUG_LOG

	// serial midi out

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

#endif

	// send to usb
	tud_midi_packet_write(buf);
	return true;
}

static bool send_double_midi_msg(u8 status1, u8 data1_1, u8 data1_2, u8 status2, u8 data2_1, u8 data2_2) {
	// exit if not enough space for both messages
	if (midi_send_head - midi_send_tail + num_midi_bytes(status1) + num_midi_bytes(status2) > MIDI_BUFFER_SIZE)
		return false;
	// send both messages
	send_midi_msg(status1, data1_1, data1_2);
	send_midi_msg(status2, data2_1, data2_2);
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
	update_channel_pitchbend();
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
	HAL_UART_Receive_DMA(&huart3, midi_receive_buffer, MIDI_BUFFER_SIZE);
}

// cue all midi data for one string, returns whether there is still space in the midi buffer
static bool cue_midi_string_out(void) {
	typedef enum MidiOutState {
		MSG_NOTE,
		MSG_POLY_PRESSURE,
		MSG_CHAN_PRESSURE,
		MSG_PRESSURE_CC,
		MSG_POSITION_CC,
		MSG_LFOS,
	} MidiOutState;

	static u8 string_id = 0;
	static MidiOutState msg_state = MSG_NOTE;

	const Touch* touch = get_touch(string_id, 0);
	const SynthString* s_string = get_synth_string(string_id);
	u16 string_pres = clampi(s_string->cur_touch.pres, 0, TOUCH_FULL_PRES);
	u8 string_vel = maxi(s_string->start_velocity, 1);
	LastSentString* m_last = &last_sent_string[string_id];
	bool using_mpe = sys_params.midi_out_pres_type == MP_MPE_PRESSURE;

	midi_out_channel = using_mpe ? string_id + 1 : sys_params.midi_out_chan;

	switch (msg_state) {
	case MSG_NOTE: {
		u8 last_note = m_last->note_number;
		u8 string_note = clampi(s_string->note_number, 0, 127);
		// string is touched
		if (s_string->touched) {
			// we last sent a note off => send a note on
			if (last_note == 255) {
				if (!send_midi_msg(MIDI_NOTE_ON, string_note, string_vel))
					return false;
				m_last->note_number = string_note;
			}
			// we last sent a different note => send both a note off and note on
			else if (last_note != string_note) {
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
			if (sys_params.midi_out_pres_type == MP_CHANNEL_PRESSURE) {
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
	case MSG_PRESSURE_CC: // cc 40-47
		if (sys_params.midi_out_ccs) {
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
	case MSG_POSITION_CC: // cc 32-39
		if (sys_params.midi_out_ccs) {
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
		if (string_id == 7) {
			if (sys_params.midi_out_lfos) {
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
		}

		// done, set up for next string
		string_id = (string_id + 1) & 7;
		msg_state = MSG_NOTE;
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
	if (huart3.TxXferCount)
		return;

	// send thru
	if (sys_params.midi_soft_thru) {
		while (thru_buffer_count) {
			if (!send_midi_msg(thru_buffer[thru_buffer_tail][0], thru_buffer[thru_buffer_tail][1],
			                   thru_buffer[thru_buffer_tail][2]))
				return;
			thru_buffer_tail = (thru_buffer_tail + 1) % THRU_BUFFER_SIZE;
			thru_buffer_count--;
		}
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

	// send one param value
	if (sys_params.midi_out_params) {
		Param send_param = NUM_PARAMS;
		Param start_param = last_sent_param + 1;
		u8 bank = start_param / 32;
		u8 position = start_param & 31;
		// look for set bits to the left of last_sent_param
		u32 bank_bits = send_param_val[bank] & ~((1 << position) - 1);
		if (bank_bits) {
			position = __builtin_ctz(bank_bits);
			send_param = bank * 32 + position;
		}
		// look for set bits in the other two banks
		else if (send_param_val[(bank + 1) % 3]) {
			bank = (bank + 1) % 3;
			position = __builtin_ctz(send_param_val[bank]);
			send_param = bank * 32 + position;
		}
		else if (send_param_val[(bank + 2) % 3]) {
			bank = (bank + 2) % 3;
			position = __builtin_ctz(send_param_val[bank]);
			send_param = bank * 32 + position;
		}
		if (send_param != NUM_PARAMS) {
			if (!send_midi_msg(MIDI_CONTROL_CHANGE, midi_cc_table_rvs[send_param], param_cc_value(send_param)))
				return;
			send_param_val[bank] &= ~(1 << position);
			last_sent_param = send_param;
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

// apply midi messages to plinky
static void process_midi_msg(u8 status, u8 data1, u8 data2) {
	MidiMessageType type = status & MIDI_TYPE_MASK;

	//  == not channel based == //

	if (type == MIDI_SYSTEM_COMMON_MSG) {
		// midi reset / panic
		if (status == MIDI_SYSTEM_RESET)
			midi_clear_all();
		// time-related => forward to clock
		if (status >= MIDI_TIMING_CLOCK && status <= MIDI_STOP)
			clock_rcv_midi(status);
		// clock messages get consumed, sysex gets ignored, the remaining six system common msgs get forwarded
		else if (sys_params.midi_soft_thru
		         && ((status >= MIDI_TIME_CODE && status <= MIDI_TUNE_REQUEST) || status >= MIDI_ACTIVE_SENSING))
			forward_midi_msg(status, data1, data2);
		return;
	}

	u8 in_channel = status & MIDI_CHANNEL_MASK;
	bool using_mpe = sys_params.midi_in_pres_type == MP_MPE_PRESSURE;
	bool is_manager_msg = using_mpe && in_channel == manager_chan;
	bool is_member_msg = using_mpe && in_channel > manager_chan && in_channel <= manager_chan + num_member_chans;

	// == unused channels: forward & exit == //

	// mpe
	if (using_mpe) {
		if (!is_manager_msg && !is_member_msg) {
			if (sys_params.midi_soft_thru)
				forward_midi_msg(status, data1, data2);
			return;
		}
	}
	// default
	else if (in_channel != sys_params.midi_in_chan) {
		// we forward voice messages not on either our in or out channels
		if (sys_params.midi_soft_thru && in_channel != sys_params.midi_out_chan)
			forward_midi_msg(status, data1, data2);
		return;
	}

	// turn silent note ons into note offs
	if (type == MIDI_NOTE_ON && data2 == 0)
		type = MIDI_NOTE_OFF;

	// == central channel == //

	if (!using_mpe || is_manager_msg) {
		switch (type) {
		case MIDI_PROGRAM_CHANGE:
			if (data1 < NUM_PRESETS)
				cue_mem_item(data1);
			break;
		case MIDI_NOTE_OFF: {
			bool string_found = false;
			MidiString* m_string;
			// find string pressing this note
			for (u8 string_id = 0; string_id < NUM_STRINGS; string_id++) {
				m_string = &midi_string[string_id];
				if (!m_string->mpe && m_string->state == MS_PRESSED && m_string->note_number == data1) {
					string_found = true;
					break;
				}
			}
			if (string_found)
				m_string->state = m_string->sustain_pressed ? MS_SUSTAINED : MS_UNPRESSED;
		} break;
		case MIDI_NOTE_ON: {
			s32 midi_pitch = 12 *
			                     // pitch from octave parameter
			                     ((param_index(P_OCT) << 9)
			                      // pitch from pitch parameter
			                      + (param_val(P_PITCH) >> 7))
			                 // pitch from midi note
			                 + NOTE_NR_TO_PITCH(data1)
			                 // pitchbend
			                 + channel_pitchbend_pitch;
			u8 string_id = 255;

			// find string pressing or sustaining this note
			for (u8 i = 0; i < NUM_STRINGS; ++i) {
				MidiString* m_string = &midi_string[i];
				if (!m_string->mpe && m_string->state != MS_UNPRESSED && m_string->note_number == data1) {
					string_id = i;
					break;
				}
			}

			// no existing string found => find new string
			if (string_id == 255)
				string_id = find_string_for_pitch(midi_pitch);

			// no space to register a new midi press => exit
			if (string_id == 255)
				break;

			// save results
			register_press(string_id, false, data1, data2, string_position_from_pitch(string_id, midi_pitch));
		} break;
		case MIDI_POLY_KEY_PRESSURE:
			// apply to all non-mpe strings holding this note
			for (u8 string_id = 0; string_id < NUM_STRINGS; string_id++) {
				MidiString* m_string = &midi_string[string_id];
				if (!m_string->mpe && m_string->note_number == data1)
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
			switch (data1) {
			// update all string mod wheels
			case CC_MOD_WHEEL:
				for (u8 string_id = 0; string_id < NUM_STRINGS; string_id++)
					midi_string[string_id].mod_wheel.msb = data2;
				break;
			case CC_MOD_WHEEL_LSB:
				for (u8 string_id = 0; string_id < NUM_STRINGS; string_id++)
					midi_string[string_id].mod_wheel.lsb = data2;
				mod_wheel_14bit = true;
				break;
			// update all string sustains
			case CC_SUSTAIN:
				bool new_sustain = data2 >= 64;
				for (u8 string_id = 0; string_id < NUM_STRINGS; string_id++)
					apply_sustain(new_sustain, string_id);
				break;
			// clears all strings and clears effects buffers
			case CC_ALL_SOUNDS_OFF:
				for (u8 string_id = 0; string_id < NUM_STRINGS; string_id++)
					force_release_string(string_id);
				clear_synth_strings();
				delay_clear();
				reverb_clear();
				break;
			case CC_RESET_ALL_CTR:
				// global
				channel_pressure = 0;
				channel_pitchbend.value = UINT14_HALF;
				update_channel_pitchbend();
				// per string
				for (u8 string_id = 0; string_id < NUM_STRINGS; string_id++)
					reset_controls(string_id);
				break;
			case CC_LOCAL_CONTROL:
				bool on = data2 >= 64;
				if (set_sys_param(SYS_LOCAL_ON, on) && !on)
					clear_latch();
				break;
			case CC_ALL_NOTES_OFF:
				for (u8 string_id = 0; string_id < NUM_STRINGS; string_id++)
					force_release_string(string_id);
				break;
			default:
				// update parameters from ccs
				params_rcv_cc(data1, data2, false, 0);
				break;
			}
			break;
		default:
			break;
		}
	}

	// == mpe member channels == //

	if (is_member_msg) {
		u8 string_id = in_channel - 1; // only works in low zone!
		MidiString* m_string = &midi_string[string_id];
		switch (type) {
		case MIDI_NOTE_OFF:
			if (m_string->state == MS_PRESSED)
				m_string->state = m_string->sustain_pressed ? MS_SUSTAINED : MS_UNPRESSED;
			break;
		case MIDI_NOTE_ON: {
			s32 midi_pitch = 12 *
			                     // pitch from octave parameter
			                     ((param_index(P_OCT) << 9)
			                      // pitch from pitch parameter
			                      + (param_val(P_PITCH) >> 7))
			                 // pitch from midi note
			                 + NOTE_NR_TO_PITCH(data1)
			                 // pitchbend
			                 + m_string->pitchbend_pitch;
			register_press(string_id, true, data1, data2, string_position_from_pitch(string_id, midi_pitch));
		} break;
		case MIDI_PITCH_BEND:
			m_string->pitchbend.lsb = data1;
			m_string->pitchbend.msb = data2;
			update_string_pitchbend(string_id);
			break;
		case MIDI_CHANNEL_PRESSURE:
			m_string->pressure = data1;
			break;
		case MIDI_CONTROL_CHANGE:
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
			case CC_ALL_SOUNDS_OFF:
				force_release_string(string_id);
				clear_synth_string(string_id);
				break;
			case CC_RESET_ALL_CTR:
				reset_controls(string_id);
				break;
			case CC_ALL_NOTES_OFF:
				force_release_string(string_id);
				break;
			default:
				// update parameters from ccs
				params_rcv_cc(data1, data2, true, string_id);
				break;
			}
			break;
		default:
			break;
		}
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
			// status byte
			if (data & MIDI_STATUS_BYTE_MASK) {
				// real-time msg
				if ((data & MIDI_REAL_TIME_MASK) == MIDI_REAL_TIME_MASK)
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
		last_read_pos = read_pos;
	}

	// usb midi in
	u8 midi_packet[4];
	// handle incoming packets
	while (tud_midi_available() && tud_midi_packet_read(midi_packet))
		process_midi_msg(midi_packet[1], midi_packet[2], midi_packet[3]);
}

bool midi_try_get_touch(u8 string_id, s16* pressure, s16* position, s8* note_number, u8* start_velocity,
                        s32* pitchbend_pitch) {
	MidiString* m_string = &midi_string[string_id];
	// not pressed => exit
	if (m_string->state == MS_UNPRESSED)
		return false;

	// synthesize internal pressure from midi velocity and midi pressure
	u16 midi_pressure14 = 0;
	switch (sys_params.midi_in_pres_type) {
	case MP_CHANNEL_PRESSURE:
		midi_pressure14 = channel_pressure << 7;
		break;
	case MP_POLY_AFTERTOUCH:
	case MP_MPE_PRESSURE:
		midi_pressure14 = maxi(m_string->pressure << 7, channel_pressure);
		break;
	default:
		break;
	}

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
	*pitchbend_pitch = channel_pitchbend_pitch + (m_string->mpe ? m_string->pitchbend_pitch : 0);
	return true;
}

// == CUE MIDI OUT == //

void midi_send_clock(void) {
	clocks_to_send++;
}

void midi_send_transport(MidiMessageType transport_type) {
	if (transport_type < MIDI_TIMING_CLOCK)
		return;
	send_transport = transport_type;
}

void midi_send_param(Param param_id) {
	u8 bank = param_id / 32;
	u8 position = param_id & 31;
	send_param_val[bank] |= 1 << position;
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
