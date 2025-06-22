#pragma once
#include "synth/params.h"
#include "utils.h"

typedef enum MidiMessageType {
	// Channel Voice Messages
	MIDI_NOTE_OFF = 0x80,          // 1000 0000
	MIDI_NOTE_ON = 0x90,           // 1001 0000
	MIDI_POLY_KEY_PRESSURE = 0xA0, // 1010 0000
	MIDI_CONTROL_CHANGE = 0xB0,    // 1011 0000
	MIDI_PROGRAM_CHANGE = 0xC0,    // 1100 0000
	MIDI_CHANNEL_PRESSURE = 0xD0,  // 1101 0000
	MIDI_PITCH_BEND = 0xE0,        // 1110 0000

	// System Common Messages
	MIDI_SYSTEM_EXCLUSIVE = 0xF0, // 1111 0000
	MIDI_TIME_CODE = 0xF1,        // 1111 0001
	MIDI_SONG_POSITION = 0xF2,    // 1111 0010
	MIDI_SONG_SELECT = 0xF3,      // 1111 0011
	MIDI_TUNE_REQUEST = 0xF6,     // 1111 0110
	MIDI_END_OF_EXCLUSIVE = 0xF7, // 1111 0111

	// System Real-Time Messages
	MIDI_TIMING_CLOCK = 0xF8,   // 1111 1000
	MIDI_START = 0xFA,          // 1111 1010
	MIDI_CONTINUE = 0xFB,       // 1111 1011
	MIDI_STOP = 0xFC,           // 1111 1100
	MIDI_ACTIVE_SENSING = 0xFE, // 1111 1110
	MIDI_SYSTEM_RESET = 0xFF,   // 1111 1111

	// Section Starts
	MIDI_SYSTEM_COMMON_MSG = MIDI_SYSTEM_EXCLUSIVE,
	MIDI_SYSTEM_REAL_TIME_MSG = MIDI_TIMING_CLOCK,

	// Dummy
	MIDI_NONE = 0,

} MidiMessageType;

// clang-format off
static const s8 midi_cc_table[128] = {
	//					0			1			2			3			4			5			6			7
	/*   0 */			-1,			-1,			P_NOISE,	P_ENV_LVL1,		P_DISTORTION,	P_GLIDE,	-1,			P_SYNTH_LVL,
	/*   8 */			P_SYNTH_WET_DRY,P_PITCH,	-1,			P_GATE_LENGTH,P_DLY_TIME,	P_SHAPE,		P_INTERVAL,	P_SMP_SCRUB,
	/*  16 */			P_SMP_GRAINSIZE,P_SMP_SPEED,P_SMP_STRETCH,P_ENV_LVL2,P_ATTACK2,		P_DECAY2,		P_SUSTAIN2,		P_RELEASE2,
	/*  24 */			P_A_RATE,	P_A_DEPTH,	P_A_OFFSET,	P_B_RATE,	P_B_DEPTH,	P_B_OFFSET,	-1,			P_HPF,

	// CCs 32 through 63 reserved for 14-bit CCs, each of them representing the LSB of CC [number - 32]
	/*  32 */			-1,			-1,			-1,			-1,			-1,			-1,			-1,			-1,
	/*  40 */			-1,			-1,			-1,			-1,			-1,			-1,			-1,			-1,
	/*  48 */			-1,			-1,			-1,			-1,			-1,			-1,			-1,			-1,
	/*  56 */			-1,			-1,			-1,			-1,			-1,			-1,			-1,			-1,
	/*  64 */			-1,			-1,			-1,			-1,			-1,			-1,			-1,			P_RESO, // reso needs to be moved
	// End of 14-bit CC reserved numbers

	/*  72 */			P_RELEASE1,		P_ATTACK1,		P_SUSTAIN1,		P_DECAY1,		P_X_RATE,	P_X_DEPTH,	P_X_OFFSET,	P_Y_RATE,
	/*  80 */			P_Y_DEPTH,	P_Y_OFFSET,	P_SAMPLE,	P_PATTERN,	-1,			P_STEP_OFFSET,	-1,			-1,
	/*  88 */			-1,			P_INPUT_LVL,	P_INPUT_WET_DRY,P_RVB_SEND,	P_RVB_TIME,	P_RVB_SHIMMER,	P_DLY_SEND,	P_DLY_FEEDBACK,
	/*  96 */			-1,			-1,			-1,			-1,			-1,			P_LATCH_TOGGLE,			P_ARP_TOGGLE,	P_ARP_ORDER,
	/* 104 */			P_ARP_CLK_DIV,	P_ARP_CHANCE,	P_ARP_EUC_LEN,	P_ARP_OCTAVES,	P_SEQ_ORDER,	P_SEQ_CLK_DIV,	P_SEQ_CHANCE,	P_SEQ_EUC_LEN,
	/* 112 */			P_DLY_PINGPONG,	P_DLY_WOBBLE,	P_RVB_WOBBLE,	-1,			P_SMP_SCRUB_JIT,	P_SMP_GRAINSIZE_JIT, P_SMP_SPEED_JIT, P_SMP_UNUSED1,
	/* 120 */			-1,			-1,			-1,			-1,			-1,			-1,			-1,			-1,
};
// clang-format on