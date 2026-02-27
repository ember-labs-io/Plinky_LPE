#pragma once
#include "utils.h"

extern TIM_HandleTypeDef htim3;

ADC_DAC_Calib* adc_dac_calib_ptr(void);

void init_adc_dac(void);

u16 adc_get_raw(ADC_DAC_Index index);
float adc_get_smooth(ADCSmoothIndex index);

void adc_dac_tick(void);

// cv

bool new_seq_cv_gate(void);
bool cv_try_get_touch(u8 string_id, s16* pressure, s16* position, u8* note_number, u8* start_velocity,
                      s32* pitchbend_pitch);
void send_cv_pitch(bool pitch_hi, u32 pitch_4x);
void cv_calib(void);

// pwm cv outs take range 0-256 and generate 6.6V at 256

// 256 * 5 / 6.6 = 194, rounded up so that the measured voltage ends up at 5.00V
#define CV_OUT_5V 195

static inline void send_cv_clock(bool high) {
	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, high ? CV_OUT_5V : 0);
}

static inline void send_cv_trigger(bool high) {
	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, high ? CV_OUT_5V : 0);
}

static inline void send_cv_gate(bool high) {
	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, high ? CV_OUT_5V : 0);
}

static inline void send_cv_pressure(u16 data) {
	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, (data * CV_OUT_5V) >> 16);
}

// #define SENSE1_Pin GPIO_PIN_8
// #define SENSE1_GPIO_Port GPIOE
// #define SENSE2_Pin GPIO_PIN_15
// #define SENSE2_GPIO_Port GPIOE
//
// rj: this is ignoring MX_GPIO_Init() in main.c, could be cleaner after low level hardware setup cleanup

static inline bool cv_pitch_present(void) {
	return HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_15) == GPIO_PIN_RESET;
}