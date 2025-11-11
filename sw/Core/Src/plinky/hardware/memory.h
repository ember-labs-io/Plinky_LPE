#pragma once
#include "utils.h"

// This module manages ram and internal flash
//
// Flash is 256 pages of 2048 bytes each, holding:
// - 32 presets
// - 96 pattern quarters (24 patterns x 4)
// - 8 sample infos
// - 1 floating preset
// - 4 floating pattern quarters (1 pattern x 4)
// - 1 calibration page (fixed page id: 255)
//
// The system uses wear leveling to spread the pages out over page id 0-254
// Each of these pages holds a PageFooter and a recent copy of SysParams
//
// At any point, ram holds:
// - 1 preset
// - 1 full pattern (4 quarters)
// - 1 sample info
// - 1 set system parameters
//
// The preset and pattern quarters are floating. This means they do not directly represent any of the preset/pattern
// slots. Instead they exist as the current state of the device. Users can save the floating preset and pattern to a
// slot when desired. They can also (re)load saved patterns and preset slots into the floating items
//
// The preset, pattern quarters and sample info auto-save to flash when necessary and at set intervals. On each
// auto-save, a recent copy of sys_params is included

// ram contents
extern SysParams sys_params;
extern Preset cur_preset;
extern PatternQuarter cur_pattern_qtr[4];
extern SampleInfo cur_sample_info;

// utils
u32 get_sample_address(void);
void set_sys_param(SysParam param, u16 value);

// web-editor
u8* preset_flash_ptr(u8 preset_id);
void load_preset(u8 preset_id, bool show_message);

// get ram state
bool preset_outdated(void);  // only for sequencer
bool pattern_outdated(void); // only for sequencer

// init
void check_bootloader_flash(void);
void init_memory(void);

// main
void memory_frame(void);
void revert_presets(void);

// update ram
void log_ram_edit(MemSegment segment);
void update_preset_ram(void);
void update_pattern_ram(void);
void update_sample_ram(void);

// save/load
void apply_cued_mem_items(void);
void cue_mem_item(u8 item_id);

// ui
void update_clear_item(u8 item_id);
void long_press_load_item(u8 item_id);
void save_preset(void);
void clear_mem_item(void);

// calib
FlashCalibType flash_read_calib(void);
void flash_write_calib(FlashCalibType flash_calib_type);

// visuals
u8 draw_cued_preset_id(void);
u8 draw_preset_id(void);
u8 draw_cued_pattern_id(bool with_arp_icon);
void draw_pattern_id(bool with_arp_icon);
void draw_preset_name(u8 xtab);
void draw_ui_load_visuals(void);

u8 ui_load_led(u8 x, u8 y, u8 pulse1, u8 pulse2);