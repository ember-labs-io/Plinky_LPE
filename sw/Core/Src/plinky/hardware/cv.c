#include "cv.h"
#include "adc_dac.h"
#include "gfx/gfx.h"
#include "leds.h"
#include "synth/sampler.h"
#include "touchstrips.h"

// this is defined in main.c
extern DAC_HandleTypeDef hdac1;

void send_cv_pitch_hi(s32 data, bool apply_calib) {
	if (apply_calib) {
		data = (s32)((data * adc_dac_calib[DAC_PITCH_CV_HI].scale) + adc_dac_calib[DAC_PITCH_CV_HI].bias);
		s32 step = abs((s32)(adc_dac_calib[DAC_PITCH_CV_HI].scale * (2048.f * 12.f)));
		for (u8 k = 0; k < 3; ++k)
			if (data > 65535)
				data -= step;
			else
				break;
		for (u8 k = 0; k < 3; ++k)
			if (data < 0)
				data += step;
			else
				break;
	}
	HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_2, DAC_ALIGN_12B_L, clampi(data, 0, 65535));
}

void send_cv_pitch_lo(s32 data, bool apply_calib) {
	if (apply_calib) {
		data = (s32)((data * adc_dac_calib[DAC_PITCH_CV_LO].scale) + adc_dac_calib[DAC_PITCH_CV_LO].bias);
		s32 step = abs((int)(adc_dac_calib[DAC_PITCH_CV_LO].scale * (2048.f * 12.f)));
		for (s32 k = 0; k < 3; ++k)
			if (data > 65535)
				data -= step;
			else
				break;
		for (s32 k = 0; k < 3; ++k)
			if (data < 0)
				data += step;
			else
				break;
	}
	HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_L, clampi(data, 0, 65535));
}