#pragma once
#include "utils.h"

extern SeqFlags seq_flags;

// == SEQ INFO == //

bool seq_playing(void);
bool seq_recording(void);
SeqState seq_state(void);
u32 seq_substep(u32 resolution); // ui & params_tick

// == MAIN SEQ FUNCTIONS == //

void seq_tick(void);
void seq_try_rec_touch(u8 string_id, s16 pressure, s16 position, bool pres_increasing);
void seq_try_get_touch(u8 string_id, s16* pressure, s16* position);

// == SEQ COMMANDS == //

void seq_continue(void);
void seq_play(void);
void seq_start_previewing(void);
void seq_end_previewing(void);
void seq_toggle_rec(void);
void seq_cue_to_stop(void);
void seq_stop(void);

// == SEQ STEP ACTIONS == //

void seq_force_play_step(void);
bool seq_inc_step(void);
bool seq_dec_step(void);
void seq_clear_step(void);

// == SEQ PATTERN ACTIONS == //

void seq_try_set_start(u8 new_step);
void seq_set_end(u8 new_step);

// == SEQ VISUALS == //

void seq_ptn_start_visuals(void);
void seq_ptn_end_visuals(void);
void seq_draw_step_recording(void);
u8 seq_led(u8 x, u8 y, u8 sync_pulse);
u8 seq_press_led(u8 x, u8 y);