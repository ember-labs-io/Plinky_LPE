#include "leds.h"

// One capture/compare register per row
#define P1 TIM1->CCR1
#define P2 TIM1->CCR2
#define P3 TIM1->CCR3
#define P4 TIM1->CCR4
#define P5 TIM4->CCR2
#define P6 TIM4->CCR3
#define P7 TIM2->CCR3
#define P8 TIM2->CCR4

// One GPIO output per two columns
#define N1 2
#define N2 5
#define N3 7
#define N4 0
#define N5 15

// I/O mode configuration bits
// 00: Input mode
// 01: General purpose output mode
// 10: Alternate function mode
// 11: Analog mode (reset state)
#define OUTPUT_DISABLE_BITS ~((3 << (N1 * 2)) + (3 << (N2 * 2)) + (3 << (N3 * 2)) + (3 << (N4 * 2)) + (3 << (N5 * 2)))
const static u32 OutputEnableBits[5] = {(1 << (N4 * 2)), (1 << (N3 * 2)), (1 << (N2 * 2)), (1 << (N1 * 2)),
                                        (1 << (N5 * 2))};
// BSRR register bits
// Bits 15:0: Set pins
// Bits 31:16: Reset pins
#define SET_ALL_PINS_BITS ((1 << N1) | (1 << N2) | (1 << N3) | (1 << N4) | (1 << N5))
#define RESET_ALL_PINS_BITS (((1 << N1) | (1 << N2) | (1 << N3) | (1 << N4) | (1 << N5)) << 16)

u8 leds[LED_COLS][LED_COL_HEIGHT];
static u8 active_column = 0;

void leds_init(void) {
	TIM1->CR1 = 0; // reset all
	TIM2->CR1 = 0;
	TIM4->CR1 = 0;
	TIM1->CR1 = TIM_CR1_CMS_0; // center aligned mode
	TIM2->CR1 = TIM_CR1_CMS_0;
	TIM4->CR1 = TIM_CR1_CMS_0;
	TIM1->ARR = 512; // reload
	TIM2->ARR = 512;
	TIM4->ARR = 512;
	TIM1->CCMR1 = 0x00007060; // 1 2 <-set mode to PWM positive or negative
	TIM1->CCMR2 = 0x00007060; // 3 4
	TIM1->BDTR = 0x00008000;  // no breaks! (TIM1 got outputs protection HiZ state by default)
	TIM4->CCMR1 = 0x00006000; // 2
	TIM4->CCMR2 = 0x00000070; // 3
	TIM2->CCMR1 = 0x00000000; // -
	TIM2->CCMR2 = 0x00007060; // 3 4
	TIM1->CCER = 0x00001111;  // enable PWM outputs
	TIM4->CCER = 0x00000110;
	TIM2->CCER = 0x00001100;
	__disable_irq();
	TIM1->CR1 |= TIM_CR1_CEN; // start! all timers must be in sync to prevent current spikes
	TIM4->CR1 |= TIM_CR1_CEN;
	TIM2->CR1 |= TIM_CR1_CEN;
	__enable_irq();

	// clear all leds
	memset(leds, 0, sizeof(leds));
}

void leds_update(void) {
	GPIOD->MODER &= OUTPUT_DISABLE_BITS; // disable all outputs

	const u8* col = leds[active_column];
	// odd frames
	if (active_column & 1) {
		GPIOD->BSRR = RESET_ALL_PINS_BITS;

		P1 = col[0];
		P2 = col[1] ^ 0x1FF;
		P3 = col[2];
		P4 = col[3] ^ 0x1FF;
		P5 = col[4];
		P6 = col[5] ^ 0x1FF;
		P7 = col[6];
		P8 = col[7] ^ 0x1FF;
	}
	// even frames
	else {
		GPIOD->BSRR = SET_ALL_PINS_BITS;

		P1 = col[0] ^ 0x1FF;
		P2 = col[1];
		P3 = col[2] ^ 0x1FF;
		P4 = col[3];
		P5 = col[4] ^ 0x1FF;
		P6 = col[5];
		P7 = col[6] ^ 0x1FF;
		P8 = col[7];
	}

	GPIOD->MODER |= OutputEnableBits[active_column >> 1]; // activate column

	active_column = (active_column + 1) % 10; // next column
}

void leds_bootswish(void) {
	for (int f = 0; f < 64; ++f) {
		HAL_Delay(20);
		for (int y = 0; y < 9; ++y) {
			int y1 = (y * 512) + 256;
			int dy = (y1 - 2048);
			dy *= dy;
			for (int x = 0; x < 8; ++x) {
				int x1 = (x * 512) + 256;
				int dx = (x1 - 2048);
				dx *= dx;
				int dist = (int)sqrtf((float)(dx + dy));
				dist -= f * 128;
				int k = -dist / 8;
				if (k > 255)
					k = 512 - k;
				if (k < 0)
					k = 0;
				leds[y][x] = ((k * k) >> 8);
			}
		}
	}
}