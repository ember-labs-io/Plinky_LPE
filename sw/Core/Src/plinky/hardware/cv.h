#pragma once
#include "utils.h"

// this is defined in main.c
extern TIM_HandleTypeDef htim3;

enum ECVQuant {
	CVQ_OFF,
	CVQ_ON,
	CVQ_SCALE,
	CVQ_LAST,
};

void send_cv_pitch_hi(s32 data, bool apply_calib);
void send_cv_pitch_lo(s32 data, bool apply_calib);

static inline void send_cv_clock(bool high) {
	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, high ? 255 : 0);
}

static inline void send_cv_trigger(bool high) {
	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, high ? 255 : 0);
}

static inline void send_cv_gate(u16 data) {
	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, data >> 8);
}

static inline void send_cv_pressure(u16 data) {
	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, data >> 8);
}

// #define SENSE1_Pin GPIO_PIN_8
// #define SENSE1_GPIO_Port GPIOE
// #define SENSE2_Pin GPIO_PIN_15
// #define SENSE2_GPIO_Port GPIOE
//
// rj: this is ignoring MX_GPIO_Init() in main.c, could be cleaner after low level hardware setup cleanup

static inline bool cv_gate_present(void) {
	return HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_8) == GPIO_PIN_RESET;
}
static inline bool cv_pitch_present(void) {
	return HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_15) == GPIO_PIN_RESET;
}
