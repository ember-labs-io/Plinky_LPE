#include "flash.h"
#include "adc_dac.h"
#include "gfx/gfx.h"
#include "ram.h"
#include "synth/audio.h"
#include "synth/params.h"
#include "touchstrips.h"

typedef struct PageFooter {
	u8 idx; // preset 0-31, pattern (quarters!) 32-127, sample 128-136, blank=0xff
	u8 version;
	u16 crc;
	u32 seq;
} PageFooter;
static_assert(sizeof(Preset) + sizeof(SysParams) + sizeof(PageFooter) <= 2048, "?");
static_assert(sizeof(PatternQuarter) + sizeof(SysParams) + sizeof(PageFooter) <= 2048, "?");
static_assert(sizeof(SampleInfo) + sizeof(SysParams) + sizeof(PageFooter) <= 2048, "?");

typedef struct FlashPage {
	union {
		u8 raw[FLASH_PAGE_SIZE - sizeof(SysParams) - sizeof(PageFooter)];
		Preset preset;
		PatternQuarter pattern_quarter;
		SampleInfo sample_info;
	};
	SysParams sys_params;
	PageFooter footer;
} FlashPage;
static_assert(sizeof(FlashPage) == 2048, "?");

#define FLASH_ADDR_256 (0x08000000 + 256 * FLASH_PAGE_SIZE)

const static u64 MAGIC = 0xf00dcafe473ff02a;
const static u8 CALIB_PAGE = 255;

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

const Preset* preset_flash_ptr(u8 preset_id) {
	if (preset_id >= NUM_PRESETS)
		return init_params_ptr();
	FlashPage* fp = flash_page_ptr(latest_page_id[preset_id]);
	if (fp->footer.idx != preset_id || fp->footer.version != CUR_PRESET_VERSION)
		return init_params_ptr();
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
}

void flash_write_block(void* dst, const void* src, int size) {
	u64* s = (u64*)src;
	volatile u64* d = (volatile u64*)dst;
	while (size >= 8) {
		HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, (u32)(size_t)(d++), *s++);
		size -= 8;
	}
}

void flash_write_page(const void* src, u32 size, u8 page_id) {
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

FlashCalibType flash_read_calib(void) {
	FlashCalibType flash_calib_type = FLASH_CALIB_NONE;
	volatile u64* flash = (volatile u64*)(FLASH_ADDR_256 + CALIB_PAGE * 2048);
	if (!(flash[0] == MAGIC && flash[255] == ~MAGIC))
		return FLASH_CALIB_NONE;
	// read touch calibration data
	volatile u64* src = flash + 1;
	if (*src != ~(u64)(0)) {
		flash_calib_type |= FLASH_CALIB_TOUCH;
		memcpy(touch_calib_ptr(), (u64*)src, sizeof(TouchCalibData) * NUM_TOUCH_READINGS);
	}
	// read adc/dac calibration data
	src += sizeof(TouchCalibData) * NUM_TOUCH_READINGS / 8;
	if (*src != ~(u64)(0)) {
		flash_calib_type |= FLASH_CALIB_ADC_DAC;
		memcpy(adc_dac_calib_ptr(), (u64*)src, sizeof(ADC_DAC_Calib) * NUM_ADC_DAC_ITEMS);
	}
	return flash_calib_type;
}

void flash_write_calib(FlashCalibType flash_calib_type) {
	HAL_FLASH_Unlock();
	flash_erase_page(CALIB_PAGE);
	u64* flash = (u64*)(FLASH_ADDR_256 + CALIB_PAGE * 2048);
	HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, (u32)flash, MAGIC);
	// write touch calibration data
	u64* dst = flash + 1;
	if (flash_calib_type & FLASH_CALIB_TOUCH)
		flash_write_block(dst, touch_calib_ptr(), sizeof(TouchCalibData) * NUM_TOUCH_READINGS);
	// write adc/dac calibration data
	dst += (sizeof(TouchCalibData) * NUM_TOUCH_READINGS + 7) / 8;
	if (flash_calib_type & FLASH_CALIB_ADC_DAC)
		flash_write_block(dst, adc_dac_calib_ptr(), sizeof(ADC_DAC_Calib) * NUM_ADC_DAC_ITEMS);
	HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, (u32)(flash + 255), ~MAGIC);
	HAL_FLASH_Lock();
}