#pragma once
#include "utils.h"

extern volatile bool encoder_pressed;
extern volatile s8 encoder_value;

bool enc_recently_used(void);
void clear_last_encoder_use(void);

void encoder_init(void);
void encoder_irq(void);
void encoder_tick(void);