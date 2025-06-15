#pragma once
#include "utils.h"

// position of the ADC reading in the adc_buffer
typedef enum ADC_DAC_Index {
	ADC_PITCH,
	ADC_GATE,
	ADC_X_CV,
	ADC_Y_CV,
	ADC_A_CV,
	ADC_B_CV,
	ADC_B_KNOB = 6,
	ADC_A_KNOB = 7,
	DAC_PITCH_CV_LO,
	DAC_PITCH_CV_HI,
	NUM_ADC_DAC_ITEMS,
} ADC_DAC_Index;

// position of the ADC value in the adc_smoother array
typedef enum ADCSmoothIndex {
	ADC_S_A_CV = 0,
	ADC_S_B_CV = 1,
	ADC_S_X_CV = 2,
	ADC_S_Y_CV = 3,
	ADC_S_A_KNOB = 4,
	ADC_S_B_KNOB = 5,
	ADC_S_PITCH = 6,
	ADC_S_GATE = 7,
} ADCSmoothIndex;
// rj: can we get rid of this remapping?

typedef struct ADC_DAC_Calib {
	float bias, scale;
} ADC_DAC_Calib;

ADC_DAC_Calib* adc_dac_calib_ptr(void);

void adc_dac_init(void);

u16 adc_get_raw(ADC_DAC_Index index);
float adc_get_calib(ADC_DAC_Index index);
float adc_get_smooth(ADCSmoothIndex index);

void adc_update_inputs(void);

// cleanup in cv
extern ADC_DAC_Calib adc_dac_calib[10];