#include "arp.h"
#include "conditional_step.h"
#include "data/tables.h"
#include "hardware/ram.h"
#include "params.h"
#include "sequencer.h"
#include "strings.h"
#include "time.h"

ArpOrder arp_order = ARP_UP;
s8 arp_oct_offset = 0;

static ConditionalStep c_step;
static u8 arp_touch_mask = 0;        // strings touched by arp
static s8 cur_string = -1;           // for keeping track of the current arp string
static bool moving_down = false;     // are we moving up or down the strings?
static s32 free_clock = 0;           // for keeping track of free running steps
static s8 non_pedal_string = -1;     // for keeping track of the moving string in pedal ArpOrders
static u8 strings_used_by_rand1 = 0; // for keeping tracks of which strings have been used by random ArpOrders
static u8 strings_used_by_rand2 = 0; // this makes random effectively a shuffle, not a true random
static u32 strings_frame_tick = -1;

void arp_next_strings_frame_trig(void) {
	strings_frame_tick = synth_tick;
}

static u8 get_random_bit(u8 mask) {
	if (!mask)
		return 0;
	u8 num = __builtin_popcount(mask);
	num = rand() % num;
	for (; num--;)
		mask &= mask - 1;
	return mask ^ (mask & (mask - 1));
}

// returns whether this wrapped around
static bool inc_cur_string(u8 avail_touch_mask) {
	s8 prev_string = cur_string;
	do {
		cur_string = (cur_string + 1) & 7;
	} while (!(avail_touch_mask & (1 << cur_string)));
	return (prev_string >= cur_string);
}

// returns whether this wrapped around
static bool dec_cur_string(u8 avail_touch_mask) {
	s8 prev_string = cur_string;
	do {
		cur_string = (cur_string - 1) & 7;
	} while (!(avail_touch_mask & (1 << cur_string)));
	return (prev_string <= cur_string);
}

// returns whether this wrapped around
static bool step_up(u8 avail_touch_mask, s8 bottom_oct_offset, u8 top_oct_offset) {
	bool wrapped = inc_cur_string(avail_touch_mask);
	if (wrapped)
		// when wrapping around the top octave
		if (++arp_oct_offset > top_oct_offset)
			switch (arp_order) {
			// up/down orders need to inverse direction
			case ARP_UPDOWN:
			case ARP_PEDAL_UPDOWN:
				dec_cur_string(avail_touch_mask);
			case ARP_UPDOWN_REP:
			case ARP_UPDOWN8:
				moving_down = true;
				arp_oct_offset = top_oct_offset;
				dec_cur_string(avail_touch_mask);
				break;
			// other orders just start again at the bottom
			default:
				arp_oct_offset = bottom_oct_offset;
				break;
			}
	arp_touch_mask = 1 << cur_string;
	return wrapped;
}

// returns whether this wrapped around
static bool step_down(u8 avail_touch_mask, s8 bottom_oct_offset, u8 top_oct_offset) {
	bool wrapped = dec_cur_string(avail_touch_mask);
	if (wrapped)
		// when wrapping around the bottom octave
		if (--arp_oct_offset < bottom_oct_offset)
			switch (arp_order) {
			// up/down orders need to inverse direction
			case ARP_UPDOWN:
			case ARP_PEDAL_UPDOWN:
				inc_cur_string(avail_touch_mask);
			case ARP_UPDOWN_REP:
			case ARP_UPDOWN8:
				moving_down = false;
				arp_oct_offset = bottom_oct_offset;
				inc_cur_string(avail_touch_mask);
				break;
			// other orders just start again at the top
			default:
				arp_oct_offset = top_oct_offset;
				break;
			}
	arp_touch_mask = 1 << cur_string;
	return wrapped;
}

static void step_random(u8 avail_touch_mask, s8 bottom_oct_offset, u8 top_oct_offset) {
	// both random notes play in the same (random) octave
	arp_oct_offset = bottom_oct_offset + (rand() % (top_oct_offset + 1 - bottom_oct_offset));

	u8 strings_left = avail_touch_mask & ~strings_used_by_rand1;
	// no more strings to play
	if (strings_left == 0) {
		// release all strings for random playing
		strings_used_by_rand1 = 0;
		strings_left = avail_touch_mask;
	}

	// get first randomly played string
	arp_touch_mask = get_random_bit(strings_left);
	strings_used_by_rand1 |= arp_touch_mask;

	// we need a second random string, analogous to first but taking out the string that was already generated
	if (arp_order == ARP_RANDOM2 || arp_order == ARP_RANDOM28) {
		strings_left = avail_touch_mask & ~strings_used_by_rand2;
		strings_left &= ~(1 << cur_string);
		if (strings_left == 0) {
			strings_used_by_rand2 = 0;
			strings_left = avail_touch_mask & ~(1 << cur_string);
		}
		if (strings_left) {
			// get second randomly played string
			u8 mask = get_random_bit(strings_left);
			strings_used_by_rand2 |= mask;
			arp_touch_mask |= mask;
		}
	}
}

static void advance_step(u8 avail_touch_mask) {
	arp_touch_mask = 0;
	// 8-step patterns => fake touches on all strings
	if (arp_order >= ARP_UP8)
		avail_touch_mask = 0b11111111;
	// map the used octaves evenly above/below the current octave
	u8 arp_octs = param_val(P_ARP_OCTAVES);
	u8 top_oct_offset = (arp_octs + 1) / 2;
	s8 bottom_oct_offset = top_oct_offset - arp_octs;
	switch (arp_order) {
	case ARP_UP:
	case ARP_UP8:
		step_up(avail_touch_mask, bottom_oct_offset, top_oct_offset);
		break;
	case ARP_DOWN:
	case ARP_DOWN8:
		step_down(avail_touch_mask, bottom_oct_offset, top_oct_offset);
		break;
	case ARP_UPDOWN:
	case ARP_UPDOWN8:
	case ARP_UPDOWN_REP:
		moving_down ? step_down(avail_touch_mask, bottom_oct_offset, top_oct_offset)
		            : step_up(avail_touch_mask, bottom_oct_offset, top_oct_offset);
		break;
	case ARP_PEDAL_DOWN:
	case ARP_PEDAL_UP:
	case ARP_PEDAL_UPDOWN:
		// if we have a remembered non-pedal string and it's still available, we play and clear it
		if (non_pedal_string >= 0 && (avail_touch_mask & (1 << non_pedal_string))) {
			arp_touch_mask = 1 << non_pedal_string;
			non_pedal_string = -1;
		}
		// otherwise, calculate both the current pedal and upcoming non-pedal strings
		else {
			// clear the lowest active (aka pedal) bit
			u8 avail_no_pedal_mask = avail_touch_mask & (avail_touch_mask - 1);
			if (avail_no_pedal_mask == 0)
				avail_no_pedal_mask = avail_touch_mask;
			// use arp functions to define the next non-pedal string and save it for the next step
			if (arp_order == ARP_PEDAL_DOWN || (arp_order == ARP_PEDAL_UPDOWN && moving_down))
				step_down(avail_no_pedal_mask, bottom_oct_offset, top_oct_offset);
			else
				step_up(avail_no_pedal_mask, bottom_oct_offset, top_oct_offset);
			non_pedal_string = cur_string;
			// set the touch mask to the lowest active bit in avail_touch_mask
			arp_touch_mask = avail_touch_mask ^ avail_no_pedal_mask;
		}
		break;
	case ARP_RANDOM:
	case ARP_RANDOM8:
	case ARP_RANDOM2:
	case ARP_RANDOM28:
		step_random(avail_touch_mask, bottom_oct_offset, top_oct_offset);
		break;
	case ARP_CHORD:
		// play all touched strings
		arp_touch_mask = avail_touch_mask;
		// in chord mode with no euclid length, we randomly drop chord notes
		if (abs(c_step.euclid_len) <= 1) {
			u32 dens_abs = abs(c_step.density);
			for (u8 string_id = 0; string_id < 8; ++string_id)
				if (arp_touch_mask & (1 << string_id)) {
					bool play_note = (rand() & 32767) < (dens_abs >> 1);
					if (!play_note)
						arp_touch_mask ^= (1 << string_id);
				}
		}
		break;
	default:
		return;
	}
}

u8 arp_tick(u8 string_touch_mask) {
	static bool step_next_strings_frame = false;

	// update properties
	arp_order = param_val(P_ARP_ORDER);
	c_step.euclid_len = param_val(P_ARP_EUC_LEN);
	c_step.density = param_val(P_ARP_CHANCE);

	// does this tick generate a step?
	bool arp_step = false;
	s32 arp_div = param_val(P_ARP_CLK_DIV);
	// clock synced
	if (arp_div >= 0) {
		u8 step_32nds = sync_divs_32nds[(clampi(arp_div, 0, 65535) * NUM_SYNC_DIVS) >> 16];
		if (pulse_32nd && (counter_32nds % step_32nds == 0)) {
			step_next_strings_frame = true;
			strings_frame_tick = -1; // u32 max
		}
		if (step_next_strings_frame && synth_tick >= strings_frame_tick) {
			step_next_strings_frame = false;
			arp_step = true;
		}
	}
	// free running
	else {
		// reuse the pitches table to turn the linear arp_div value into an exponential time duration
		u32 clock_diff = (u32)(table_interp(pitches, 32768 + (-arp_div >> 2)) * (1 << 24));
		free_clock -= clock_diff; // rewrite to positive
		if (free_clock < 0) {
			free_clock += 1 << 31;
			arp_step = true;
		}
	}

	// no touch
	if (!string_touch_mask) {
		arp_touch_mask = 0;
		return arp_touch_mask;
	}

	// no step
	if (!arp_step)
		return arp_touch_mask;

	// step
	do_conditional_step(&c_step, arp_order == ARP_CHORD);
	// move to the next position, this also fills arp_touch_mask
	if (c_step.advance_step)
		advance_step(string_touch_mask);
	// suppress touches if required by conditional step
	if (!c_step.play_step)
		arp_touch_mask = 0;
	// all arp triggers clean the string to make sure no remnants of the previous note are heard
	for (u8 i = 0; i < NUM_STRINGS; i++)
		if (arp_touch_mask & (1 << i))
			clean_string(i);
	// (re)trigger envelopes
	if (c_step.advance_step)
		env_trig_mask |= arp_touch_mask;
	return arp_touch_mask;
}

void arp_reset(void) {
	arp_touch_mask = 0;
	c_step.euclid_trigs = 0;
	non_pedal_string = -1;
	strings_used_by_rand1 = 0;
	strings_used_by_rand2 = 0;
	arp_oct_offset = 0;
	cur_string = -1;
	moving_down = false;
	strings_frame_tick = -1;
}
