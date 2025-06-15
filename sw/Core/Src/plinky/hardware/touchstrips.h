#pragma once
#include "utils.h"

// this module manages physical touches on the touch-sensors
// - we define nine touchstrips (8 synth columns + 1 row of shift buttons), each of which have two capacitive sensors
// - each touchstrip gets read up to two times per cycle, leading to 18 touch readings and 36 saved sensor values
// - after processing, these readings are reduced to 9 touches

#define NUM_TOUCHES 9
#define NUM_TOUCH_FRAMES 8
#define NUM_TOUCH_READINGS 18
#define TOUCH_MIN_POS 0
#define TOUCH_MAX_POS 2047

// full pressure is defined as the point where the current pressure reaches the calibrated pressure, this will result in
// envelope 1 fully opening - pressure values beyond this do occur, but will not affect the sound any further
#define TOUCH_MIN_PRES -2048
#define TOUCH_FULL_PRES 2047

extern u8 touch_frame;

void MX_TSC_Init(void);
CalibData* touch_calib_ptr(void);

// get touch info

bool touch_read_this_frame(u8 strip_id);
Touch* get_touch_prev(u8 touch_id, u8 frames_back);

// main

u8 read_touchstrips(void);
void reset_touches(void);