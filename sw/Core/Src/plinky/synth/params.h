#pragma once
#include "utils.h"

// this module deals with selecting parameters, editing their values, and applying mod-source modulations to them

// utils
const Preset* init_params_ptr(void);
bool editing_param(void);
bool arp_active(void);
void params_rcv_cc(u8 d1, u8 d2);

// main
bool update_preset(Preset* preset);
void revert_preset(Preset* preset);
void params_tick(void);

// param retrieval
s32 param_val(Param param_id);
s32 param_val_poly(Param param_id, u8 string_id);
s8 param_index(Param param_id);
s8 param_index_poly(Param param_id, u8 string_id);
s8 param_index_unmod(Param param_id);

// param saving
void save_param_raw(Param param_id, ModSource mod_src, s16 data);
void save_param_index(Param param_id, s8 index);

// pad action calls
bool try_restore_param(bool mode_a);
void close_edit_mode(void);

void touch_edit_strip(u16 position, bool is_press_start);
void press_param_pad(u8 pad_id, bool is_press_start);
void press_mod_pad(u8 pad_y);

// encoder calls
void edit_param_from_encoder(s8 enc_diff, float enc_acc);
void params_toggle_default_value(void);
void hold_encoder_for_params(u16 duration);

// visuals
bool mod_clear_visuals(void);

void take_param_snapshots(void);
bool params_want_to_draw(void);
void draw_cur_param(void);
void draw_arp_flag(void);
void draw_latch_flag(void);

bool is_snap_param(u8 x, u8 y);
s16 value_editor_column_led(u8 y);
u8 ui_editing_led(u8 x, u8 y, u8 pulse);
void param_function_leds(u8 pulse);
