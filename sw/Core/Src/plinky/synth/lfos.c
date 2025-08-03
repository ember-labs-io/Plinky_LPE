#include "lfos.h"
#include "data/tables.h"
#include "gfx/gfx.h"
#include "hardware/accelerometer.h"
#include "hardware/adc_dac.h"
#include "hardware/expander.h"
#include "hardware/ram.h"
#include "params.h"
#include "time.h"

#define LFO_SCOPE_FRAMES 16

s32 param_with_lfo[NUM_PARAMS];

static s32 lfo_cur[NUM_LFOS];
static u8 lfo_scope_frame = 0;
static u8 lfo_scope_data[LFO_SCOPE_FRAMES][NUM_LFOS];

// random float value normalized to [-1, 1)
static float rnd_norm(u16 half_cycle) {
	return (float)(rndtab[half_cycle] * (2.f / 256.f) - 1.f);
}
static float eval_tri(float pos, u32 half_cycle) {
	return 1.f - (pos + pos);
}
// unipolar pseudo exponential up/down
static float eval_env(float pos, u32 half_cycle) {
	if (half_cycle & 1) {
		pos *= pos;
		pos *= pos;
		return pos;
	}
	else {
		pos = 1.f - pos;
		pos *= pos;
		pos *= pos;
		return 1.f - pos;
	}
}
static float eval_sin(float pos, u32 half_cycle) {
	pos = pos * pos * (3.f - pos - pos);
	return 1.f - (pos + pos);
}
static float eval_saw(float pos, u32 half_cycle) {
	return (half_cycle & 1) ? pos - 1.f : 1.f - pos;
}
static float eval_square(float pos, u32 half_cycle) {
	return (half_cycle & 1) ? 0.f : 1.f;
}
static float eval_bi_square(float pos, u32 half_cycle) {
	return (half_cycle & 1) ? -1.f : 1.f;
}
static float eval_castle(float pos, u32 half_cycle) {
	return (half_cycle & 1) ? ((pos < 0.5f) ? 0.f : -1.f) : ((pos < 0.5f) ? 1.f : 0.f);
}
// generates a sharp triangle
static float triggy(float pos) {
	pos = 1.f - (pos + pos);
	pos = pos * pos;
	return pos * pos;
}
static float eval_trigs(float pos, u32 half_cycle) {
	if (half_cycle & 1)
		pos = 1 - pos;
	return pos < 0.5f ? triggy(pos) : 0.f;
}
static float eval_bi_trigs(float pos, u32 half_cycle) {
	if (half_cycle & 1)
		pos = 1 - pos;
	return pos < 0.5f ?                                      //
	           (half_cycle & 1) ? -triggy(pos) : triggy(pos) //
	                  : 0.f;
}
static float eval_step_rand(float pos, u32 half_cycle) {
	return rnd_norm(half_cycle);
}
static float eval_smooth_rand(float pos, u32 half_cycle) {
	float n0 = rnd_norm(half_cycle);
	float n1 = rnd_norm(half_cycle + 1);
	return n0 + (n1 - n0) * ((half_cycle & 1) ? 1.f - pos : pos);
}

static float (*lfo_funcs[NUM_LFO_SHAPES])(float pos, u32 half_cycle) = {
    [LFO_TRI] = eval_tri,
    [LFO_SIN] = eval_sin,
    [LFO_SMOOTH_RAND] = eval_smooth_rand,
    [LFO_STEP_RAND] = eval_step_rand,
    [LFO_BI_SQUARE] = eval_bi_square,
    [LFO_SQUARE] = eval_square,
    [LFO_CASTLE] = eval_castle,
    [LFO_BI_TRIGS] = eval_bi_trigs,
    [LFO_TRIGS] = eval_trigs,
    [LFO_ENV] = eval_env,
    [LFO_SAW] = eval_saw,
};

void update_lfos(void) {
	static u64 lfo_clock_q32[NUM_LFOS] = {0}; // lfo phase acculumator clock, counts half(!) lfo cycles in q32
	static s8 prev_scope_pos[NUM_LFOS] = {0};

	// every 16 frames, lfo_scope_frame increments and data for that frame is cleared
	bool new_scope_frame = (synth_tick & 15) == 0;
	if (new_scope_frame) {
		lfo_scope_frame = (synth_tick >> 4) & 15;
		lfo_scope_data[lfo_scope_frame][0] = 0;
		lfo_scope_data[lfo_scope_frame][1] = 0;
		lfo_scope_data[lfo_scope_frame][2] = 0;
		lfo_scope_data[lfo_scope_frame][3] = 0;
	}

	for (u8 lfo_id = 0; lfo_id < NUM_LFOS; ++lfo_id) {
		u8 lfo_page_offset = lfo_id * 6;
		// apply lfo modulation to the parameters of the lfo itself
		apply_lfo_mods(P_A_RATE + lfo_page_offset);
		apply_lfo_mods(P_A_SYM + lfo_page_offset);
		apply_lfo_mods(P_A_SHAPE + lfo_page_offset);
		apply_lfo_mods(P_A_DEPTH + lfo_page_offset);
		apply_lfo_mods(P_A_OFFSET + lfo_page_offset);
		apply_lfo_mods(P_A_SCALE + lfo_page_offset);

		s32 lfo_rate = param_val(P_A_RATE + lfo_page_offset);
		// free running - resulting lfo rate of this phase diff calculation is roughly 0.037-4913 Hz
		if (lfo_rate <= 0) {
			u32 phase_diff_q32 = (u32)(table_interp(pitches, -lfo_rate) * (1 << 24));
			lfo_clock_q32[lfo_id] += phase_diff_q32;
		}
		// synced
		else {
			u16 step_32nds = sync_divs_32nds[(clampi(lfo_rate, 0, 65535) * NUM_SYNC_DIVS) >> 16];
			u16 prev_phase_q16 = (lfo_clock_q32[lfo_id] >> 16) & 0xFFFF;
			u16 new_phase_q16 = clock_pos_q16(step_32nds);
			// add cycle if the phase rolls over
			if (new_phase_q16 < prev_phase_q16)
				lfo_clock_q32[lfo_id] += ((u64)1 << 32);
			lfo_clock_q32[lfo_id] = (lfo_clock_q32[lfo_id] & 0xFFFFFFFF00000000) | ((u32)new_phase_q16 << 16);
		}
		// calc half cycle & position in cycle
		LfoShape lfo_shape = param_val(P_A_SHAPE + lfo_page_offset);
		u32 lfo_clock_q16 = (u32)(lfo_clock_q32[lfo_id] >> 16);
		float cycle_center = param_val_float(P_A_SYM + lfo_page_offset) * 0.49f + 0.5f; // range [0.01, 0.99]
		u32 half_cycle;
		float cycle_pos;
		switch (lfo_shape) {
		case LFO_SMOOTH_RAND:
		case LFO_STEP_RAND:
		case LFO_BI_TRIGS:
		case LFO_TRIGS:
			half_cycle = (lfo_clock_q16 >> 17) << 1;                 // first half of two cycles
			cycle_pos = (lfo_clock_q16 & 131071) * (1.f / 131071.f); // position in two cycles
			if (cycle_pos < cycle_center)
				cycle_pos /= cycle_center;
			else {
				half_cycle++; // second half of two cycles
				cycle_pos = (1.f - cycle_pos) / (1.f - cycle_center);
			}
			break;
		default:
			half_cycle = (lfo_clock_q16 >> 16) << 1;
			cycle_pos = (lfo_clock_q16 & 65535) * (1.f / 65536.f);
			if (cycle_pos < cycle_center)
				cycle_pos /= cycle_center;
			else {
				half_cycle++;
				cycle_pos = (1.f - cycle_pos) / (1.f - cycle_center);
			}
			// gfx_debug(0, "%d", (u32)(cycle_pos * 1000));
			break;
		}

		s32 lfo_val = (s32)(
		    // call the appropriate evaluation function based on lfo_shape
		    (*lfo_funcs[lfo_shape])(cycle_pos, half_cycle)
		    // multiply by lfo depth param
		    * param_val_float(P_A_DEPTH + lfo_page_offset)
		    // scale to 16fp
		    * 65536.f);

		// cv offset (cv scale param at 100% equals actual scale by 200%)
		lfo_val += (s32)(adc_get_smooth(ADC_S_A_CV + lfo_id) * (param_val(P_A_SCALE + lfo_page_offset) << 1));
		// offset by potentiometers and accelerometer
		lfo_val += (s32)(((lfo_id < 2)
		                      // knob A and B
		                      ? adc_get_smooth(ADC_S_A_KNOB + lfo_id)
		                      // accel X and Y
		                      : accel_get_axis(lfo_id - 2))
		                 // scale to 16fp
		                 * 65536.f);
		// offset from offset param
		lfo_val += param_val(P_A_OFFSET + lfo_page_offset);

		// save lfo positions for oled scope
		s8 old_scope_pos = prev_scope_pos[lfo_id];
		s8 scope_pos = clampi((-(lfo_val * 7 + (1 << 16)) >> 17) + 4, 0, 7);
		bool moving_up = scope_pos > old_scope_pos;
		// a new scope frame always needs to write at least one pixel
		if (new_scope_frame && old_scope_pos == scope_pos)
			lfo_scope_data[lfo_scope_frame][lfo_id] |= 1 << scope_pos;
		// draw line towards the new position
		while (old_scope_pos != scope_pos) {
			old_scope_pos += moving_up ? 1 : -1;
			lfo_scope_data[lfo_scope_frame][lfo_id] |= 1 << old_scope_pos;
		}
		// save position
		prev_scope_pos[lfo_id] = scope_pos;

		// send to expander
		set_expander_lfo_data(lfo_id, lfo_val);

		// save to array for later use
		lfo_cur[lfo_id] = lfo_val;
	}
}

void apply_lfo_mods(Param param_id) {
	if (param_id == P_VOLUME)
		return;
	s16* param = cur_preset.params[param_id];
	s32 new_val = param[SRC_BASE] << 16;
	for (u8 lfo_id = 0; lfo_id < NUM_LFOS; lfo_id++)
		new_val += (lfo_cur[lfo_id] * param[SRC_LFO_A + lfo_id]);
	param_with_lfo[param_id] = new_val;
}

void draw_lfos(void) {
	u8* vr = oled_buffer();
	vr += OLED_WIDTH - 16;
	u8 draw_frame = (lfo_scope_frame + 1) & 15;
	for (u8 x = 0; x < 16; ++x) {
		vr[0] &= ~(lfo_scope_data[draw_frame][0] >> 1);
		vr[128] &= ~(lfo_scope_data[draw_frame][1] >> 1);
		vr[256] &= ~(lfo_scope_data[draw_frame][2] >> 1);
		vr[384] &= ~(lfo_scope_data[draw_frame][3] >> 1);

		vr[0] |= lfo_scope_data[draw_frame][0];
		vr[128] |= lfo_scope_data[draw_frame][1];
		vr[256] |= lfo_scope_data[draw_frame][2];
		vr[384] |= lfo_scope_data[draw_frame][3];
		vr++;
		draw_frame = (draw_frame + 1) & 15;
	}
}