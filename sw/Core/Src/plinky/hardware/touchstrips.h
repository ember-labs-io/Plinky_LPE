#pragma once
#include "utils.h"

// this module manages physical touches on the touch-sensors
// - we define nine touchstrips (8 synth columns + 1 function strip), each of which have two capacitive sensors
// - each touchstrip gets read up to two times per cycle, leading to 18 touch readings and 36 saved sensor values
// - after processing, these readings are reduced to 9 touches

extern u8 touch_frame;
extern u16 strip_touched;

const Touch* get_touch(u8 touch_id, u8 frames_back);

// main

void init_touchstrips(void);
u8 read_touchstrips(void);

// calib

TouchCalibData* touch_calib_ptr(void);
void touch_calib(FlashCalibType flash_calib_type);
