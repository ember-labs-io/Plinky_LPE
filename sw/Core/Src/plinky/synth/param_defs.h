#pragma once
#include "utils.h"

// the msb indicates whether the range is signed
#define UNSIGNED 0
#define SIGNED 128
// the actual range is stored in the 7 lsb of a byte
#define RANGE_MASK 127

#define RAW_QUART (RAW_SIZE / 4)
#define RAW_EIGHTH (RAW_SIZE / 8)
// map to the value closest to 0 that is fully inside of the requested index
#define INDEX_TO_RAW(index, range) (((abs(index) << 10) + ((range) - 1)) / ((index) >= 0 ? (range) : -(range)))

typedef enum RangeType {
	R_UVALUE, // unsigned value
	R_SVALUE, // signed value
	R_BINARY, // on/off
	R_OCTAVE, // octave
	R_DEGREE, // degree
	R_SCALE,  // scale
	R_COLUMN, // column
	R_ROOT,   // root note
	R_DLYCLK, // delay clock
	R_SEQCLK, // sequencer clock
	R_DUACLK, // dual clock, synced & free (arp, lfos)
	R_EUCLEN, // euclid length
	R_ARPORD, // arp order
	R_ARPOCT, // arp octaves
	R_SEQORD, // seq order
	R_SAMPLE, // sample id
	R_PATN,   // pattern
	R_STOFFS, // step offset
	R_LFOSHP, // lfo shape
	R_VOLUME, // volume
	R_UNUSED,
	NUM_RANGE_TYPES,
} RangeType;

const static u16 param_info[NUM_RANGE_TYPES] = {
    [R_UVALUE] = UNSIGNED,
    [R_SVALUE] = SIGNED,
    [R_BINARY] = UNSIGNED + 2,
    [R_OCTAVE] = SIGNED + 5,
    [R_DEGREE] = SIGNED + 25,
    [R_SCALE] = UNSIGNED + NUM_SCALES,
    [R_COLUMN] = UNSIGNED + 13,
    [R_ROOT] = UNSIGNED + 12,
    [R_DLYCLK] = SIGNED + 13, // max 1 bar synced
    [R_SEQCLK] = UNSIGNED + NUM_SYNC_DIVS + 1,
    [R_DUACLK] = SIGNED + NUM_SYNC_DIVS,
    [R_EUCLEN] = UNSIGNED + 16,
    [R_ARPORD] = UNSIGNED + NUM_ARP_ORDERS,
    [R_ARPOCT] = UNSIGNED + 4,
    [R_SEQORD] = UNSIGNED + NUM_SEQ_ORDERS,
    [R_SAMPLE] = UNSIGNED + NUM_SAMPLES + 1,
    [R_PATN] = UNSIGNED + NUM_PATTERNS,
    [R_STOFFS] = SIGNED + 65,
    [R_LFOSHP] = UNSIGNED + NUM_LFO_SHAPES,
    [R_UNUSED] = 0,
};

// clang-format off

const static RangeType range_type[NUM_PARAMS] = {
   [P_SHAPE] = R_SVALUE,       [P_DISTORTION] = R_UVALUE,   [P_PITCH] = R_SVALUE,         [P_OCT] = R_OCTAVE,         [P_GLIDE] = R_UVALUE,         [P_INTERVAL] = R_SVALUE,      // Sound 1
   [P_NOISE] = R_UVALUE,       [P_RESO] = R_UVALUE,         [P_DEGREE] = R_DEGREE,        [P_SCALE] = R_SCALE,        [P_MICROTONE] = R_UVALUE,     [P_COLUMN] = R_COLUMN,        // Sound 2
   [P_ENV_LVL1] = R_UVALUE,    [P_ATTACK1] = R_UVALUE,      [P_DECAY1] = R_UVALUE,        [P_SUSTAIN1] = R_UVALUE,    [P_RELEASE1] = R_UVALUE,      [P_ROOT] = R_ROOT,            // Envelope 1
   [P_ENV_LVL2] = R_UVALUE,    [P_ATTACK2] = R_UVALUE,      [P_DECAY2] = R_UVALUE,        [P_SUSTAIN2] = R_UVALUE,    [P_RELEASE2] = R_UVALUE,      [P_ENV2_UNUSED] = R_UNUSED,   // Envelope 2
   [P_DLY_SEND] = R_UVALUE,    [P_DLY_TIME] = R_DLYCLK,     [P_PING_PONG] = R_UVALUE,     [P_DLY_WOBBLE] = R_UVALUE,  [P_DLY_FEEDBACK] = R_UVALUE,  [P_TEMPO] = R_SVALUE,         // Delay
   [P_RVB_SEND] = R_UVALUE,    [P_RVB_TIME] = R_UVALUE,     [P_SHIMMER] = R_UVALUE,       [P_RVB_WOBBLE] = R_UVALUE,  [P_RVB_UNUSED] = R_UNUSED,    [P_SWING] = R_SVALUE,         // Reverb
   [P_ARP_TGL] = R_BINARY,     [P_ARP_ORDER] = R_ARPORD,    [P_ARP_CLK_DIV] = R_DUACLK,   [P_ARP_CHANCE] = R_SVALUE,  [P_ARP_EUC_LEN] = R_EUCLEN,   [P_ARP_OCTAVES] = R_ARPOCT,   // Arp
   [P_LATCH_TGL] = R_BINARY,   [P_SEQ_ORDER] = R_SEQORD,    [P_SEQ_CLK_DIV] = R_SEQCLK,   [P_SEQ_CHANCE] = R_SVALUE,  [P_SEQ_EUC_LEN] = R_EUCLEN,   [P_GATE_LENGTH] = R_UVALUE,   // Sequencer
   [P_SCRUB] = R_UVALUE,       [P_GR_SIZE] = R_UVALUE,      [P_PLAY_SPD] = R_SVALUE,      [P_SMP_STRETCH] = R_SVALUE, [P_SAMPLE] = R_SAMPLE,        [P_PATTERN] = R_PATN,         // Sampler 1
   [P_SCRUB_JIT] = R_UVALUE,   [P_GR_SIZE_JIT] = R_UVALUE,  [P_PLAY_SPD_JIT] = R_UVALUE,  [P_SMP_UNUSED1] = R_UNUSED, [P_SMP_UNUSED2] = R_UNUSED,   [P_STEP_OFFSET] = R_STOFFS,   // Sampler 2
   [P_A_SCALE] = R_SVALUE,     [P_A_OFFSET] = R_SVALUE,     [P_A_DEPTH] = R_SVALUE,       [P_A_RATE] = R_DUACLK,      [P_A_SHAPE] = R_LFOSHP,       [P_A_SYM] = R_SVALUE,         // LFO A
   [P_B_SCALE] = R_SVALUE,     [P_B_OFFSET] = R_SVALUE,     [P_B_DEPTH] = R_SVALUE,       [P_B_RATE] = R_DUACLK,      [P_B_SHAPE] = R_LFOSHP,       [P_B_SYM] = R_SVALUE,         // LFO B
   [P_X_SCALE] = R_SVALUE,     [P_X_OFFSET] = R_SVALUE,     [P_X_DEPTH] = R_SVALUE,       [P_X_RATE] = R_DUACLK,      [P_X_SHAPE] = R_LFOSHP,       [P_X_SYM] = R_SVALUE,         // LFO X
   [P_Y_SCALE] = R_SVALUE,     [P_Y_OFFSET] = R_SVALUE,     [P_Y_DEPTH] = R_SVALUE,       [P_Y_RATE] = R_DUACLK,      [P_Y_SHAPE] = R_LFOSHP,       [P_Y_SYM] = R_SVALUE,         // LFO Y
   [P_SYN_LVL] = R_UVALUE,     [P_SYN_WET_DRY] = R_UVALUE,  [P_HPF] = R_UVALUE,           [P_MIX_UNUSED1] = R_UNUSED, [P_MIX_UNUSED4] = R_UNUSED,   [P_VOLUME] = R_UVALUE,        // Mixer 1
   [P_IN_LVL] = R_UVALUE,      [P_IN_WET_DRY] = R_UVALUE,   [P_SYS_UNUSED1] = R_UNUSED,   [P_MIX_UNUSED2] = R_UNUSED, [P_MIX_UNUSED3] = R_UNUSED,   [P_MIX_WIDTH] = R_UVALUE,     // Mixer 2
};

const static Preset init_params = {
    .seq_start = 0,
    .seq_len = 8,
    .version = LPE_PRESET_VERSION,
    .params = {
        [P_SHAPE] = {0},            [P_DISTORTION] = {RAW_HALF},                                [P_PITCH] = {0},                                    [P_OCT] = {0},                  [P_GLIDE] = {0},                            [P_INTERVAL] = {0},                 // Sound 1
        [P_NOISE] = {0},            [P_RESO] = {0},                                             [P_DEGREE] = {0},                                   [P_SCALE] = {0},                [P_MICROTONE] = {RAW_EIGHTH},               [P_COLUMN] = {INDEX_TO_RAW(7, 13)}, // Sound 2
        [P_ENV_LVL1] = {RAW_HALF},  [P_ATTACK1] = {RAW_EIGHTH},                                 [P_DECAY1] = {RAW_QUART},                           [P_SUSTAIN1] = {RAW_SIZE},      [P_RELEASE1] = {RAW_EIGHTH},                [P_ROOT] = {0},                     // Envelope 1
        [P_ENV_LVL2] = {RAW_HALF},  [P_ATTACK2] = {RAW_EIGHTH},                                 [P_DECAY2] = {RAW_QUART},                           [P_SUSTAIN2] = {RAW_SIZE},      [P_RELEASE2] = {RAW_EIGHTH},                [P_ENV2_UNUSED] = {},               // Envelope 2
        [P_DLY_SEND] = {0},         [P_DLY_TIME] = {INDEX_TO_RAW(3, NUM_SYNC_DIVS)},            [P_PING_PONG] = {RAW_SIZE},                         [P_DLY_WOBBLE] = {RAW_QUART},   [P_DLY_FEEDBACK] = {RAW_HALF},              [P_TEMPO] = {0},                    // Delay
        [P_RVB_SEND] = {RAW_QUART}, [P_RVB_TIME] = {RAW_HALF},                                  [P_SHIMMER] = {RAW_QUART},                          [P_RVB_WOBBLE] = {RAW_QUART},   [P_RVB_UNUSED] = {},                        [P_SWING] = {0},                    // Reverb
        [P_ARP_TGL] = {0},          [P_ARP_ORDER] = {INDEX_TO_RAW(ARP_UP, NUM_ARP_ORDERS)},     [P_ARP_CLK_DIV] = {INDEX_TO_RAW(2, NUM_SYNC_DIVS)}, [P_ARP_CHANCE] = {RAW_SIZE},    [P_ARP_EUC_LEN] = {INDEX_TO_RAW(8, 17)},    [P_ARP_OCTAVES] = {0},              // Arp
        [P_LATCH_TGL] = {0},        [P_SEQ_ORDER] = {INDEX_TO_RAW(SEQ_ORD_FWD, NUM_SEQ_ORDERS)},[P_SEQ_CLK_DIV] = {INDEX_TO_RAW(5, NUM_SYNC_DIVS)}, [P_SEQ_CHANCE] = {RAW_SIZE},    [P_SEQ_EUC_LEN] = {INDEX_TO_RAW(8, 17)},    [P_GATE_LENGTH] = {RAW_SIZE},       // Sequencer
        [P_SCRUB] = {0},            [P_GR_SIZE] = {RAW_HALF},                                   [P_PLAY_SPD] = {RAW_HALF},                          [P_SMP_STRETCH] = {RAW_HALF},   [P_SAMPLE] = {0},                           [P_PATTERN] = {0},                  // Sampler 1
        [P_SCRUB_JIT] = {0},        [P_GR_SIZE_JIT] = {0},                                      [P_PLAY_SPD_JIT] = {0},                             [P_SMP_UNUSED1] = {},           [P_SMP_UNUSED2] = {},                       [P_STEP_OFFSET] = {0},              // Sampler 2
        [P_A_SCALE] = {RAW_HALF},   [P_A_OFFSET] = {0},                                         [P_A_DEPTH] = {0},                                  [P_A_RATE] = {-RAW_HALF},       [P_A_SHAPE] = {0},                          [P_A_SYM] = {0},                    // LFO A
        [P_B_SCALE] = {RAW_HALF},   [P_B_OFFSET] = {0},                                         [P_B_DEPTH] = {0},                                  [P_B_RATE] = {-562},            [P_B_SHAPE] = {0},                          [P_B_SYM] = {0},                    // LFO B
        [P_X_SCALE] = {RAW_HALF},   [P_X_OFFSET] = {0},                                         [P_X_DEPTH] = {0},                                  [P_X_RATE] = {-451},            [P_X_SHAPE] = {0},                          [P_X_SYM] = {0},                    // LFO X
        [P_Y_SCALE] = {RAW_HALF},   [P_Y_OFFSET] = {0},                                         [P_Y_DEPTH] = {0},                                  [P_Y_RATE] = {-355},            [P_Y_SHAPE] = {0},                          [P_Y_SYM] = {0},                    // LFO Y
        [P_SYN_LVL] = {RAW_HALF},   [P_SYN_WET_DRY] = {RAW_HALF},                               [P_HPF] = {0},                                      [P_MIX_UNUSED1] = {},           [P_MIX_UNUSED4] = {},                       [P_VOLUME] = {0},                   // Mixer 1
        [P_IN_LVL] = {RAW_HALF},    [P_IN_WET_DRY] = {RAW_HALF},                                [P_SYS_UNUSED1] = {},                               [P_MIX_UNUSED2] = {},           [P_MIX_UNUSED3] = {},                       [P_MIX_WIDTH] = {RAW_SIZE * 7 / 8}, // Mixer 2
    },
    .poly_params = {
        [PP_SHAPE]       = {0, 0, 0, 0, 0, 0, 0},
        [PP_DISTORTION]  = {RAW_HALF, RAW_HALF, RAW_HALF, RAW_HALF, RAW_HALF, RAW_HALF, RAW_HALF},
        [PP_PITCH]       = {0, 0, 0, 0, 0, 0, 0},
        [PP_OCT]         = {0, 0, 0, 0, 0, 0, 0},
        [PP_GLIDE]       = {0, 0, 0, 0, 0, 0, 0},
        [PP_INTERVAL]    = {0, 0, 0, 0, 0, 0, 0},
        [PP_NOISE]       = {0, 0, 0, 0, 0, 0, 0},
        [PP_RESO]        = {0, 0, 0, 0, 0, 0, 0},
        [PP_DEGREE]      = {0, 0, 0, 0, 0, 0, 0},
        [PP_SCALE]       = {0, 0, 0, 0, 0, 0, 0},
        [PP_MICROTONE]   = {RAW_EIGHTH, RAW_EIGHTH, RAW_EIGHTH, RAW_EIGHTH, RAW_EIGHTH, RAW_EIGHTH, RAW_EIGHTH},
        [PP_COLUMN]      = {INDEX_TO_RAW(7, 13), INDEX_TO_RAW(7, 13), INDEX_TO_RAW(7, 13), INDEX_TO_RAW(7, 13), INDEX_TO_RAW(7, 13), INDEX_TO_RAW(7, 13), INDEX_TO_RAW(7, 13)},
        [PP_ENV_LVL1]    = {RAW_HALF, RAW_HALF, RAW_HALF, RAW_HALF, RAW_HALF, RAW_HALF, RAW_HALF},
        [PP_ATTACK1]     = {RAW_EIGHTH, RAW_EIGHTH, RAW_EIGHTH, RAW_EIGHTH, RAW_EIGHTH, RAW_EIGHTH, RAW_EIGHTH},
        [PP_DECAY1]      = {RAW_QUART, RAW_QUART, RAW_QUART, RAW_QUART, RAW_QUART, RAW_QUART, RAW_QUART},
        [PP_SUSTAIN1]    = {RAW_SIZE, RAW_SIZE, RAW_SIZE, RAW_SIZE, RAW_SIZE, RAW_SIZE, RAW_SIZE},
        [PP_RELEASE1]    = {RAW_EIGHTH, RAW_EIGHTH, RAW_EIGHTH, RAW_EIGHTH, RAW_EIGHTH, RAW_EIGHTH, RAW_EIGHTH},
        [PP_ROOT]        = {0, 0, 0, 0, 0, 0, 0},
        [PP_ENV_LVL2]    = {RAW_HALF, RAW_HALF, RAW_HALF, RAW_HALF, RAW_HALF, RAW_HALF, RAW_HALF},
        [PP_ATTACK2]     = {RAW_EIGHTH, RAW_EIGHTH, RAW_EIGHTH, RAW_EIGHTH, RAW_EIGHTH, RAW_EIGHTH, RAW_EIGHTH},
        [PP_DECAY2]      = {RAW_QUART, RAW_QUART, RAW_QUART, RAW_QUART, RAW_QUART, RAW_QUART, RAW_QUART},
        [PP_SUSTAIN2]    = {RAW_SIZE, RAW_SIZE, RAW_SIZE, RAW_SIZE, RAW_SIZE, RAW_SIZE, RAW_SIZE},
        [PP_RELEASE2]    = {RAW_EIGHTH, RAW_EIGHTH, RAW_EIGHTH, RAW_EIGHTH, RAW_EIGHTH, RAW_EIGHTH, RAW_EIGHTH},
        [PP_SCRUB]       = {0, 0, 0, 0, 0, 0, 0},
        [PP_GR_SIZE]     = {RAW_HALF, RAW_HALF, RAW_HALF, RAW_HALF, RAW_HALF, RAW_HALF, RAW_HALF},
        [PP_PLAY_SPD]    = {RAW_HALF, RAW_HALF, RAW_HALF, RAW_HALF, RAW_HALF, RAW_HALF, RAW_HALF},
        [PP_SMP_STRETCH] = {RAW_HALF, RAW_HALF, RAW_HALF, RAW_HALF, RAW_HALF, RAW_HALF, RAW_HALF},
        [PP_SCRUB_JIT]   = {0, 0, 0, 0, 0, 0, 0},
        [PP_GR_SIZE_JIT] = {0, 0, 0, 0, 0, 0, 0},
        [PP_PLAY_SPD_JIT] = {0, 0, 0, 0, 0, 0, 0},
    }
};