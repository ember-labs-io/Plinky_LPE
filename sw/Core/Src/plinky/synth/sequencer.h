#pragma once
#include "utils.h"

extern SeqFlags seq_flags;

// == SEQ INFO == //

bool seq_playing(void);
bool seq_recording(void);
SeqState seq_state(void);
u32 seq_substep(u32 resolution); // viz

// == MAIN SEQ FUNCTIONS == //

void seq_tick(void);
void seq_try_rec_touch(u8 string_id, s16 pressure, s16 position, bool pres_increasing);
void seq_try_get_touch(u8 string_id, s16* pressure, s16* position);

// == SEQ COMMANDS == //

void seq_continue(void);
void seq_play(void);
void seq_stop(void);

// == SEQ ACTIONS == //

bool seq_inc_step(void);
void seq_cue_start_step(u8 new_step);
void seq_set_end_step(u8 new_step);

// == UI == //

void seq_press_left(bool from_default_ui);
void seq_press_right(void);
void seq_press_clear(void);
void seq_press_rec(void);
void seq_press_play(void);
void seq_release_play(bool short_press);

// == SEQ VISUALS == //

void seq_ptn_start_visuals(void);
void seq_ptn_end_visuals(void);
void seq_draw_step_recording(void);
u8 seq_led(u8 x, u8 y, u8 sync_pulse, bool bright);
u8 seq_press_led(u8 x, u8 y);