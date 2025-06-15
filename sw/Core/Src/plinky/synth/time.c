#include "time.h"
#include "arp.h"
#include "params.h"
#include "sequencer.h"

// cleanup
#include "hardware/adc_dac.h"
#include "hardware/cv.h"
// -- cleanup

typedef enum ExtTrig { EXT_NONE, EXT_MIDI, EXT_CV } ExtTrig;

#define MAX_BPM_10X 2400
#define MIN_BPM_10X 300

#define SYNTH_SAMPLE_RATE 32000
#define AUDIO_SAMPLE_RATE 31250
// largest amount of ticks between two valid 4ppqn pulses
#define MAX_CLOCK_GAP_TICKS ((600 * SYNTH_SAMPLE_RATE) / (MIN_BPM_10X * SAMPLES_PER_TICK))

u32 synth_tick = 0;     // global synth_tick counter
u16 bpm_10x = 120 * 10; // bpm with one decimal precision
bool using_internal_clock = true;

// 32nds timing for arp & sequencer

#define SYNC_DIVS_LCM 3840 // Least common multiple of all possible sync divisions
bool pulse_32nd = false;   // True if this tick is the start of a new 32nd note
u16 counter_32nds = 0;     // Counts 32nd notes, rolls over at SYNC_DIVS_LCM

volatile static ExtTrig ext_trig = EXT_NONE;
static u32 clock_16ths_q21 = 0; // accumulator clock, counts up to 1/16th in Q21
static u32 ticks_since_16th = 0;
static u8 midi_clock = 0;
static bool resync_next_tick = false;

void trigger_cv_clock(void) {
	ext_trig = EXT_CV;
}

void trigger_midi_clock(void) {
	midi_clock++;
	if (midi_clock == 6) {
		ext_trig = EXT_MIDI;
		midi_clock = 0;
	}
}

void trigger_tap_tempo(void) {
	static u8 tap_count = 0;
	static u32 tap_start_tick;
	static u32 tap_latest_tick;
	// reset count when the gap is too large
	if (synth_tick - tap_latest_tick > MAX_CLOCK_GAP_TICKS)
		tap_count = 0;
	// save the initial tap time
	if (!tap_count)
		tap_start_tick = synth_tick;
	// save the most recent tap time
	tap_latest_tick = synth_tick;
	// process taps
	tap_count++;
	if (tap_count > 1) {
		float tap_per_min =
		    (SYNTH_SAMPLE_RATE * (tap_count - 1) * 60.f) / ((synth_tick - tap_start_tick) * SAMPLES_PER_TICK);
		bpm_10x = clampi((int)(tap_per_min * 10.f + 0.5f), MIN_BPM_10X, MAX_BPM_10X);
		// save result to parameter
		save_param_raw(P_TEMPO, SRC_BASE, ((bpm_10x - 1200) * PARAM_SIZE) / 1200);
	}
}

void cue_clock_resync(void) {
	resync_next_tick = true;
}

void clock_resync(void) {
	midi_clock = 0;
	clock_16ths_q21 = 0;
	ticks_since_16th = 0;
	counter_32nds = 0;
	pulse_32nd = true;
	send_cv_clock(true);
}

void clock_tick(void) {
	static u32 last_16th_ticks = 0;
	bool pulse_16th = false;

	// increase ticks
	synth_tick++;
	ticks_since_16th++;

	// external clock
	if (ext_trig) {
		// new cv clock triggers sequencer start
		if (using_internal_clock && ext_trig == EXT_CV)
			seq_play_from_start();
		pulse_16th = true;
		using_internal_clock = false;
		ext_trig = EXT_NONE;
	}

	// revert to internal clock on absence of external clock signal
	if (!using_internal_clock && ticks_since_16th >= MAX_CLOCK_GAP_TICKS) {
		using_internal_clock = true;
		// also stop sequencer
		seq_stop();
	}

	// internal clock => get bpm from param
	if (using_internal_clock) {
		bpm_10x = ((param_val(P_TEMPO) * 1200) >> 16) + 1200;
		// accumulator clock: rollover at 1 << 21 siginifies 16th note
		clock_16ths_q21 += ((u64)1 << 21) * bpm_10x * SAMPLES_PER_TICK / (AUDIO_SAMPLE_RATE * 150);
		if (clock_16ths_q21 >= 1 << 21) {
			clock_16ths_q21 &= (1 << 21) - 1;
			pulse_16th = true;
		}
	}
	// external clock => get bpm from last two 16ths
	else {
		float ext_bpm_10x =
		    minf((160.0f * AUDIO_SAMPLE_RATE) / ((ticks_since_16th + last_16th_ticks) * 0.5f * SAMPLES_PER_TICK),
		         MAX_BPM_10X);
		ext_bpm_10x += (bpm_10x - ext_bpm_10x) * 0.8f; // smooth it
		bpm_10x = (u16)(ext_bpm_10x + 0.5f);           // round it, save it
	}

	// resync requested
	if (resync_next_tick) {
		clock_resync();
		resync_next_tick = false;
		return;
	}

	// generate cv clock out
	send_cv_clock(pulse_16th);

	pulse_32nd = false;
	// start of 16th
	if (pulse_16th) {
		last_16th_ticks = ticks_since_16th;
		ticks_since_16th = 0;
		pulse_32nd = true;
	}
	// halfway 16th
	else
		pulse_32nd = (ticks_since_16th == (last_16th_ticks >> 1));
	// count 32nds
	if (pulse_32nd)
		counter_32nds = (counter_32nds + 1) % SYNC_DIVS_LCM;
}