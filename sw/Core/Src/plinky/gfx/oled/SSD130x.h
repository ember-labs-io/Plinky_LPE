#pragma once
#include "utils.h"

extern I2C_HandleTypeDef hi2c2;

#define OLED_BUFFER_SIZE OLED_HEIGHT / 8 * OLED_WIDTH + 1 // first byte is always 0x40

// SSD130x display utilities

#define SSD130X_SETLOWCOLUMN 0x00
#define SSD130X_EXTERNALVCC 0x01
#define SSD130X_SWITCHCAPVCC 0x02
#define SSD130X_SETHIGHCOLUMN 0x10
#define SSD130X_MEMORYMODE 0x20
#define SSD130X_COLUMNADDR 0x21
#define SSD130X_PAGEADDR 0x22
#define SSD130X_DEACTIVATE_SCROLL 0x2E
#define SSD130X_SETSTARTLINE 0x40
#define SSD130X_SEGREMAP 0xA0
#define SSD130X_SETMULTIPLEX 0xA8
#define SSD130X_DISPLAYALLON 0xA5
#define SSD130X_DISPLAYALLON_RESUME 0xA4
#define SSD130X_NORMALDISPLAY 0xA6
#define SSD130X_INVERTDISPLAY 0xA7
#define SSD130X_SETCONTRAST 0x81
#define SSD130X_CHARGEPUMP 0x8D
#define SSD130X_COMSCANINC 0xC0
#define SSD130X_COMSCANDEC 0xC8
#define SSD130X_SETDISPLAYOFFSET 0xD3
#define SSD130X_SETDISPLAYCLOCKDIV 0xD5
#define SSD130X_SETPRECHARGE 0xD9
#define SSD130X_SETCOMPINS 0xDA
#define SSD130X_SETVCOMDETECT 0xDB
#define SSD130X_DISPLAYOFF 0xAE
#define SSD130X_DISPLAYON 0xAF

#define I2C_ADDRESS (0x3c << 1)

static inline void ssd130x_wait(void) {
	while (HAL_I2C_GetState(&hi2c2) == HAL_I2C_STATE_BUSY_TX)
		;
}

static inline void ssd130x_command(unsigned char c) {
	u8 buf[2] = {0, c};
	HAL_I2C_Master_Transmit(&hi2c2, I2C_ADDRESS, buf, 2, 20);
	HAL_Delay(1);
}

static inline void ssd130x_flip(u8* buffer) {
	ssd130x_wait();
	ssd130x_command(0); // Page start address (0 = reset)
	HAL_I2C_Master_Transmit(&hi2c2, I2C_ADDRESS, buffer, OLED_BUFFER_SIZE, 500);
}

static void ssd130x_init() {
	// Init sequence
	ssd130x_command(SSD130X_DISPLAYOFF);                    // 0xAE
	ssd130x_command(SSD130X_SETDISPLAYCLOCKDIV);            // 0xD5
	ssd130x_command(hw_version == HW_PLINKY ? 0x80 : 0xF4); // clock ratio

	ssd130x_command(SSD130X_SETMULTIPLEX); // 0xA8
	ssd130x_command(OLED_HEIGHT - 1);

	ssd130x_command(SSD130X_SETDISPLAYOFFSET);   // 0xD3
	ssd130x_command(0x0);                        // no offset
	ssd130x_command(SSD130X_SETSTARTLINE | 0x0); // line #0
	ssd130x_command(SSD130X_CHARGEPUMP);         // 0x8D
	ssd130x_command(0x14);                       // switchcap
	ssd130x_command(SSD130X_MEMORYMODE);         // 0x20
	ssd130x_command(0x00);                       // 0x0 act like ks0108
	ssd130x_command(SSD130X_SEGREMAP | 0x1);
	ssd130x_command(SSD130X_COMSCANDEC);

	ssd130x_command(SSD130X_SETCOMPINS);                    // 0xDA
	ssd130x_command(hw_version == HW_PLINKY ? 0x02 : 0x12); // com pins
	ssd130x_command(SSD130X_SETCONTRAST);                   // 0x81
	ssd130x_command(0x8F);

	ssd130x_command(SSD130X_SETPRECHARGE);  // 0xd9
	ssd130x_command(0xF1);                  // switchcap
	ssd130x_command(SSD130X_SETVCOMDETECT); // 0xDB
	ssd130x_command(0x40);
	ssd130x_command(SSD130X_DISPLAYALLON_RESUME); // 0xA4
	ssd130x_command(SSD130X_NORMALDISPLAY);       // 0xA6

	ssd130x_command(SSD130X_DEACTIVATE_SCROLL);

	// prepare flip
	u8 col_offset = hw_version == HW_PLINKY ? 0 : 4;
	ssd130x_command(SSD130X_COLUMNADDR);
	ssd130x_command(col_offset);                  // Column start address (0 = reset)
	ssd130x_command(OLED_WIDTH - 1 + col_offset); // Column end address (127 = reset)
	ssd130x_command(SSD130X_PAGEADDR);
	ssd130x_command(0);                 // Page start address (0 = reset)
	ssd130x_command(3);                 // Page end address - 32 pixels. 1=16 pixels
	ssd130x_command(SSD130X_DISPLAYON); //--turn on oled panel
}