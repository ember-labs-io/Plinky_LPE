#pragma once
#include "utils.h"

// this module manages SPI communication, which does two things:
// - read/write sample audio data from/to 2 x 16MB external flash
// - send lfo data to the expander

#define MAX_SPI_STATE 32

// custom irq handler
extern bool alex_dma_mode;
void alex_dma_done(void);

void spi_init(void);
void spi_tick(void);
void spi_ready_for_sampler(u8 grain_id);

// sampler
extern volatile u8 spi_state;
extern u8 spi_bit_tx[256 + 4];
int spi_erase64k(u32 addr, void (*callback)(u8), u8 param);
int spi_write256(u32 addr);

static inline void spi_delay(void) {
	volatile static u8 dummy;
	for (u8 i = 0; i < 10; ++i)
		dummy++;
}