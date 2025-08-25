#pragma once
#include "utils.h"

#define FIRMWARE_VERSION "0.2.1"
#define GOLDEN_CHECKSUM 0xb5a7228c

typedef enum HardwareVersion {
	HW_PLINKY,
	HW_PLINKY_PLUS,
} HardwareVersion;

typedef enum UIMode {
	UI_DEFAULT,       // regular playing mode
	UI_EDITING_A,     // editing any of the A parameters
	UI_EDITING_B,     // editing any of the B parameters
	UI_PTN_START,     // setting the start of the sequencer pattern
	UI_PTN_END,       // setting the end of the sequencer pattern
	UI_LOAD,          // load screen: preset / pattern / sample
	UI_SAMPLE_EDIT,   // sample edit screen
	UI_SETTINGS_MENU, // edit system settings
} UIMode;

typedef enum CalibMode {
	CALIB_NONE = 0,
	CALIB_TOUCH,
	CALIB_CV,
} CalibMode;

extern HardwareVersion hw_version;
extern UIMode ui_mode;
extern CalibMode calib_mode;

void plinky_init(void);
void plinky_codec_tick(u32* audio_out, u32* audio_in);
void plinky_loop(void);
void plinky_reboot_sequence(u16 reboot_delay);