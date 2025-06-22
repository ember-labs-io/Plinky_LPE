#include "midi.h"
#include "gfx/gfx.h"
#include "hardware/ram.h"
#include "synth/arp.h"
#include "synth/params.h"
#include "synth/sequencer.h"
#include "synth/strings.h"
#include "synth/time.h"
#include "touchstrips.h"
#include "tusb.h"

// cleanup
#include "../../web_usb.h"
// -- cleanup

// midi uart, lives in main.c
extern UART_HandleTypeDef huart3;

#define NUM_14BIT_CCS 32
#define MIDI_BUFFER_SIZE 16

u8 midi_chan_pressure[NUM_MIDI_CHANNELS];
s16 midi_chan_pitchbend[NUM_MIDI_CHANNELS];
u8 midi_goal_note[NUM_STRINGS]; // the string is playing this note

void set_midi_goal_note(u8 string_id, u8 midi_note) {
	midi_goal_note[string_id] = midi_note;
}

static u8 clocks_to_send;
static MidiMessageType send_transport;

void midi_send_clock(void) {
	clocks_to_send++;
}

void midi_send_transport(MidiMessageType transport_type) {
	if (transport_type < MIDI_TIMING_CLOCK)
		return;
	send_transport = transport_type;
}

// buffers
static u8 midi_receive_buffer[MIDI_BUFFER_SIZE];
static u8 midi_send_buffer[2 * MIDI_BUFFER_SIZE];
static u8 midi_send_head;
static u8 midi_send_tail;

void midi_init(void) {
	// serial
	HAL_UART_Receive_DMA(&huart3, midi_receive_buffer, sizeof(midi_receive_buffer));
	// usb
	tusb_init();
}

// === OUTPUT LOOP === //

// throw send buffer to the uart
static void midi_buffer_to_uart(void) {
	if (huart3.TxXferCount == 0 && midi_send_head != midi_send_tail) {
		u8 from = midi_send_tail & 15;
		u8 to = midi_send_head & 15;
		if (to > from) {
			midi_send_tail += (to - from);
			HAL_UART_Transmit_DMA(&huart3, midi_send_buffer + from, to - from);
		}
		else if (to < from) {
			// wrapped! send from->16, and 0->to
			u8 send_len = (MIDI_BUFFER_SIZE - from) + to;
			// copy looped part to end so we can send it all in one go! good on us.
			memcpy(midi_send_buffer + MIDI_BUFFER_SIZE, midi_send_buffer, to);
			midi_send_tail += send_len;
			HAL_UART_Transmit_DMA(&huart3, midi_send_buffer + from, send_len);
		}
	}
}

// add midi bytes to the send buffer
static bool send_midi_serial(const u8* data, int len) {
	if (len <= 0)
		return true;
	if (midi_send_head - midi_send_tail + len > MIDI_BUFFER_SIZE)
		return false; // full
	while (len--)
		midi_send_buffer[(midi_send_head++) & 15] = *data++;
	return true;
}

// send midi msg to uart and usb, returns false if serial too full
static bool send_midi_msg(u8 status, u8 data1, u8 data2) {
	// prepare message
	u8 num_bytes = 3;
	if (status == MIDI_PROGRAM_CHANGE || status == MIDI_CHANNEL_PRESSURE)
		num_bytes = 2;
	int midi_ch_out = clampi((mini(param_val_raw(P_MIDI_CH_OUT, 0), PARAM_SIZE - 1) * 16) / PARAM_SIZE, 0, 15);
	if (status < MIDI_SYSTEM_EXCLUSIVE)
		status += midi_ch_out; // set output channel
	u8 buf[4] = {status >> 4, status, data1, data2};
	// send to serial
	if (!send_midi_serial(buf + 1, num_bytes)) {
		return false;
	}
	// if serial was successful, send to usb
	// we assume this succeeds as it's so much faster than serial midi
	tud_midi_packet_write(buf);
	return true;
}

// outgoing midi gets processed once and sent out identically to serial and usb
void process_all_midi_out(void) {
	static u8 string_id = 0;
	static u8 note[NUM_STRINGS];             // last sent midi note
	static u8 note_on_pressure[NUM_STRINGS]; // pressure/velocity sent on note on
	static u8 aftertouch[NUM_STRINGS];       // last sent poly aftertouch
	static u8 position[NUM_STRINGS];         // last sent position CC
	static u8 pressure[NUM_STRINGS];         // last sent pressure CC

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
		u8 target_note = midi_goal_note[string_id];
		u8 target_pressure = clampi((synthf->pres - 100) / 48, 0, 127);

		// take out some undesired note/pressure values
		if (!target_note)
			target_pressure = 0;
		if (arp_on() && !arp_touched(string_id))
			target_pressure = 0;
		if (!target_pressure)
			target_note = 0;

		// one loop checks all needed outgoing midi messages for one voice
		bool sent = false;
		u8 cur_note = note[string_id];
		// note has changed
		if (target_note != cur_note) {
			// we were playing a note => send note off
			if (cur_note) {
				if (!send_midi_msg(MIDI_NOTE_OFF, cur_note, 0))
					return; // if the buffer is full, we exit the function and try again next time
				note[string_id] = 0;
				aftertouch[string_id] = 0;
				sent = true;
			}
			// we start playing a new note => send note on
			if (target_note != 0 && pos_stable && pres_stable) {
				// we use the current pressure as the note velocity
				if (!send_midi_msg(MIDI_NOTE_ON, target_note, target_pressure))
					return;
				note[string_id] = target_note;
				note_on_pressure[string_id] = target_pressure;
				aftertouch[string_id] = 0;
				sent = true;
			}
		}
		// we define aftertouch as any pressure on top of the pressure when the note started
		u8 goal_aftertouch = maxi(target_pressure - note_on_pressure[string_id], 0);
		if (abs(goal_aftertouch - aftertouch[string_id]) > 4) {
			// poly aftertouch (only when pressure difference is larger than 4)
			if (!send_midi_msg(MIDI_POLY_KEY_PRESSURE, cur_note, goal_aftertouch))
				return;
			aftertouch[string_id] = goal_aftertouch;
			sent = true;
		}
		// voice position, CC32 - CC39
		u8 goal_position = clampi(127 - (synthf->pos / 13 - 16), 0, 127);
		if (abs(goal_position - position[string_id]) > 1 && pres_significant && pres_stable) {
			if (!send_midi_msg(MIDI_CONTROL_CHANGE, 32 + string_id, goal_position))
				return;
			position[string_id] = goal_position;
			sent = true;
		}
		// voice pressure, CC40 - CC47
		if (abs(target_pressure - pressure[string_id]) > 1) {
			if (!send_midi_msg(MIDI_CONTROL_CHANGE, 40 + string_id, target_pressure))
				return;
			pressure[string_id] = target_pressure;
			sent = true;
		}

		// jump to next voice
		string_id = (string_id + 1) & 7;

		// if we sent any midi message for this voice, send the midi buffer to the uart and exit the function
		// the next time we enter the function again, string_id will be set to the next voice
		if (sent) {
			midi_buffer_to_uart();
			return;
		}
	}
}

// === INPUT LOOP === //

// apply midi messages to plinky
static void process_midi_msg(u8 status, u8 d1, u8 d2) {
	static u8 cc14_lsb[NUM_14BIT_CCS];

	u8 chan = status & 0x0F; // save the channel
	u8 type = status & 0xF0; // take the channel out
	u8 midi_ch_in = clampi((mini(param_val_raw(P_MIDI_CH_IN, 0), PARAM_SIZE - 1) * 16) / PARAM_SIZE, 0, 15);

	// allow only selected channel and system msgs
	if ((chan != midi_ch_in) && (type != MIDI_SYSTEM_COMMON_MSG))
		return;

	// turn silent note ons into note offs
	if (type == MIDI_NOTE_ON && d2 == 0)
		type = MIDI_NOTE_OFF;

	switch (type) {
	case MIDI_PROGRAM_CHANGE:
		if (d1 < 32)
			load_preset(d1, false);
		break;
	// note related: to the strings!
	case MIDI_NOTE_OFF:
	case MIDI_NOTE_ON:
	case MIDI_POLY_KEY_PRESSURE:
		strings_rcv_midi(status, d1, d2);
		break;
	case MIDI_PITCH_BEND:
		midi_chan_pitchbend[chan] = (d1 + (d2 << 7)) - 0x2000;
		break;
	case MIDI_CHANNEL_PRESSURE:
		midi_chan_pressure[chan] = d1;
		break;
	case MIDI_CONTROL_CHANGE: {
		if (d1 >= NUM_14BIT_CCS && d1 < (2 * NUM_14BIT_CCS))
			cc14_lsb[d1 - 32] = d2;
		s8 param = (d1 < 128) ? midi_cc_table[d1] : -1;
		if (param >= 0) {
			u16 value = d2 << 7;
			if (d1 < 32)
				value += cc14_lsb[d1]; // full CC14
			set_param_from_cc(param, value);
		}
		break;
	}
	// out of the system messages we only implement the time-related ones => forward to clock
	case MIDI_SYSTEM_COMMON_MSG:
		clock_rcv_midi(status);
		break;
	}
}

// read bytes from *buf and put them into midi messages
static void midi_bytes_to_msg(const u8* buf, u8 len) {
	static u8 state = 0;
	static u8 msg[3] = {0};
	for (; len--;) {
		u8 data = *buf++;
		// status byte
		if (data & 0x80) {
			// real-time msg
			if ((data & 0xF8) == 0xF8) {
				// handle immediately
				process_midi_msg(data, 0, 0);
			}
			// channel mode msg
			else if ((data & 0xF0) == 0xF0) {
				// cancels running status, no further processing
				msg[0] = 0;
			}
			// channel voice msg, start new
			else {
				msg[0] = data;
				state = 1;
			}
		}
		// data byte
		else {
			// not gathering a channel voice msg, ignore
			if (msg[0] == 0) {
				continue;
			}
			// running status
			if (state == 3) {
				state = 1;
			}
			// save data
			msg[state++] = data;
			// program change and channel pressure only have one data byte
			if (state == 2 && ((msg[0] & 0xF0) == MIDI_PROGRAM_CHANGE || (msg[0] & 0xF0) == MIDI_CHANNEL_PRESSURE)) {
				process_midi_msg(msg[0], msg[1], 0);
			}
			// we received a full midi msg, process
			else if (state == 3) {
				process_midi_msg(msg[0], msg[1], msg[2]);
			}
		}
	}
}

// handles up to one buffer (16 bytes) of incoming serial midi data
static void process_serial_midi_in(void) {
	// pass the midi out buffer to the uart
	// rj: why is this in the midi in function?
	midi_buffer_to_uart();
	// get midi bytes from buffer and pass them to midi_bytes_to_msg()
	static u8 last_read_pos = 0;
	u8 read_pos = MIDI_BUFFER_SIZE - __HAL_DMA_GET_COUNTER(huart3.hdmarx);
	if (read_pos != last_read_pos) {
		if (read_pos > last_read_pos) {
			midi_bytes_to_msg(&midi_receive_buffer[last_read_pos], read_pos - last_read_pos);
		}
		else {
			midi_bytes_to_msg(&midi_receive_buffer[last_read_pos], MIDI_BUFFER_SIZE - last_read_pos);
			midi_bytes_to_msg(&midi_receive_buffer[0], read_pos);
		}
	}
	last_read_pos = read_pos;
}

static void process_usb_midi_in(void) {
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

void process_midi(void) {
	process_all_midi_out();
	pump_web_usb(true);
	process_serial_midi_in();
	process_usb_midi_in();
}

// == AUX == //

// panic
static void midi_panic(void) {
	memset(midi_chan_pressure, 0, sizeof(midi_chan_pressure));
	memset(midi_chan_pitchbend, 0, sizeof(midi_chan_pitchbend));
	strings_clear_midi();
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
	midi_panic();
	HAL_UART_Receive_DMA(&huart3, midi_receive_buffer, sizeof(midi_receive_buffer));
}