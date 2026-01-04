#include "plinky.h"
#include "gfx/gfx.h"
#include "hardware/accelerometer.h"
#include "hardware/adc_dac.h"
#include "hardware/codec.h"
#include "hardware/encoder.h"
#include "hardware/leds.h"
#include "hardware/memory.h"
#include "hardware/midi.h"
#include "hardware/spi.h"
#include "hardware/touchstrips.h"
#include "synth/arp.h"
#include "synth/audio.h"
#include "synth/params.h"
#include "synth/sampler.h"
#include "synth/sequencer.h"
#include "synth/synth.h"
#include "synth/time.h"
#include "ui/led_viz.h"
#include "ui/oled_viz.h"
#include "ui/pad_actions.h"
#include "ui/settings_menu.h"
#include "usb/usb.h"

u32 debug_time[TIME_LOG_ITEMS];
const char* debug_label[TIME_LOG_ITEMS] = {"ts",     "au_pre", "pr_ram", "seq",    "s_tch", "prm_t",
                                           "sp_ram", "vcs",    "spi",    "au_pst", "frame", "fps"};

UIMode ui_mode = UI_DEFAULT;

HardwareVersion hw_version;

CalibMode calib_mode = CALIB_NONE;

static void define_hardware_version(void) {
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	GPIO_InitStruct.Pin = GPIO_PIN_1;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLDOWN;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
	HAL_Delay(1);
	GPIO_PinState state = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_1);
	hw_version = state == GPIO_PIN_SET ? HW_PLINKY_PLUS : HW_PLINKY;
}

static void open_usb_bootloader(void) {
	oled_clear();
	draw_str(0, 0, F_16_BOLD, "Re-flash");
	draw_str(0, 16, F_16, "over USB DFU");
	oled_flip();
	HAL_Delay(100);

	// https://community.st.com/s/question/0D50X00009XkeeW/stm32l476rg-jump-to-bootloader-from-software
	typedef void (*pFunction)(void);
	pFunction JumpToApplication;
	HAL_RCC_DeInit();
	HAL_DeInit();
	SysTick->CTRL = 0;
	SysTick->LOAD = 0;
	SysTick->VAL = 0;
	__disable_irq();
	__DSB();
	__HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH(); /* Remap is bot visible at once. Execute some unrelated command! */
	__DSB();
	__ISB();
	JumpToApplication = (void (*)(void))(*((uint32_t*)(0x1FFF0000 + 4)));
	__set_MSP(*(__IO uint32_t*)0x1FFF0000);
	JumpToApplication();
}

static void launch_calib(u8 phase) {
	static u16 knob_a_start = 0;
	static u16 knob_b_start = 0;

	switch (phase) {
	// first phase: auto-launch calibration if none found, save knob values
	case 0:
		FlashCalibType flash_calib_type = flash_read_calib();
		if (!(flash_calib_type & FLASH_CALIB_TOUCH))
			touch_calib(flash_calib_type | FLASH_CALIB_TOUCH);
		if (!(flash_calib_type & FLASH_CALIB_ADC_DAC))
			cv_calib();
		HAL_Delay(80);
		knob_a_start = adc_get_raw(ADC_A_KNOB);
		knob_b_start = adc_get_raw(ADC_B_KNOB);
		break;
	// second phase: launch calib/bootloader based on knob turns
	case 1:
		u16 knob_a_delta = abs(knob_a_start - adc_get_raw(ADC_A_KNOB));
		u16 knob_b_delta = abs(knob_b_start - adc_get_raw(ADC_B_KNOB));
		if (knob_a_delta > 4096 && knob_b_delta > 4096)
			open_usb_bootloader();
		// legacy implementation, calibration can now be called from the settings menu
		if (knob_a_delta > 4096)
			touch_calib(FLASH_CALIB_COMPLETE);
		if (knob_b_delta > 4096)
			cv_calib();
		if (knob_a_delta > 4096 || knob_b_delta > 4096) {
			draw_logo();
			leds_bootswish();
		}
		break;
	}
}

void plinky_init(void) {
	init_accel();
	define_hardware_version();
	init_gfx(); // including oled
	check_bootloader_flash();
	init_touchstrips();
	init_audio();
	init_codec();
	init_adc_dac();
	init_spi();
	init_midi();
	init_usb();
	init_leds();
	init_memory();
	launch_calib(0);
	leds_bootswish();
	launch_calib(1);
	init_encoder();
}

static void log_time(void) {
	static u8 index = 0;
	static u32 frame_start = 0;
	static u32 last_us = 0;
	u32 new_us = micros();
	// frame start
	if (index == 0)
		frame_start = new_us;
	// log section
	else
		debug_time[index - 1] = new_us - last_us;
	// last section (logs section and full frame time)
	if (index == TIME_LOG_ITEMS - 2)
		debug_time[index] = new_us - frame_start;
	last_us = new_us;
	// leave last index open for the slow frame fps
	index = (index + 1) % (TIME_LOG_ITEMS - 1);
}

// this runs with precise audio timing
void plinky_codec_tick(u32* audio_out, u32* audio_in) {
	log_time();

	// read physical touches
	u8 read_phase = read_touchstrips();

	log_time();

	// once per touchstrip read cycle:
	if (!read_phase) {
		encoder_tick();
		pad_actions_frame();
	}

	// update all leds
	leds_update();

	// pre-process audio
	audio_pre(audio_out, audio_in);

	log_time();

	// don't do anything else while calibrating
	if (calib_mode)
		return;

	// in the process of recording a new sample
	if (sampler_mode > SM_PREVIEW) {
		// handle recording audio and exit
		sampler_recording_tick(audio_out, audio_in);
		return;
	}

	// make sure preset ram is up to date
	update_preset_ram();

	log_time();

	// midi
	midi_tick();

	// clock
	clock_tick();

	// sequencer
	seq_tick();

	log_time();

	// combine physical, latch, sequencer, arp touches
	generate_string_touches();

	log_time();

	// evaluate parameters and modulations
	params_tick();

	log_time();

	// make sure sample and pattern ram is up to date
	update_sample_ram();
	update_pattern_ram();

	log_time();

	// generate the voices, based on touches and parameters
	handle_synth_voices(audio_out);

	log_time();

	// restart spi loop if necessary
	spi_tick();

	log_time();

	// apply audio effects and send result to output buffer
	audio_post(audio_out, audio_in);

	log_time();
}

// this is the main loop, only code that is blocking in some way lives here
#if defined(TIME_LOGGING) || defined(FPS_WINDOW)
void plinky_loop(void) {
	while (1) {
		// save frame fps to last debug item
		static u32 last_us = 0;
		u32 new_us = micros();
		debug_time[TIME_LOG_ITEMS - 1] = 100000000 / (new_us - last_us); // 100x fps
		last_us = new_us;

#ifdef TIME_LOGGING
		// serial log one saved  item per loop
		static u8 log_index = 0;
		if (log_index == TIME_LOG_ITEMS)
			debug_log("Frame Start\n");
		else
			debug_log("%s %u\n", debug_label[log_index], debug_time[log_index]);
		log_index = (log_index + 1) % (TIME_LOG_ITEMS + 1);
#endif
#else

void plinky_loop(void) {
	while (1) {
#endif
		// set output volume
		codec_update_volume();
		// handle spi flash writes for the sampler
		if (ui_mode == UI_SAMPLE_EDIT) {
			switch (sampler_mode) {
			case SM_ERASING:
				// this fully blocks the loop until the sample is erased, also draws its own visuals
				clear_flash_sample();
				break;
			case SM_RECORDING:
			case SM_STOPPING1:
			case SM_STOPPING2:
			case SM_STOPPING3:
			case SM_STOPPING4:
				// pump blocks of the ram delay buffer to spi flash
				write_flash_sample_blocks();
			default:
				break;
			}
		}
		// visuals
		take_param_snapshots();
		draw_oled_visuals();
		draw_led_visuals();
		// read accelerometer values
		accel_read();
		// ram updates and writing ram to flash
		memory_frame();
		// web editor and usd midi data
		usb_frame();
		// execute actions triggered by setting menu
		settings_menu_actions();
	}
}