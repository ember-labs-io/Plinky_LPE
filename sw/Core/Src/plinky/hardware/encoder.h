#pragma once
#include "utils.h"

extern volatile bool encoder_pressed;

bool enc_recently_used(void);
void clear_last_encoder_use(void);

void init_encoder(void);
void encoder_irq(void);
void encoder_tick(void);