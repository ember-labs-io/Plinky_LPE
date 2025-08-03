#include "sequencer.h"
#include "conditional_step.h"
#include "gfx/gfx.h"
#include "hardware/midi.h"
#include "hardware/ram.h"
#include "params.h"
#include "time.h"
#include "ui/shift_states.h"

// cleanup
#include "hardware/adc_dac.h"
// -- cleanup

#define GATE_LEN_SUBSTEPS 256
#define SEQ_CLOCK_SYNCED (step_32nds >= 0)
#define CUR_QUARTER ((cur_seq_step >> 4) & 3)

SeqFlags seq_flags = {0};
static ConditionalStep c_step;

// timing
static s16 step_32nds = 0;       // 32nds in a step
static u32 last_step_ticks = 0;  // duration
static u32 ticks_since_step = 0; // current duration

// pattern
static s8 cur_seq_step = 0;  // current step, modulated by step offset
static u8 cur_seq_start = 0; // where we start playing, modulated by step offset
static u8 cued_ptn_start = 255;
static u64 random_steps_avail = 0; // bitmask of unplayed steps in random modes

// recording
static u8 last_edited_step_global = 255;
static u8 visuals_substep = 0;

// == SEQ INFO == //

bool seq_playing(void) {
	return seq_flags.playing;
}

bool seq_recording(void) {
	return seq_flags.recording;
}

SeqState seq_state(void) {
	if (seq_flags.recording)
		return seq_flags.playing ? SEQ_LIVE_RECORDING : SEQ_STEP_RECORDING;
	if (!seq_flags.playing)
		return SEQ_IDLE;
	if (seq_flags.previewing)
		return SEQ_PREVIEWING;
	if (seq_flags.stop_at_next_step)
		return SEQ_FINISHING_STEP;
	return SEQ_PLAYING;
}

u32 seq_substep(u32 resolution) {
	if (ticks_since_step == 0)
		return 0;
	if (ticks_since_step >= last_step_ticks)
		return resolution - 1;
	return (ticks_since_step * resolution) / last_step_ticks;
}

// == SEQ TOOLIES == //

// keep cur_seq_step within bounds of both the local sequence length and the global max of 64
static void align_cur_step(void) {
	cur_seq_step = (modi(cur_seq_step - cur_seq_start, cur_preset.seq_len) + cur_seq_start) & 63;
}

// calculate start step from preset and step offset modulation
static void recalc_start_step(void) {
	cur_seq_start = (cur_preset.seq_start + param_val(P_STEP_OFFSET) + 64) & 63;
	// this always needs an align of cur step as well
	align_cur_step();
}

static void jump_to_step(u8 step) {
	cur_seq_step = step;
	last_edited_step_global = 255;
	align_cur_step();
}

static void seq_set_start(u8 new_step) {
	// save the relative step position
	u8 relative_step = cur_seq_step - cur_seq_start + 64;
	// set the new pattern start
	cur_preset.seq_start = new_step;
	log_ram_edit(SEG_PRESET);
	recalc_start_step();
	// set the new absolute step position
	jump_to_step((cur_seq_start + relative_step) & 63);
}

static void apply_cued_changes(void) {
	bool needs_start_recalc = false;
	// apply new start step
	if (cued_ptn_start != 255) {
		seq_set_start(cued_ptn_start);
		needs_start_recalc = true;
		cued_ptn_start = 255;
	}
	if (apply_cued_load_items() || needs_start_recalc)
		recalc_start_step();
}

// == MAIN SEQ FUNCTIONS == //

// perform a sequencer step
static void seq_step(void) {
	if (!seq_flags.first_pulse)
		last_step_ticks = ticks_since_step;
	ticks_since_step = 0;

	if (!seq_flags.playing && !seq_flags.force_next_step) {
		c_step.play_step = false;
		return;
	}
	seq_flags.force_next_step = false;

	// perform a conditional step
	c_step.euclid_len = param_val(P_SEQ_EUC_LEN);
	c_step.density = param_val(P_SEQ_CHANCE);
	do_conditional_step(&c_step, false);

	// the first pulse doesn't advance a step
	if (!c_step.advance_step || seq_flags.first_pulse) {
		seq_flags.first_pulse = false;
		return;
	}

	// we're advancing, let's define what the next step is going to be
	SeqOrder seq_order = param_val(P_SEQ_ORDER);
	bool wrapped = false;
	switch (seq_order) {
	case SEQ_ORD_PAUSE:
		break;
	case SEQ_ORD_FWD:
		wrapped = seq_inc_step();
		break;
	case SEQ_ORD_BACK:
		wrapped = seq_dec_step();
		break;
	case SEQ_ORD_PINGPONG: {
		u8 end_step = cur_seq_start + cur_preset.seq_len - 1;
		// current step is at either extreme => switch directions

		// == this should just be equal signs?? test! == //

		if ((!seq_flags.playing_backwards && cur_seq_step >= end_step)
		    || (seq_flags.playing_backwards && cur_seq_step <= cur_seq_start)) {
			seq_flags.playing_backwards = !seq_flags.playing_backwards;
			wrapped = true;
		}
		seq_flags.playing_backwards ? seq_dec_step() : seq_inc_step();
		break;
	}
	case SEQ_ORD_PINGPONG_REP: {
		u8 end_step = cur_preset.seq_len + cur_seq_start - 1;
		// current step is at either extreme => switch directions but trigger the *same* step again
		if ((!seq_flags.playing_backwards && cur_seq_step >= end_step)
		    || (seq_flags.playing_backwards && cur_seq_step <= cur_seq_start)) {
			seq_flags.playing_backwards = !seq_flags.playing_backwards;

			// == this should just be able to be cur step? test! == //

			jump_to_step(seq_flags.playing_backwards ? end_step : cur_seq_start);
			wrapped = true;
		}
		// otherwise => regular step
		else
			seq_flags.playing_backwards ? seq_dec_step() : seq_inc_step();
		break;
	}
	case SEQ_ORD_RANDOM: {
		// no steps left: end of a "loop"
		if (!random_steps_avail) {
			// all steps are available again
			random_steps_avail = ((u64)1 << cur_preset.seq_len) - 1;
			wrapped = true;
		}

		// == replace this with get random bit? == //

		u64 step_mask = random_steps_avail;
		// pick a value from the number of available steps
		u8 step_val = rand() % __builtin_popcountll(step_mask);
		// clear that many least significant positive bits from step mask
		while (step_val-- > 0)
			step_mask &= step_mask - 1;
		// position of next least significant bit is the next step (relative)
		step_val = step_mask ? __builtin_ctzll(step_mask) : 0;
		// jump to and sound that step (absolute)
		jump_to_step(cur_seq_start + step_val);
		// chosen step is no longer available
		random_steps_avail &= ~((u64)1 << cur_seq_step);
		break;
	}
	default:
		break;
	}
	if (wrapped)
		apply_cued_changes();
	// if play was pressed while playing the current step, this is where the sequencer actually stops playing
	if (seq_flags.playing && seq_flags.stop_at_next_step)
		seq_stop();
}

// executed every frame
void seq_tick(void) {
	// update properties
	ticks_since_step++;
	u8 seq_div = param_val(P_SEQ_CLK_DIV);
	step_32nds = seq_div == NUM_SYNC_DIVS ? -1 : sync_divs_32nds[clampi(seq_div, 0, NUM_SYNC_DIVS - 1)];
	recalc_start_step();

	// synced
	if (SEQ_CLOCK_SYNCED) {
		if (pulse_32nd && (counter_32nds % step_32nds == 0))
			seq_step();
	}
	// following cv gate - should this move to CV?
	else if (seq_flags.playing && cv_gate_present()) {
		// hysteresis
		static bool prev_gate = true;
		float thresh = prev_gate ? 0.01f : 0.02f;
		bool new_gate = adc_get_calib(ADC_GATE) > thresh;
		// trigger a step on rising edge
		if (new_gate && !prev_gate)
			seq_step();
		prev_gate = new_gate;
	}
}

static void rec_substep(PatternStringStep* string_step, u8 substep, u8 seq_pres, u8 seq_pos) {
	if (substep < 8) {
		string_step->pres[substep] = seq_pres;
		if ((substep & 1) == 0 || string_step->pos[substep / 2] == 0)
			string_step->pos[substep / 2] = seq_pos;
	}
	// recording into the end of the step => move all substeps backwards before writing
	else {
		// pressure
		for (u8 i = 0; i < 7; i++)
			string_step->pres[i] = string_step->pres[i + 1];
		string_step->pres[7] = seq_pres;
		// position
		if (substep == 9) {
			for (u8 i = 0; i < 3; i++)
				string_step->pos[i] = string_step->pos[i + 1];
			string_step->pos[3] = seq_pos;
		}
	}
	log_ram_edit(SEG_PAT0 + CUR_QUARTER);
}

// try recording string touch to sequencer
void seq_try_rec_touch(u8 string_id, s16 pressure, s16 position, bool pres_increasing) {
	static u8 last_seen_step = 255;
	static u8 last_seen_substep = 255;
	static u8 string_recording = 0;           // are we recording on string? bitmask
	static u8 substep_recorded = 0;           // has string recorded this substep? bitmask
	static u8 record_to_substep[NUM_STRINGS]; // remembers for each string where they were writing

	// not recording => exit
	if (!seq_flags.recording || pattern_outdated())
		return;

	PatternStringStep* string_step = string_step_ptr(string_id, false, cur_seq_step);
	u8 mask = 1 << string_id;
	u8 substep = seq_substep(8);

	// step record mode
	if (!seq_flags.playing) {
		// new step => no string has recorded yet
		if (cur_seq_step != last_seen_step) {
			last_seen_step = cur_seq_step;
			string_recording = 0;
			visuals_substep = 0;
		}
		// new substep => increase substep counter and reset recording flags
		if (substep != last_seen_substep) {
			last_seen_substep = substep;
			for (u8 s_id = 0; s_id < NUM_STRINGS; s_id++)
				if (string_recording & (1 << s_id)) {
					if (record_to_substep[s_id] < 8)
						record_to_substep[s_id]++;
					else
						record_to_substep[s_id] = 9 - (record_to_substep[s_id] & 1); // toggle between 8 and 9
				}
			substep_recorded = 0;
			// visuals
			if (string_recording) {
				visuals_substep++;
				if (visuals_substep == 24)
					visuals_substep = 8;
			}
		}
	}
	else {
		last_seen_step = 255;
		last_seen_substep = 255;
	}

	// holding clear sets the pressure to zero, which effectively clears the sequencer at this point
	u8 seq_pres = shift_state == SS_CLEAR ? 0 : pres_compress(pressure);
	u8 seq_pos = shift_state == SS_CLEAR ? 0 : pos_compress(position);

	// are we touching something to record?
	if ((seq_pres > 0 && pres_increasing) || shift_state == SS_CLEAR) {
		// live recording => just record over whatever substep we are currently on
		if (seq_flags.playing)
			rec_substep(string_step, substep, seq_pres, seq_pos);
		// step recording
		else {
			// we weren't recording yet => clear step and write first substep
			if ((string_recording & mask) == 0) {
				memset(string_step->pres, 0, sizeof(string_step->pres));
				memset(string_step->pos, 0, sizeof(string_step->pos));
				string_recording |= mask;
				record_to_substep[string_id] = 0;
				rec_substep(string_step, 0, seq_pres, seq_pos);
				// align clock
				clock_reset();
				ticks_since_step = 0;
			}
			// we are recording and we havent written this substep yet => record
			else if ((substep_recorded & mask) == 0) {
				rec_substep(string_step, record_to_substep[string_id], seq_pres, seq_pos);
				substep_recorded |= mask;
			}
		}
	}
}

// try receiving touch data from sequencer
void seq_try_get_touch(u8 string_id, s16* pressure, s16* position) {
	// exit if we're not playing a sequencer note
	if (!c_step.play_step || shift_state == SS_CLEAR)
		return;
	PatternStringStep* string_step = string_step_ptr(string_id, true, cur_seq_step);
	// exit if there is no data in the step
	if (!string_step)
		return;
	// exit if there's no pressure in the substep
	u8 substep = seq_substep(PTN_SUBSTEPS);
	if (!string_step->pres[substep])
		return;
	// exit if we're beyond the gate length
	if (seq_substep(GATE_LEN_SUBSTEPS) > (param_val_poly(P_GATE_LENGTH, string_id) >> 8)) {
		return;
	}

	// we're playing from the sequencer => create touch from pattern
	*pressure = pres_decompress(string_step->pres[substep]);
	*position = pos_decompress(string_step->pos[substep / 2]);
}

// == SEQ COMMANDS == //

// equivalent to midi continue: start playing from current position
void seq_continue(void) {
	apply_cued_changes();
	seq_flags.first_pulse = true;
	if (!seq_flags.playing) {
		seq_flags.playing = true;
		midi_send_transport(cur_seq_step == cur_seq_start ? MIDI_START : MIDI_CONTINUE);
	}
}

// equivalent to midi start: reset and start playing from beginning
void seq_play(void) {
	seq_flags.playing_backwards = false;
	random_steps_avail = 0;
	c_step.euclid_trigs = 0;
	seq_flags.playing_backwards = false;
	jump_to_step(cur_seq_start);
	seq_continue();
}

// play sequencer in preview mode
void seq_start_previewing(void) {
	seq_flags.previewing = true;
	seq_continue();
}

// turn off seq_flags.previewing, playing status remains unchanged
void seq_end_previewing(void) {
	seq_flags.previewing = false;
}

// toggle recording
void seq_toggle_rec(void) {
	seq_flags.recording = !seq_flags.recording;
	visuals_substep = 0; // a mode change resets the step record visuals
}

// sequencer will stop playing at the end of the current step
void seq_cue_to_stop(void) {
	seq_flags.stop_at_next_step = true;
}

// stop sequencer immediately
void seq_stop(void) {
	// if (seq_flags.playing)
	midi_send_transport(MIDI_STOP);
	seq_flags.previewing = false;
	seq_flags.playing = false;
	seq_flags.stop_at_next_step = false;
	c_step.play_step = false;
	visuals_substep = 0; // a mode change resets the step record visuals
	apply_cued_changes();
}

// == SEQ STEP ACTIONS == //

// only allowed when not playing
void seq_force_play_step(void) {
	seq_flags.force_next_step = true;
	seq_flags.first_pulse = true;
}

// returns whether this wrapped
bool seq_inc_step(void) {
	u8 prev_step = cur_seq_step;
	jump_to_step((cur_seq_step + 1) & 63);
	return cur_seq_step <= prev_step;
}

// returns whether this wrapped
bool seq_dec_step(void) {
	u8 prev_step = cur_seq_step;
	jump_to_step((cur_seq_step + 64 - 1) & 63);
	return cur_seq_step >= prev_step;
}

void seq_try_set_start(u8 new_step) {
	// get the unmodulated new start step
	u8 new_start = (new_step - param_val(P_STEP_OFFSET) + 64) & 63;
	// 1. not playing => change immediately
	// 2. goal identical to cued means double press on the same step => change immediately
	if (!seq_flags.playing || cued_ptn_start == new_start) {
		// ignore if we already have this start step
		if (cur_seq_start != new_step)
			seq_set_start(new_start);
		cued_ptn_start = 255;
	}
	// 3. otherwise => cue change for later change
	else
		cued_ptn_start = new_start;
}

void seq_set_end(u8 new_step) {
	u8 new_len = (new_step - cur_seq_start + 1 + 64) & 63;
	if (new_len != cur_preset.seq_len) {
		cur_preset.seq_len = new_len;
		log_ram_edit(SEG_PRESET);
	}
	align_cur_step();
}

void seq_clear_step(void) {
	// clear pressures from all substeps, from all strings, for the current step
	bool data_saved = false;
	PatternStringStep* string_step = string_step_ptr(0, false, cur_seq_step);
	for (u8 string_id = 0; string_id < 8; ++string_id, ++string_step) {
		for (u8 substep_id = 0; substep_id < 8; ++substep_id) {
			if (string_step->pres[substep_id] > 0) {
				data_saved = true;
				string_step->pres[substep_id] = 0;
			}
		}
	}
	if (data_saved)
		log_ram_edit(SEG_PAT0 + CUR_QUARTER);
}

// == SEQ VISUALS == //

void seq_ptn_start_visuals(void) {
	fdraw_str(0, 0, F_20_BOLD, I_PREV "Start %d", cur_seq_start + 1);
	fdraw_str(0, 16, F_20_BOLD, I_PLAY "Current %d", cur_seq_step + 1);
}

void seq_ptn_end_visuals(void) {
	fdraw_str(0, 0, F_20_BOLD, I_NEXT "End %d", ((cur_seq_start + cur_preset.seq_len) & 63) + 1);
	fdraw_str(0, 16, F_20_BOLD, I_INTERVAL "Length %d", cur_preset.seq_len);
}

static void draw_pres_substep(u8 id, u8 y, u8 draw_style) {
	u8 x = 43 + id * 8 + 2;
	switch (draw_style) {
	case 1:
		fill_rectangle(x, y, x + 5, y + 3);
		break;
	case 2:
		half_rectangle(x, y, x + 5, y + 3);
		break;
	}
}

static void draw_pos_substep(u8 id, u8 y, u8 draw_style) {
	u8 x = 43 + id * 16 + 2;
	switch (draw_style) {
	case 1:
		fill_rectangle(x, y + 4, x + 13, y + 7);
		break;
	case 2:
		half_rectangle(x, y + 4, x + 13, y + 7);
		break;
	}
}

void seq_draw_step_recording(void) {
	const static u8 y = 2;
	const static u8 spacing = 8;
	const static u8 left_offset = 43;

	fill_rectangle(left_offset - 1, y - 1, left_offset + 8 * spacing + 2, y + 8);
	inverted_rectangle(left_offset - 1, y - 1, left_offset + 8 * spacing + 2, y + 8);
	for (u8 i = 0; i <= 8; i++)
		vline(i * spacing + left_offset, y, y + 7 - (i & 1) * 3, 2);

	// recording into substeps
	if (visuals_substep <= 8) {
		for (u8 i = 0; i < visuals_substep; i++)
			draw_pres_substep(i, y, 1);
		u8 pos_substeps = (visuals_substep + 1) / 2;
		for (u8 i = 0; i < pos_substeps; i++)
			draw_pos_substep(i, y, 1);
		return;
	}

	// recording into last substep, substeps are being pushed back
	for (u8 i = 0; i < 8; i++) {
		u8 draw_style =
		    (visuals_substep <= 16 && i < 16 - visuals_substep) || (visuals_substep > 16 && i >= 24 - visuals_substep)
		        ? 1
		        : 2;
		draw_pres_substep(i, y, draw_style);
		if ((i & 1) == 0)
			draw_pos_substep(i / 2, y, draw_style);
	}
}

u8 seq_led(u8 x, u8 y, u8 sync_pulse) {
	u8 k = 0;
	u8 step = x + y * 8;
	// all active steps
	if (((step - cur_seq_start) & 63) < cur_preset.seq_len)
		k = maxi(k, ui_mode == UI_DEFAULT ? 48 : 96);
	// start/end steps
	switch (ui_mode) {
	case UI_PTN_START:
		if (step == cur_seq_start)
			k = 255;
		break;
	case UI_PTN_END:
		if (((step + 1) & 63) == ((cur_seq_start + cur_preset.seq_len) & 63))
			k = 255;
		break;
	default:
		break;
	}
	// playhead
	if (step == cur_seq_step)
		k = maxi(k, sync_pulse);
	// cued new start of pattern
	if (step == cued_ptn_start && seq_playing())
		k = maxi(k, (sync_pulse * 4) & 255);
	return k;
}

u8 seq_press_led(u8 x, u8 y) {
	PatternStringStep* string_step = string_step_ptr(x, true, cur_seq_step);
	u8 substep = seq_substep(8);
	if (string_step && string_step->pos[substep / 2] / 32 == y)
		return string_step->pres[substep];
	return 0;
}