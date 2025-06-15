#include "adc_dac.h"
#include "cv.h"
#include "synth/params.h"

// these are defined in main.c
extern DAC_HandleTypeDef hdac1;
extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim6;
extern TIM_HandleTypeDef htim3;

#define ADC_CHANS 8
#define ADC_SAMPLES 8

#define ADC_CHANS 8
#define ADC_SAMPLES 8

ADC_DAC_Calib adc_dac_calib[NUM_ADC_DAC_ITEMS] = {
    // cv inputs
    {52100.f, 1.f / -9334.833333f}, // pitch
    {31716.f, 0.2f / -6548.1f},     // gate
    {31665.f, 0.2f / -6548.1f},     // X
    {31666.f, 0.2f / -6548.1f},     // Y
    {31041.f, 0.2f / -6548.1f},     // A
    {31712.f, 0.2f / -6548.1f},     // B

    // potentiometers
    {32768.f, 1.05f / -32768.f}, // B knob
    {32768.f, 1.05f / -32768.f}, // A knob

    // cv outputs, volt/octave: 2048 per semitone
    {42490.f, (26620 - 42490) * (1.f / (2048.f * 12.f * 2.f))}, // pitch lo
    {42511.f, (26634 - 42511) * (1.f / (2048.f * 12.f * 2.f))}, // pitch hi
};

ADC_DAC_Calib* adc_dac_calib_ptr(void) {
	return adc_dac_calib;
}

// only global for calib and testjig - preferred local
u16 adc_buffer[ADC_CHANS * ADC_SAMPLES];

static ValueSmoother adc_smoother[ADC_CHANS];

void adc_dac_init(void) {
	// adc init
	for (s16 i = 0; i < ADC_CHANS * ADC_SAMPLES; ++i)
		adc_buffer[i] = 32768;
	HAL_ADC_Start_DMA(&hadc1, (uint32_t*)&adc_buffer, ADC_CHANS * ADC_SAMPLES);
	// dac init
	HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
	HAL_DAC_Start(&hdac1, DAC_CHANNEL_2);
	HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
	HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
	HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
	HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);
	// start both adc and dac
	HAL_TIM_Base_Start(&htim6);
}

u16 adc_get_raw(ADC_DAC_Index index) {
	u32 raw_value = 0;
	u16* src = adc_buffer + index;
	// gate input: get max value to better respond to short gates
	if (index == ADC_GATE) {
		for (u8 i = 0; i < ADC_SAMPLES; ++i) {
			raw_value = maxi(raw_value, *src);
			src += ADC_CHANS;
		}
		return raw_value;
	}
	// all other inputs: get average
	for (u8 i = 0; i < ADC_SAMPLES; ++i) {
		raw_value += *src;
		src += ADC_CHANS;
	}
	return raw_value / ADC_SAMPLES;
}

float adc_get_calib(ADC_DAC_Index index) { // only one use in arp.h
	return (adc_get_raw(index) - adc_dac_calib[index].bias) * adc_dac_calib[index].scale;
}

float adc_get_smooth(ADCSmoothIndex index) {
	if (index == ADC_S_PITCH) {
		s32 pitch = (s32)(adc_smoother[ADC_S_PITCH].y2 * (512.f * 12.f));
		// quantize pitch according to param
		if (param_val(P_CV_QUANT))
			pitch = (pitch + 256) & (~511);
		return pitch;
	}
	return adc_smoother[index].y2;
}

// same as general smooth_value(), but with faster constants
static void adc_smooth_value(ValueSmoother* s, float new_val) {
	// inspired by  https ://cytomic.com/files/dsp/DynamicSmoothing.pdf
	const static float sens = 10.f;
	float band = fabsf(s->y2 - s->y1);
	float g = minf(1.f, 0.1f + band * sens);
	s->y1 += (new_val - s->y1) * g;
	s->y2 += (s->y1 - s->y2) * g;
}

void adc_update_inputs(void) {
	// why don't we clamp in the calib stage? and why aren't all calls to value_calib clamped?
	adc_smooth_value(adc_smoother + ADC_S_A_CV, clampf(adc_get_calib(ADC_A_CV), -1.f, 1.f));
	adc_smooth_value(adc_smoother + ADC_S_B_CV, clampf(adc_get_calib(ADC_B_CV), -1.f, 1.f));
	adc_smooth_value(adc_smoother + ADC_S_X_CV, clampf(adc_get_calib(ADC_X_CV), -1.f, 1.f));
	adc_smooth_value(adc_smoother + ADC_S_Y_CV, clampf(adc_get_calib(ADC_Y_CV), -1.f, 1.f));
	// why are the knobs saved in 4/5 and not in ADC_A_KNOB / ADC_B_KNOB ?
	smooth_value(adc_smoother + ADC_S_A_KNOB, clampf(adc_get_calib(ADC_A_KNOB), -1.f, 1.f), 1.f);
	smooth_value(adc_smoother + ADC_S_B_KNOB, clampf(adc_get_calib(ADC_B_KNOB), -1.f, 1.f), 1.f);
	adc_smooth_value(adc_smoother + ADC_S_PITCH, cv_pitch_present() ? adc_get_calib(ADC_PITCH) : 0.f);
	// why do we do another mapping here, instead of incorporating this in cv calib?
	adc_smooth_value(adc_smoother + ADC_S_GATE,
	                 cv_gate_present() ? clampf(adc_get_calib(ADC_GATE) * 1.15f - 0.05f, 0.f, 1.f) : 1.f);
}
