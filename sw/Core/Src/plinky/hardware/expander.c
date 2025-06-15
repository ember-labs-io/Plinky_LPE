#include "expander.h"

#define EXPANDER_ZERO 0x800
#define EXPANDER_RANGE 0x7ff
#define EXPANDER_MAX 0xfff
#define EXPANDER_GAIN 0.715f

static u16 expander_out[4] = {EXPANDER_ZERO, EXPANDER_ZERO, EXPANDER_ZERO, EXPANDER_ZERO};

u16 get_expander_lfo_data(u8 lfo_id) {
	return expander_out[lfo_id];
}

void set_expander_lfo_data(u8 lfo_id, s32 lfo_val) {
	float expander_val = lfo_val * (EXPANDER_GAIN * EXPANDER_RANGE / 65536.f);
	expander_out[lfo_id] = clampi(EXPANDER_ZERO - (int)(expander_val), 0, EXPANDER_MAX);
}