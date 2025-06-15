#include "flash.h"
#include "adc_dac.h"
#include "gfx/gfx.h"
#include "ram.h"
#include "synth/audio.h"
#include "synth/param_defs.h"
#include "touchstrips.h"

const static u64 MAGIC = 0xf00dcafe473ff02a;
const static u8 calib_sector = 255;

static u8 latest_page_id[NUM_FLASH_ITEMS] = {};
static u8 backup_page_id[NUM_PRESETS] = {};
static u8 next_free_page = 0;
static u32 next_seq = 0;

bool flash_busy = false;

// flash pointers

static u16 compute_hash(const void* data, int nbytes) {
	u16 hash = 123;
	const u8* src = (const u8*)data;
	for (int i = 0; i < nbytes; ++i)
		hash = hash * 23 + *src++;
	return hash;
}

static FlashPage* flash_page_ptr(u8 page) {
	return (FlashPage*)(FLASH_ADDR_256 + page * FLASH_PAGE_SIZE);
}

Preset* preset_flash_ptr(u8 preset_id) {
	if (preset_id >= NUM_PRESETS)
		return (Preset*)&init_params;
	FlashPage* fp = flash_page_ptr(latest_page_id[preset_id]);
	if (fp->footer.idx != preset_id || fp->footer.version != CUR_PRESET_VERSION)
		return (Preset*)&init_params;
	return (Preset*)fp;
}

PatternQuarter* ptn_quarter_flash_ptr(u8 quarter_id) {
	if (quarter_id >= NUM_PTN_QUARTERS)
		return (PatternQuarter*)zero;
	FlashPage* fp = flash_page_ptr(latest_page_id[PATTERNS_START + quarter_id]);
	if (fp->footer.idx != PATTERNS_START + quarter_id || fp->footer.version != CUR_PRESET_VERSION)
		return (PatternQuarter*)zero;
	return (PatternQuarter*)fp;
}

SampleInfo* sample_info_flash_ptr(u8 sample0) {
	if (sample0 >= NUM_SAMPLES)
		return (SampleInfo*)zero;
	FlashPage* fp = flash_page_ptr(latest_page_id[F_SAMPLES_START + sample0]);
	if (fp->footer.idx != F_SAMPLES_START + sample0 || fp->footer.version != CUR_PRESET_VERSION)
		return (SampleInfo*)zero;
	return (SampleInfo*)fp;
}

// main

void check_bootloader_flash(void) {
	u8 count = 0;
	u32* rb32 = (u32*)reverb_ram_buf;
	u32 magic = rb32[64];
	char* rb = (char*)reverb_ram_buf;
	for (; count < 64; ++count)
		if (rb[count] != 1)
			break;
	DebugLog("bootloader left %d ones for us magic is %08x\r\n", count, magic);
	const u32* app_base = (const u32*)delay_ram_buf;

	if (count != 64 / 4 || magic != 0xa738ea75) {
		return;
	}
	char buf[32];
	// checksum!
	u32 checksum = 0;
	for (u32 i = 0; i < 65536 / 4; ++i) {
		checksum = checksum * 23 + ((u32*)delay_ram_buf)[i];
	}
	if (checksum != GOLDEN_CHECKSUM) {
		DebugLog("bootloader checksum failed %08x != %08x\r\n", checksum, GOLDEN_CHECKSUM);
		oled_clear();
		draw_str(0, 0, F_8, "bad bootloader crc");
		snprintf(buf, sizeof(buf), "%08x vs %08x", (unsigned int)checksum, (unsigned int)GOLDEN_CHECKSUM);
		draw_str(0, 8, F_8, buf);
		oled_flip();
		HAL_Delay(10000);
		return;
	}
	oled_clear();
	snprintf(buf, sizeof(buf), "%08x %d", (unsigned int)magic, count);
	draw_str(0, 0, F_16, buf);
	snprintf(buf, sizeof(buf), "%08x %08x", (unsigned int)app_base[0], (unsigned int)app_base[1]);
	draw_str(0, 16, F_12, buf);
	oled_flip();

	rb32[64]++; // clear the magic

	DebugLog("bootloader app base is %08x %08x\r\n", (unsigned int)app_base[0], (unsigned int)app_base[1]);

	/*
	 * We refuse to program the first word of the app until the upload is marked
	 * complete by the host.  So if it's not 0xffffffff, we should try booting it.
	 */
	if (app_base[0] == 0xffffffff || app_base[0] == 0) {
		HAL_Delay(10000);
		return;
	}

	// first word is stack base - needs to be in RAM region and word-aligned
	if ((app_base[0] & 0xff000003) != 0x20000000) {
		HAL_Delay(10000);
		return;
	}

	/*
	 * The second word of the app is the entrypoint; it must point within the
	 * flash area (or we have a bad flash).
	 */
	if (app_base[1] < 0x08000000 || app_base[1] >= 0x08010000) {
		HAL_Delay(10000);
		return;
	}
	DebugLog("FLASHING BOOTLOADER! DO NOT RESET\r\n");
	oled_clear();
	draw_str(0, 0, F_12_BOLD, "FLASHING\nBOOTLOADER");
	char verbuf[5] = {};
	memcpy(verbuf, (void*)(delay_ram_buf + 65536 - 4), 4);
	draw_str(0, 24, F_8, verbuf);

	oled_flip();
	HAL_FLASH_Unlock();
	FLASH_EraseInitTypeDef EraseInitStruct;
	EraseInitStruct.TypeErase = FLASH_TYPEERASE_PAGES;
	EraseInitStruct.Banks = FLASH_BANK_1;
	EraseInitStruct.Page = 0;
	EraseInitStruct.NbPages = 65536 / 2048;
	u32 SECTORError = 0;
	if (HAL_FLASHEx_Erase(&EraseInitStruct, &SECTORError) != HAL_OK) {
		DebugLog("BOOTLOADER flash erase error %d\r\n", SECTORError);
		oled_clear();
		draw_str(0, 0, F_16_BOLD, "BOOTLOADER\nERASE ERROR");
		oled_flip();
		HAL_Delay(10000);
		return;
	}
	DebugLog("BOOTLOADER flash erased ok!\r\n");

	__HAL_FLASH_DATA_CACHE_DISABLE();
	__HAL_FLASH_INSTRUCTION_CACHE_DISABLE();
	__HAL_FLASH_DATA_CACHE_RESET();
	__HAL_FLASH_INSTRUCTION_CACHE_RESET();
	__HAL_FLASH_INSTRUCTION_CACHE_ENABLE();
	__HAL_FLASH_DATA_CACHE_ENABLE();
	u64* s = (u64*)delay_ram_buf;
	volatile u64* d = (volatile u64*)0x08000000;
	u32 size_bytes = 65536;
	for (; size_bytes > 0; size_bytes -= 8) {
		HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, (u32)(size_t)(d++), *s++);
	}
	HAL_FLASH_Lock();
	DebugLog("BOOTLOADER has been flashed!\r\n");
	oled_clear();
	draw_str(0, 0, F_12_BOLD, "BOOTLOADER\nFLASHED OK!");
	draw_str(0, 24, F_8, verbuf);
	oled_flip();
	HAL_Delay(3000);
}

void init_flash() {
	u8 dummy_page = 0;
	memset(latest_page_id, dummy_page, sizeof(latest_page_id));
	memset(backup_page_id, dummy_page, sizeof(backup_page_id));
	u32 highest_seq = 0;
	next_free_page = 0;
	memset(&sys_params, 0, sizeof(sys_params));
	// scan for the latest page for each object
	for (u8 page = 0; page < 255; ++page) {
		FlashPage* p = flash_page_ptr(page);
		u8 i = p->footer.idx;
		if (i >= NUM_FLASH_ITEMS)
			continue; // skip blank
		if (p->footer.version < CUR_PRESET_VERSION)
			continue; // skip old
		u16 check = compute_hash(p, 2040);
		if (check != p->footer.crc) {
			DebugLog("flash page %d has a bad crc!\r\n", page);
			if (page == dummy_page) {
				// shit, the dummy page is dead! move to a different dummy
				for (u8 i = 0; i < NUM_FLASH_ITEMS; ++i)
					if (latest_page_id[i] == dummy_page)
						latest_page_id[i]++;
				for (u8 i = 0; i < NUM_PRESETS; ++i)
					if (backup_page_id[i] == dummy_page)
						backup_page_id[i]++;
				dummy_page++;
			}
			continue;
		}
		if (p->footer.seq > highest_seq) {
			highest_seq = p->footer.seq;
			next_free_page = page + 1;
			sys_params = p->sys_params;
		}
		FlashPage* existing = flash_page_ptr(latest_page_id[i]);
		if (existing->footer.idx != i || p->footer.seq > existing->footer.seq || existing->footer.version < 2)
			latest_page_id[i] = page;
	}
	next_seq = highest_seq + 1;
	memcpy(backup_page_id, latest_page_id, sizeof(backup_page_id));
}

void flash_toggle_preset(u8 preset_id) {
	u8 temp = backup_page_id[preset_id];
	backup_page_id[preset_id] = latest_page_id[preset_id];
	latest_page_id[preset_id] = temp;
}

// writing flash

void flash_erase_page(u8 page) {
	FLASH_WaitForLastOperation((u32)FLASH_TIMEOUT_VALUE);
	SET_BIT(FLASH->CR, FLASH_CR_BKER); // bank 2
	MODIFY_REG(FLASH->CR, FLASH_CR_PNB, ((page & 0xFFU) << FLASH_CR_PNB_Pos));
	SET_BIT(FLASH->CR, FLASH_CR_PER);
	SET_BIT(FLASH->CR, FLASH_CR_STRT);
	FLASH_WaitForLastOperation((u32)FLASH_TIMEOUT_VALUE);
	CLEAR_BIT(FLASH->CR, (FLASH_CR_PER | FLASH_CR_PNB));
	// u32* mem = (u32*)(FLASH_ADDR_256 + page * 2048);
	// for (int i = 0; i < 2048 / 4; ++i)
	// 	if (mem[i] != 0xffffffff) {
	// 		DebugLog("flash mem page %d failed to erase at address %d - val %08x\r\n", page, i * 4, mem[i]);
	// 		break;
	// 	}
}

void flash_write_block(void* dst, void* src, int size) {
	u64* s = (u64*)src;
	volatile u64* d = (volatile u64*)dst;
	// int osize = size;
	while (size >= 8) {
		HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, (u32)(size_t)(d++), *s++);
		size -= 8;
	}
	// int fail = 0;
	// {
	// 	u64* s = (u64*)src;
	// 	volatile u64* d = (volatile u64*)dst;
	// 	for (int i = 0; i < osize; i += 8) {
	// 		if (*s != *d) {
	// 			u32 s0 = (*s);
	// 			u32 s1 = (*s >> 32);
	// 			u32 d0 = (*d);
	// 			u32 d1 = (*d >> 32);
	// 			DebugLog("flash program failed at offset %d - %08x %08x vs dst %08x %08x\r\n", i, s0, s1, d0, d1);
	// 			++fail;
	// 		}
	// 		s++;
	// 		d++;
	// 	}
	// }
	// if (fail != 0)
	// 	DebugLog("flash program block failed!\r\n");
}

void flash_write_page(void* src, u32 size, u8 page_id) {
	flash_busy = true;
	HAL_FLASH_Unlock();
	bool in_use;
	do {
		FlashPage* p = flash_page_ptr(next_free_page);
		in_use = next_free_page == 255;
		in_use |= (p->footer.idx < NUM_FLASH_ITEMS && latest_page_id[p->footer.idx] == next_free_page);
		in_use |= (p->footer.idx < NUM_PRESETS && backup_page_id[p->footer.idx] == next_free_page);
		if (in_use)
			++next_free_page;
	} while (in_use);
	flash_erase_page(next_free_page);
	u8 flash_page = next_free_page++;
	u8* dst = (u8*)(FLASH_ADDR_256 + flash_page * FLASH_PAGE_SIZE);
	flash_write_block(dst, src, size);
	flash_write_block(dst + FLASH_PAGE_SIZE - sizeof(SysParams) - sizeof(PageFooter), &sys_params, sizeof(SysParams));
	PageFooter footer;
	footer.idx = page_id;
	footer.seq = next_seq++;
	footer.version = CUR_PRESET_VERSION;
	footer.crc = compute_hash(dst, 2040);
	flash_write_block(dst + 2040, &footer, 8);
	HAL_FLASH_Lock();
	latest_page_id[page_id] = flash_page;
	flash_busy = false;
}

// calib

u8 flash_read_calib(void) {
	volatile u64* flash = (volatile u64*)(FLASH_ADDR_256 + calib_sector * 2048);
	u8 ver = 0;
	u8 ok = 0;
	if (flash[0] == MAGIC && flash[255] == ~MAGIC)
		ver = 2;
	if (ver == 0) {
		DebugLog("no calibration found in flash\r\n");
		return 0;
	}
	volatile u64* s = flash + 1;
	if (*s != ~(u64)(0)) {
		ok |= 1;
		memcpy(touch_calib_ptr(), (u64*)s, sizeof(CalibData) * NUM_TOUCH_READINGS);
	}
	s += sizeof(CalibData) * NUM_TOUCH_READINGS / 8;
	if (*s != ~(u64)(0)) {
		ok |= 2;
		memcpy(adc_dac_calib_ptr(), (u64*)s, sizeof(ADC_DAC_Calib) * 10);
	}
	s += sizeof(ADC_DAC_Calib) * 10 / 8;
	return ok;
}