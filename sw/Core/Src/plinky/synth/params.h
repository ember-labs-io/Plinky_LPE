#pragma once
#include "param_defs.h"

// this module deals with selecting parameters, editing their values, and applying mod-source modulations to them

// helpers
Param get_recent_param(void);
bool strip_available_for_synth(u8 strip_id);
void params_update_touch_pointers(void);

// main
void params_tick(void);

// param retrieval calls
s16 param_val_raw(Param param_id, ModSource mod_src);
s32 param_val_unscaled(Param param_id);
s32 param_val(Param param_id);
float param_val_float(Param param_id);
s32 param_val_poly(Param param_id, u8 string_id);

// save param calls
void save_param_raw(Param param_id, ModSource mod_src, s16 data);
void save_param(Param param_id, ModSource mod_src, s16 data);

// pad action calls
void try_left_strip_for_params(u16 position, bool is_press_start);
bool press_param(u8 pad_id, u8 strip_id, bool is_press_start);
void select_mod_src(ModSource mod_src);
void reset_left_strip(void);

// shift state calls
void enter_param_edit_mode(bool mode_a);
void try_exit_param_edit_mode(bool param_select);

// encoder calls
void edit_param_from_encoder(Param param_id, s8 enc_diff, float enc_acc);
void params_toggle_default_value(Param param_id);
void hold_encoder_for_params(u16 duration);
void check_param_toggles(Param param_id);

// midi cc
void set_param_from_cc(Param param_id, u16 cc_value);

// visuals
void take_param_snapshots(void);
bool draw_cur_param(void);
bool is_snap_param(u8 x, u8 y);
s16 value_editor_column_led(u8 y);
u8 ui_editing_led(u8 x, u8 y, u8 pulse);
void param_shift_leds(u8 pulse);
