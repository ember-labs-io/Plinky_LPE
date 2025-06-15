#pragma once
#include "utils.h"

extern volatile bool encoder_pressed;
extern volatile s8 encoder_value;

bool enc_recently_used(void);

void encoder_init(void);
void encoder_irq(void);
void encoder_tick(void);