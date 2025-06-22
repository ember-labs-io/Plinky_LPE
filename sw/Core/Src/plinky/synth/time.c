#include "time.h"
#include "hardware/cv.h"
#include "hardware/midi.h"
#include "params.h"
#include "sequencer.h"
#include "ui/oled_viz.h"

#define MAX_BPM_10X 2400
#define MIN_BPM_10X 300

#define SAMPLE_RATE 31250
#define SYNC_DIVS_LCM 3840 // Least common multiple of all possible sync divisions

// amount of ticks for one pulse at the lowest allowable bpm
#define MAX_CLOCK_GAP_TICKS(ppqn) ((600 * SAMPLE_RATE) / (ppqn * MIN_BPM_10X * SAMPLES_PER_TICK))
#define CUR_PULSE_COUNT(ppqn) ((ppqn * clock_32nds_q21) >> 24)

ClockType clock_type = CLK_INTERNAL;
u32 synth_tick = 0;     // global synth_tick counter
u16 bpm_10x = 120 * 10; // bpm with one decimal precision

bool pulse_32nd = false; // true if this tick is the start of a new 32nd note
u16 counter_32nds = 0;   // counts 32nd notes, rolls over at SYNC_DIVS_LCM

// global
static u32 clock_32nds_q21 = 0;
static ValueSmoother bpm_smoother;
static bool reset_clock_next_tick = false;

// midi clock
static bool midi_pulse = false;
static u8 midi_pulse_counter = 0;
static u8 midi_ppqn = 24;
static u32 ticks_since_midi_pulse = 0;
static u32 last_midi_pulse_ticks = 0;
static bool start_seq_from_midi_start = false;
static bool start_seq_from_midi_continue = false;

// cv clock
static volatile u8 cv_pulse = 0;
static u8 cv_pulse_counter = 0;
static u8 cv_ppqn = 4;
static u32 ticks_since_cv_pulse = 0;
static u32 last_cv_pulse_ticks = 0;
static bool cv_pulse_handled = false;

u32 clock_pos_q16(u16 loop_32nds) {
	u32 cycle_32nds = (counter_32nds % loop_32nds) << 16;
	u16 fract = (clock_32nds_q21 & ((1 << 21) - 1)) >> 5;
	return (cycle_32nds + fract) / loop_32nds;
}

void trigger_cv_clock(void) {
	cv_pulse++;
}

void trigger_tap_tempo(void) {
	static u8 tap_count = 0;
	static u32 tap_start_tick;
	static u32 tap_latest_tick;
	// reset count when the gap is too large
	if (synth_tick - tap_latest_tick > MAX_CLOCK_GAP_TICKS(1))
		tap_count = 0;
	// save the initial tap time
	if (!tap_count)
		tap_start_tick = synth_tick;
	// save the most recent tap time
	tap_latest_tick = synth_tick;
	// process taps
	tap_count++;
	if (tap_count > 1) {
		float tap_per_min = (SAMPLE_RATE * (tap_count - 1) * 60.f) / ((synth_tick - tap_start_tick) * SAMPLES_PER_TICK);
		bpm_10x = clampi((int)(tap_per_min * 10.f + 0.5f), MIN_BPM_10X, MAX_BPM_10X);
		// save result to parameter
		save_param_raw(P_TEMPO, SRC_BASE, ((bpm_10x - 1200) * PARAM_SIZE) / 1200);
	}
}

static void calc_bmp_from_ext(u32 last_pulse_duration, u32 cur_pulse_duration, u8 ppqn) {
	smooth_value(&bpm_smoother, (75.f * SAMPLE_RATE) / ((((last_pulse_duration + cur_pulse_duration) * ppqn) << 3)),
	             MIN_BPM_10X);
	bpm_10x = (u16)(bpm_smoother.y2 + 0.5f);
}

static void set_clock_type(ClockType new_type) {
	if (new_type == clock_type)
		return;
	switch (new_type) {
	case CLK_INTERNAL:
		flash_message(F_16_BOLD, "Internal", "Clock");
		seq_stop();
		break;
	case CLK_MIDI:
		flash_message(F_20_BOLD, "Midi in", "Clock");
		midi_pulse_counter = CUR_PULSE_COUNT(midi_ppqn); // sync up with the running clock
		break;
	case CLK_CV:
		flash_message(F_20_BOLD, "CV in", "Clock");
		// not playing => cv clock start restarts the sequencer
		if (!seq_playing()) {
			seq_play();
			clock_type = CLK_CV;
			reset_clock_next_tick = true;
		}
		// playing => sync up with the running clock
		else
			cv_pulse_counter = CUR_PULSE_COUNT(cv_ppqn);
		break;
	}
	// going from internal to external => set the displayed bpm value
	if (clock_type == CLK_INTERNAL)
		set_smoother(&bpm_smoother, bpm_10x);
	clock_type = new_type;
}

static void cleanup_clock_flags(void) {
	if (cv_pulse_handled)
		cv_pulse--;
	cv_pulse_handled = false;
	midi_pulse = false;
}

void cue_clock_reset(void) {
	reset_clock_next_tick = true;
}

void clock_reset(void) {
	cleanup_clock_flags();
	cv_pulse_counter = 0;
	midi_pulse_counter = 0;
	clock_32nds_q21 = 0;
	counter_32nds = 0;
	pulse_32nd = true; // this is the start of a 32nd
	if (seq_playing())
		send_cv_clock(true); // this is the start of a 16th (4 ppqn)
	midi_send_clock();       // this is the start of a 24 ppqn pulse
	reset_clock_next_tick = false;
}

void clock_tick(void) {
	// Clock priority strategy:
	//
	// 1. CV clock
	// 2. Midi clock
	// 3. Internal clock
	//
	// Clock automatically switches to higher priority source when a pulse comes in
	// Clock automatically switches one priority down when pulses stop coming in
	// Clock waits for the equivalent of one pulse in MIN_BPM_10X before reverting to a lower priority clock
	//
	// When clock switches from cv to midi, the sequencer is not stopped (tempos might be identical)
	// When clock switches from midi to internal, the sequencer is stopped (tempos are most likely not identical)
	// When clock falls thru from cv to internal, it technically goes: cv => midi => (sequencer stopped) => internal

	// increase ticks
	synth_tick++;
	ticks_since_cv_pulse++;
	ticks_since_midi_pulse++;

	if (cv_pulse) {
		// track pulses
		last_cv_pulse_ticks = ticks_since_cv_pulse;
		ticks_since_cv_pulse = 0;
		cv_pulse_counter++;
		cv_pulse_handled = true;
		// check clock priority
		if (clock_type != CLK_CV)
			set_clock_type(CLK_CV);
	}

	// cv clock timeout
	if (clock_type == CLK_CV && ticks_since_cv_pulse > MAX_CLOCK_GAP_TICKS(cv_ppqn))
		set_clock_type(CLK_MIDI);

	if (midi_pulse) {
		// track pulses
		last_midi_pulse_ticks = ticks_since_midi_pulse;
		ticks_since_midi_pulse = 0;
		midi_pulse_counter++;
		// check clock priority
		if (clock_type == CLK_INTERNAL)
			set_clock_type(CLK_MIDI);
		// midi triggers
		if (start_seq_from_midi_start) {
			start_seq_from_midi_start = false;
			// ignore if we're not in midi clock mode
			if (clock_type == CLK_MIDI) {
				seq_play();
				reset_clock_next_tick = true;
			}
		}
		if (start_seq_from_midi_continue) {
			start_seq_from_midi_continue = false;
			// ignore if we're not in midi clock mode
			if (clock_type == CLK_MIDI) {
				seq_continue();
				reset_clock_next_tick = true;
			}
		}
	}

	// midi clock timeout
	if (clock_type == CLK_MIDI && ticks_since_midi_pulse > MAX_CLOCK_GAP_TICKS(midi_ppqn))
		set_clock_type(CLK_INTERNAL);

	// up to this point, did we do anything that requires a clock reset? => cleanup, reset and exit
	if (reset_clock_next_tick) {
		clock_reset();
		return;
	}

	u32 prev_clock = clock_32nds_q21;

	// handle global accumulator clock
	switch (clock_type) {
	case CLK_CV: {
		u32 max_gap = MAX_CLOCK_GAP_TICKS(cv_ppqn);
		// cv pulse => snap the clock to the pulse position
		if (cv_pulse) {
			clock_32nds_q21 = (cv_pulse_counter << 24) / cv_ppqn;
			cv_pulse_counter = cv_pulse_counter % (cv_ppqn << 2);
			if (last_cv_pulse_ticks <= max_gap && ticks_since_cv_pulse <= max_gap)
				// we only calculate the bmp to show on the display
				calc_bmp_from_ext(last_cv_pulse_ticks, ticks_since_cv_pulse, cv_ppqn);
			break;
		}
		// valid last pulse length => calculate clock from previous pulse
		else if (last_cv_pulse_ticks <= max_gap) {
			clock_32nds_q21 += (1 << 24) / (cv_ppqn * last_cv_pulse_ticks);
			break;
		}
	} // no valid pulse length => progress by midi clock
	case CLK_MIDI: {
		u32 max_gap = MAX_CLOCK_GAP_TICKS(midi_ppqn);
		// midi pulse => snap the clock to the pulse position
		if (midi_pulse && clock_type == CLK_MIDI) {
			clock_32nds_q21 = (midi_pulse_counter << 24) / midi_ppqn;
			midi_pulse_counter = midi_pulse_counter % (midi_ppqn << 2);
			if (last_midi_pulse_ticks <= max_gap && ticks_since_midi_pulse <= max_gap)
				// we only calculate the bmp to show on the display
				calc_bmp_from_ext(last_midi_pulse_ticks, ticks_since_midi_pulse, midi_ppqn);
			break;
		}
		// valid last pulse length => calculate clock from previous pulse
		else if (last_midi_pulse_ticks <= max_gap) {
			clock_32nds_q21 += (1 << 24) / (midi_ppqn * last_midi_pulse_ticks);
			break;
		}
	} // no valid pulse length => progress by internal clock
	default:
		// internal clock => calculate clock from bpm param
		bpm_10x = ((param_val(P_TEMPO) * 1200) >> 16) + 1200;
		clock_32nds_q21 += ((u64)1 << 27) * bpm_10x / (75.f * SAMPLE_RATE);
		break;
	}

	cleanup_clock_flags();

	// check for midi pulse
	const u8 ppqn_out = 24;
	if (((clock_32nds_q21 & ((1 << 21) - 1)) * ppqn_out >> 24) != ((prev_clock & ((1 << 21) - 1)) * ppqn_out >> 24))
		midi_send_clock();

	// check for 32nd note pulse
	if (clock_32nds_q21 >= ((counter_32nds & 31) + 1) << 21) {
		pulse_32nd = true;
		counter_32nds = (counter_32nds + 1) % SYNC_DIVS_LCM;

		// while playing sequencer, send cv clock on 16th pulses (4 ppqn)
		if (seq_playing())
			send_cv_clock((counter_32nds & 1) == 0);

		// the clock rolls over at 32 32nd notes
		if (clock_32nds_q21 >= (1 << 26))
			clock_32nds_q21 &= (1 << 26) - 1;

		return;
	}

	pulse_32nd = false;
}

void clock_rcv_midi(u8 midi_status) {
	switch (midi_status) {
	case MIDI_START:
		start_seq_from_midi_start = true;
		break;
	case MIDI_CONTINUE:
		start_seq_from_midi_continue = true;
		break;
	case MIDI_STOP:
		seq_stop();
		break;
	case MIDI_TIMING_CLOCK:
		midi_pulse = true;
		break;
	}
}