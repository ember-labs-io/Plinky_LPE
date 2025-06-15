#pragma once
#include "utils.h"
#include <assert.h>

#define NUM_KNOBS 2

#define NUM_PRESETS 32
#define NUM_PATTERNS 24
#define NUM_SAMPLES 8

#define CUR_PRESET_VERSION 2
#define MAX_PTN_STEPS 64
#define PTN_STEPS_PER_QTR (MAX_PTN_STEPS / 4)
#define PTN_SUBSTEPS 8

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

typedef struct SysParams {
	u8 curpreset;
	u8 paddy;
	u8 systemflags;
	s8 headphonevol;
	u8 pad[16 - 4];
} SysParams;

typedef struct PageFooter {
	u8 idx; // preset 0-31, pattern (quarters!) 32-127, sample 128-136, blank=0xff
	u8 version;
	u16 crc;
	u32 seq;
} PageFooter;

typedef struct Preset {
	s16 params[96][8];
	u8 flags;
	u8 seq_start;
	u8 seq_len;
	u8 paddy[3];
	u8 version;
	u8 category;
	u8 name[8];
} Preset;
static_assert((sizeof(Preset) & 15) == 0, "?");
static_assert(sizeof(Preset) + sizeof(SysParams) + sizeof(PageFooter) <= 2048, "?");

typedef struct PatternStringStep {
	u8 pos[PTN_SUBSTEPS / 2];
	u8 pres[PTN_SUBSTEPS];
} PatternStringStep;

typedef struct PatternQuarter {
	PatternStringStep steps[PTN_STEPS_PER_QTR][NUM_STRINGS];
	s8 autoknob[PTN_STEPS_PER_QTR * PTN_SUBSTEPS][NUM_KNOBS];
} PatternQuarter;
static_assert(sizeof(PatternQuarter) + sizeof(SysParams) + sizeof(PageFooter) <= 2048, "?");
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
static_assert(sizeof(SampleInfo) + sizeof(SysParams) + sizeof(PageFooter) <= 2048, "?");
static_assert((sizeof(SampleInfo) & 15) == 0, "?");

typedef struct FlashPage {
	union {
		u8 raw[FLASH_PAGE_SIZE - sizeof(SysParams) - sizeof(PageFooter)];
		Preset preset;
		PatternQuarter pattern_quarter;
		SampleInfo sample_info;
	};
	SysParams sys_params;
	PageFooter footer;
} FlashPage;
static_assert(sizeof(FlashPage) == 2048, "?");