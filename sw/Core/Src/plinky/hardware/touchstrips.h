#pragma once
#include "utils.h"

// this module manages physical touches on the touch-sensors
// - we define nine touchstrips (8 synth columns + 1 row of shift buttons), each of which have two capacitive sensors
// - each touchstrip gets read up to two times per cycle, leading to 18 touch readings and 36 saved sensor values
// - after processing, these readings are reduced to 9 touches

extern u8 touch_frame;

TouchCalibData* touch_calib_ptr(void);

// get touch info

bool touch_read_this_frame(u8 strip_id);
Touch* get_touch_prev(u8 touch_id, u8 frames_back);

// main

void init_touchstrips(void);
u8 read_touchstrips(void);

// calib

void touch_calib(FlashCalibType flash_calib_type);
