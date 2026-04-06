#pragma once

// == DIMENSIONS == //

// TOUCH

#define NUM_TOUCH_FRAMES 8
#define NUM_TOUCH_READINGS 18

#define TOUCH_MIN_POS 0
#define TOUCH_MAX_POS 2047

// full pressure is defined as the point where the current pressure reaches the calibrated pressure, this will result in
// envelope 1 fully opening - pressure values beyond this do occur, but will not affect the sound any further
#define TOUCH_MIN_PRES -2048
#define TOUCH_FULL_PRES 2047

// in ms
#define PRESS_DELAY 50
#define SHORT_PRESS_TIME 300
#define LONG_PRESS_TIME 1000
#define POST_PRESS_DELAY 150

// AUDIO
#define SAMPLE_RATE 31250
#define SAMPLES_PER_TICK 64
#define TICK_RATE (1.f * SAMPLE_RATE / SAMPLES_PER_TICK)
#define TICK_LENGTH_MS (1000.f * SAMPLES_PER_TICK / SAMPLE_RATE)
#define MAX_SAMPLE_LEN (1024 * 1024 * 2) // max sample length in samples

// SYNTH

#define BASE_FREQUENCY 8.175799f // C-1
#define NUM_NOTES 97             // C-1 - C7
#define MAX_PITCH SEMIS_TO_PITCH(NUM_NOTES - 1)

#define PITCH_PER_SEMI 512
#define PITCH_PER_OCT 6144

#define NUM_TOUCHSTRIPS 9
#define PADS_PER_STRIP 8

#define NUM_KNOBS 2
#define NUM_STRINGS 8

#define NUM_PRESETS 32
#define NUM_PATTERNS 24
#define NUM_SAMPLES 8
#define NO_SAMPLE NUM_SAMPLES

#define MAX_PTN_STEPS 64
#define PTN_STEPS_PER_QTR (MAX_PTN_STEPS / 4)
#define PTN_SUBSTEPS 8

#define NUM_VOICES 8
#define OSCS_PER_VOICE 4
#define NUM_GRAINS 32

#define NUM_LFOS 4
#define NUM_MIDI_CHANNELS 16

// PARAMS

#define RAW_SIZE 1024
#define RAW_HALF (RAW_SIZE / 2)
#define NUM_14BIT_CCS 32

// MEMORY

#define OG_PRESET_VERSION 2
#define LPE_PRESET_VERSION 16

#define OG_SYS_PARAMS_VERSION 0
#define REV_SYS_PARAMS_VERSION 15
#define LPE_SYS_PARAMS_VERSION 17

#define PATTERNS_START NUM_PRESETS
#define SAMPLES_START (PATTERNS_START + NUM_PATTERNS)

#define NUM_PTN_QUARTERS (NUM_PATTERNS * 4)
#define F_SAMPLES_START (PATTERNS_START + NUM_PTN_QUARTERS)
#define FLOAT_PRESET_ID (F_SAMPLES_START + NUM_SAMPLES)
#define FLOAT_PATTERN_ID (FLOAT_PRESET_ID + 1)
#define NUM_FLASH_ITEMS (FLOAT_PATTERN_ID + 4)

// TIME

#define MAX_BPM_10X 2400
#define MIN_BPM_10X 300
#define MAX_SWING 0.5f // 0.3333f represents triplet-feel swing
#define NUM_SYNC_DIVS 28
#define NUM_PPQN_VALUES 7
static u16 const sync_divs_32nds[NUM_SYNC_DIVS] = {1,  2,  3,  4,  5,   6,   8,   10,  12,  16,  20,  24,  32,  40,
                                                   48, 64, 80, 96, 128, 160, 192, 256, 320, 384, 512, 640, 768, 1024};
static u8 const ppqn_values[NUM_PPQN_VALUES] = {1, 2, 4, 8, 16, 24, 48};

// MIDI

#define NUM_BEND_RANGES 8
static u8 const bend_ranges[NUM_BEND_RANGES] = {1, 2, 7, 12, 14, 24, 48, 96};

// GRAPHICS

#define OLED_WIDTH 128
#define OLED_HEIGHT 32
#define NUM_ICONS 64

// == ENUMS == //

// MODULE ENUMS

// position of the ADC reading in the adc_buffer
typedef enum ADC_DAC_Index {
	ADC_PITCH,
	ADC_GATE,
	ADC_X_CV,
	ADC_Y_CV,
	ADC_A_CV,
	ADC_B_CV,
	ADC_B_KNOB = 6,
	ADC_A_KNOB = 7,
	DAC_PITCH_CV_LO,
	DAC_PITCH_CV_HI,
	NUM_ADC_DAC_ITEMS,
} ADC_DAC_Index;

// position of the ADC value in the adc_smoother array
typedef enum ADCSmoothIndex {
	ADC_S_A_CV = 0,
	ADC_S_B_CV = 1,
	ADC_S_X_CV = 2,
	ADC_S_Y_CV = 3,
	ADC_S_A_KNOB = 4,
	ADC_S_B_KNOB = 5,
	ADC_S_PITCH = 6,
	ADC_S_GATE = 7,
} ADCSmoothIndex;
// rj: can we get rid of this remapping?

typedef enum ClockType {
	CLK_INTERNAL,
	CLK_MIDI,
	CLK_CV,
} ClockType;

typedef enum FunctionPad {
	FN_NONE = -1,
	FN_SHIFT_A,
	FN_SHIFT_B,
	FN_LOAD,
	FN_LEFT,
	FN_RIGHT,
	FN_CLEAR,
	FN_RECORD,
	FN_PLAY,
} FunctionPad;

typedef enum SamplerMode {
	SM_PREVIEW,   // previewing a recorded sample
	SM_ERASING,   // clearing sample memory
	SM_PRE_ARMED, // ready to be armed (rec level can be adjusted here)
	SM_ARMED,     // armed for auto-recording when audio starts
	SM_RECORDING, // recording sample
	SM_STOPPING1, // we stop for 4 cycles to write 0s at the end
	SM_STOPPING2,
	SM_STOPPING3,
	SM_STOPPING4,
} SamplerMode;

typedef enum ArpOrder {
	ARP_UP,
	ARP_DOWN,
	ARP_UPDOWN,
	ARP_UPDOWN_REP,
	ARP_PEDAL_UP,
	ARP_PEDAL_DOWN,
	ARP_PEDAL_UPDOWN,
	ARP_SHUFFLE,
	ARP_SHUFFLE2,
	ARP_CHORD,
	ARP_UP8,
	ARP_DOWN8,
	ARP_UPDOWN8,
	ARP_SHUFFLE8,
	ARP_SHUFFLE28,
	NUM_ARP_ORDERS,
} ArpOrder;

typedef enum LfoShape {
	LFO_TRI,
	LFO_SIN,
	LFO_SMOOTH_RAND,
	LFO_STEP_RAND,
	LFO_BI_SQUARE,
	LFO_SQUARE,
	LFO_CASTLE,
	LFO_SAW,
	LFO_BI_TRIGS,
	LFO_TRIGS,
	LFO_ENV,
	NUM_LFO_SHAPES,
} LfoShape;

typedef enum SeqState {
	SEQ_IDLE,
	SEQ_PLAYING,
	SEQ_FINISHING_STEP,
	SEQ_PREVIEWING,
	SEQ_LIVE_RECORDING,
	SEQ_STEP_RECORDING,
} SeqState;

typedef enum SeqOrder {
	SEQ_ORD_PAUSE,
	SEQ_ORD_FWD,
	SEQ_ORD_BACK,
	SEQ_ORD_PINGPONG,
	SEQ_ORD_PINGPONG_REP,
	SEQ_ORD_SHUFFLE,
	NUM_SEQ_ORDERS,
} SeqOrder;

typedef enum CVQuantType {
	CVQ_OFF,
	CVQ_CHROMATIC,
	CVQ_SCALE,
	NUM_CV_QUANT_TYPES,
} CVQuantType;

typedef enum SysParam {
	SYS_PRESET_ID,
	SYS_MIDI_IN_CHAN,
	SYS_MIDI_OUT_CHAN,
	SYS_ACCEL_SENS,
	SYS_VOLUME,
	SYS_CV_QUANT,
	SYS_CV_PPQN_IN,
	SYS_CV_PPQN_OUT,
	SYS_REVERSE_ENCODER,
	SYS_PRESET_ALIGNED,
	SYS_PATTERN_ALIGNED,
	SYS_MIDI_IN_CLOCK_MULT,
	SYS_MIDI_IN_VEL_BALANCE,
	SYS_MIDI_OUT_VEL_BALANCE,
	SYS_MIDI_IN_PRES_TYPE,
	SYS_MIDI_OUT_PRES_TYPE,
	SYS_MIDI_OUT_YZ_CONTROL,
	SYS_MIDI_SOFT_THRU,
	SYS_MIDI_CHANNEL_BEND_RANGE_IN,
	SYS_MIDI_STRING_BEND_RANGE_IN,
	SYS_MIDI_STRING_BEND_RANGE_OUT,
	SYS_LOCAL_CTRL_OFF,
	SYS_MPE_IN,
	SYS_MPE_OUT,
	SYS_MPE_ZONE,
	SYS_MPE_CHANS,
	SYS_MIDI_IN_SCALE_QUANT,
	SYS_MIDI_IN_FILTER,
	SYS_MIDI_OUT_FILTER,
	SYS_MIDI_TRS_OUT_OFF,
	SYS_MIDI_TUNING,
	SYS_REFERENCE_PITCH,
	SYS_CV_GATE_IN_IS_PRESSURE,
	SYS_EDIT_POLY_PARAMS,
	NUM_SYS_PARAM_ITEMS,
} SysParam;

typedef enum MidiPressureType {
	MP_NONE,
	MP_CHANNEL_PRESSURE,
	MP_POLY_AFTERTOUCH,
	NUM_MIDI_PRESSURE_TYPES,
} MidiPressureType;

// PITCH

typedef enum Scale {
	S_MAJOR,
	S_MINOR,
	S_HARMMINOR,
	S_PENTA,
	S_PENTAMINOR,
	S_HIRAJOSHI,
	S_INSEN,
	S_IWATO,
	S_MINYO,

	S_FIFTHS,
	S_TRIADMAJOR,
	S_TRIADMINOR,

	S_DORIAN,
	S_PHYRGIAN,
	S_LYDIAN,
	S_MIXOLYDIAN,
	S_AEOLIAN,
	S_LOCRIAN,

	S_BLUESMINOR,
	S_BLUESMAJOR,

	S_ROMANIAN,
	S_WHOLETONE,

	S_HARMONICS,
	S_HEXANY,
	S_JUST,

	S_CHROMATIC,
	S_DIMINISHED,
	NUM_SCALES,
} Scale;

// PARAMS

typedef enum ModSource {
	SRC_BASE,
	SRC_ENV2,
	SRC_PRES,
	SRC_LFO_A,
	SRC_LFO_B,
	SRC_LFO_X,
	SRC_LFO_Y,
	SRC_RND,
	NUM_MOD_SOURCES,
} ModSource;

typedef enum PresetCategory {
	CAT_BLANK,
	CAT_BASS,
	CAT_LEADS,
	CAT_PADS,
	CAT_ARPS,
	CAT_PLINKS,
	CAT_PLONKS,
	CAT_BEEPS,
	CAT_BOOPS,
	CAT_SFX,
	CAT_LINEIN,
	CAT_SAMPLER,
	CAT_DONK,
	CAT_JOLLY,
	CAT_SADNESS,
	CAT_WILD,
	CAT_GNARLY,
	CAT_WEIRD,
	NUM_PST_CATS
} PresetCategory;

typedef enum ParamRow {
	R_SOUND1,
	R_SOUND2,
	R_ENV1,
	R_ENV2,
	R_DLY,
	R_RVB,
	R_ARP,
	R_SEQ,
	R_SMP1,
	R_SMP2,
	R_A,
	R_B,
	R_X,
	R_Y,
	R_MIX1,
	R_MIX2,
	R_NUM_ROWS,
} ParamRow;

// clang-format off

typedef enum Param {
    P_SHAPE = R_SOUND1 * 6,     P_DISTORTION,   P_PITCH,        P_OCT,          P_GLIDE,        P_INTERVAL,      // Sound 1
	P_NOISE = R_SOUND2 * 6,     P_RESO,         P_DEGREE,       P_SCALE,        P_MICROTONE,    P_COLUMN,        // Sound 2
	P_ENV_LVL1 = R_ENV1 * 6,    P_ATTACK1,      P_DECAY1,       P_SUSTAIN1,     P_RELEASE1,     P_ROOT,   		// Envelope 1
	P_ENV_LVL2 = R_ENV2 * 6,    P_ATTACK2,      P_DECAY2,       P_SUSTAIN2,     P_RELEASE2,     P_ENV2_UNUSED,   // Envelope 2
	P_DLY_SEND = R_DLY * 6,     P_DLY_TIME,     P_PING_PONG,	P_DLY_WOBBLE,	P_DLY_FEEDBACK,	P_TEMPO,        // Delay
	P_RVB_SEND = R_RVB * 6,     P_RVB_TIME,     P_SHIMMER,	    P_RVB_WOBBLE,	P_RVB_UNUSED,	P_SWING,        // Reverb
	P_ARP_TGL = R_ARP * 6,      P_ARP_ORDER,    P_ARP_CLK_DIV,  P_ARP_CHANCE,	P_ARP_EUC_LEN,	P_ARP_OCTAVES,  // Arp
	P_LATCH_TGL = R_SEQ * 6,    P_SEQ_ORDER,    P_SEQ_CLK_DIV,  P_SEQ_CHANCE,	P_SEQ_EUC_LEN,	P_GATE_LENGTH,  // Sequencer
	P_SCRUB = R_SMP1 * 6,       P_GR_SIZE,      P_PLAY_SPD,	    P_SMP_STRETCH,	P_SAMPLE,	    P_PATTERN,      // Sampler 1
	P_SCRUB_JIT = R_SMP2 * 6,   P_GR_SIZE_JIT,  P_PLAY_SPD_JIT,	P_SMP_UNUSED1,	P_SMP_UNUSED2,	P_STEP_OFFSET,  // Sampler 2
	P_A_SCALE = R_A * 6,        P_A_OFFSET,     P_A_DEPTH,      P_A_RATE,	    P_A_SHAPE,	    P_A_SYM,        // LFO A
	P_B_SCALE = R_B * 6,        P_B_OFFSET,     P_B_DEPTH,      P_B_RATE,	    P_B_SHAPE,	    P_B_SYM,        // LFO B
	P_X_SCALE = R_X * 6,        P_X_OFFSET,     P_X_DEPTH,      P_X_RATE,	    P_X_SHAPE,	    P_X_SYM,        // LFO X
	P_Y_SCALE = R_Y * 6,        P_Y_OFFSET,     P_Y_DEPTH,      P_Y_RATE,	    P_Y_SHAPE,	    P_Y_SYM,        // LFO Y
	P_SYN_LVL = R_MIX1 * 6,     P_SYN_WET_DRY,  P_HPF,          P_MIX_UNUSED1,	P_MIX_UNUSED4,	P_VOLUME,       // Mixer 1
	P_IN_LVL = R_MIX2 * 6,      P_IN_WET_DRY,   P_SYS_UNUSED1,  P_MIX_UNUSED2,	P_MIX_UNUSED3,	P_MIX_WIDTH,    // Mixer 2

    NUM_PARAMS = R_NUM_ROWS * 6,
} Param;

// clang-format on

// MEMORY

typedef enum FlashCalibType {
	FLASH_CALIB_NONE = 0b00,
	FLASH_CALIB_TOUCH = 0b01,
	FLASH_CALIB_ADC_DAC = 0b10,
	FLASH_CALIB_COMPLETE = 0b11,
} FlashCalibType;

typedef enum MemSegment {
	SEG_PRESET,
	SEG_PAT0,
	SEG_PAT1,
	SEG_PAT2,
	SEG_PAT3,
	SEG_SYS_PARAMS,
	SEG_SAMPLE_INFO,
	NUM_MEM_SEGMENTS
} MemSegment;

// MIDI

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

	// Section Start
	MIDI_SYSTEM_COMMON_MSG = 0xF0,

	// Mask
	MIDI_STATUS_BYTE_MASK = 0x80,
	MIDI_TYPE_MASK = 0xF0,
	MIDI_CHANNEL_MASK = 0x0F,
	MIDI_REAL_TIME_MASK = 0xF8,

	// Dummy
	MIDI_NONE = 0,

} MidiMessageType;

// list of reserved midi CCs:
//
// 0 - bank select
// 1 - mod wheel
// 6 - (N)RPN value msb
// 10 - pan
// 32 through 63 - LSB of 14 bit CCs 0 through 31
// 64 - sustain pedal
// 96 - data increment
// 97 - data decrement
// 98 - NRPN LSB
// 99 - NRPN MSB
// 100 - RPN LSB
// 101 - RPN MSB
//
// available midi CCs: 30, 65-68, 70, 84, 86, 87, 88, 115

typedef enum MidiCC {
	CC_MOD_WHEEL = 1,
	CC_DATA_MSB = 6,
	CC_MOD_WHEEL_LSB = 33,
	CC_DATA_LSB = 38,
	CC_SUSTAIN = 64,
	CC_DATA_INC = 96,
	CC_DATA_DEC = 97,
	CC_NRPN_LSB = 98,
	CC_NRPN_MSB = 99,
	CC_RPN_LSB = 100,
	CC_RPN_MSB = 101,
	CC_ALL_SOUNDS_OFF = 120,
	CC_RESET_ALL_CTR = 121,
	CC_LOCAL_CONTROL = 122,
	CC_ALL_NOTES_OFF = 123,
} MidiCC;

// clang-format off
 const static Param midi_cc_table[128] = {
	//			0				1				2				3				4				5				6				7	
	/*   0 */	NUM_PARAMS,		NUM_PARAMS, 	P_NOISE,		P_ENV_LVL1,		P_DISTORTION,	P_GLIDE,		NUM_PARAMS,		P_SYN_LVL,
	/*   8 */	P_SYN_WET_DRY,	P_PITCH,		NUM_PARAMS,		P_GATE_LENGTH,	P_DLY_TIME,		P_SHAPE,		P_INTERVAL,		P_SCRUB,
	/*  16 */	P_GR_SIZE,		P_PLAY_SPD,		P_SMP_STRETCH,	P_ENV_LVL2,		P_ATTACK2,		P_DECAY2,		P_SUSTAIN2,		P_RELEASE2,
	/*  24 */	P_A_RATE,		P_A_DEPTH,		P_A_OFFSET,		P_B_RATE,		P_B_DEPTH,		P_B_OFFSET,		NUM_PARAMS,		P_HPF,

	/*  32 */	NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,
	/*  40 */	NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,
	/*  48 */	NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,
	/*  56 */	NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,

	/*  64 */	NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		P_LATCH_TGL,	NUM_PARAMS,		P_RESO,
	/*  72 */	P_RELEASE1,		P_ATTACK1,		P_SUSTAIN1,		P_DECAY1,		P_X_RATE,		P_X_DEPTH,		P_X_OFFSET,		P_Y_RATE,
	/*  80 */	P_Y_DEPTH,		P_Y_OFFSET,		P_SAMPLE,		P_PATTERN,		NUM_PARAMS,		P_STEP_OFFSET,	NUM_PARAMS,		NUM_PARAMS,
	/*  88 */	NUM_PARAMS,		P_IN_LVL,	    P_IN_WET_DRY,	P_RVB_SEND,		P_RVB_TIME,		P_SHIMMER,		P_DLY_SEND,		P_DLY_FEEDBACK,
	/*  96 */	NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		P_ARP_TGL,		P_ARP_ORDER,
	/* 104 */	P_ARP_CLK_DIV,	P_ARP_CHANCE,	P_ARP_EUC_LEN,	P_ARP_OCTAVES,	P_SEQ_ORDER,	P_SEQ_CLK_DIV,	P_SEQ_CHANCE,	P_SEQ_EUC_LEN,
	/* 112 */	P_PING_PONG,	P_DLY_WOBBLE,	P_RVB_WOBBLE,	NUM_PARAMS,		P_SCRUB_JIT,	P_GR_SIZE_JIT, 	P_PLAY_SPD_JIT, P_SMP_UNUSED1,
	/* 120 */	NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,
};

// reverse lookup table
const static u8 midi_cc_table_rvs[NUM_PARAMS] = {
	[P_SHAPE] = 13,       [P_DISTORTION] = 4,    [P_PITCH] = 9,          [P_OCT] = 255,          [P_GLIDE] = 5,          [P_INTERVAL] = 14,       // Sound 1
	[P_NOISE] = 2,        [P_RESO] = 71,         [P_DEGREE] = 255,       [P_SCALE] = 255,        [P_MICROTONE] = 255,    [P_COLUMN] = 255,        // Sound 2
	[P_ENV_LVL1] = 3,     [P_ATTACK1] = 73,      [P_DECAY1] = 75,        [P_SUSTAIN1] = 74,      [P_RELEASE1] = 72,      [P_ROOT] = 255,   		  // Envelope 1
	[P_ENV_LVL2] = 19,    [P_ATTACK2] = 20,      [P_DECAY2] = 21,        [P_SUSTAIN2] = 22,      [P_RELEASE2] = 23,      [P_ENV2_UNUSED] = 255,   // Envelope 2
	[P_DLY_SEND] = 94,    [P_DLY_TIME] = 12,     [P_PING_PONG] = 112,    [P_DLY_WOBBLE] = 113,   [P_DLY_FEEDBACK] = 95,  [P_TEMPO] = 255,         // Delay
	[P_RVB_SEND] = 91,    [P_RVB_TIME] = 92,     [P_SHIMMER] = 93,       [P_RVB_WOBBLE] = 114,   [P_RVB_UNUSED] = 255,   [P_SWING] = 255,         // Reverb
	[P_ARP_TGL] = 102,    [P_ARP_ORDER] = 103,   [P_ARP_CLK_DIV] = 104,  [P_ARP_CHANCE] = 105,   [P_ARP_EUC_LEN] = 106,  [P_ARP_OCTAVES] = 107,   // Arp
	[P_LATCH_TGL] = 101,  [P_SEQ_ORDER] = 108,   [P_SEQ_CLK_DIV] = 109,  [P_SEQ_CHANCE] = 110,   [P_SEQ_EUC_LEN] = 111,  [P_GATE_LENGTH] = 11,    // Sequencer
	[P_SCRUB] = 15,       [P_GR_SIZE] = 16,      [P_PLAY_SPD] = 17,      [P_SMP_STRETCH] = 18,   [P_SAMPLE] = 82,        [P_PATTERN] = 83,        // Sampler 1
	[P_SCRUB_JIT] = 116,  [P_GR_SIZE_JIT] = 117, [P_PLAY_SPD_JIT] = 118, [P_SMP_UNUSED1] = 119,  [P_SMP_UNUSED2] = 255,  [P_STEP_OFFSET] = 85,    // Sampler 2
	[P_A_SCALE] = 255,    [P_A_OFFSET] = 26,     [P_A_DEPTH] = 25,       [P_A_RATE] = 24,        [P_A_SHAPE] = 255,      [P_A_SYM] = 255,         // LFO A
	[P_B_SCALE] = 255,    [P_B_OFFSET] = 29,     [P_B_DEPTH] = 28,       [P_B_RATE] = 27,        [P_B_SHAPE] = 255,      [P_B_SYM] = 255,         // LFO B
	[P_X_SCALE] = 255,    [P_X_OFFSET] = 78,     [P_X_DEPTH] = 77,       [P_X_RATE] = 76,        [P_X_SHAPE] = 255,      [P_X_SYM] = 255,         // LFO X
	[P_Y_SCALE] = 255,    [P_Y_OFFSET] = 81,     [P_Y_DEPTH] = 80,       [P_Y_RATE] = 79,        [P_Y_SHAPE] = 255,      [P_Y_SYM] = 255,         // LFO Y
	[P_SYN_LVL] = 7,      [P_SYN_WET_DRY] = 8,   [P_HPF] = 31,           [P_MIX_UNUSED1] = 255,  [P_MIX_UNUSED4] = 255,  [P_VOLUME] = 255,        // Mixer 1
	[P_IN_LVL] = 89,      [P_IN_WET_DRY] = 90,   [P_SYS_UNUSED1] = 255,  [P_MIX_UNUSED2] = 255,  [P_MIX_UNUSED3] = 255,  [P_MIX_WIDTH] = 255,     // Mixer 2
};

const static u8 midi_nrpn_table[NUM_PARAMS] = {
	//			0				1				2				3				4				5
	/*   0 */	P_SHAPE,        P_DISTORTION,   P_PITCH,        P_OCT,          P_GLIDE,        P_INTERVAL,      // Sound 1
	/*   6 */	P_NOISE,        P_RESO,         P_DEGREE,       P_SCALE,        P_MICROTONE,    P_COLUMN,        // Sound 2
	/*  12 */	P_ENV_LVL1,     P_ATTACK1,      P_DECAY1,       P_SUSTAIN1,     P_RELEASE1,     P_ROOT,   		 // Envelope 1
	/*  18 */	P_ENV_LVL2,     P_ATTACK2,      P_DECAY2,       P_SUSTAIN2,     P_RELEASE2,     P_ENV2_UNUSED,   // Envelope 2
	/*  24 */	P_DLY_SEND,     P_DLY_TIME,     P_PING_PONG,    P_DLY_WOBBLE,   P_DLY_FEEDBACK, P_TEMPO,         // Delay
	/*  30 */	P_RVB_SEND,     P_RVB_TIME,     P_SHIMMER,      P_RVB_WOBBLE,   P_RVB_UNUSED,   P_SWING,         // Reverb
	/*  36 */	P_ARP_TGL,      P_ARP_ORDER,    P_ARP_CLK_DIV,  P_ARP_CHANCE,   P_ARP_EUC_LEN,  P_ARP_OCTAVES,   // Arp
	/*  42 */	P_LATCH_TGL,    P_SEQ_ORDER,    P_SEQ_CLK_DIV,  P_SEQ_CHANCE,   P_SEQ_EUC_LEN,  P_GATE_LENGTH,   // Sequencer
	/*  48 */	P_SCRUB,        P_GR_SIZE,      P_PLAY_SPD,     P_SMP_STRETCH,  P_SAMPLE,       P_PATTERN,       // Sampler 1
	/*  54 */	P_SCRUB_JIT,    P_GR_SIZE_JIT,  P_PLAY_SPD_JIT, P_SMP_UNUSED1,  P_SMP_UNUSED2,  P_STEP_OFFSET,   // Sampler 2
	/*  60 */	P_A_SCALE,      P_A_OFFSET,     P_A_DEPTH,      P_A_RATE,       P_A_SHAPE,      P_A_SYM,         // LFO A
	/*  66 */	P_B_SCALE,      P_B_OFFSET,     P_B_DEPTH,      P_B_RATE,       P_B_SHAPE,      P_B_SYM,         // LFO B
	/*  72 */	P_X_SCALE,      P_X_OFFSET,     P_X_DEPTH,      P_X_RATE,       P_X_SHAPE,      P_X_SYM,         // LFO X
	/*  78 */	P_Y_SCALE,      P_Y_OFFSET,     P_Y_DEPTH,      P_Y_RATE,       P_Y_SHAPE,      P_Y_SYM,         // LFO Y
	/*  84 */	P_SYN_LVL,      P_SYN_WET_DRY,  P_HPF,          P_MIX_UNUSED1,  P_MIX_UNUSED4,  P_VOLUME,        // Mixer 1
	/*  90 */	P_IN_LVL,       P_IN_WET_DRY,   P_SYS_UNUSED1,  P_MIX_UNUSED2,  P_MIX_UNUSED3,  P_MIX_WIDTH,     // Mixer 2
};

typedef enum PolyParam {
    PP_SHAPE,         PP_DISTORTION,   PP_PITCH,         PP_OCT,             PP_GLIDE,         PP_INTERVAL,	// Sound 1
    PP_NOISE,         PP_RESO,         PP_DEGREE,        PP_SCALE,           PP_MICROTONE,     PP_COLUMN,	// Sound 2
    PP_ENV_LVL1,      PP_ATTACK1,      PP_DECAY1,        PP_SUSTAIN1,        PP_RELEASE1,	   PP_ROOT,		// Envelope 1
    PP_ENV_LVL2,      PP_ATTACK2,      PP_DECAY2,        PP_SUSTAIN2,        PP_RELEASE2,                	// Envelope 2
    PP_SCRUB,         PP_GR_SIZE,      PP_PLAY_SPD,      PP_SMP_STRETCH,                                 	// Sampler 1
    PP_SCRUB_JIT,     PP_GR_SIZE_JIT,  PP_PLAY_SPD_JIT,                                                 	// Sampler 2

    NUM_POLY_PARAMS,
} PolyParam;

const static Param param_from_poly_param[NUM_POLY_PARAMS] = {
    [PP_SHAPE] = P_SHAPE,           [PP_DISTORTION] = P_DISTORTION,     [PP_PITCH] = P_PITCH,           [PP_OCT] = P_OCT,               [PP_GLIDE] = P_GLIDE,           [PP_INTERVAL] = P_INTERVAL,	// Sound 1
    [PP_NOISE] = P_NOISE,           [PP_RESO] = P_RESO,                 [PP_DEGREE] = P_DEGREE,         [PP_SCALE] = P_SCALE,           [PP_MICROTONE] = P_MICROTONE,   [PP_COLUMN] = P_COLUMN,   	// Sound 2
    [PP_ENV_LVL1] = P_ENV_LVL1,     [PP_ATTACK1] = P_ATTACK1,           [PP_DECAY1] = P_DECAY1,         [PP_SUSTAIN1] = P_SUSTAIN1,     [PP_RELEASE1] = P_RELEASE1,     [PP_ROOT] = P_ROOT,         // Envelope 1
    [PP_ENV_LVL2] = P_ENV_LVL2,     [PP_ATTACK2] = P_ATTACK2,           [PP_DECAY2] = P_DECAY2,         [PP_SUSTAIN2] = P_SUSTAIN2,     [PP_RELEASE2] = P_RELEASE2,                               	// Envelope 2
    [PP_SCRUB] = P_SCRUB,           [PP_GR_SIZE] = P_GR_SIZE,           [PP_PLAY_SPD] = P_PLAY_SPD,     [PP_SMP_STRETCH] = P_SMP_STRETCH,                                                          	// Sampler 1
    [PP_SCRUB_JIT] = P_SCRUB_JIT,   [PP_GR_SIZE_JIT] = P_GR_SIZE_JIT,   [PP_PLAY_SPD_JIT] = P_PLAY_SPD_JIT,                                                                                       	// Sampler 2
};

const static PolyParam poly_param_from_param[NUM_PARAMS] = {
    [P_SHAPE] = PP_SHAPE,           [P_DISTORTION] = PP_DISTORTION,     [P_PITCH] = PP_PITCH,           [P_OCT] = PP_OCT,               [P_GLIDE] = PP_GLIDE,           [P_INTERVAL] = PP_INTERVAL,	// Sound 1
    [P_NOISE] = PP_NOISE,           [P_RESO] = PP_RESO,                 [P_DEGREE] = PP_DEGREE,         [P_SCALE] = PP_SCALE,           [P_MICROTONE] = PP_MICROTONE,   [P_COLUMN] = PP_COLUMN,   	// Sound 2
    [P_ENV_LVL1] = PP_ENV_LVL1,     [P_ATTACK1] = PP_ATTACK1,           [P_DECAY1] = PP_DECAY1,         [P_SUSTAIN1] = PP_SUSTAIN1,     [P_RELEASE1] = PP_RELEASE1,     [P_ROOT] = PP_ROOT,         // Envelope 1
    [P_ENV_LVL2] = PP_ENV_LVL2,     [P_ATTACK2] = PP_ATTACK2,           [P_DECAY2] = PP_DECAY2,         [P_SUSTAIN2] = PP_SUSTAIN2,     [P_RELEASE2] = PP_RELEASE2,                               	// Envelope 2
    [P_SCRUB] = PP_SCRUB,           [P_GR_SIZE] = PP_GR_SIZE,           [P_PLAY_SPD] = PP_PLAY_SPD,     [P_SMP_STRETCH] = PP_SMP_STRETCH,                                                          	// Sampler 1
    [P_SCRUB_JIT] = PP_SCRUB_JIT,   [P_GR_SIZE_JIT] = PP_GR_SIZE_JIT,   [P_PLAY_SPD_JIT] = PP_PLAY_SPD_JIT,    
};

// clang-format on

// == GRAPHICS == //

typedef enum Font {
	F_8 = 0,
	F_12,
	F_16,
	F_16_BOLD,
	F_20_BOLD,
	NUM_FONTS,
} Font;

#define I_KNOB "\x80"
#define I_SEND "\x81"
#define I_TOUCH "\x82"
#define I_DISTORT "\x83"
#define I_ADSR_A "\x84"
#define I_ADSR_D "\x85"
#define I_ADSR_S "\x86"
#define I_ADSR_R "\x87"
#define I_SLIDERS "\x88"
#define I_FORK "\x89"
#define I_PIANO "\x8a"
#define I_NOTES "\x8b"
#define I_DELAY "\x8c"
#define I_REVERB "\x8d"
#define I_SEQ "\x8e"
#define I_RANDOM "\x8f"
#define I_AB "\x90"
#define I_A "\x91"
#define I_B "\x92"
#define I_ALFO "\x93"
#define I_BLFO "\x94"
#define I_XY "\x95"
#define I_X "\x96"
#define I_Y "\x97"
#define I_XLFO "\x98"
#define I_YLFO "\x99"
#define I_REWIND "\x9a"
#define I_PLAY "\x9b"
#define I_RECORD "\x9c"
#define I_LEFT "\x9d"
#define I_RIGHT "\x9e"
#define I_PREV "\x9f"
#define I_NEXT "\xa0"
#define I_CROSS "\xa1"
#define I_PRESET "\xa2"
#define I_ORDER "\xa3"
#define I_WAVE "\xa4"
#define I_MICRO "\xa5"
#define I_LENGTH "\xa6"
#define I_TIME "\xa7"
#define I_FEEDBACK "\xa8"
#define I_TIMES "\xa9"
#define I_OFFSET "\xaa"
#define I_INTERVAL "\xab"
#define I_PERIOD "\xac"
#define I_AMPLITUDE "\xad"
#define I_WARP "\xae"
#define I_SHAPE "\xaf"
#define I_TILT "\xb0"
#define I_GLIDE "\xb1"
#define I_COLOR "\xb2"
#define I_FM "\xb3"
#define I_OCTAVE "\xb4"
#define I_HPF "\xb5"
#define I_DIVIDE "\xb6"
#define I_PERCENT "\xb7"
#define I_TEMPO "\xb8"
#define I_PHONES "\xb9"
#define I_JACK "\xba"
#define I_ENV "\xbb"
#define I_LOAD "\xbc"
#define I_SAVE "\xbd"
#define I_UP "\xbe"
#define I_DOWN "\xbf"

// == NAMES == //

const static char* const param_row_name[R_NUM_ROWS] = {

    [R_SOUND1] = I_SLIDERS "Sound", [R_SOUND2] = I_SLIDERS "Sound", [R_ENV1] = I_ENV "Env 1",
    [R_ENV2] = I_ENV "Env 2",       [R_ARP] = I_NOTES "Arp",        [R_SEQ] = I_NOTES "Seq",
    [R_DLY] = I_DELAY "Delay",      [R_RVB] = I_REVERB "Reverb",    [R_A] = I_ALFO "LFO",
    [R_B] = I_BLFO "LFO",           [R_X] = I_XLFO "LFO",           [R_Y] = I_YLFO "LFO",
    [R_SMP1] = I_WAVE "Sample",     [R_SMP2] = I_WAVE "Sample",     [R_MIX1] = I_SLIDERS "Mixer",
    [R_MIX2] = I_SLIDERS "Mixer"

};

// clang-format off

const static char* const param_name[NUM_PARAMS] = {
   [P_SHAPE] = I_SHAPE "WTable Pos",      	[P_DISTORTION] = I_DISTORT "Distortion",   	[P_PITCH] = I_PIANO "Pitch",         		[P_OCT] = I_OCTAVE "Octave",         	[P_GLIDE] = I_GLIDE "Glide",         		[P_INTERVAL] = I_OFFSET "Interval",			// Sound 1
   [P_NOISE] = I_WAVE "Noise",       		[P_RESO] = I_DISTORT "Resonance",         	[P_DEGREE] = I_OFFSET "Degree",       		[P_SCALE] = I_PIANO "Scale",        	[P_MICROTONE] = I_MICRO "Microtone",     	[P_COLUMN] = I_OFFSET "Column",				// Sound 2
   [P_ENV_LVL1] = I_TOUCH "Sens",			[P_ATTACK1] = I_ADSR_A "Attack",      		[P_DECAY1] = I_ADSR_D "Decay",        		[P_SUSTAIN1] = I_ADSR_S "Sustain",    	[P_RELEASE1] = I_ADSR_R "Release",      	[P_ROOT] = I_PIANO "Root",   				// Envelope 1
   [P_ENV_LVL2] = I_AMPLITUDE "Level",  	[P_ATTACK2] = I_ADSR_A "Attack",      		[P_DECAY2] = I_ADSR_D "Decay",        		[P_SUSTAIN2] = I_ADSR_S "Sustain",    	[P_RELEASE2] = I_ADSR_R "Release",      	[P_ENV2_UNUSED] = I_CROSS "<unused>",   	// Envelope 2
   [P_DLY_SEND] = I_SEND "Send",    		[P_DLY_TIME] = I_TEMPO "Clock Div",     	[P_PING_PONG] = I_TILT "2nd Tap",     		[P_DLY_WOBBLE] = I_WAVE "Wobble",  		[P_DLY_FEEDBACK] = I_FEEDBACK "Feedback",	[P_TEMPO] = I_PLAY "Tempo",         		// Delay
   [P_RVB_SEND] = I_SEND "Send",    		[P_RVB_TIME] = I_TIME "Time",     			[P_SHIMMER] = I_FEEDBACK "Shimmer",     	[P_RVB_WOBBLE] = I_WAVE "Wobble",  		[P_RVB_UNUSED] = I_CROSS "<unused>",    	[P_SWING] = I_TILT "Swing 8th",         	// Reverb
   [P_ARP_TGL] = I_PLAY "Enable",     		[P_ARP_ORDER] = I_ORDER "Order",    		[P_ARP_CLK_DIV] = I_TEMPO "Clock Div",   	[P_ARP_CHANCE] = I_PERCENT "Chance (S)",[P_ARP_EUC_LEN] = I_LENGTH "Euclid Len",   	[P_ARP_OCTAVES] = I_OCTAVE "Octaves",   	// Arp
   [P_LATCH_TGL] = I_PLAY "Enable",   		[P_SEQ_ORDER] = I_ORDER "Order",    		[P_SEQ_CLK_DIV] = I_TEMPO "Clock Div",   	[P_SEQ_CHANCE] = I_PERCENT "Chance (S)",[P_SEQ_EUC_LEN] = I_LENGTH "Euclid Len",   	[P_GATE_LENGTH] = I_INTERVAL "Gate Len",	// Sequencer
   [P_SCRUB] = I_RIGHT "Scrub",       		[P_GR_SIZE] = I_PERIOD "Grain Size",      	[P_PLAY_SPD] = I_RIGHT "Play Spd",      	[P_SMP_STRETCH] = I_TIME "Stretch", 	[P_SAMPLE] = I_SEQ "ID",        			[P_PATTERN] = I_SEQ "Pattern ID",      		// Sampler 1
   [P_SCRUB_JIT] = I_RIGHT "Scrub Jit",		[P_GR_SIZE_JIT] = I_PERIOD "Size Jit",		[P_PLAY_SPD_JIT] = I_RIGHT "Spd Jit",		[P_SMP_UNUSED1] = I_CROSS "<unused>", 	[P_SMP_UNUSED2] = I_CROSS "<unused>",   	[P_STEP_OFFSET] = I_OFFSET "Step Ofs",   	// Sampler 2
   [P_A_SCALE] = I_AMPLITUDE "CV Depth",    [P_A_OFFSET] = I_OFFSET "Offset",     		[P_A_DEPTH] = I_AMPLITUDE "Depth",			[P_A_RATE] = I_TEMPO "Clock Div",		[P_A_SHAPE] = I_SHAPE "Shape",       		[P_A_SYM] = I_WARP "Symmetry",         		// LFO A
   [P_B_SCALE] = I_AMPLITUDE "CV Depth",    [P_B_OFFSET] = I_OFFSET "Offset",     		[P_B_DEPTH] = I_AMPLITUDE "Depth",			[P_B_RATE] = I_TEMPO "Clock Div",      	[P_B_SHAPE] = I_SHAPE "Shape",       		[P_B_SYM] = I_WARP "Symmetry",         		// LFO B
   [P_X_SCALE] = I_AMPLITUDE "CV Depth",    [P_X_OFFSET] = I_OFFSET "Offset",     		[P_X_DEPTH] = I_AMPLITUDE "Depth",			[P_X_RATE] = I_TEMPO "Clock Div",      	[P_X_SHAPE] = I_SHAPE "Shape",       		[P_X_SYM] = I_WARP "Symmetry",         		// LFO X
   [P_Y_SCALE] = I_AMPLITUDE "CV Depth",    [P_Y_OFFSET] = I_OFFSET "Offset",     		[P_Y_DEPTH] = I_AMPLITUDE "Depth",			[P_Y_RATE] = I_TEMPO "Clock Div",      	[P_Y_SHAPE] = I_SHAPE "Shape",       		[P_Y_SYM] = I_WARP "Symmetry",         		// LFO Y
   [P_SYN_LVL] = I_WAVE "Synth Lvl",    	[P_SYN_WET_DRY] = I_REVERB "Wet/Dry",		[P_HPF] = I_HPF "High Pass",           		[P_MIX_UNUSED1] = I_CROSS "<unused>",  	[P_MIX_UNUSED4] = I_CROSS "<unused>",       [P_VOLUME] = I_PHONES "Volume",        		// Mixer 1
   [P_IN_LVL] = I_JACK "Input Lvl",     	[P_IN_WET_DRY] = I_JACK "In Wet/Dry",   	[P_SYS_UNUSED1] = I_CROSS "<unused>",   	[P_MIX_UNUSED2] = I_CROSS "<unused>",	[P_MIX_UNUSED3] = I_CROSS "<unused>",		[P_MIX_WIDTH] = I_PHONES "Width",			// Mixer 2
};

// clang-format on

const static char* const mod_src_name[NUM_MOD_SOURCES] = {
    [SRC_BASE] = I_SLIDERS "Base", [SRC_ENV2] = I_ENV "Env 2 >>",  [SRC_PRES] = I_TOUCH "Pres >>",
    [SRC_LFO_A] = I_A "Mod A >>",  [SRC_LFO_B] = I_B "Mod B >>",   [SRC_LFO_X] = I_X "Mod X >>",
    [SRC_LFO_Y] = I_Y "Mod Y >>",  [SRC_RND] = I_RANDOM "Rand >>",
};

const static char* const arp_mode_name[NUM_ARP_ORDERS] = {
    [ARP_UP] = "Up",
    [ARP_DOWN] = "Down",
    [ARP_UPDOWN] = "Up/Down",
    [ARP_UPDOWN_REP] = "Up/Down\nRepeat",
    [ARP_PEDAL_UP] = "Up\nPedal",
    [ARP_PEDAL_DOWN] = "Down\nPedal",
    [ARP_PEDAL_UPDOWN] = "Up/Down\nPedal",
    [ARP_SHUFFLE] = "Shuffle",
    [ARP_SHUFFLE2] = "Shuffle 2x",
    [ARP_CHORD] = "Chord",
    [ARP_UP8] = "Up\n8 Steps",
    [ARP_DOWN8] = "Down\n8 Steps",
    [ARP_UPDOWN8] = "Up/Down\n8 Steps",
    [ARP_SHUFFLE8] = "Shuffle\n8 Steps",
    [ARP_SHUFFLE28] = "Shuffle 2x\n8 Steps",
};

const static char* const seq_mode_name[NUM_SEQ_ORDERS] = {
    [SEQ_ORD_PAUSE] = "Pause",
    [SEQ_ORD_FWD] = "Forward",
    [SEQ_ORD_BACK] = "Reverse",
    [SEQ_ORD_PINGPONG] = "Ping Pong",
    [SEQ_ORD_PINGPONG_REP] = "Ping Pong\nRepeat",
    [SEQ_ORD_SHUFFLE] = "Shuffle",
};

static const char* const lfo_shape_name[NUM_LFO_SHAPES] = {
    [LFO_TRI] = "Triangle",
    [LFO_SIN] = "Sine",
    [LFO_SMOOTH_RAND] = "Random\nSmooth",
    [LFO_STEP_RAND] = "Random\nStepped",
    [LFO_BI_SQUARE] = "Square\nBipolar",
    [LFO_SQUARE] = "Square\nUnipolar",
    [LFO_CASTLE] = "Castle",
    [LFO_SAW] = "Saw",
    [LFO_BI_TRIGS] = "Triggers\nBipolar",
    [LFO_TRIGS] = "Triggers\nUnipolar",
    [LFO_ENV] = "Envelope",
};

static const char* const preset_category_name[NUM_PST_CATS] = {
    "",    "Bass",    "Leads",   "Pads", "Arps",  "Plinks",  "Plonks", "Beeps",  "Boops",
    "SFX", "Line-In", "Sampler", "Donk", "Jolly", "Sadness", "Wild",   "Gnarly", "Weird",
};

const static char* const scale_name[NUM_SCALES] = {
    [S_MAJOR] = "Major",
    [S_MINOR] = "Minor",
    [S_HARMMINOR] = "Harmonic",
    [S_PENTA] = "Penta\nMajor",
    [S_PENTAMINOR] = "Penta\nMinor",
    [S_HIRAJOSHI] = "Hirajoshi",
    [S_INSEN] = "Insen",
    [S_IWATO] = "Iwato",
    [S_MINYO] = "Minyo",
    [S_FIFTHS] = "Fifths",
    [S_TRIADMAJOR] = "Triad\nMajor",
    [S_TRIADMINOR] = "Triad\nMinor",
    [S_DORIAN] = "Dorian",
    [S_PHYRGIAN] = "Phrygian",
    [S_LYDIAN] = "Lydian",
    [S_MIXOLYDIAN] = "Mixolydian",
    [S_AEOLIAN] = "Aeolian",
    [S_LOCRIAN] = "Lacrian",
    [S_BLUESMINOR] = "Blues\nMinor",
    [S_BLUESMAJOR] = "Blues\nMajor",
    [S_ROMANIAN] = "Romanian",
    [S_WHOLETONE] = "Wholetone",
    [S_HARMONICS] = "Harmonics",
    [S_HEXANY] = "Hexany",
    [S_JUST] = "Just",
    [S_CHROMATIC] = "Chromatic",
    [S_DIMINISHED] = "Diminished",
};

// == TYPEDEFS == //

typedef struct ValueSmoother {
	float y1;
	float y2;
} ValueSmoother;

typedef struct Touch {
	s16 pres;
	u16 pos;
} Touch;

const static Touch init_touch = {TOUCH_MIN_PRES, TOUCH_MIN_POS};

// compressed to fit in u8s
typedef struct LatchTouch {
	u8 pos;
	u8 pres;
} LatchTouch;

typedef struct SynthString {
	Touch touch_frames[NUM_TOUCH_FRAMES]; // last eight frames of touches
	Touch touch_sorted[NUM_TOUCH_FRAMES]; // sorted copy of touch
	u8 ext_touch;                         // bitmask for last eight frames of touches
	Touch cur_touch;                      // active touch for this frame
	u8 note_number;
	u8 start_velocity;
	s32 pitchbend_pitch;
	LatchTouch latch_touch;
	bool touched : 1;
	bool env_trigger : 1;
} SynthString;

const static SynthString init_synth_string = {
    .touch_frames = {init_touch, init_touch, init_touch, init_touch, init_touch, init_touch, init_touch, init_touch},
    .touch_sorted = {init_touch, init_touch, init_touch, init_touch, init_touch, init_touch, init_touch, init_touch},
    .cur_touch = init_touch,
};

typedef struct TouchCalibData {
	u16 pres[PADS_PER_STRIP];
	s16 pos[PADS_PER_STRIP];
} TouchCalibData;

typedef struct ADC_DAC_Calib {
	float bias;
	float scale;
} ADC_DAC_Calib;

typedef struct SeqFlags {
	bool playing : 1;
	bool recording : 1;
	bool previewing : 1;
	bool playing_backwards : 1;
	bool stop_at_next_step : 1;
	bool is_first_pulse : 1;
	bool do_manual_step : 1;
	bool unused : 1;
} SeqFlags;

typedef struct ConditionalStep {
	s8 euclid_len;
	u8 euclid_trigs;
	s32 density;
	bool play_step;
	bool advance_step;
} ConditionalStep;

typedef struct SysParams {
	// 0 bytes
	u8 preset_id;
	u8 midi_in_chan : 4;
	u8 midi_out_chan : 4;
	u8 accel_sens;
	u8 volume_lsb;
	// 4 bytes
	u8 volume_msb : 3; // add 3 bits to make editing in 0-1024 range possible
	CVQuantType cv_quant : 2;
	bool reverse_encoder : 1;
	bool preset_aligned : 1;  // is cur_preset identical to preset[preset_id]?
	bool pattern_aligned : 1; // is cur_pattern_qtr identical to pattern[preset.params[P_PATTERN]]?
	// 5 bytes
	u8 cv_in_ppqn : 3;
	u8 cv_out_ppqn : 3;
	u8 midi_in_clock_mult : 2; // 0 = 1/2x, 1 = 1x, 2 = 2x
	// 6 bytes
	u8 midi_in_vel_balance;  // balance between incoming velocity and pressure
	u8 midi_out_vel_balance; // balance between outgoing velocity and pressure
	// 8 bytes
	MidiPressureType midi_in_pres_type : 2;
	MidiPressureType midi_out_pres_type : 2;
	bool midi_out_yz_control : 1;
	u8 midi_channel_bend_range_in : 3;
	// 9 bytes
	bool mpe_in : 1;
	bool mpe_out : 1;
	u8 midi_string_bend_range_in : 3;
	u8 midi_string_bend_range_out : 3;
	// 10 bytes
	u8 mpe_chans : 6; // values 0-14 represent 1-15 channels
	bool midi_soft_thru : 1;
	bool local_ctrl_off : 1;
	// 11 bytes
	bool midi_rcv_clock : 1;
	bool midi_rcv_transport : 1;
	bool midi_rcv_param_ccs : 2;
	bool midi_send_clock : 1;
	bool midi_send_transport : 1;
	bool midi_send_param_ccs : 2; // 0 = off, 1 = CCs, 2 = 14bit CCs
	// 12 bytes
	bool midi_send_lfo_cc : 1;
	u8 mpe_zone : 1; // 0 = lower, 1 = upper
	bool midi_in_scale_quant : 1;
	bool midi_trs_out_off : 1;
	bool midi_tuning : 1;
	bool edit_poly_params : 1; // show eight edit strips for poly params
	u8 pad1 : 2;
	// 13 bytes
	u8 cv_gate_in_is_pressure : 1;
	u8 reference_pitch : 4;
	u8 pad2 : 3;
	// 14 bytes
	u8 pad3;
	// 15 bytes
	u8 version;
	// 16 bytes
} SysParams;

typedef struct Preset {
	s16 params[NUM_PARAMS][NUM_MOD_SOURCES];
	u8 pad;
	u8 seq_start;
	u8 seq_len;
	u8 paddy[3];
	u8 version;
	u8 category;
	u8 name[8];
	s16 poly_params[NUM_POLY_PARAMS][NUM_STRINGS - 1];
	u8 padding[12];
} Preset;
static_assert((sizeof(Preset) & 15) == 0, "?");

typedef struct PatternStringStep {
	u8 pos[PTN_SUBSTEPS / 2];
	u8 pres[PTN_SUBSTEPS];
} PatternStringStep;

typedef struct PatternQuarter {
	PatternStringStep steps[PTN_STEPS_PER_QTR][NUM_STRINGS];
	s8 autoknob[PTN_STEPS_PER_QTR * PTN_SUBSTEPS][NUM_KNOBS];
} PatternQuarter;
static_assert((sizeof(PatternQuarter) & 15) == 0, "?");

typedef struct SampleInfo {
	u8 waveform4_b[1024]; // 4 bits x 2048 points, every 1024 samples
	int splitpoints[8];
	int samplelen; // must be after splitpoints, so that splitpoints[8] is always the length.
	s8 notes[8];
	u8 pitched;
	u8 loop; // bottom bit: loop; next bit: slice vs all
	u8 paddy[2];
} SampleInfo;
static_assert((sizeof(SampleInfo) & 15) == 0, "?");