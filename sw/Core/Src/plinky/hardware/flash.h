#pragma once
#include "utils.h"

// this module manages 512KB (256 * 2048 bytes) of onboard flash
//
// this flash stores:
// - 1x SysParams
// - 32 presets
// - 96 sequencer pattern quarters
// - 8 SampleInfo's
// - calibration data

extern bool flash_busy;

void check_bootloader_flash(void);
void init_flash();
void flash_toggle_preset(u8 preset_id);

// flash pointers

const Preset* preset_flash_ptr(u8 preset_id);
PatternQuarter* ptn_quarter_flash_ptr(u8 quarter_id);
SampleInfo* sample_info_flash_ptr(u8 sample0);

// writing flash

void flash_erase_page(u8 page);
void flash_write_block(void* dst, const void* src, int size);
void flash_write_page(const void* src, u32 size, u8 page_id);

// calib

FlashCalibType flash_read_calib(void);
void flash_write_calib(FlashCalibType flash_calib_type);