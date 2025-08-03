#pragma once
#include "utils.h"

extern ArpOrder arp_order;
extern s8 arp_oct_offset;

void arp_next_strings_frame_trig(void);

u8 arp_tick(u8 string_touch_mask);
void arp_reset(void);