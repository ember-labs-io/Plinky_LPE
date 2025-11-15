// include tinyusb, undo their bool def
#include "tusb.h"
#undef bool

#include "midi.h"
// regular includes
#include "memory.h"
#include "synth/params.h"
#include "synth/pitch_tools.h"
#include "synth/strings.h"
#include "synth/synth.h"
#include "synth/time.h"

// midi uart, lives in main.c
extern UART_HandleTypeDef huart3;

#define MIDI_BUFFER_SIZE 16
#define PITCH_OFFSET_FROM_NOTE(midi_note) ((((midi_note) - 24) << 9) + pitchbend / 8)

typedef enum MidiStringState {
	MS_UNUSED,
	MS_PRESSED,
	MS_SUSTAINED,
	MS_RINGING_OUT,
} MidiStringState;

typedef struct MidiString {
	MidiStringState state;
	u8 note; // played note
	u8 velocity;
	u8 pressure;
	bool suppressed; // suppressed by other touch

	// midi out
	u8 goal_note;          // note we want to send to midi out
	u16 position;          // position on string
	u8 last_sent_note;     // last sent midi note
	u8 note_on_pressure;   // pressure/velocity sent on note on
	u8 aftertouch;         // last sent poly aftertouch
	u8 last_sent_position; // last sent position CC
	u8 last_sent_pressure; // last sent pressure CC
} MidiString;

static const MidiString midi_init_string = {MS_UNUSED, 255, 0, 0, false, 255, 0, 255, 0, 0, 0, 0};

// buffers - double sized to allow linearizing cross-boundary reads/writes
static u8 midi_receive_buffer[2 * MIDI_BUFFER_SIZE];
static u8 midi_send_buffer[2 * MIDI_BUFFER_SIZE];
static u8 midi_send_head;
static u8 midi_send_tail;

// midi state
static u8 channel_pressure;
static s16 pitchbend;
static bool sustain_pressed = false;
static MidiString midi_string[NUM_STRINGS];

// cue midi out
static u8 clocks_to_send;
static MidiMessageType send_transport;

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

	// a midi note is playing this string => get touch
	*pressure = 1 + (midi_string[string_id].velocity + maxi(midi_string[string_id].pressure, channel_pressure)) * 16;
	*position = midi_string[string_id].position;
}

s32 midi_get_pitch(u8 string_id) {
	return PITCH_OFFSET_FROM_NOTE(midi_string[string_id].note);
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

static s32 string_pitch_at_pad(u8 string_id, u8 pad_y) {
	Scale scale = param_index_poly(P_SCALE, string_id);
	return pitch_at_step(scale, param_index_poly(P_DEGREE, string_id) + scale_steps_at_string(scale, string_id) + pad_y)
	       + 12 * ((param_index_poly(P_OCT, string_id) << 9) + (param_val_poly(P_PITCH, string_id) >> 7));
}

static s32 string_center_pitch(u8 string_id) {
	return (string_pitch_at_pad(string_id, 0) + string_pitch_at_pad(string_id, 7)) >> 1;
}

static u8 string_playing_note(u8 note) {
	for (u8 string_id = 0; string_id < 8; ++string_id)
		if (midi_string[string_id].state != MS_UNUSED && midi_string[string_id].note == note)
			return string_id;
	return 255;
}

// apply midi messages to plinky
static void process_midi_msg(u8 status, u8 data1, u8 data2) {
	MidiMessageType type = status & MIDI_TYPE_MASK;

	// out of the system messages we only implement the time-related ones => forward to clock
	if (type == MIDI_SYSTEM_COMMON_MSG) {
		clock_rcv_midi(status);
		return;
	}

	// channel voice messages: only listen on the chosen channel
	if ((status & MIDI_CHANNEL_MASK) != sys_params.midi_in_chan)
		return;

	// turn silent note ons into note offs
	if (type == MIDI_NOTE_ON && data2 == 0)
		type = MIDI_NOTE_OFF;

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
			// pitch falls below center of bottom string
			if (midi_pitch < string_center_pitch(0))
				desired_string = 0;
			// pitch falls above center of top string
			else if (midi_pitch >= string_center_pitch(7))
				desired_string = 7;
			// find the string with the closest center pitch
			else {
				s32 min_dist = __INT32_MAX__;
				for (u8 i = 0; i < NUM_STRINGS; i++) {
					u32 pitch_dist = abs(string_center_pitch(i) - midi_pitch);
					if (pitch_dist < min_dist) {
						min_dist = pitch_dist;
						desired_string = i;
					}
				}
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
				midi_string[string_id].position = 7 << 8;
				for (u8 pad = 7; pad > 0; pad--)
					if (midi_pitch >= string_pitch_at_pad(string_id, pad))
						midi_string[string_id].position = (7 - pad) << 8;
			}
		}
		// save in string
		if (string_id < NUM_STRINGS)
			midi_string[string_id] = (MidiString){MS_PRESSED, data1, data2};
	} break;
	case MIDI_POLY_KEY_PRESSURE: {
		u8 string_id = string_playing_note(data1);
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
		// sustain
		if (data1 == 64) {
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
		}
		// update parameters from ccs
		else
			params_rcv_cc(data1, data2);
		break;
	default:
		break;
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

// send midi msg to uart and usb, returns false if serial too full
static bool send_midi_msg(u8 status, u8 data1, u8 data2) {
	// prepare message
	u8 num_bytes = num_midi_bytes(status);
	if (status < MIDI_SYSTEM_EXCLUSIVE)
		status += sys_params.midi_out_chan;
	// midi buffer full
	if (midi_send_head - midi_send_tail + num_bytes > MIDI_BUFFER_SIZE)
		return false;
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

// outgoing midi gets processed once and sent out identically to serial and usb
static void cue_midi_out(void) {
// exit if uart is used for debug logging
#ifdef DEBUG_LOG
	return;
#endif

	static u8 string_id = 0;

	// exit if the uart is not ready
	if (huart3.TxXferCount)
		return;
	if (send_transport != MIDI_NONE) {
		if (!send_midi_msg(send_transport, 0, 0))
			return;
		send_transport = MIDI_NONE;
	}
	while (clocks_to_send) {
		if (!send_midi_msg(MIDI_TIMING_CLOCK, 0, 0))
			return;
		clocks_to_send--;
	}
	// we check for a maximum of eight times (once for each voice)
	u8 num_loops = 0;
	while (num_loops < 8) {
		num_loops++;
		//  get a bunch of parameters from the synth
		Touch* synthf = get_string_touch(string_id);
		Touch* prevsynthf = get_string_touch_prev(string_id, 2);
		bool pres_stable = abs(prevsynthf->pres - synthf->pres) < 100;
		bool pos_stable = abs(prevsynthf->pos - synthf->pos) < 32;
		bool pres_significant = synthf->pres > 200;
		// these are our targets
		u8 target_note = midi_string[string_id].goal_note;
		u8 target_pressure = clampi((synthf->pres - 100) / 48, 0, 127);

		// take out some undesired note/pressure values
		if (!target_note)
			target_pressure = 0;
		if (!target_pressure)
			target_note = 0;

		// one loop checks all needed outgoing midi messages for one voice
		u8 initial_head = midi_send_head;
		MidiString* str = &midi_string[string_id];
		u8 cur_note = str->last_sent_note;
		// note has changed
		if (target_note != cur_note) {
			// we were playing a note => send note off
			if (cur_note) {
				if (!send_midi_msg(MIDI_NOTE_OFF, cur_note, 0))
					return; // if the buffer is full, we exit the function and try again next time
				str->last_sent_note = 0;
				str->aftertouch = 0;
			}
			// we start playing a new note => send note on
			if (target_note && pos_stable && pres_stable) {
				// we use the current pressure as the note velocity
				if (!send_midi_msg(MIDI_NOTE_ON, target_note, target_pressure))
					return;
				str->last_sent_note = target_note;
				str->note_on_pressure = target_pressure;
				str->aftertouch = 0;
			}
		}
		// we define aftertouch as any pressure on top of the pressure when the note started
		u8 goal_aftertouch = maxi(target_pressure - str->note_on_pressure, 0);
		if (abs(goal_aftertouch - str->aftertouch) > 4) {
			// poly aftertouch (only when pressure difference is larger than 4)
			if (!send_midi_msg(MIDI_POLY_KEY_PRESSURE, cur_note, goal_aftertouch))
				return;
			str->aftertouch = goal_aftertouch;
		}
		// voice position, CC32 - CC39
		u8 goal_position = clampi(127 - (synthf->pos / 13 - 16), 0, 127);
		if (abs(goal_position - str->last_sent_position) > 1 && pres_significant && pres_stable) {
			if (!send_midi_msg(MIDI_CONTROL_CHANGE, 32 + string_id, goal_position))
				return;
			str->last_sent_position = goal_position;
		}
		// voice pressure, CC40 - CC47
		if (abs(target_pressure - str->last_sent_pressure) > 1) {
			if (!send_midi_msg(MIDI_CONTROL_CHANGE, 40 + string_id, target_pressure))
				return;
			str->last_sent_pressure = target_pressure;
		}

		// jump to next voice
		string_id = (string_id + 1) & 7;

		// if we sent any midi message for this voice, exit the function
		// the next time we enter the function again, string_id will be set to the next voice
		if (midi_send_head != initial_head)
			break;
	}
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
	const static u8 max_packets_per_call = 2;
	u8 midi_packet[4];
	u8 packets_handled = 0;
	do {
		if (!tud_midi_available() || !tud_midi_packet_read(midi_packet))
			return;
		process_midi_msg(midi_packet[1], midi_packet[2], midi_packet[3]);
		packets_handled++;
	} while (packets_handled < max_packets_per_call);
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
	midi_string[string_id].goal_note = midi_note;
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
