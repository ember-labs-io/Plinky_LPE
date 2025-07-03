#include "plinky.h"
#include "hardware/accelerometer.h"
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
#include "synth/sampler.h"
#include "synth/sequencer.h"
#include "synth/strings.h"
#include "synth/time.h"
#include "ui/led_viz.h"
#include "ui/oled_viz.h"
#include "ui/pad_actions.h"

UIMode ui_mode = UI_DEFAULT;

HardwareVersion hw_version;

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

void plinky_init(void) {
	accel_init();
	reset_touches();
	define_hardware_version();
	HAL_Delay(100); // stablise power before bringing oled up
	gfx_init();     // also initializes oled
	check_bootloader_flash();
	audio_init();
	codec_init();
	adc_dac_init();
	HAL_Delay(1);
	spi_init();
	midi_init();
	leds_init();
	flash_read_calib();
	HAL_Delay(80);
	leds_bootswish();
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