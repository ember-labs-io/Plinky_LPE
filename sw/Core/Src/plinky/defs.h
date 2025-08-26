#pragma once
#include "utils.h"

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

// SYNTH

#define NUM_TOUCHSTRIPS 9
#define PADS_PER_STRIP 8

#define NUM_KNOBS 2
#define NUM_STRINGS 8

#define NUM_PRESETS 32
#define NUM_PATTERNS 24
#define NUM_SAMPLES 8

#define MAX_PTN_STEPS 64
#define PTN_STEPS_PER_QTR (MAX_PTN_STEPS / 4)
#define PTN_SUBSTEPS 8

#define NUM_VOICES 8
#define OSCS_PER_VOICE 4
#define NUM_GRAINS 32

#define SAMPLES_PER_TICK 64
#define NUM_LFOS 4
#define NUM_MIDI_CHANNELS 16

// PARAMS

#define RAW_SIZE 1024 // range of a parameter - make local after param cleanups
#define RAW_HALF (RAW_SIZE / 2)

// MEMORY

#define CUR_PRESET_VERSION 2

#define PATTERNS_START NUM_PRESETS
#define SAMPLES_START (PATTERNS_START + NUM_PATTERNS)

#define NUM_PTN_QUARTERS (NUM_PATTERNS * 4)
#define F_SAMPLES_START (PATTERNS_START + NUM_PTN_QUARTERS)
#define NUM_FLASH_ITEMS (F_SAMPLES_START + NUM_SAMPLES)

// TIME

#define MAX_BPM_10X 2400
#define MIN_BPM_10X 300
#define MAX_SWING 0.5f // 0.3333f represents triplet-feel swing
#define NUM_SYNC_DIVS 22
static u16 const sync_divs_32nds[NUM_SYNC_DIVS] = {1,  2,  3,  4,  5,  6,  8,  10,  12,  16,  20,
                                                   24, 32, 40, 48, 64, 80, 96, 128, 160, 192, 256};

// GRAPHICS

#define OLED_WIDTH 128
#define OLED_HEIGHT 32
#define NUM_ICONS 64

// == TYPEDEFS == //

typedef struct ValueSmoother {
	float y1;
	float y2;
} ValueSmoother;

typedef struct Touch {
	s16 pres;
	u16 pos;
} Touch;

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
	bool first_pulse : 1;
	bool force_next_step : 1;
	bool unused : 1;
} SeqFlags;

typedef struct ConditionalStep {
	s8 euclid_len;
	u8 euclid_trigs;
	s32 density;
	bool play_step;
	bool advance_step;
} ConditionalStep;

typedef struct Osc {
	u32 phase;
	u32 prev_sample;
	s32 phase_diff;
	s32 goal_phase_diff;
	s32 pitch;
} Osc;

typedef struct GrainPair {
	int fpos24;
	int pos[2];
	int vol24;
	int dvol24;
	int dpos24;
	float grate_ratio;
	float multisample_grate;
	int bufadjust; // for reverse grains, we adjust the dma buffer address by this many samples
	int outflags;
} GrainPair;

typedef struct Voice {
	// oscillator (sampler only uses the pitch value)
	Osc osc[OSCS_PER_VOICE];
	// env 1
	float env1_lvl;
	bool env1_decaying;
	ValueSmoother lpg_smoother[2];
	// env 1 visuals
	float env1_peak;
	float env1_norm;
	// env 2
	float env2_lvl;
	u16 env2_lvl16;
	bool env2_decaying;
	// noise
	float noise_lvl;
	// sampler state
	GrainPair grain_pair[2];
	int playhead8;
	u8 slice_id;
	u16 touch_pos_start;
	ValueSmoother touch_pos;
} Voice;

typedef struct SysParams {
	u8 curpreset;
	u8 paddy;
	u8 systemflags;
	u8 headphonevol;
	u8 pad[16 - 4];
} SysParams;

typedef struct Preset {
	s16 params[96][8];
	u8 pad;
	u8 seq_start;
	u8 seq_len;
	u8 paddy[3];
	u8 version;
	u8 category;
	u8 name[8];
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

typedef enum ShiftState {
	SS_NONE = -1,
	SS_SHIFT_A,
	SS_SHIFT_B,
	SS_LOAD,
	SS_LEFT,
	SS_RIGHT,
	SS_CLEAR,
	SS_RECORD,
	SS_PLAY,
} ShiftState;

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

#define C (0 * 512)
#define Cs (1 * 512)
#define D (2 * 512)
#define Ds (3 * 512)
#define E (4 * 512)
#define F (5 * 512)
#define Fs (6 * 512)
#define G (7 * 512)
#define Gs (8 * 512)
#define A (9 * 512)
#define As (10 * 512)
#define B (11 * 512)

#define Es F
#define Bs C

#define Ab Gs
#define Bb As
#define Cb B
#define Db Cs
#define Eb Ds
#define Fb E
#define Gb Fs

#define CENTS(c) (((c) * 512) / 100)

#define MAX_SCALE_STEPS 12

const static u16 scale_table[NUM_SCALES][16] = {
    [S_CHROMATIC] = {12, C, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B},
    [S_MAJOR] = {7, C, D, E, F, G, A, B},
    [S_MINOR] = {7, C, D, Eb, F, G, Ab, Bb},
    [S_HARMMINOR] = {7, C, D, Ds, F, G, Gs, B},
    [S_PENTA] = {5, C, D, E, G, A},
    [S_PENTAMINOR] = {5, C, Ds, F, G, As},
    [S_HIRAJOSHI] = {5, C, D, Ds, G, Gs},
    [S_INSEN] = {5, C, Cs, F, G, As},
    [S_IWATO] = {5, C, Cs, F, Fs, As},
    [S_MINYO] = {5, C, D, F, G, A},

    [S_FIFTHS] = {2, C, G},
    [S_TRIADMAJOR] = {3, C, E, G},
    [S_TRIADMINOR] = {3, C, Eb, G},

    // these are dups of major/minor/rotations thereof, but lets throw them in anyway
    [S_DORIAN] = {7, C, D, Ds, F, G, A, As},
    [S_PHYRGIAN] = {7, C, Db, Eb, F, G, Ab, Bb},
    [S_LYDIAN] = {7, C, D, E, Fs, G, A, B},
    [S_MIXOLYDIAN] = {7, C, D, E, F, G, A, Bb},
    [S_AEOLIAN] = {7, C, D, Eb, F, G, Ab, Bb},
    [S_LOCRIAN] = {7, C, Db, Eb, F, Gb, Ab, Bb},

    [S_BLUESMAJOR] = {6, C, D, Ds, E, G, A},
    [S_BLUESMINOR] = {6, C, Ds, F, Fs, G, As},

    [S_ROMANIAN] = {7, C, D, Ds, Fs, G, A, As},
    [S_WHOLETONE] = {6, C, D, E, Fs, Gs, As},

    // microtonal stuff
    [S_HARMONICS] = {4, C, E - CENTS(14), G + CENTS(2), Bb - CENTS(31)},
    [S_HEXANY] = {5, CENTS(0), CENTS(386), CENTS(498), CENTS(702),
                  CENTS(814)}, // kinda C,E,F,G,G# but the E is quite flat

    [S_JUST] = {7, CENTS(0), CENTS(204), CENTS(386), CENTS(498), CENTS(702), CENTS(884), CENTS(1088)},
    [S_DIMINISHED] = {8, C, D, Ds, F, Fs, Gs, A, B},
};

#undef C
#undef D
#undef E
#undef F
#undef G
#undef A
#undef B
#undef Cs
#undef Ds
#undef Es
#undef Fs
#undef Gs
#undef As
#undef Bs
#undef Cb
#undef Db
#undef Eb
#undef Fb
#undef Gb
#undef Ab
#undef Bb

// Alex notes:
//
// pitch table is (64*8) steps per semitone, ie 512 per semitone
// so heres my maths, this comes out at 435
// 8887421 comes from the value of pitch when playing a C
// the pitch of middle c in plinky as written is (4.0/(65536.0*65536.0/8887421.0/31250.0f))
// which is 1.0114729530400526 too low
// which is 0.19749290999 semitones flat
// *512 = 101. so I need to add 101 to pitch_base

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
	P_ENV_LVL1 = R_ENV1 * 6,    P_ATTACK1,      P_DECAY1,       P_SUSTAIN1,     P_RELEASE1,     P_ENV1_UNUSED,   // Envelope 1
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
	P_SYN_LVL = R_MIX1 * 6,     P_SYN_WET_DRY,  P_HPF,          P_MIDI_CH_IN,	P_CV_QUANT,	    P_VOLUME,       // Mixer 1
	P_IN_LVL = R_MIX2 * 6,      P_IN_WET_DRY,   P_SYS_UNUSED1,  P_MIDI_CH_OUT,	P_ACCEL_SENS,	P_MIX_WIDTH,    // Mixer 2

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

typedef enum RamSegment {
	SEG_PRESET,
	SEG_PAT0,
	SEG_PAT1,
	SEG_PAT2,
	SEG_PAT3,
	SEG_SYS,
	SEG_SAMPLE,
	NUM_RAM_SEGMENTS
} RamSegment;

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

	// Section Starts
	MIDI_SYSTEM_COMMON_MSG = MIDI_SYSTEM_EXCLUSIVE,
	MIDI_SYSTEM_REAL_TIME_MSG = MIDI_TIMING_CLOCK,

	// Dummy
	MIDI_NONE = 0,

} MidiMessageType;

// clang-format off
 const static Param midi_cc_table[128] = {
	//			0				1				2				3				4				5				6				7	
	/*   0 */	NUM_PARAMS,		NUM_PARAMS, 	P_NOISE,		P_ENV_LVL1,		P_DISTORTION,	P_GLIDE,		NUM_PARAMS,		P_SYN_LVL,
	/*   8 */	P_SYN_WET_DRY,	P_PITCH,		NUM_PARAMS,		P_GATE_LENGTH,	P_DLY_TIME,		P_SHAPE,		P_INTERVAL,		P_SCRUB,
	/*  16 */	P_GR_SIZE,		P_PLAY_SPD,		P_SMP_STRETCH,	P_ENV_LVL2,		P_ATTACK2,		P_DECAY2,		P_SUSTAIN2,		P_RELEASE2,
	/*  24 */	P_A_RATE,		P_A_DEPTH,		P_A_OFFSET,		P_B_RATE,		P_B_DEPTH,		P_B_OFFSET,		NUM_PARAMS,		P_HPF,

	/* 	CCs 32 through 63 reserved for 14-bit CCs, each of them representing the LSB of CC [number - 32] */
	/*  32 */	NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,
	/*  40 */	NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,
	/*  48 */	NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,
	/*  56 */	NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,

	/*  64 */	NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		P_RESO,
	/*  72 */	P_RELEASE1,		P_ATTACK1,		P_SUSTAIN1,		P_DECAY1,		P_X_RATE,		P_X_DEPTH,		P_X_OFFSET,		P_Y_RATE,
	/*  80 */	P_Y_DEPTH,		P_Y_OFFSET,		P_SAMPLE,		P_PATTERN,		NUM_PARAMS,		P_STEP_OFFSET,	NUM_PARAMS,		NUM_PARAMS,
	/*  88 */	NUM_PARAMS,		P_IN_LVL,	    P_IN_WET_DRY,	P_RVB_SEND,		P_RVB_TIME,		P_SHIMMER,		P_DLY_SEND,		P_DLY_FEEDBACK,
	/*  96 */	NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		P_LATCH_TGL,	P_ARP_TGL,		P_ARP_ORDER,
	/* 104 */	P_ARP_CLK_DIV,	P_ARP_CHANCE,	P_ARP_EUC_LEN,	P_ARP_OCTAVES,	P_SEQ_ORDER,	P_SEQ_CLK_DIV,	P_SEQ_CHANCE,	P_SEQ_EUC_LEN,
	/* 112 */	P_PING_PONG,	P_DLY_WOBBLE,	P_RVB_WOBBLE,	NUM_PARAMS,		P_SCRUB_JIT,	P_GR_SIZE_JIT, 	P_PLAY_SPD_JIT, P_SMP_UNUSED1,
	/* 120 */	NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,		NUM_PARAMS,
};

// clang-format on

// == GRAPHICS == //

typedef enum Font {
	BOLD = 16,
	F_8 = 0,
	F_12,
	F_16,
	F_20,
	F_24,
	F_28,
	F_32,
	F_8_BOLD = BOLD,
	F_12_BOLD,
	F_16_BOLD,
	F_20_BOLD,
	F_24_BOLD,
	F_28_BOLD,
	F_32_BOLD,
	NUM_FONTS,
} Font;

// offset to make fonts down-align to the same pixel (currently incomplete)
const static u8 font_y_offset[NUM_FONTS] = {
    [F_16] = 3,
    [F_12_BOLD] = 3,
    [F_16_BOLD] = 3,
};

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
   [P_SHAPE] = I_SHAPE "Shape",       		[P_DISTORTION] = I_DISTORT "Distortion",   	[P_PITCH] = I_PIANO "Pitch",         		[P_OCT] = I_OCTAVE "Octave",         	[P_GLIDE] = I_GLIDE "Glide",         		[P_INTERVAL] = I_OFFSET "Interval",			// Sound 1
   [P_NOISE] = I_WAVE "Noise",       		[P_RESO] = I_DISTORT "Resonance",         	[P_DEGREE] = I_OFFSET "Degree",       		[P_SCALE] = I_PIANO "Scale",        	[P_MICROTONE] = I_MICRO "Microtone",     	[P_COLUMN] = I_OFFSET "Column",				// Sound 2
   [P_ENV_LVL1] = I_TOUCH "Sens",			[P_ATTACK1] = I_ADSR_A "Attack",      		[P_DECAY1] = I_ADSR_D "Decay",        		[P_SUSTAIN1] = I_ADSR_S "Sustain",    	[P_RELEASE1] = I_ADSR_R "Release",      	[P_ENV1_UNUSED] = I_CROSS "<unused>",   	// Envelope 1
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
   [P_SYN_LVL] = I_WAVE "Synth Lvl",    	[P_SYN_WET_DRY] = I_REVERB "Wet/Dry",		[P_HPF] = I_HPF "High Pass",           		[P_MIDI_CH_IN] = I_RIGHT "In Chan",  	[P_CV_QUANT] = I_JACK "CV Quant",       	[P_VOLUME] = I_PHONES "Volume",        		// Mixer 1
   [P_IN_LVL] = I_JACK "Input Lvl",     	[P_IN_WET_DRY] = I_JACK "In Wet/Dry",   	[P_SYS_UNUSED1] = I_CROSS "<unused>",   	[P_MIDI_CH_OUT] = I_LEFT "Out Chan",	[P_ACCEL_SENS] = I_INTERVAL "Accel Sens",	[P_MIX_WIDTH] = I_PHONES "Width",			// Mixer 2
};

// clang-format on

const static char* const mod_src_name[NUM_MOD_SOURCES] = {
    [SRC_BASE] = I_SLIDERS "Base", [SRC_ENV2] = I_ENV "Env 2 >>",  [SRC_PRES] = I_TOUCH "Pres >>",
    [SRC_LFO_A] = I_A "Mod A >>",  [SRC_LFO_B] = I_B "Mod B >>",   [SRC_LFO_X] = I_X "Mod X >>",
    [SRC_LFO_Y] = I_Y "Mod Y >>",  [SRC_RND] = I_RANDOM "Rand >>",
};

const static char* const arm_mode_name[NUM_ARP_ORDERS] = {
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

static const char* const cv_quant_name[NUM_CV_QUANT_TYPES] = {
    [CVQ_OFF] = "Off",
    [CVQ_CHROMATIC] = "Chrom",
    [CVQ_SCALE] = "Scale",
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

static inline const char* note_name(int note) {
	note += 12;
	if (note < 0 || note > 8 * 12)
		return "";
	static char buf[4];
	int octave = note / 12;
	note -= octave * 12;
	buf[0] = "CCDDEFFGGAAB"[note];
	buf[1] = " + +  + + + "[note];
	buf[2] = '0' + octave;
	return buf;
}