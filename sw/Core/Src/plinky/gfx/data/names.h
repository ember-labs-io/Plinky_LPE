#include "hardware/cv.h"
#include "icons.h"
#include "synth/arp.h"
#include "synth/lfos.h"
#include "synth/params.h"
#include "synth/sequencer.h"

const static char* const param_page_names[PG_NUM_PAGES] = {

    [PG_SOUND1] = I_SLIDERS "Sound",
    [PG_SOUND2] = I_PIANO "Sound",
    [PG_ENV1] = I_ADSR_A "Env1",
    [PG_ENV2] = I_ADSR_A "Env2",
    [PG_ARP] = I_NOTES "Arp",
    [PG_SEQ] = I_NOTES "Seq",
    [PG_DELAY] = I_DELAY "Delay",
    [PG_REVERB] = I_REVERB "Reverb",
    [PG_A] = I_A,
    [PG_B] = I_B,
    [PG_X] = I_X,
    [PG_Y] = I_Y,
    [PG_SAMPLER] = I_WAVE "Sampler",
    [PG_JITTER] = I_RANDOM "Jitter"

};

const static char* const param_names[NUM_PARAMS] = {
    [P_ATTACK2] = I_ADSR_A "Attack2",
    [P_DECAY2] = I_ADSR_D "Decay2",
    [P_SUSTAIN2] = I_ADSR_S "Sustain2",
    [P_RELEASE2] = I_ADSR_R "Release2",
    [P_SWING] = I_TEMPO "Swing",

    [P_ENV_LVL1] = I_TOUCH "Sensitivity",
    [P_DISTORTION] = I_DISTORT "Distort",
    [P_ATTACK1] = I_ADSR_A "Attack",
    [P_DECAY1] = I_ADSR_D "Decay",
    [P_SUSTAIN1] = I_ADSR_S "Sustain",
    [P_RELEASE1] = I_ADSR_R "Release",

    [P_SYNTH_LVL] = I_WAVE "Synth Lvl",
    [P_INPUT_LVL] = I_JACK "Input Lvl",
    [P_INPUT_WET_DRY] = I_JACK "In Wet/Dry",
    [P_SYNTH_WET_DRY] = I_REVERB "Main Wet/Dry",
    [P_HPF] = I_HPF "High Pass",
    [P_RESO] = I_DISTORT "Resonance",

    [P_OCT] = I_OCTAVE "Octave",
    [P_PITCH] = I_PIANO "Pitch",
    [P_GLIDE] = I_GLIDE "Glide",
    [P_INTERVAL] = I_INTERVAL "Interval",
    [P_GATE_LENGTH] = I_INTERVAL "Gate Len",
    [P_ENV_LVL2] = I_AMPLITUDE "Env Level",

    [P_SHAPE] = "Shape",
    [P_RVB_UNUSED] = "<unused>",

    [P_SCALE] = I_PIANO "Scale",
    [P_DEGREE] = I_FEEDBACK "Degree",
    [P_MICROTONE] = I_MICRO "Microtone",
    [P_COLUMN] = I_INTERVAL "Stride",

    [P_ARP_TOGGLE] = I_NOTES "Arp On/Off",
    [P_LATCH_TOGGLE] = "Latch On/Off",

    [P_ARP_ORDER] = I_ORDER "Arp",
    [P_ARP_CLK_DIV] = I_DIVIDE "Divide",
    [P_ARP_CHANCE] = I_PERCENT "Prob %",
    [P_ARP_EUC_LEN] = I_LENGTH "Euclid Len",
    [P_ARP_OCTAVES] = I_OCTAVE "Octaves",
    [P_TEMPO] = "BPM",

    [P_SEQ_ORDER] = I_ORDER "Seq",
    [P_SEQ_CLK_DIV] = I_DIVIDE "Divide",
    [P_SEQ_CHANCE] = I_PERCENT "Prob %",
    [P_SEQ_EUC_LEN] = I_LENGTH "Euclid Len",
    [P_STEP_OFFSET] = I_SEQ "Step Ofs",
    [P_PATTERN] = I_PRESET "Pattern",

    [P_DLY_SEND] = I_SEND "Send",
    [P_DLY_TIME] = I_TIME "Time",
    [P_DLY_FEEDBACK] = I_FEEDBACK "Feedback",
    //[P_DLCOLOR]=I_COLOR "Colour",
    [P_DLY_WOBBLE] = I_AMPLITUDE "Wobble",
    [P_DLY_PINGPONG] = I_DIVIDE "2nd Tap",

    [P_RVB_SEND] = I_SEND "Send",
    [P_RVB_TIME] = I_TIME "Time",
    [P_RVB_SHIMMER] = I_FEEDBACK "Shimmer",
    //[P_RVCOLOR]=I_COLOR "Colour",
    [P_RVB_WOBBLE] = I_AMPLITUDE "Wobble",
    //[P_RVB_UNUSED]=" ",

    [P_SAMPLE] = I_WAVE "Sample",
    [P_SMP_SCRUB] = "Scrub",
    [P_SMP_SPEED] = I_NOTES "Rate",
    [P_SMP_GRAINSIZE] = I_PERIOD "Grain Sz",
    [P_SMP_STRETCH] = I_TIME "Timestretch",
    [P_CV_QUANT] = I_JACK "CV Quantise",

    [P_ACCEL_SENS] = I_AMPLITUDE "Accel Sens",
    [P_MIX_WIDTH] = I_AMPLITUDE "Stereo Width",
    [P_NOISE] = I_WAVE "Noise",
    [P_SMP_SCRUB_JIT] = "Scrub",
    [P_SMP_SPEED_JIT] = I_NOTES "Rate",
    [P_SMP_GRAINSIZE_JIT] = I_PERIOD "Grain Sz",
    [P_SMP_UNUSED1] = I_ENV "<unused>",
    [P_VOLUME] = "'Phones Vol",

    [P_A_OFFSET] = I_OFFSET "CV Offset",
    [P_A_SCALE] = I_TIMES "CV Scale",
    [P_A_DEPTH] = I_AMPLITUDE "LFO Depth",
    [P_A_RATE] = I_PERIOD "LFO Rate",
    [P_A_SHAPE] = I_SHAPE "LFO Shape",
    [P_A_SYM] = I_WARP "LFO Warp",

    [P_B_OFFSET] = I_OFFSET "CV Offset",
    [P_B_SCALE] = I_TIMES "CV Scale",
    [P_B_DEPTH] = I_AMPLITUDE "LFO Depth",
    [P_B_RATE] = I_PERIOD "LFO Rate",
    [P_B_SHAPE] = I_SHAPE "LFO Shape",
    [P_B_SYM] = I_WARP "LFO Warp",

    [P_X_OFFSET] = I_OFFSET "CV Offset",
    [P_X_SCALE] = I_TIMES "CV Scale",
    [P_X_DEPTH] = I_AMPLITUDE "LFO Depth",
    [P_X_RATE] = I_PERIOD "LFO Rate",
    [P_X_SHAPE] = I_SHAPE "LFO Shape",
    [P_X_SYM] = I_WARP "LFO Warp",

    [P_Y_OFFSET] = I_OFFSET "CV Offset",
    [P_Y_SCALE] = I_TIMES "CV Scale",
    [P_Y_DEPTH] = I_AMPLITUDE "LFO Depth",
    [P_Y_RATE] = I_PERIOD "LFO Rate",
    [P_Y_SHAPE] = I_SHAPE "LFO Shape",
    [P_Y_SYM] = I_WARP "LFO Warp",

    [P_MIDI_CH_IN] = I_PIANO "MIDI In Ch",
    [P_MIDI_CH_OUT] = I_PIANO "MIDI Out Ch",

};

const static char* const mod_names[NUM_MOD_SOURCES] = {
    [SRC_BASE] = I_SLIDERS "Base",   [SRC_RND] = I_RANDOM "Random", [SRC_ENV2] = I_ENV "Env",
    [SRC_PRES] = I_TOUCH "Pressure", [SRC_LFO_A] = I_A "Knob/LFO",  [SRC_LFO_B] = I_B "Knob/LFO",
    [SRC_LFO_X] = I_X "CV/LFO",      [SRC_LFO_Y] = I_Y "CV/LFO",
};

const static char* const arp_modenames[NUM_ARP_ORDERS] = {
    [ARP_UP] = "Up",
    [ARP_DOWN] = "Down",
    [ARP_UPDOWN] = "Up/Down",
    [ARP_UPDOWN_REP] = "Up/Down\nRepeat",
    [ARP_PEDAL_UP] = "Pedal    \nUp",
    [ARP_PEDAL_DOWN] = "Pedal    \nDown",
    [ARP_PEDAL_UPDOWN] = "Pedal\nUp/Down",
    [ARP_RANDOM] = "Rnd",
    [ARP_RANDOM2] = "2xRnd",
    [ARP_CHORD] = "Chord",
    [ARP_UP8] = "Up\n8 Steps",
    [ARP_DOWN8] = "Down\n8 Steps",
    [ARP_UPDOWN8] = "Up/Down\n8 Steps",
    [ARP_RANDOM8] = "Rnd\n8 Steps",
    [ARP_RANDOM28] = "2xRnd\n8 Steps",
};

const static char* const seqmodenames[NUM_SEQ_ORDERS] = {
    [SEQ_ORD_PAUSE] = "Pause",
    [SEQ_ORD_FWD] = "Forward",
    [SEQ_ORD_BACK] = "Reverse",
    [SEQ_ORD_PINGPONG] = "Pingpong",
    [SEQ_ORD_PINGPONG_REP] = "PingPong Rep",
    [SEQ_ORD_RANDOM] = "Random",
};

static const char* const cvquantnames[CVQ_LAST] = {
    [CVQ_OFF] = "Off",
    [CVQ_ON] = "On",
    [CVQ_SCALE] = "Scale",
};

static const char* const lfo_names[NUM_LFO_SHAPES] = {
    [LFO_TRI] = "Triangle",
    [LFO_SIN] = "Sine",
    [LFO_SMOOTH_NOISE] = "SmthRnd",
    [LFO_STEP_NOISE] = "StepRnd",
    [LFO_BI_SQUARE] = "BiSquare",
    [LFO_SQUARE] = "Square",
    [LFO_CASTLE] = "Castle",
    [LFO_BI_TRIGS] = "BiTrigs",
    [LFO_TRIGS] = "Trigs",
    [LFO_ENV] = "Env",
    [LFO_SAW] = "Saw",
};

static const char* const preset_cats[CAT_LAST] = {
    "",    "Bass",    "Leads",   "Pads", "Arps",  "Plinks",  "Plonks", "Beeps",  "Boops",
    "SFX", "Line-In", "Sampler", "Donk", "Jolly", "Sadness", "Wild",   "Gnarly", "Weird",
};
