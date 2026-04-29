#pragma once
#include "utils.h"

void init_sysex(void);
void process_sysex_byte(u8 byte);
bool draw_midi_tuning_name(void);