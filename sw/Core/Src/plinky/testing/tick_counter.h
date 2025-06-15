#pragma once
#include "utils.h"

typedef struct TickCounter {
	u32 starttime;
	u32 total;
	u32 max;
	u32 n;
} TickCounter;

static inline void tc_init(void) {
	DWT->CTRL |= 1;
	DWT->CYCCNT = 0; // reset the counter
}

static inline void tc_start(TickCounter* r) {
	r->starttime = RDTSC();
}

static inline void tc_stop(TickCounter* r) {
	if (!r->starttime)
		return;
	int c = RDTSC() - r->starttime;
	r->n++;
	r->max = maxi(r->max, c);
	r->total += c;
}

static inline void tc_reset(TickCounter* r) {
	r->total = 0;
	r->max = 0;
	r->n = 0;
}

static inline void tc_log(TickCounter* r, const char* nm) {
	if (!r->n)
		return;
	TickCounter tc = *r;
	tc_reset(r);
	DebugLog("%s - %d mx:%d %d / ", nm, tc.total / tc.n, tc.max, tc.n);
}

static inline void tc_gfx_log(TickCounter* r, const char* nm) {
	u32 avg = r->total / r->n;
	gfx_debug(0, "%s avg %i.%ik", nm, avg / 1000, (avg % 1000) / 10);
	gfx_debug(1, "%s max %i.%ik", nm, r->max / 1000, (r->max % 1000) / 10);
}