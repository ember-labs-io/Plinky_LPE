#include "plinky.h"
#include "gfx/gfx.h"
#include "hardware/accelerometer.h"
#include "hardware/adc_dac.h"
#include "hardware/codec.h"
#include "hardware/encoder.h"
#include "hardware/flash.h"
#include "hardware/leds.h"
#include "hardware/midi.h"
#include "hardware/ram.h"
#include "hardware/spi.h"
#include "hardware/touchstrips.h"
#include "synth/arp.h"
#include "synth/audio.h"
#include "synth/params.h"
#include "synth/sampler.h"
#include "synth/sequencer.h"
#include "synth/strings.h"
#include "synth/synth.h"
#include "synth/time.h"
#include "ui/led_viz.h"
#include "ui/oled_viz.h"
#include "ui/pad_actions.h"
#include "usb/web_editor.h"

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
		calib_mode = CALIB_TOUCH;
		if (!(flash_calib_type & FLASH_CALIB_TOUCH))
			touch_calib(flash_calib_type | FLASH_CALIB_TOUCH);
		calib_mode = CALIB_CV;
		if (!(flash_calib_type & FLASH_CALIB_ADC_DAC))
			cv_calib();
		calib_mode = CALIB_NONE;
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
		calib_mode = CALIB_TOUCH;
		if (knob_a_delta > 4096)
			touch_calib(FLASH_CALIB_COMPLETE);
		calib_mode = CALIB_CV;
		if (knob_b_delta > 4096)
			cv_calib();
		calib_mode = CALIB_NONE;
		break;
	}
}

void plinky_init(void) {
	accel_init();
	define_hardware_version();
	HAL_Delay(100); // stablise power before bringing oled up
	gfx_init();     // also initializes oled
	check_bootloader_flash();
	init_touchstrips();
	audio_init();
	codec_init();
	adc_dac_init();
	HAL_Delay(1);
	spi_init();
	midi_init();
	leds_init();
	launch_calib(0);
	leds_bootswish();
	launch_calib(1);
	init_flash();
	init_ram();
	encoder_init();
}

// this runs with precise audio timing
void plinky_codec_tick(u32* audio_out, u32* audio_in) {
	// read physical touches
	u8 read_phase = read_touchstrips();
	// once per touchstrip read cycle:
	if (!read_phase) {
		handle_pad_action_long_presses();
		encoder_tick();
	}
	// update all leds
	leds_update();

	// don't do anything else while calibrating
	if (calib_mode)
		return;

	// pre-process audio
	audio_pre(audio_out, audio_in);

	// in the process of recording a new sample
	if (sampler_mode > SM_PREVIEW) {
		// handle recording audio and exit
		sampler_recording_tick(audio_out, audio_in);
		return;
	}

	// make sure preset ram is up to date
	update_preset_ram(false);
	// midi
	process_midi();
	// clock
	clock_tick();
	// sequencer
	seq_tick();
	// combine physical, latch, sequencer touches; run arp
	generate_string_touches();
	// evaluate parameters and modulations
	params_tick();
	// make sure sample and pattern ram is up to date
	update_sample_ram(false);
	update_pattern_ram(false);
	// generate the voices, based on touches and parameters
	handle_synth_voices(audio_out);
	// restart spi loop if necessary
	spi_tick();
	// apply audio effects and send result to output buffer
	audio_post(audio_out, audio_in);
}

// this is the main loop, only code that is blocking in some way lives here
void plinky_loop(void) {
	while (1) {
		// set output volume
		codec_set_volume(sys_params.headphonevol + 45);
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
		ram_frame();
		// handle usb web editor
		web_editor_frame();
	}
}

void plinky_reboot_sequence(u16 reboot_delay) {
	// reboot on release
	if (!encoder_pressed && reboot_delay > 500) {
		HAL_Delay(500);
		HAL_NVIC_SystemReset();
	}
	// stage 2
	if (reboot_delay > 500)
		flash_message(F_20_BOLD, "REBOOT!!", "");
	// stage 1
	else if (reboot_delay > 250)
		flash_message(F_20_BOLD, "REBOOT?", "");
}