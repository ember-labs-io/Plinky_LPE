#pragma once
#include "utils.h"

#define QUARTER (PARAM_SIZE / 4)
#define EIGHTH (PARAM_SIZE / 8)
#define INDEX_TO_RAW(index, range) ((((index) << 10) + ((index) >= 0 ? (range) - 1 : 0)) / (range))

// the msb indicates whether the range is signed
#define UNSIGNED 0
#define SIGNED 128
// the actual range is stored in the 7 lsb of a byte
#define RANGE_MASK 127

#define SAMPLE_ID_RANGE (NUM_SAMPLES + 1)

typedef enum RangeType {
	R_UVALUE, // unsigned value
	R_SVALUE, // signed value
	R_BINARY, // on/off
	R_OCTAVE, // octave
	R_DEGREE, // degree
	R_SCALE,  // scale
	R_COLUMN, // column
	R_CLOCK1, // single clock div (sequencer)
	R_CLOCK2, // dual clock div (arp, lfos)
	R_EUCLEN, // euclid length
	R_ARPORD, // arp order
	R_ARPOCT, // arp octaves
	R_SEQORD, // seq order
	R_SAMPLE, // sample id
	R_PATN,   // pattern
	R_STOFFS, // step offset
	R_LFOSHP, // lfo shape
	R_MIDICH, // midi channel
	R_CVQNT,  // cv quantize mode
	R_VOLUME, // volume
	R_UNUSED,
	NUM_RANGE_TYPES,
} RangeType;

const static u8 param_info[NUM_RANGE_TYPES] = {
    [R_UVALUE] = UNSIGNED,
    [R_SVALUE] = SIGNED,
    [R_BINARY] = UNSIGNED + 2,
    [R_OCTAVE] = SIGNED + 5,
    [R_DEGREE] = SIGNED + 25,
    [R_SCALE] = UNSIGNED + NUM_SCALES,
    [R_COLUMN] = UNSIGNED + 13,
    [R_CLOCK1] = UNSIGNED + NUM_SYNC_DIVS,
    [R_CLOCK2] = SIGNED + NUM_SYNC_DIVS,
    [R_EUCLEN] = SIGNED + 17,
    [R_ARPORD] = UNSIGNED + NUM_ARP_ORDERS,
    [R_ARPOCT] = UNSIGNED + 5,
    [R_SEQORD] = UNSIGNED + NUM_SEQ_ORDERS,
    [R_SAMPLE] = UNSIGNED + NUM_SAMPLES + 1,
    [R_PATN] = UNSIGNED + NUM_PATTERNS,
    [R_STOFFS] = SIGNED + 65,
    [R_LFOSHP] = UNSIGNED + NUM_LFO_SHAPES,
    [R_MIDICH] = UNSIGNED + NUM_MIDI_CHANNELS,
    [R_CVQNT] = UNSIGNED + NUM_CV_QUANT_TYPES,
    [R_UNUSED] = 0,
};

// clang-format off

const static RangeType range_type[NUM_PARAMS] = {
   [P_SHAPE] = R_SVALUE,       [P_DISTORTION] = R_SVALUE,   [P_PITCH] = R_SVALUE,         [P_OCT] = R_OCTAVE,         [P_GLIDE] = R_UVALUE,         [P_INTERVAL] = R_SVALUE,      // Sound 1
   [P_NOISE] = R_UVALUE,       [P_RESO] = R_UVALUE,         [P_DEGREE] = R_DEGREE,        [P_SCALE] = R_SCALE,        [P_MICROTONE] = R_UVALUE,     [P_COLUMN] = R_COLUMN,        // Sound 2
   [P_ENV_LVL1] = R_UVALUE,    [P_ATTACK1] = R_UVALUE,      [P_DECAY1] = R_UVALUE,        [P_SUSTAIN1] = R_UVALUE,    [P_RELEASE1] = R_UVALUE,      [P_ENV1_UNUSED] = R_UNUSED,   // Envelope 1
   [P_ENV_LVL2] = R_UVALUE,    [P_ATTACK2] = R_UVALUE,      [P_DECAY2] = R_UVALUE,        [P_SUSTAIN2] = R_UVALUE,    [P_RELEASE2] = R_UVALUE,      [P_ENV2_UNUSED] = R_UNUSED,   // Envelope 2
   [P_DLY_SEND] = R_UVALUE,    [P_DLY_TIME] = R_CLOCK2,     [P_PING_PONG] = R_UVALUE,     [P_DLY_WOBBLE] = R_UVALUE,  [P_DLY_FEEDBACK] = R_UVALUE,  [P_TEMPO] = R_SVALUE,         // Delay
   [P_RVB_SEND] = R_UVALUE,    [P_RVB_TIME] = R_UVALUE,     [P_SHIMMER] = R_UVALUE,       [P_RVB_WOBBLE] = R_UVALUE,  [P_RVB_UNUSED] = R_UVALUE,    [P_SWING] = R_SVALUE,         // Reverb
   [P_ARP_TGL] = R_BINARY,     [P_ARP_ORDER] = R_ARPORD,    [P_ARP_CLK_DIV] = R_CLOCK2,   [P_ARP_CHANCE] = R_UVALUE,  [P_ARP_EUC_LEN] = R_EUCLEN,   [P_ARP_OCTAVES] = R_ARPOCT,   // Arp
   [P_LATCH_TGL] = R_BINARY,   [P_SEQ_ORDER] = R_SEQORD,    [P_SEQ_CLK_DIV] = R_CLOCK1,   [P_SEQ_CHANCE] = R_SVALUE,  [P_SEQ_EUC_LEN] = R_EUCLEN,   [P_GATE_LENGTH] = R_UVALUE,   // Sequencer
   [P_SCRUB] = R_UVALUE,       [P_GR_SIZE] = R_UVALUE,      [P_PLAY_SPD] = R_SVALUE,      [P_SMP_STRETCH] = R_SVALUE, [P_SAMPLE] = R_SAMPLE,        [P_PATTERN] = R_PATN,         // Sampler 1
   [P_SCRUB_JIT] = R_UVALUE,   [P_GR_SIZE_JIT] = R_UVALUE,  [P_PLAY_SPD_JIT] = R_UVALUE,  [P_SMP_UNUSED1] = R_UVALUE, [P_SMP_UNUSED2] = R_UNUSED,   [P_STEP_OFFSET] = R_STOFFS,   // Sampler 2
   [P_A_SCALE] = R_SVALUE,     [P_A_OFFSET] = R_SVALUE,     [P_A_DEPTH] = R_SVALUE,       [P_A_RATE] = R_CLOCK2,      [P_A_SHAPE] = R_LFOSHP,       [P_A_SYM] = R_SVALUE,         // LFO A
   [P_B_SCALE] = R_SVALUE,     [P_B_OFFSET] = R_SVALUE,     [P_B_DEPTH] = R_SVALUE,       [P_B_RATE] = R_CLOCK2,      [P_B_SHAPE] = R_LFOSHP,       [P_B_SYM] = R_SVALUE,         // LFO B
   [P_X_SCALE] = R_SVALUE,     [P_X_OFFSET] = R_SVALUE,     [P_X_DEPTH] = R_SVALUE,       [P_X_RATE] = R_CLOCK2,      [P_X_SHAPE] = R_LFOSHP,       [P_X_SYM] = R_SVALUE,         // LFO X
   [P_Y_SCALE] = R_SVALUE,     [P_Y_OFFSET] = R_SVALUE,     [P_Y_DEPTH] = R_SVALUE,       [P_Y_RATE] = R_CLOCK2,      [P_Y_SHAPE] = R_LFOSHP,       [P_Y_SYM] = R_SVALUE,         // LFO Y
   [P_SYN_LVL] = R_UVALUE,     [P_SYN_WET_DRY] = R_SVALUE,  [P_HPF] = R_UVALUE,           [P_MIDI_CH_IN] = R_MIDICH,  [P_CV_QUANT] = R_CVQNT,       [P_VOLUME] = R_UVALUE,        // Mixer 1
   [P_IN_LVL] = R_UVALUE,      [P_IN_WET_DRY] = R_SVALUE,   [P_SYS_UNUSED1] = R_UNUSED,   [P_MIDI_CH_OUT] = R_MIDICH, [P_ACCEL_SENS] = R_SVALUE,    [P_MIX_WIDTH] = R_SVALUE,     // Mixer 2
};

// clang-format on

static Preset const init_params = {

    .seq_start = 0,
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
        [P_SCALE] = {INDEX_TO_RAW(S_MAJOR, NUM_SCALES)},
        [P_MICROTONE] = {EIGHTH},
        [P_COLUMN] = {INDEX_TO_RAW(7, 13)},
        [P_INTERVAL] = {0},
        [P_DEGREE] = {0, 0, 0},

        [P_NOISE] = {0, 0, 0},

        [P_PLAY_SPD] = {HALF_PARAM_SIZE},
        [P_GR_SIZE] = {HALF_PARAM_SIZE},
        [P_SMP_STRETCH] = {HALF_PARAM_SIZE},

        [P_ARP_ORDER] = {INDEX_TO_RAW(ARP_UP, NUM_ARP_ORDERS)},
        [P_ARP_CLK_DIV] = {INDEX_TO_RAW(2, NUM_SYNC_DIVS)},
        [P_ARP_CHANCE] = {PARAM_SIZE},
        [P_ARP_EUC_LEN] = {INDEX_TO_RAW(8, 17)},
        [P_ARP_OCTAVES] = {INDEX_TO_RAW(0, 4)},
        [P_GLIDE] = {0},

        [P_SEQ_ORDER] = {INDEX_TO_RAW(SEQ_ORD_FWD, NUM_SEQ_ORDERS)},
        [P_SEQ_CLK_DIV] = {INDEX_TO_RAW(6, NUM_SYNC_DIVS + 1)},
        [P_SEQ_CHANCE] = {PARAM_SIZE},
        [P_SEQ_EUC_LEN] = {INDEX_TO_RAW(8, 17)},
        [P_PATTERN] = {INDEX_TO_RAW(0, NUM_PATTERNS)},
        [P_STEP_OFFSET] = {0},
        [P_TEMPO] = {0},

        [P_GATE_LENGTH] = {PARAM_SIZE},

        //[P_DLY_SEND]={HALF_PARAM_SIZE},
        [P_DLY_TIME] = {INDEX_TO_RAW(3, 8)},
        [P_DLY_FEEDBACK] = {HALF_PARAM_SIZE},
        //[P_DLCOLOR]={PARAM_SIZE},
        [P_DLY_WOBBLE] = {QUARTER},
        [P_PING_PONG] = {PARAM_SIZE},

        [P_RVB_SEND] = {QUARTER},
        [P_RVB_TIME] = {HALF_PARAM_SIZE},
        [P_SHIMMER] = {QUARTER},
        //[P_RVCOLOR]={PARAM_SIZE-QUARTER},
        [P_RVB_WOBBLE] = {QUARTER},
        //[P_RVB_UNUSED]={0},

        [P_SYN_LVL] = {HALF_PARAM_SIZE},
        [P_MIX_WIDTH] = {(HALF_PARAM_SIZE * 7) / 8},
        [P_IN_WET_DRY] = {0},
        [P_IN_LVL] = {HALF_PARAM_SIZE},
        [P_SYN_WET_DRY] = {0},

        [P_ATTACK2] = {EIGHTH},
        [P_DECAY2] = {QUARTER},
        [P_SUSTAIN2] = {PARAM_SIZE},
        [P_RELEASE2] = {EIGHTH},
        [P_SWING] = {0},
        [P_ENV_LVL2] = {HALF_PARAM_SIZE},
        [P_CV_QUANT] = {INDEX_TO_RAW(CVQ_OFF, NUM_CV_QUANT_TYPES)},

        [P_A_OFFSET] = {0},
        [P_A_SCALE] = {HALF_PARAM_SIZE},
        [P_A_DEPTH] = {0},
        [P_A_RATE] = {QUARTER},
        //[P_A_SHAPE] = {INDEX_TO_RAW(LFO_ENV,NUM_LFO_SHAPES)},
        [P_A_SYM] = {0},

        [P_B_OFFSET] = {0},
        [P_B_SCALE] = {HALF_PARAM_SIZE},
        [P_B_DEPTH] = {0},
        [P_B_RATE] = {HALF_PARAM_SIZE},
        [P_B_SHAPE] = {0},
        [P_B_SYM] = {0},

        [P_X_OFFSET] = {0},
        [P_X_SCALE] = {HALF_PARAM_SIZE},
        [P_X_DEPTH] = {0},
        [P_X_RATE] = {-246},
        [P_X_SHAPE] = {0},
        [P_X_SYM] = {0},

        [P_Y_OFFSET] = {0},
        [P_Y_SCALE] = {HALF_PARAM_SIZE},
        [P_Y_DEPTH] = {0},
        [P_Y_RATE] = {-630},
        [P_Y_SHAPE] = {0},
        [P_Y_SYM] = {0},

        [P_ACCEL_SENS] = {HALF_PARAM_SIZE},

        [P_MIDI_CH_IN] = {0},
        [P_MIDI_CH_OUT] = {0},

    }};