// include tinyusb, undo their bool def
#include "tusb.h"
#undef bool

#include "midi.h"
// regular includes
#include "hardware/touchstrips.h"
#include "memory.h"
#include "synth/lfos.h"
#include "synth/params.h"
#include "synth/pitch_tools.h"
#include "synth/strings.h"
#include "synth/synth.h"
#include "synth/time.h"
#include "ui/oled_viz.h"

// midi uart, lives in main.c
extern UART_HandleTypeDef huart3;

#define MIDI_BUFFER_SIZE 16
#define THRU_BUFFER_SIZE 16
#define PARAM_BUFFER_SIZE 8
#define PITCH_OFFSET_FROM_NOTE(midi_note) ((((midi_note) - 24) << 9) + (pitchbend >> 3))
#define PITCH_OFFSET_FROM_NOTE_MPE(string_id, midi_note)                                                               \
	((((midi_note) - 24) << 9)                                                                                         \
	 + ((midi_string[string_id].assigned_by_mpe ? (midi_string[string_id].pitchbend * channel_bend_sens)               \
	                                            : (pitchbend * global_bend_sens))                                      \
	    >> 13))
#define MIXED_PRESSURE(full_pressure, midi_velocity)                                                                   \
	({                                                                                                                 \
		u8 velo_mult = sys_params.midi_out_vel_balance;                                                                \
		velo_mult == 100                                                                                               \
		    ? 0                                                                                                        \
		    : clampi(((100 * (full_pressure) >> 4) - 100 - velo_mult * (midi_velocity)) / (100 - velo_mult), 0, 127);  \
	})

typedef enum MidiStringState {
	MS_UNUSED,
	MS_PRESSED,
	MS_SUSTAINED,
	MS_RINGING_OUT,
} MidiStringState;

typedef struct MidiString {
	MidiStringState state;
	u8 note;
	u8 velocity;
	u8 pressure;
	s16 pitchbend;
	bool suppressed;      // suppressed by other touch
	bool assigned_by_mpe; // prevents global notes from mapping to mpe strings

	// midi out
	u16 position;          // position on string
	u8 cur_note;           // note currently playing on string
	u8 last_note;          // last sent midi note
	u8 last_pressure;      // last sent poly pressure
	u8 last_pressure_cc;   // last sent pressure CC
	u8 last_position_cc;   // last sent position CC
	u8 last_lfo[NUM_LFOS]; // last sent lfo CC
} MidiString;

static const MidiString midi_init_string = {MS_UNUSED, 255, 0, 0, 0, false, false, 0, 255, 255};

// buffers - double sized to allow linearizing cross-boundary reads/writes
static u8 midi_receive_buffer[2 * MIDI_BUFFER_SIZE];
static u8 midi_send_buffer[2 * MIDI_BUFFER_SIZE];
static u8 midi_send_head;
static u8 midi_send_tail;

// midi state
static u8 mod_wheel;
static u8 channel_pressure;
static s16 pitchbend;
static u16 global_bend_sens = 2 * PITCH_PER_SEMI;   // default: 2 semis
static u16 channel_bend_sens = 48 * PITCH_PER_SEMI; // default: 48 semis
static bool sustain_pressed = false;
static MidiString midi_string[NUM_STRINGS];

// mpe
static u8 manager_chan = 0;
static u8 num_member_chans = 8;

// cue midi out
static u8 clocks_to_send = 0;
static MidiMessageType send_transport = 0;
static u8 last_channel_pressure = 0;

// soft thru
static u8 thru_buffer[THRU_BUFFER_SIZE][3];
static u8 thru_buffer_head = 0;
static u8 thru_buffer_tail = 0;
static u8 thru_buffer_count = 0;

// parameter ccs
static u8 param_buffer[PARAM_BUFFER_SIZE][2];
static u8 param_buffer_head = 0;
static u8 param_buffer_tail = 0;
static u8 param_buffer_count = 0;
static u8 param_last[NUM_PARAMS];

// == UTILS == //

void midi_clear_all(void) {
	memset(&midi_receive_buffer, 0, 2 * MIDI_BUFFER_SIZE);
	memset(&midi_send_buffer, 0, 2 * MIDI_BUFFER_SIZE);
	midi_send_head = 0;
	midi_send_tail = 0;
	channel_pressure = 0;
	pitchbend = 0;
	sustain_pressed = false;
	for (u8 string_id = 0; string_id < NUM_STRINGS; string_id++)
		memcpy(&midi_string[string_id], &midi_init_string, sizeof(MidiString));
	memset(&thru_buffer, 0, sizeof(thru_buffer));
	thru_buffer_head = 0;
	thru_buffer_tail = 0;
	thru_buffer_count = 0;
	memset(&param_last, 255, NUM_PARAMS);
	memset(&param_buffer, 0, PARAM_BUFFER_SIZE * 2);
	param_buffer_head = 0;
	param_buffer_tail = 0;
	param_buffer_count = 0;
}

bool midi_string_used(u8 string_id) {
	return midi_string[string_id].state != MS_UNUSED && !midi_string[string_id].suppressed;
}

void midi_try_get_touch(u8 string_id, s16* pressure, s16* position) {
	// midi strings are suppressed by any existing pressure (from touch, sequencer, etc)
	midi_string[string_id].suppressed = *pressure > 0;

	// not touched or suppressed => exit
	if (midi_string[string_id].state == MS_UNUSED || midi_string[string_id].state == MS_RINGING_OUT
	    || midi_string[string_id].suppressed)
		return;

	// string uses midi, get touch
	u8 midi_pressure = 0;
	switch (sys_params.midi_in_pres_type) {
	case MP_CHANNEL_PRESSURE:
		midi_pressure = channel_pressure;
		break;
	case MP_POLY_AFTERTOUCH:
	case MP_MPE_PRESSURE:
		midi_pressure = midi_string[string_id].pressure;
		break;
	default:
		break;
	}

	// apply pressure from mod wheel
	midi_pressure = maxi(midi_pressure, mod_wheel);

	// map internal pressure based on velocity/pressure balance
	u8 velo_mult = sys_params.midi_in_vel_balance;
	*pressure =
	    (((velo_mult * (midi_string[string_id].velocity + 1) + (100 - velo_mult) * (midi_pressure + 1)) << 4) + 50)
	    / 100;
	*position = midi_string[string_id].position;
}

s32 midi_get_pitch(u8 string_id) {
	return PITCH_OFFSET_FROM_NOTE_MPE(string_id, midi_string[string_id].note);
}

// a midi note ends when it's ringing out and it's gone quiet, or overwritten by a different press
void midi_try_end_note(u8 string_id) {
	if (midi_string[string_id].state == MS_RINGING_OUT
	    && (voices[string_id].env1_lvl < 0.001f || midi_string[string_id].suppressed))
		midi_string[string_id].state = MS_UNUSED;
}

// == MAIN == //

void init_midi(void) {
	midi_clear_all();
	HAL_UART_Receive_DMA(&huart3, midi_receive_buffer, MIDI_BUFFER_SIZE);
}

static u8 string_holds_note(u8 note) {
	for (u8 string_id = 0; string_id < NUM_STRINGS; ++string_id)
		if (!midi_string[string_id].assigned_by_mpe && midi_string[string_id].note == note)
			return string_id;
	return 255;
}

static u8 string_playing_note(u8 note) {
	for (u8 string_id = 0; string_id < NUM_STRINGS; ++string_id)
		if (!midi_string[string_id].assigned_by_mpe && midi_string[string_id].state != MS_UNUSED
		    && midi_string[string_id].note == note)
			return string_id;
	return 255;
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
			u8 string_id = string_playing_note(data1);
			if (string_id < NUM_STRINGS)
				midi_string[string_id].state = sustain_pressed ? MS_SUSTAINED : MS_RINGING_OUT;
		} break;
		case MIDI_NOTE_ON: {
			// try to map to existing string
			u8 string_id = string_playing_note(data1);
			// none found => find new string
			if (string_id == 255) {
				s32 midi_pitch = 12 *
				                     // pitch from octave parameter
				                     ((param_index(P_OCT) << 9)
				                      // pitch from pitch parameter
				                      + (param_val(P_PITCH) >> 7))
				                 // pitch from midi note
				                 + PITCH_OFFSET_FROM_NOTE(data1);
				// find the best string for this midi note
				u8 desired_string = 0;
				u32 min_pitch_dist = abs(string_center_pitch(0, param_index_poly(P_SCALE, 0)) - midi_pitch);
				for (u8 i = 1; i < NUM_STRINGS; i++) {
					u32 pitch_dist = abs(string_center_pitch(i, param_index_poly(P_SCALE, i)) - midi_pitch);
					if (pitch_dist >= min_pitch_dist)
						break;
					min_pitch_dist = pitch_dist;
					desired_string = i;
				}
				// find closest non-sounding string
				u8 min_string_dist = 255;
				for (u8 i = 0; i < NUM_VOICES; i++) {
					u8 dist = abs(i - desired_string);
					if (midi_string[i].state == MS_UNUSED && voices[i].env1_lvl < 0.001f && dist < min_string_dist) {
						min_string_dist = dist;
						string_id = i;
					}
				}
				// none found => find quietest non-pressed string
				float min_vol = __FLT_MAX__;
				if (string_id == 255) {
					for (u8 i = 0; i < NUM_VOICES; i++) {
						float vol = voices[i].env1_lvl;
						if ((midi_string[i].state == MS_UNUSED || midi_string[i].state == MS_RINGING_OUT)
						    && !(string_touched & (1 << i)) && vol < min_vol) {
							min_vol = vol;
							string_id = i;
						}
					}
				}
				// collect position on found string
				if (string_id < NUM_STRINGS) {
					Scale scale = param_index_poly(P_SCALE, string_id);
					s16 string_step_offset = step_at_string(string_id, scale);
					MidiString* m_string = &midi_string[string_id];
					m_string->position = 7 << 8;
					for (u8 pad = 7; pad > 0; pad--) {
						if (midi_pitch < pitch_at_step(string_step_offset + pad, scale))
							break;
						m_string->position = (7 - pad) << 8;
					}
				}
			}
			// save in string
			if (string_id < NUM_STRINGS) {
				MidiString* m_string = &midi_string[string_id];
				m_string->state = MS_PRESSED;
				m_string->note = data1;
				m_string->velocity = data2;
				m_string->assigned_by_mpe = false;
			}
		} break;
		case MIDI_POLY_KEY_PRESSURE: {
			u8 string_id = string_holds_note(data1);
			if (string_id < NUM_STRINGS)
				midi_string[string_id].pressure = data2;
		} break;
		case MIDI_PITCH_BEND:
			pitchbend = (data1 + (data2 << 7)) - 0x2000;
			break;
		case MIDI_CHANNEL_PRESSURE:
			channel_pressure = data1;
			break;
		case MIDI_CONTROL_CHANGE:
			switch (data1) {
			// mod wheel
			case 1:
				mod_wheel = data2;
				break;
			// sustain
			case 64:
				bool new_sustain = data2 >= 64;
				if (new_sustain != sustain_pressed) {
					sustain_pressed = new_sustain;
					// on a sustain release, release all midi notes that were held by the sustain
					if (!sustain_pressed) {
						for (u8 string_id = 0; string_id < NUM_STRINGS; string_id++)
							if (midi_string[string_id].state == MS_SUSTAINED)
								midi_string[string_id].state = MS_RINGING_OUT;
					}
				}
				break;
			default:
				// update parameters from ccs
				params_rcv_cc(data1, data2);
				break;
			}
			break;
		default:
			break;
		}
	}

	// == mpe member channels == //

	if (is_member_msg) {
		MidiString* m_string = &midi_string[in_channel - 1]; // only works in low zone!
		switch (type) {
		case MIDI_NOTE_OFF:
			m_string->state = sustain_pressed ? MS_SUSTAINED : MS_RINGING_OUT;
			break;
		case MIDI_NOTE_ON: {
			m_string->state = MS_PRESSED;
			m_string->note = data1;
			m_string->velocity = data2;
			m_string->assigned_by_mpe = true;
		} break;
		case MIDI_PITCH_BEND:
			m_string->pitchbend = (data1 + (data2 << 7)) - 0x2000;
			break;
		case MIDI_CHANNEL_PRESSURE:
			m_string->pressure = data1;
			break;
		default:
			break;
		}
	}
}

static u8 num_midi_bytes(u8 status) {
	if (status >= MIDI_TUNE_REQUEST)
		return 1;
	if ((status & MIDI_TYPE_MASK) == MIDI_PROGRAM_CHANGE || (status & MIDI_TYPE_MASK) == MIDI_CHANNEL_PRESSURE
	    || status == MIDI_TIME_CODE || status == MIDI_SONG_SELECT)
		return 2;
	return 3;
}

// send midi msg to uart and usb, returns false if buffer too full
static bool send_midi_msg(u8 status, u8 data1, u8 data2) {
	u8 num_bytes = num_midi_bytes(status);
	// exit if serial buffer full
	if (midi_send_head - midi_send_tail + num_bytes > MIDI_BUFFER_SIZE)
		return false;
	// prepare message
	if (status < MIDI_SYSTEM_EXCLUSIVE)
		status += sys_params.midi_out_chan;
	// prepare usb packet
	u8 buf[4] = {status >> 4, status, data1, data2};

#ifndef DEBUG_LOG
	// send to serial
	const u8* src = buf + 1;
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
	u16 string_pres = clampi(get_string_pressures()[string_id], 0, TOUCH_FULL_PRES);
	MidiString* m_string = &midi_string[string_id];

	switch (msg_state) {
	case MSG_NOTE: {
		u8 last_note = m_string->last_note;
		u8 cur_note = m_string->cur_note;
		// string is touched
		if (string_touched & (1 << string_id)) {
			// we last sent a note off => send a note on
			if (last_note == 255) {
				if (!send_midi_msg(MIDI_NOTE_ON, cur_note, m_string->velocity))
					return false;
				m_string->last_note = cur_note;
			}
			// we last sent a different note => send both a note off and note on
			else if (last_note != cur_note) {
				if (!send_double_midi_msg(MIDI_NOTE_OFF, last_note, 0, MIDI_NOTE_ON, cur_note, m_string->velocity))
					return false;
				m_string->last_note = cur_note;
			}
		}
		// string is not touched but we last sent a valid note => send note off
		else if (last_note != 255) {
			if (!send_midi_msg(MIDI_NOTE_OFF, last_note, 0))
				return false;
			m_string->last_note = 255;
		}
		msg_state++;
		// fall thru
	}
	case MSG_POLY_PRESSURE: {
		static u8 poly_pres = 0;
		static u8 min_diff = 5;
		// only update when poly pressure is selected
		if (sys_params.midi_out_pres_type == MP_POLY_AFTERTOUCH) {
			poly_pres = MIXED_PRESSURE(string_pres, m_string->velocity);
			// require a difference of 5, unless this is an extreme value
			min_diff = (poly_pres == 0 || poly_pres == 127) ? 1 : 5;
		}
		// send if changed
		if (abs(poly_pres - m_string->last_pressure) >= min_diff) {
			if (!send_midi_msg(MIDI_POLY_KEY_PRESSURE, m_string->last_note, poly_pres))
				return false;
			m_string->last_pressure = poly_pres;
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
			max_velocity = m_string->velocity;
		}
		// all strings have been tracked
		if (string_id == 7) {
			// only update when mono pressure is selected
			if (sys_params.midi_out_pres_type == MP_CHANNEL_PRESSURE) {
				chan_pres = MIXED_PRESSURE(max_string_pressure, max_velocity);
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
			if (abs(strip_pressure - m_string->last_pressure_cc) >= min_diff) {
				if (!send_midi_msg(MIDI_CONTROL_CHANGE, 40 + string_id, strip_pressure))
					return false;
				m_string->last_pressure_cc = strip_pressure;
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
			if (abs(strip_position - m_string->last_position_cc) >= min_diff) {
				if (!send_midi_msg(MIDI_CONTROL_CHANGE, 32 + string_id, strip_position))
					return false;
				m_string->last_position_cc = strip_position;
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
					if (abs(lfo_val - m_string->last_lfo[lfo_id])) {
						if (!send_midi_msg(MIDI_CONTROL_CHANGE, 24 + lfo_id, lfo_val))
							return false;
						m_string->last_lfo[lfo_id] = lfo_val;
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

	// send params
	if (sys_params.midi_out_params) {
		while (param_buffer_count) {
			if (!send_midi_msg(MIDI_CONTROL_CHANGE, param_buffer[param_buffer_tail][0],
			                   param_buffer[param_buffer_tail][1]))
				return;
			param_buffer_tail = (param_buffer_tail + 1) % PARAM_BUFFER_SIZE;
			param_buffer_count--;
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

void process_midi(void) {
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
	// refresh usb buffer
	tud_task();
	// handle incoming packets
	while (tud_midi_available() && tud_midi_packet_read(midi_packet))
		process_midi_msg(midi_packet[1], midi_packet[2], midi_packet[3]);
}

void midi_panic(void) {
	midi_clear_all();
	forward_midi_msg(MIDI_SYSTEM_RESET, 0, 0);
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

void midi_set_goal_note(u8 string_id, u8 midi_note) {
	midi_string[string_id].cur_note = midi_note;
}

void midi_set_start_velocity(u8 string_id, s16 pressure) {
	midi_string[string_id].velocity = clampi(pressure * 100 / (sys_params.midi_out_vel_balance << 4) - 1, 0, 127);
}

void midi_set_param_value(Param param, u8 cc_value) {
	if (param_buffer_count >= PARAM_BUFFER_SIZE) {
		flash_message(F_16_BOLD, "BUFFER FULL", "MIDI PARAM OUT");
		return;
	}

	u8 cc_number = midi_cc_table_rvs[param];
	// no valid cc for this param
	if (cc_number == 255)
		return;

	// value identical to last sent value
	if (param_last[param] == cc_value)
		return;

	param_last[param] = cc_value;
	param_buffer[param_buffer_head][0] = cc_number;
	param_buffer[param_buffer_head][1] = cc_value;
	param_buffer_head = (param_buffer_head + 1) % PARAM_BUFFER_SIZE;
	param_buffer_count++;
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
