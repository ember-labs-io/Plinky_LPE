#pragma once
#include "utils.h"

// sys param getters
static u16 get_preset_id(void) {
	return sys_params.preset_id;
}

static u16 get_midi_in_chan(void) {
	return sys_params.midi_in_chan;
}

static u16 get_midi_out_chan(void) {
	return sys_params.midi_out_chan;
}

static u16 get_accel_sens(void) {
	return sys_params.accel_sens;
}

static u16 get_volume(void) {
	return (sys_params.volume_msb << 8) + sys_params.volume_lsb;
}

static u16 get_cv_quant(void) {
	return sys_params.cv_quant;
}

static u16 get_cv_ppqn_in(void) {
	return sys_params.cv_in_ppqn;
}

static u16 get_cv_ppqn_out(void) {
	return sys_params.cv_out_ppqn;
}

static u16 get_reverse_encoder(void) {
	return sys_params.reverse_encoder;
}

static u16 get_preset_aligned(void) {
	return sys_params.preset_aligned;
}

static u16 get_pattern_aligned(void) {
	return sys_params.pattern_aligned;
}

static u16 get_midi_in_clock_mult(void) {
	return sys_params.midi_in_clock_mult;
}

static u16 get_midi_in_vel_balance(void) {
	return sys_params.midi_in_vel_balance;
}

static u16 get_midi_out_vel_balance(void) {
	return sys_params.midi_out_vel_balance;
}

static u16 get_midi_in_pres_type(void) {
	return sys_params.midi_in_pres_type;
}

static u16 get_midi_out_pres_type(void) {
	return sys_params.midi_out_pres_type;
}

static u16 get_midi_soft_thru(void) {
	return sys_params.midi_soft_thru;
}

static u16 get_midi_channel_bend_range_in(void) {
	return sys_params.midi_channel_bend_range_in;
}

static u16 get_midi_string_bend_range_in(void) {
	return sys_params.midi_string_bend_range_in;
}

static u16 get_midi_string_bend_range_out(void) {
	return sys_params.midi_string_bend_range_out;
}

static u16 get_local_ctrl_off(void) {
	return sys_params.local_ctrl_off;
}

static u16 get_mpe_in(void) {
	return sys_params.mpe_in;
}

static u16 get_mpe_out(void) {
	return sys_params.mpe_out;
}

static u16 get_mpe_zone(void) {
	return sys_params.mpe_zone;
}

static u16 get_mpe_chans(void) {
	return sys_params.mpe_chans;
}

static u16 get_midi_in_scale_quant(void) {
	return sys_params.midi_in_scale_quant;
}

static u16 get_midi_in_filter(void) {
	return sys_params.midi_rcv_param_ccs + sys_params.midi_rcv_transport * 3 + sys_params.midi_rcv_clock * 6;
}

static u16 get_midi_out_filter_1(void) {
	return sys_params.midi_send_param_ccs + sys_params.midi_send_transport * 3 + sys_params.midi_send_clock * 6;
}

static u16 get_midi_out_filter_2(void) {
	return (sys_params.mpe_out_fine_tuning << 2) | (sys_params.midi_send_lfo_cc << 1) | sys_params.midi_out_yz_control;
}

static u16 get_midi_trs_out_off(void) {
	return sys_params.midi_trs_out_off;
}

static u16 get_midi_tuning(void) {
	return sys_params.midi_tuning;
}

static u16 get_reference_pitch(void) {
	return sys_params.reference_pitch;
}

static u16 get_cv_gate_in_is_pressure(void) {
	return sys_params.cv_gate_in_is_pressure;
}

static u16 get_edit_poly_params(void) {
	return sys_params.edit_poly_params;
}

typedef u16 (*SysParamGetter)(void);

const SysParamGetter sys_param_getters[] = {
    [SYS_PRESET_ID] = get_preset_id,
    [SYS_MIDI_IN_CHAN] = get_midi_in_chan,
    [SYS_MIDI_OUT_CHAN] = get_midi_out_chan,
    [SYS_ACCEL_SENS] = get_accel_sens,
    [SYS_VOLUME] = get_volume,
    [SYS_CV_QUANT] = get_cv_quant,
    [SYS_CV_PPQN_IN] = get_cv_ppqn_in,
    [SYS_CV_PPQN_OUT] = get_cv_ppqn_out,
    [SYS_REVERSE_ENCODER] = get_reverse_encoder,
    [SYS_PRESET_ALIGNED] = get_preset_aligned,
    [SYS_PATTERN_ALIGNED] = get_pattern_aligned,
    [SYS_MIDI_IN_CLOCK_MULT] = get_midi_in_clock_mult,
    [SYS_MIDI_IN_VEL_BALANCE] = get_midi_in_vel_balance,
    [SYS_MIDI_OUT_VEL_BALANCE] = get_midi_out_vel_balance,
    [SYS_MIDI_IN_PRES_TYPE] = get_midi_in_pres_type,
    [SYS_MIDI_OUT_PRES_TYPE] = get_midi_out_pres_type,
    [SYS_MIDI_SOFT_THRU] = get_midi_soft_thru,
    [SYS_MIDI_CHANNEL_BEND_RANGE_IN] = get_midi_channel_bend_range_in,
    [SYS_MIDI_STRING_BEND_RANGE_IN] = get_midi_string_bend_range_in,
    [SYS_MIDI_STRING_BEND_RANGE_OUT] = get_midi_string_bend_range_out,
    [SYS_LOCAL_CTRL_OFF] = get_local_ctrl_off,
    [SYS_MPE_IN] = get_mpe_in,
    [SYS_MPE_OUT] = get_mpe_out,
    [SYS_MPE_ZONE] = get_mpe_zone,
    [SYS_MPE_CHANS] = get_mpe_chans,
    [SYS_MIDI_IN_SCALE_QUANT] = get_midi_in_scale_quant,
    [SYS_MIDI_IN_FILTER] = get_midi_in_filter,
    [SYS_MIDI_OUT_FILTER_1] = get_midi_out_filter_1,
    [SYS_MIDI_OUT_FILTER_2] = get_midi_out_filter_2,
    [SYS_MIDI_TRS_OUT_OFF] = get_midi_trs_out_off,
    [SYS_MIDI_TUNING] = get_midi_tuning,
    [SYS_REFERENCE_PITCH] = get_reference_pitch,
    [SYS_CV_GATE_IN_IS_PRESSURE] = get_cv_gate_in_is_pressure,
    [SYS_EDIT_POLY_PARAMS] = get_edit_poly_params,
};

// sys param setters
static void set_preset_id(u16 value) {
	sys_params.preset_id = value;
}

static void set_midi_in_chan(u16 value) {
	sys_params.midi_in_chan = value;
}

static void set_midi_out_chan(u16 value) {
	sys_params.midi_out_chan = value;
}

static void set_accel_sens(u16 value) {
	sys_params.accel_sens = value;
}

static void set_volume(u16 value) {
	sys_params.volume_lsb = value & 255;
	sys_params.volume_msb = (value >> 8) & 7;
}

static void set_cv_quant(u16 value) {
	sys_params.cv_quant = value;
}

static void set_cv_ppqn_in(u16 value) {
	sys_params.cv_in_ppqn = value;
}

static void set_cv_ppqn_out(u16 value) {
	sys_params.cv_out_ppqn = value;
}

static void set_reverse_encoder(u16 value) {
	sys_params.reverse_encoder = value;
}

static void set_preset_aligned(u16 value) {
	sys_params.preset_aligned = value;
}

static void set_pattern_aligned(u16 value) {
	sys_params.pattern_aligned = value;
}

static void set_midi_in_clock_mult(u16 value) {
	sys_params.midi_in_clock_mult = value;
}

static void set_midi_in_vel_balance(u16 value) {
	sys_params.midi_in_vel_balance = value;
}

static void set_midi_out_vel_balance(u16 value) {
	sys_params.midi_out_vel_balance = value;
}

static void set_midi_in_pres_type(u16 value) {
	sys_params.midi_in_pres_type = value;
}

static void set_midi_out_pres_type(u16 value) {
	sys_params.midi_out_pres_type = value;
}

static void set_midi_soft_thru(u16 value) {
	sys_params.midi_soft_thru = value;
}

static void set_midi_channel_bend_range_in(u16 value) {
	sys_params.midi_channel_bend_range_in = value;
}

static void set_midi_string_bend_range_in(u16 value) {
	sys_params.midi_string_bend_range_in = value;
}

static void set_midi_string_bend_range_out(u16 value) {
	sys_params.midi_string_bend_range_out = value;
}

static void set_local_ctrl_off(u16 value) {
	sys_params.local_ctrl_off = value;
}

static void set_mpe_in(u16 value) {
	sys_params.mpe_in = value;
}

static void set_mpe_out(u16 value) {
	sys_params.mpe_out = value;
}

static void set_mpe_zone(u16 value) {
	sys_params.mpe_zone = value;
}

static void set_mpe_chans(u16 value) {
	sys_params.mpe_chans = value;
}

static void set_midi_in_scale_quant(u16 value) {
	sys_params.midi_in_scale_quant = value;
}

static void set_midi_in_filter(u16 value) {
	sys_params.midi_rcv_param_ccs = value % 3;
	sys_params.midi_rcv_transport = (value / 3) % 2;
	sys_params.midi_rcv_clock = (value / 6) % 2;
}

static void set_midi_out_filter_1(u16 value) {
	sys_params.midi_send_param_ccs = value % 3;
	sys_params.midi_send_transport = (value / 3) % 2;
	sys_params.midi_send_clock = (value / 6) % 2;
}

static void set_midi_out_filter_2(u16 value) {
	sys_params.mpe_out_fine_tuning = (value >> 2) & 1;
	sys_params.midi_send_lfo_cc = (value >> 1) & 1;
	sys_params.midi_out_yz_control = value & 1;
}

static void set_midi_trs_out_off(u16 value) {
	sys_params.midi_trs_out_off = value;
}

static void set_midi_tuning(u16 value) {
	sys_params.midi_tuning = value;
}

static void set_reference_pitch(u16 value) {
	sys_params.reference_pitch = value;
}

static void set_cv_gate_in_is_pressure(u16 value) {
	sys_params.cv_gate_in_is_pressure = value;
}

static void set_edit_poly_params(u16 value) {
	sys_params.edit_poly_params = value;
}

typedef void (*SysParamSetter)(u16);

const SysParamSetter sys_param_setters[] = {
    [SYS_PRESET_ID] = set_preset_id,
    [SYS_MIDI_IN_CHAN] = set_midi_in_chan,
    [SYS_MIDI_OUT_CHAN] = set_midi_out_chan,
    [SYS_ACCEL_SENS] = set_accel_sens,
    [SYS_VOLUME] = set_volume,
    [SYS_CV_QUANT] = set_cv_quant,
    [SYS_CV_PPQN_IN] = set_cv_ppqn_in,
    [SYS_CV_PPQN_OUT] = set_cv_ppqn_out,
    [SYS_REVERSE_ENCODER] = set_reverse_encoder,
    [SYS_PRESET_ALIGNED] = set_preset_aligned,
    [SYS_PATTERN_ALIGNED] = set_pattern_aligned,
    [SYS_MIDI_IN_CLOCK_MULT] = set_midi_in_clock_mult,
    [SYS_MIDI_IN_VEL_BALANCE] = set_midi_in_vel_balance,
    [SYS_MIDI_OUT_VEL_BALANCE] = set_midi_out_vel_balance,
    [SYS_MIDI_IN_PRES_TYPE] = set_midi_in_pres_type,
    [SYS_MIDI_OUT_PRES_TYPE] = set_midi_out_pres_type,
    [SYS_MIDI_SOFT_THRU] = set_midi_soft_thru,
    [SYS_MIDI_CHANNEL_BEND_RANGE_IN] = set_midi_channel_bend_range_in,
    [SYS_MIDI_STRING_BEND_RANGE_IN] = set_midi_string_bend_range_in,
    [SYS_MIDI_STRING_BEND_RANGE_OUT] = set_midi_string_bend_range_out,
    [SYS_LOCAL_CTRL_OFF] = set_local_ctrl_off,
    [SYS_MPE_IN] = set_mpe_in,
    [SYS_MPE_OUT] = set_mpe_out,
    [SYS_MPE_ZONE] = set_mpe_zone,
    [SYS_MPE_CHANS] = set_mpe_chans,
    [SYS_MIDI_IN_SCALE_QUANT] = set_midi_in_scale_quant,
    [SYS_MIDI_IN_FILTER] = set_midi_in_filter,
    [SYS_MIDI_OUT_FILTER_1] = set_midi_out_filter_1,
    [SYS_MIDI_OUT_FILTER_2] = set_midi_out_filter_2,
    [SYS_MIDI_TRS_OUT_OFF] = set_midi_trs_out_off,
    [SYS_MIDI_TUNING] = set_midi_tuning,
    [SYS_REFERENCE_PITCH] = set_reference_pitch,
    [SYS_CV_GATE_IN_IS_PRESSURE] = set_cv_gate_in_is_pressure,
    [SYS_EDIT_POLY_PARAMS] = set_edit_poly_params,
};

const u8 sys_param_ranges[] = {
    [SYS_PRESET_ID] = 32,
    [SYS_MIDI_IN_CHAN] = 16,
    [SYS_MIDI_OUT_CHAN] = 16,
    [SYS_ACCEL_SENS] = 201,
    [SYS_VOLUME] = 0,
    [SYS_CV_QUANT] = NUM_CV_QUANT_TYPES,
    [SYS_CV_PPQN_IN] = NUM_PPQN_VALUES,
    [SYS_CV_PPQN_OUT] = NUM_PPQN_VALUES,
    [SYS_REVERSE_ENCODER] = 2,
    [SYS_PRESET_ALIGNED] = 2,
    [SYS_PATTERN_ALIGNED] = 2,
    [SYS_MIDI_IN_CLOCK_MULT] = 3,
    [SYS_MIDI_IN_VEL_BALANCE] = 129,
    [SYS_MIDI_OUT_VEL_BALANCE] = 129,
    [SYS_MIDI_IN_PRES_TYPE] = NUM_MIDI_PRESSURE_TYPES,
    [SYS_MIDI_OUT_PRES_TYPE] = NUM_MIDI_PRESSURE_TYPES,
    [SYS_MIDI_SOFT_THRU] = 2,
    [SYS_MIDI_CHANNEL_BEND_RANGE_IN] = NUM_BEND_RANGES,
    [SYS_MIDI_STRING_BEND_RANGE_IN] = NUM_BEND_RANGES,
    [SYS_MIDI_STRING_BEND_RANGE_OUT] = NUM_BEND_RANGES,
    [SYS_LOCAL_CTRL_OFF] = 2,
    [SYS_MPE_IN] = 2,
    [SYS_MPE_OUT] = 2,
    [SYS_MPE_ZONE] = 2,
    [SYS_MPE_CHANS] = 15,
    [SYS_MIDI_IN_SCALE_QUANT] = 2,
    [SYS_MIDI_IN_FILTER] = 12,
    [SYS_MIDI_OUT_FILTER_1] = 12,
    [SYS_MIDI_OUT_FILTER_2] = 8,
    [SYS_MIDI_TRS_OUT_OFF] = 2,
    [SYS_MIDI_TUNING] = 2,
    [SYS_REFERENCE_PITCH] = 16,
    [SYS_CV_GATE_IN_IS_PRESSURE] = 2,
};
