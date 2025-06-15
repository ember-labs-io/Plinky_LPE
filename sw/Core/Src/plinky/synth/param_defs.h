#pragma once
#include "hardware/cv.h"
#include "hardware/mem_defs.h"
#include "utils.h"

#define RANGE_MASK 127   // the actual range is stored in the 7 lsb of a byte
#define RANGE_SIGNED 128 // the msb indicates whether the range is signed
#define PARAM_SIZE 1024  // range of a parameter
#define HALF_PARAM_SIZE (PARAM_SIZE / 2)
#define QUARTER (PARAM_SIZE / 4)
#define EIGHTH (PARAM_SIZE / 8)
#define QUANT(v, maxi) (((v) * PARAM_SIZE + PARAM_SIZE / 2) / (maxi))

#define SAMPLE_ID_RANGE (NUM_SAMPLES + 1)

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

typedef enum ParamRow {
	PG_SOUND1,
	PG_SOUND2,
	PG_ENV1,
	PG_ENV2,
	PG_DELAY,
	PG_REVERB,
	PG_ARP,
	PG_SEQ,
	PG_SAMPLER,
	PG_JITTER,
	PG_A,
	PG_B,
	PG_X,
	PG_Y,
	PG_MIX1,
	PG_MIX2,
	PG_NUM_PAGES,
} ParamRow;

typedef enum Param {
	// Sound 1
	P_SHAPE = PG_SOUND1 * 6,
	P_DISTORTION,
	P_PITCH,
	P_OCT,
	P_GLIDE,
	P_INTERVAL,
	// Sound 2
	P_NOISE = PG_SOUND2 * 6,
	P_RESO,
	P_DEGREE,
	P_SCALE,
	P_MICROTONE,
	P_COLUMN,
	// Envelope 1
	P_ENV_LVL1 = PG_ENV1 * 6,
	P_ATTACK1,
	P_DECAY1,
	P_SUSTAIN1,
	P_RELEASE1,
	P_ENV1_UNUSED,
	// Envelope 2
	P_ENV_LVL2 = PG_ENV2 * 6,
	P_ATTACK2,
	P_DECAY2,
	P_SUSTAIN2,
	P_RELEASE2,
	P_ENV2_UNUSED,
	// Delay
	P_DLY_SEND = PG_DELAY * 6,
	P_DLY_TIME,
	P_DLY_PINGPONG,
	P_DLY_WOBBLE,
	P_DLY_FEEDBACK,
	P_TEMPO,
	// Reverb
	P_RVB_SEND = PG_REVERB * 6,
	P_RVB_TIME,
	P_RVB_SHIMMER,
	P_RVB_WOBBLE,
	P_RVB_UNUSED,
	P_SWING,
	// Arp
	P_ARP_TOGGLE = PG_ARP * 6,
	P_ARP_ORDER,
	P_ARP_CLK_DIV,
	P_ARP_CHANCE,
	P_ARP_EUC_LEN,
	P_ARP_OCTAVES,
	// Sequencer
	P_LATCH_TOGGLE = PG_SEQ * 6,
	P_SEQ_ORDER,
	P_SEQ_CLK_DIV,
	P_SEQ_CHANCE,
	P_SEQ_EUC_LEN,
	P_GATE_LENGTH,
	// Sampler 1
	P_SMP_SCRUB = PG_SAMPLER * 6,
	P_SMP_GRAINSIZE,
	P_SMP_SPEED,
	P_SMP_STRETCH,
	P_SAMPLE,
	P_PATTERN,
	// Sampler 2
	P_SMP_SCRUB_JIT = PG_JITTER * 6,
	P_SMP_GRAINSIZE_JIT,
	P_SMP_SPEED_JIT,
	P_SMP_UNUSED1,
	P_SMP_UNUSED2,
	P_STEP_OFFSET,
	// LFO A
	P_A_SCALE = PG_A * 6,
	P_A_OFFSET,
	P_A_DEPTH,
	P_A_RATE,
	P_A_SHAPE,
	P_A_SYM,
	// LFO B
	P_B_SCALE = PG_B * 6,
	P_B_OFFSET,
	P_B_DEPTH,
	P_B_RATE,
	P_B_SHAPE,
	P_B_SYM,
	// LFO X
	P_X_SCALE = PG_X * 6,
	P_X_OFFSET,
	P_X_DEPTH,
	P_X_RATE,
	P_X_SHAPE,
	P_X_SYM,
	// LFO Y
	P_Y_SCALE = PG_Y * 6,
	P_Y_OFFSET,
	P_Y_DEPTH,
	P_Y_RATE,
	P_Y_SHAPE,
	P_Y_SYM,
	// Mixer 1
	P_SYNTH_LVL = PG_MIX1 * 6,
	P_SYNTH_WET_DRY,
	P_HPF,
	P_MIDI_CH_IN,
	P_CV_QUANT,
	P_VOLUME,
	// Mixer 2
	P_INPUT_LVL = PG_MIX2 * 6,
	P_INPUT_WET_DRY,
	P_SYS_UNUSED1,
	P_MIDI_CH_OUT,
	P_ACCEL_SENS,
	P_MIX_WIDTH,

	NUM_PARAMS = PG_NUM_PAGES * 6,
} Param;

const static u8 param_range[NUM_PARAMS] = {
    // Sound 1
    [P_SHAPE] = RANGE_SIGNED,
    [P_DISTORTION] = RANGE_SIGNED,
    [P_PITCH] = RANGE_SIGNED,
    [P_OCT] = 4 + RANGE_SIGNED,
    [P_GLIDE] = 0,
    [P_INTERVAL] = RANGE_SIGNED,

    // Sound 2
    // [NOISE]
    // [RESO]
    [P_DEGREE] = RANGE_SIGNED + 24,
    [P_SCALE] = 27,
    [P_MICROTONE] = 0,
    [P_COLUMN] = 13,

    // Envelope 1
    [P_ENV_LVL1] = 0,
    [P_ATTACK1] = 0,
    [P_DECAY1] = 0,
    [P_SUSTAIN1] = 0,
    [P_RELEASE1] = 0,
    // [UNUSED]

    // Envelope 2
    // [ENV2 LEVEL]
    [P_ATTACK2] = 0,
    [P_DECAY2] = 0,
    [P_SUSTAIN2] = 0,
    [P_RELEASE2] = 0,
    // [UNUSED]

    // Delay
    [P_DLY_SEND] = 0,
    [P_DLY_TIME] = RANGE_SIGNED,
    [P_DLY_PINGPONG] = 0,
    [P_DLY_WOBBLE] = 0,
    [P_DLY_FEEDBACK] = 0,
    [P_TEMPO] = RANGE_SIGNED,

    // Reverb
    [P_RVB_SEND] = 0,
    [P_RVB_TIME] = 0,
    [P_RVB_SHIMMER] = 0,
    [P_RVB_WOBBLE] = 0,
    // [UNUSED]
    [P_SWING] = RANGE_SIGNED,

    // Arp
    // [Arp Toggle]
    [P_ARP_ORDER] = 15,
    [P_ARP_CLK_DIV] = RANGE_SIGNED,
    [P_ARP_CHANCE] = 0,
    [P_ARP_EUC_LEN] = 17 + RANGE_SIGNED,
    [P_ARP_OCTAVES] = 4,

    // Sequencer
    // [Latch Toggle]
    [P_SEQ_ORDER] = 6,
    [P_SEQ_CLK_DIV] = 23,
    [P_SEQ_CHANCE] = RANGE_SIGNED,
    [P_SEQ_EUC_LEN] = 17 + RANGE_SIGNED,
    // [Gate Len]

    // Sampler 1
    // [Scrub]
    // [Grain Size]
    [P_SMP_SPEED] = RANGE_SIGNED,
    [P_SMP_STRETCH] = RANGE_SIGNED,
    [P_SAMPLE] = 9,
    [P_PATTERN] = 24,

    // Sampler 2
    // [Scrub Jitter]
    // [Grain Size Jitter]
    // [Play Speed Jitter]
    [P_SMP_UNUSED1] = RANGE_SIGNED,
    // [UNUSED]
    [P_STEP_OFFSET] = RANGE_SIGNED + 64,

    // LFO A
    [P_A_SCALE] = RANGE_SIGNED,
    [P_A_OFFSET] = RANGE_SIGNED,
    [P_A_DEPTH] = RANGE_SIGNED,
    [P_A_RATE] = RANGE_SIGNED,
    [P_A_SHAPE] = 11, // NUM_LFO_SHAPES,
    [P_A_SYM] = RANGE_SIGNED,

    // LFO B
    [P_B_SCALE] = RANGE_SIGNED,
    [P_B_OFFSET] = RANGE_SIGNED,
    [P_B_DEPTH] = RANGE_SIGNED,
    [P_B_RATE] = RANGE_SIGNED,
    [P_B_SHAPE] = 11, // NUM_LFO_SHAPES,
    [P_B_SYM] = RANGE_SIGNED,

    // LFO X
    [P_X_SCALE] = RANGE_SIGNED,
    [P_X_OFFSET] = RANGE_SIGNED,
    [P_X_DEPTH] = RANGE_SIGNED,
    [P_X_RATE] = RANGE_SIGNED,
    [P_X_SHAPE] = 11, // NUM_LFO_SHAPES,
    [P_X_SYM] = RANGE_SIGNED,

    // LFO Y
    [P_Y_SCALE] = RANGE_SIGNED,
    [P_Y_OFFSET] = RANGE_SIGNED,
    [P_Y_DEPTH] = RANGE_SIGNED,
    [P_Y_RATE] = RANGE_SIGNED,
    [P_Y_SHAPE] = 11, // NUM_LFO_SHAPES,
    [P_Y_SYM] = RANGE_SIGNED,

    // Mixer 1
    // [Synth Level]
    [P_SYNTH_WET_DRY] = RANGE_SIGNED,
    // [HPF]
    [P_MIDI_CH_IN] = 16,
    [P_CV_QUANT] = 3,
    // [Volume]

    // Mixer 2
    // [Ext Input Level]
    [P_INPUT_WET_DRY] = RANGE_SIGNED,
    // [UNUSED]
    [P_MIDI_CH_OUT] = 16,
    [P_ACCEL_SENS] = RANGE_SIGNED,
    [P_MIX_WIDTH] = RANGE_SIGNED,
};

static Preset const init_params = {

    .seq_len = 8,
    .version = CUR_PRESET_VERSION,
    .params = {
        [P_ENV_LVL1] = {HALF_PARAM_SIZE},
        [P_DISTORTION] = {0},
        [P_ATTACK1] = {EIGHTH},
        [P_DECAY1] = {QUARTER},
        [P_SUSTAIN1] = {PARAM_SIZE},
        [P_RELEASE1] = {EIGHTH},

        [P_OCT] = {0, 0, 0},
        [P_PITCH] = {0, 0, 0},
        // [P_SCALE] = {QUANT(S_MAJOR, NUM_SCALES)},
        [P_SCALE] = {QUANT(0, 27)},
        [P_MICROTONE] = {EIGHTH},
        [P_COLUMN] = {QUANT(7, 13)},
        [P_INTERVAL] = {(0 * PARAM_SIZE) / 12},
        [P_DEGREE] = {0, 0, 0},

        [P_NOISE] = {0, 0, 0},

        [P_SMP_SPEED] = {HALF_PARAM_SIZE},
        [P_SMP_GRAINSIZE] = {HALF_PARAM_SIZE},
        [P_SMP_STRETCH] = {HALF_PARAM_SIZE},

        //		[P_ARP_ORDER]={QUANT(ARP_UP,NUM_ARP_ORDERS)},
        [P_ARP_CLK_DIV] = {QUANT(2, 22)}, // NUM_SYNC_DIVS
        [P_ARP_CHANCE] = {PARAM_SIZE},
        [P_ARP_EUC_LEN] = {QUANT(8, 17)},
        [P_ARP_OCTAVES] = {QUANT(0, 4)},
        [P_GLIDE] = {0},

        // [P_SEQ_ORDER] = {QUANT(SEQ_FWD, NUM_SEQ_ORDERS)},
        [P_SEQ_ORDER] = {QUANT(1, 6)},
        [P_SEQ_CLK_DIV] = {QUANT(6, 23)}, // NUM_SYNC_DIVS + 1
        [P_SEQ_CHANCE] = {PARAM_SIZE},
        [P_SEQ_EUC_LEN] = {QUANT(8, 17)},
        [P_PATTERN] = {QUANT(0, 24)},
        [P_STEP_OFFSET] = {0},
        [P_TEMPO] = {0},

        [P_GATE_LENGTH] = {PARAM_SIZE},

        //[P_DLY_SEND]={HALF_PARAM_SIZE},
        [P_DLY_TIME] = {QUANT(3, 8)},
        [P_DLY_FEEDBACK] = {HALF_PARAM_SIZE},
        //[P_DLCOLOR]={PARAM_SIZE},
        [P_DLY_WOBBLE] = {QUARTER},
        [P_DLY_PINGPONG] = {PARAM_SIZE},

        [P_RVB_SEND] = {QUARTER},
        [P_RVB_TIME] = {HALF_PARAM_SIZE},
        [P_RVB_SHIMMER] = {QUARTER},
        //[P_RVCOLOR]={PARAM_SIZE-QUARTER},
        [P_RVB_WOBBLE] = {QUARTER},
        //[P_RVB_UNUSED]={0},

        [P_SYNTH_LVL] = {HALF_PARAM_SIZE},
        [P_MIX_WIDTH] = {(HALF_PARAM_SIZE * 7) / 8},
        [P_INPUT_WET_DRY] = {0},
        [P_INPUT_LVL] = {HALF_PARAM_SIZE},
        [P_SYNTH_WET_DRY] = {0},

        [P_ATTACK2] = {EIGHTH},
        [P_DECAY2] = {QUARTER},
        [P_SUSTAIN2] = {PARAM_SIZE},
        [P_RELEASE2] = {EIGHTH},
        [P_SWING] = {0},
        [P_ENV_LVL2] = {HALF_PARAM_SIZE},
        [P_CV_QUANT] = {QUANT(CVQ_OFF, CVQ_LAST)},

        [P_A_OFFSET] = {0},
        [P_A_SCALE] = {HALF_PARAM_SIZE},
        [P_A_DEPTH] = {0},
        [P_A_RATE] = {0},
        //[P_A_SHAPE] = {QUANT(LFO_ENV,NUM_LFO_SHAPES)},
        [P_A_SYM] = {0},

        [P_B_OFFSET] = {0},
        [P_B_SCALE] = {HALF_PARAM_SIZE},
        [P_B_DEPTH] = {0},
        [P_B_RATE] = {100},
        [P_B_SHAPE] = {0},
        [P_B_SYM] = {0},

        [P_X_OFFSET] = {0},
        [P_X_SCALE] = {HALF_PARAM_SIZE},
        [P_X_DEPTH] = {0},
        [P_X_RATE] = {-123},
        [P_X_SHAPE] = {0},
        [P_X_SYM] = {0},

        [P_Y_OFFSET] = {0},
        [P_Y_SCALE] = {HALF_PARAM_SIZE},
        [P_Y_DEPTH] = {0},
        [P_Y_RATE] = {-315},
        [P_Y_SHAPE] = {0},
        [P_Y_SYM] = {0},

        [P_ACCEL_SENS] = {HALF_PARAM_SIZE},

        [P_MIDI_CH_IN] = {0},
        [P_MIDI_CH_OUT] = {0},

    }}; // init params

enum ECats {
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
	CAT_LAST
};
