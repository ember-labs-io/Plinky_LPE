#include "spi.h"
#include "hardware/expander.h"
#include "synth/sampler.h"

extern SPI_HandleTypeDef hspi2;

#define CHECK_RV(spi_rv, msg)                                                                                          \
	if (spi_rv != 0)                                                                                                   \
		DebugLog("SPI ERROR %d " msg "\r\n", spi_rv);

#define SPI_PORT GPIOE
#define SPI_CS0_PIN_ GPIO_PIN_1
#define SPI_CS1_PIN_ GPIO_PIN_0

bool alex_dma_mode;

static u8 spi_big_rx[256 + 4];
static u8 cur_spi_pin = SPI_CS0_PIN_;

// global for ui
volatile u8 spi_state = 0;
u8 spi_bit_tx[256 + 4];

// toolies

static void spi_assert_cs(void) {
	SPI_PORT->BSRR = SPI_CS1_PIN_ | SPI_CS0_PIN_;
	hspi2.Instance->CR1 &= ~(64);
	hspi2.Instance->CR1 |= 1 | 64;

	SPI_PORT->BRR = cur_spi_pin;
}

static void spi_release_cs(void) {
	SPI_PORT->BSRR = SPI_CS1_PIN_ | SPI_CS0_PIN_;
}

static void reset_spi_state(void) {
	spi_state = 0;
	alex_dma_mode = false;
	GPIOA->BSRR = 1 << 8; // DAC cs high
}

// init

static void spi_set_chip(u32 addr) {
	cur_spi_pin = (addr & (1 << 24)) ? SPI_CS1_PIN_ : SPI_CS0_PIN_;
}

static int spi_read_id(void) {
	u8 spi_tx_buf[6] = {0x90, 0, 0, 0, 0, 0};
	u8 spi_rx_buf[6] = {0};
	spi_assert_cs();
	spi_delay();
	int spi_rv = HAL_SPI_TransmitReceive(&hspi2, (u8*)spi_tx_buf, (u8*)spi_rx_buf, 6, -1);
	CHECK_RV(spi_rv, "spi_read_id");
	spi_release_cs();
	spi_delay();
	return (spi_rv == 0) ? (spi_rx_buf[4] + (spi_rx_buf[5] << 8)) : -1;
}

void spi_init(void) {
	spi_set_chip(0xffffffff);
	int spi_id = spi_read_id();
	DebugLog("SPI flash chip 1 id %04x\r\n", spi_id);
	spi_set_chip(0);
	spi_id = spi_read_id();
	DebugLog("SPI flash chip 0 id %04x\r\n", spi_id);
}

// main

static void dma_set_config(DMA_HandleTypeDef* hdma, u32 src_address, u32 dst_address, u32 data_length) {
	hdma->DmaBaseAddress->IFCR = (DMA_ISR_GIF1 << (hdma->ChannelIndex & 0x1CU)); /* Clear all flags */
	hdma->Instance->CNDTR = data_length;
	if ((hdma->Init.Direction) == DMA_MEMORY_TO_PERIPH) {
		hdma->Instance->CPAR = dst_address;
		hdma->Instance->CMAR = src_address;
	}
	else {
		hdma->Instance->CPAR = src_address;
		hdma->Instance->CMAR = dst_address;
	}
}

static void setup_spi_alex_dma(u32 tx_addr, u32 rx_addr, int len) { // len is in 8 bit words
	// ALEX DMA MODE! fewer interrupts; simpler code.
	alex_dma_mode = true;
	CLEAR_BIT(hspi2.Instance->CR2, SPI_CR2_LDMATX | SPI_CR2_LDMARX); // Reset the threshold bit
	SET_BIT(hspi2.Instance->CR2,
	        SPI_RXFIFO_THRESHOLD); // Set RX Fifo threshold according the reception data length: 8bit
	// config rx dma - transfer complete callback
	__HAL_DMA_DISABLE(hspi2.hdmarx);
	dma_set_config(hspi2.hdmarx, (u32)&hspi2.Instance->DR, rx_addr, len);
	__HAL_DMA_DISABLE_IT(hspi2.hdmarx, (DMA_IT_HT | DMA_IT_TE));
	__HAL_DMA_ENABLE_IT(hspi2.hdmarx, (DMA_IT_TC));
	__HAL_DMA_ENABLE(hspi2.hdmarx);
	SET_BIT(hspi2.Instance->CR2, SPI_CR2_RXDMAEN); // Enable Rx DMA Request

	// config tx dma - no interrupts
	__HAL_DMA_DISABLE(hspi2.hdmatx);
	dma_set_config(hspi2.hdmatx, tx_addr, (u32)&hspi2.Instance->DR, len);
	__HAL_DMA_DISABLE_IT(hspi2.hdmatx, (DMA_IT_HT | DMA_IT_TE | DMA_IT_TC));
	__HAL_DMA_ENABLE(hspi2.hdmatx);
	if ((hspi2.Instance->CR1 & SPI_CR1_SPE) != SPI_CR1_SPE)
		__HAL_SPI_ENABLE(&hspi2); // Enable SPI peripheral
	SET_BIT(hspi2.Instance->CR2, SPI_CR2_TXDMAEN);
}

static void spi_update_dac(int dac_chan) {
	static u16 dac_dummy;
	u16 dac_cmd;
	spi_state = MAX_SPI_STATE + dac_chan + 1; // the NEXT state
	u16 data = get_expander_lfo_data(dac_chan & 3);
	dac_cmd = (2 << 14) + ((dac_chan & 3) << 12) + (data & 0xfff);
	dac_cmd = (dac_cmd >> 8) | (dac_cmd << 8);
	// set expander dac
	cur_spi_pin = 0;
	hspi2.Instance->CR1 &= ~(1 | 64);
	hspi2.Instance->CR1 |= 64;
	SPI_PORT->BSRR = SPI_CS1_PIN_ | SPI_CS0_PIN_;
	GPIOA->BRR = 1 << 8; // dac cs low
	setup_spi_alex_dma((u32)&dac_cmd, (u32)&dac_dummy, 2);
}

static int spi_readgrain_dma(int grain_id) {
	spi_release_cs();
	u32 addr;
again:
	addr = grain_pos[grain_id] * 2;
	spi_bit_tx[0] = 3;
	spi_bit_tx[1] = addr >> 16;
	spi_bit_tx[2] = addr >> 8;
	spi_bit_tx[3] = addr >> 0;
	spi_state = grain_id;
	++spi_state;
	int start = 0;
	if (grain_id)
		start = grain_buf_end[grain_id - 1];
	int len = grain_buf_end[grain_id] - start;

	if (len <= 2) {
		if (spi_state == MAX_SPI_STATE) {
			spi_update_dac(0);
			return 0;
		}
		++grain_id;
		goto again;
	}

	spi_set_chip(addr);
	spi_assert_cs();

	setup_spi_alex_dma((u32)spi_bit_tx, (u32)(grain_buf + start), len * 2);

	return 0;
}

void spi_tick(void) {
	if (spi_state == 0) {
		if (using_sampler())
			spi_readgrain_dma(0); // kick off the dma for next time
		else
			spi_update_dac(0); // just update dac when not in sampler mode
	}
}

void spi_ready_for_sampler(u8 grain_id) {
	while (spi_state && spi_state <= grain_id + 2)
		;
}

// end of dma irq handler

void alex_dma_done(void) {
	// replacement irq handler for the HAL guff.
	DMA_HandleTypeDef* hdma = hspi2.hdmarx; /*!< SPI Rx DMA Handle parameters             */
	u32 flag_it = hdma->DmaBaseAddress->ISR;
	u32 source_it = hdma->Instance->CCR;
	if (((flag_it & (DMA_FLAG_TC1 << (hdma->ChannelIndex & 0x1CU))) != 0U) && ((source_it & DMA_IT_TC) != 0U)) {
		__HAL_DMA_DISABLE_IT(hdma, DMA_IT_TE | DMA_IT_TC | DMA_IT_HT);
		hdma->DmaBaseAddress->IFCR =
		    (DMA_ISR_TCIF1 << (hdma->ChannelIndex & 0x1CU)); /* Clear the transfer complete flag */
		if (spi_state >= MAX_SPI_STATE) {
			GPIOA->BSRR = 1 << 8; // DAC cs high
			if (spi_state == MAX_SPI_STATE + 4) {
				reset_spi_state();
			}
			else {
				int dac_chan = spi_state - MAX_SPI_STATE;
				if (dac_chan < 4 && dac_chan >= 0)
					spi_update_dac(dac_chan);
			}
		}
		else {
			spi_readgrain_dma(spi_state);
		}
	}
}

// actual writing

static int spi_write_enable(void) {
	u8 spi_tx_buf[1] = {6};
	u8 spi_rx_buf[1];
	spi_assert_cs();
	spi_delay();
	int spi_rv = HAL_SPI_TransmitReceive(&hspi2, (u8*)spi_tx_buf, (u8*)spi_rx_buf, 1, -1);
	CHECK_RV(spi_rv, "spi_write_enable");
	spi_release_cs();
	spi_delay();
	return spi_rv;
}

static int spi_wait_not_busy(const char* msg, void (*callback)(u8), u8 param) {
	int spi_rv = 0;
	int i = millis();
	u8 spi_tx_buf[1] = {5};
	u8 spi_rx_buf[1] = {23};
	spi_assert_cs();

	spi_rv = HAL_SPI_TransmitReceive(&hspi2, (u8*)spi_tx_buf, (u8*)spi_rx_buf, 1, -1);
	CHECK_RV(spi_rv, "spi_waitnotbusy1");
	spi_rx_buf[0] = 0xff;
	while (spi_rx_buf[0] & 1) {

		if (callback)
			callback(param);

		spi_rx_buf[0] = 0;
		spi_rv = HAL_SPI_TransmitReceive(&hspi2, (u8*)spi_tx_buf, (u8*)spi_rx_buf, 1, -1);
		CHECK_RV(spi_rv, "spi_waitnotbusy2");
		if (spi_rv)
			break;
	}
	spi_release_cs();
	spi_delay();
	int t = millis() - i;
	if (t > 10)
		DebugLog("flash write/erase operation [%s] took %dms\r\n", msg, t);
	return spi_rv;
}

int spi_erase64k(u32 addr, void (*callback)(u8), u8 param) {
	spi_set_chip(addr);
	spi_write_enable();
	u8 spi_tx_buf[4] = {0xd8, addr >> 16, addr >> 8, addr};
	u8 spi_rx_buf[4];
	DebugLog("spi erase %d\r\n", addr);
	spi_assert_cs();
	spi_delay();
	int spi_rv = HAL_SPI_TransmitReceive(&hspi2, (u8*)spi_tx_buf, (u8*)spi_rx_buf, 4, -1);
	CHECK_RV(spi_rv, "spi_erase1");
	spi_release_cs();
	spi_delay();
	if (spi_rv == 0)
		spi_rv = spi_wait_not_busy("erase", callback, param);
	else {
		DebugLog("HAL_SPI_TransmitReceive returned %d in erase\r\n", spi_rv);
	}
	return spi_rv;
}

int spi_write256(u32 addr) {
	spi_set_chip(addr);
	spi_write_enable();
	spi_bit_tx[0] = 2;
	spi_bit_tx[1] = addr >> 16;
	spi_bit_tx[2] = addr >> 8;
	spi_bit_tx[3] = addr >> 0;
	spi_assert_cs();

	spi_delay();
	int spi_rv = HAL_SPI_TransmitReceive(&hspi2, (u8*)spi_bit_tx, (u8*)spi_big_rx, 4 + 256, -1);
	CHECK_RV(spi_rv, "spi_write256");

	spi_release_cs();
	spi_delay();

	if (spi_rv == 0)
		spi_rv = spi_wait_not_busy("write", 0, 0);
	return spi_rv;
}
