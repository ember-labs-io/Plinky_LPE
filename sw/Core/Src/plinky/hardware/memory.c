#include "memory.h"
#include "codec.h"
#include "gfx/gfx.h"
#include "synth/audio.h"
#include "synth/params.h"
#include "synth/sampler.h"
#include "synth/sequencer.h"
#include "synth/strings.h"
#include "ui/oled_viz.h"
#include "ui/pad_actions.h"
#include "usb/web_editor.h"

// cleanup
#include "adc_dac.h"
#include "touchstrips.h"
// -- cleanup

// == DEFINES == //

#define FLASH_START 0x08000000
#define FLASH_PAGE_USABLE (FLASH_PAGE_SIZE - sizeof(SysParams) - sizeof(PageFooter))
#define FLASH_PAGE_PTR(page_id) ((FlashPage*)((FLASH_START + 256 * FLASH_PAGE_SIZE) + (page_id) * FLASH_PAGE_SIZE))
#define LATEST_FLASH_PTR(page_id) FLASH_PAGE_PTR(lastest_flash_page_id[page_id])
#define FOOTER_VERSION 2
#define CALIB_PAGE 255
#define MAGIC ((u64)0xf00dcafe473ff02a)
#define NUM_RAM_ITEMS (NUM_PRESETS + NUM_PATTERNS + NUM_SAMPLES)

// == TYPEDEFS == //

typedef enum MemItemType {
	MEM_PRESET,
	MEM_PATTERN,
	MEM_SAMPLE,
} MemItemType;

typedef struct PageFooter {
	u8 idx; // preset 0-31, pattern (quarters!) 32-127, sample 128-136, blank=0xff
	u8 version;
	u16 crc;
	u32 seq;
} PageFooter;

typedef struct FlashPage {
	union {
		u8 raw[FLASH_PAGE_USABLE];
		Preset preset;
		PatternQuarter pattern_quarter;
		SampleInfo sample_info;
	};
	SysParams sys_params;
	PageFooter footer;
} FlashPage;
static_assert(sizeof(FlashPage) == FLASH_PAGE_SIZE, "?");
static_assert(sizeof(Preset) <= FLASH_PAGE_USABLE, "?");
static_assert(sizeof(PatternQuarter) <= FLASH_PAGE_USABLE, "?");
static_assert(sizeof(SampleInfo) <= FLASH_PAGE_USABLE, "?");

// == FLASH VARS == //

static u8 lastest_flash_page_id[NUM_FLASH_ITEMS] = {};
static u8 next_free_flash_page = 0;
static u32 next_footer_seq = 0;
static bool flash_busy = false;

// == RAM VARS == //

static bool ram_initialized = false;

// load this item asap
static u8 load_preset_id = 255;
static u8 load_pattern_id = 255;
static u8 load_sample_id = 255;

// item actually in ram
static u8 ram_preset_id = 255;
static u8 ram_pattern_id = 255;
static u8 ram_sample_id = 255;

// ram item contents
SysParams sys_params;
Preset cur_preset;                 // floating preset, the one we edit and use for sound generation
PatternQuarter cur_pattern_qtr[4]; // floating pattern, the one we edit and use for recording/playing
SampleInfo cur_sample_info;

// item to change to
static u8 cued_preset_id = 255;
static u8 cued_pattern_id = 255;
static u8 cued_sample_id = 255;

static bool force_load_preset = false;
static bool force_load_pattern = false;

static u8 edit_item_id = 255; // ram item to edit | msb unset => save, msb set => clear
static u8 clear_item;         // the item that will be cleared on an X long-press in UI_LOAD

static u32 last_ram_write[NUM_MEM_SEGMENTS] = {};
static u32 last_flash_write[NUM_MEM_SEGMENTS] = {};

// == UTILS == //

static MemItemType get_item_type(u8 item_id) {
	return item_id < PATTERNS_START  ? MEM_PRESET
	       : item_id < SAMPLES_START ? MEM_PATTERN
	       : item_id < NUM_RAM_ITEMS ? MEM_SAMPLE
	                                 : NUM_RAM_ITEMS;
}

static u16 compute_hash(const void* data, u16 nbytes) {
	u16 hash = 123;
	const u8* src = (const u8*)data;
	for (u16 i = 0; i < nbytes; ++i)
		hash = hash * 23 + *src++;
	return hash;
}

u32 get_sample_address(void) {
	return ram_sample_id * MAX_SAMPLE_LEN;
}

u8* preset_flash_ptr(u8 preset_id) {
	FlashPage* fp = LATEST_FLASH_PTR(preset_id);
	if (fp->footer.idx != preset_id || fp->footer.version != FOOTER_VERSION)
		return (u8*)init_params_ptr();
	return (u8*)fp;
}

void set_sys_param(SysParam param, u16 value) {
	s32 saved_value = 0;
	switch (param) {
	case SYS_PRESET_ID:
		saved_value = sys_params.preset_id;
		break;
	case SYS_MIDI_IN_CHAN:
		saved_value = sys_params.midi_in_chan;
		break;
	case SYS_MIDI_OUT_CHAN:
		saved_value = sys_params.midi_out_chan;
		break;
	case SYS_ACCEL_SENS:
		saved_value = sys_params.accel_sens;
		break;
	case SYS_VOLUME:
		saved_value = (sys_params.volume_msb << 8) + sys_params.volume_lsb;
		break;
	case SYS_CV_QUANT:
		saved_value = sys_params.cv_quant;
		break;
	case SYS_REVERSE_ENCODER:
		saved_value = sys_params.reverse_encoder;
		break;
	case SYS_PRESET_ALIGNED:
		saved_value = sys_params.preset_aligned;
		break;
	case SYS_PATTERN_ALIGNED:
		saved_value = sys_params.pattern_aligned;
	}
	if (value == saved_value)
		return;
	switch (param) {
	case SYS_PRESET_ID:
		sys_params.preset_id = value;
		break;
	case SYS_MIDI_IN_CHAN:
		sys_params.midi_in_chan = value;
		break;
	case SYS_MIDI_OUT_CHAN:
		sys_params.midi_out_chan = value;
		break;
	case SYS_ACCEL_SENS:
		sys_params.accel_sens = value;
		break;
	case SYS_VOLUME:
		sys_params.volume_lsb = value & 255;
		sys_params.volume_msb = (value >> 8) & 7;
		break;
	case SYS_CV_QUANT:
		sys_params.cv_quant = value;
		break;
	case SYS_REVERSE_ENCODER:
		sys_params.reverse_encoder = value;
		break;
	case SYS_PRESET_ALIGNED:
		sys_params.preset_aligned = value;
		break;
	case SYS_PATTERN_ALIGNED:
		sys_params.pattern_aligned = value;
		break;
	}
	log_ram_edit(SEG_SYS_PARAMS);
}

// == FLASH WRITING == //

static void flash_erase_page(u8 page) {
	FLASH_WaitForLastOperation((u32)FLASH_TIMEOUT_VALUE);
	SET_BIT(FLASH->CR, FLASH_CR_BKER); // bank 2
	MODIFY_REG(FLASH->CR, FLASH_CR_PNB, ((page & 0xFFU) << FLASH_CR_PNB_Pos));
	SET_BIT(FLASH->CR, FLASH_CR_PER);
	SET_BIT(FLASH->CR, FLASH_CR_STRT);
	FLASH_WaitForLastOperation((u32)FLASH_TIMEOUT_VALUE);
	CLEAR_BIT(FLASH->CR, (FLASH_CR_PER | FLASH_CR_PNB));
}

static void flash_write_block(void* dst, const void* src, u16 size) {
	u64* s = (u64*)src;
	volatile u64* d = (volatile u64*)dst;
	while (size >= 8) {
		HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, (u32)(size_t)(d++), *s++);
		size -= 8;
	}
}

static void flash_write_page(u8 item_id, const void* src) {
	flash_busy = true;
	HAL_FLASH_Unlock();
	bool in_use;
	do {
		FlashPage* fp = FLASH_PAGE_PTR(next_free_flash_page);
		in_use = next_free_flash_page == 255;
		in_use |= (fp->footer.idx < NUM_FLASH_ITEMS && lastest_flash_page_id[fp->footer.idx] == next_free_flash_page);
		if (in_use)
			++next_free_flash_page;
	} while (in_use);
	flash_erase_page(next_free_flash_page);
	u16 size = (item_id < PATTERNS_START || item_id == FLOAT_PRESET_ID)     ? sizeof(Preset)
	           : (item_id < F_SAMPLES_START || item_id >= FLOAT_PATTERN_ID) ? sizeof(PatternQuarter)
	                                                                        : sizeof(SampleInfo);
	FlashPage* fp = FLASH_PAGE_PTR(next_free_flash_page);
	flash_write_block(fp, src, size);
	flash_write_block(&fp->sys_params, &sys_params, sizeof(SysParams));
	flash_write_block(&fp->footer, &(PageFooter){item_id, FOOTER_VERSION, compute_hash(fp, 2040), next_footer_seq++},
	                  sizeof(PageFooter));
	HAL_FLASH_Lock();
	lastest_flash_page_id[item_id] = next_free_flash_page++;
	flash_busy = false;
}

// == RAM STATE == //

bool preset_outdated(void) {
	return load_preset_id != ram_preset_id;
}

bool pattern_outdated(void) {
	return load_pattern_id != ram_pattern_id;
}

static bool sample_outdated(void) {
	return load_sample_id != ram_sample_id;
}

// == INIT == //

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
	if (app_base[1] < FLASH_START || app_base[1] >= 0x08010000) {
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
	EraseInitStruct.NbPages = 65536 / FLASH_PAGE_SIZE;
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
	volatile u64* d = (volatile u64*)FLASH_START;
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

void init_memory(void) {
	// init flash
	u8 dummy_page = 0;
	memset(lastest_flash_page_id, dummy_page, sizeof(lastest_flash_page_id));
	u32 highest_seq = 0;
	next_free_flash_page = 0;
	memset(&sys_params, 0, sizeof(sys_params));
	// scan for the latest page for each object
	for (u8 page = 0; page < 255; ++page) {
		FlashPage* fp = FLASH_PAGE_PTR(page);
		u8 i = fp->footer.idx;
		if (i >= NUM_FLASH_ITEMS)
			continue; // skip blank
		if (fp->footer.version < FOOTER_VERSION)
			continue; // skip old
		u16 check = compute_hash(fp, 2040);
		if (check != fp->footer.crc) {
			DebugLog("flash page %d has a bad crc!\r\n", page);
			if (page == dummy_page) {
				// shit, the dummy page is dead! move to a different dummy
				for (u8 i = 0; i < NUM_FLASH_ITEMS; ++i)
					if (lastest_flash_page_id[i] == dummy_page)
						lastest_flash_page_id[i]++;
				dummy_page++;
			}
			continue;
		}
		if (fp->footer.seq > highest_seq) {
			highest_seq = fp->footer.seq;
			next_free_flash_page = page + 1;
			sys_params = fp->sys_params;
		}
		FlashPage* existing = LATEST_FLASH_PTR(i);
		if (existing->footer.idx != i || fp->footer.seq > existing->footer.seq || existing->footer.version < 2)
			lastest_flash_page_id[i] = page;
	}
	next_footer_seq = highest_seq + 1;

	// update sys params
	Font font = F_16;
	switch (sys_params.version) {
	case LPE_SYS_PARAMS_VERSION:
		// correct!
		break;
	case OG_SYS_PARAMS_VERSION:
		// never initialized before - fill with defaults
		sys_params.midi_in_chan = 0;
		sys_params.midi_out_chan = 0;
		sys_params.accel_sens = 150; // 100%
		sys_params.cv_quant = CVQ_OFF;
		sys_params.reverse_encoder = false;
		sys_params.preset_aligned = false;
		sys_params.pattern_aligned = false;
		memset(sys_params.pad, 0, sizeof(sys_params.pad));
		sys_params.version = REV_SYS_PARAMS_VERSION;
		// fall through for further updating
	case REV_SYS_PARAMS_VERSION:
		// initialized but volume in OG mapping - remap volume
		set_sys_param(SYS_VOLUME, clampi(((s8)sys_params.volume_lsb + 45) << 4, 0, RAW_SIZE));

		// finalize
		sys_params.version = LPE_SYS_PARAMS_VERSION;
		log_ram_edit(SEG_SYS_PARAMS);
		HAL_Delay(500);
		oled_clear();
		draw_str_ctr(0, font, "updated");
		draw_str_ctr(16, font, "system settings");
		oled_flip();
		HAL_Delay(2000);
	}
	codec_update_volume();

	// update presets
	u8 presets_updated = 0;
	Preset preset;
	for (u8 preset_id = 0; preset_id < NUM_PRESETS; preset_id++) {
		// load preset
		FlashPage* fp = LATEST_FLASH_PTR(preset_id);
		if (fp->footer.idx == preset_id && fp->footer.version == FOOTER_VERSION) {
			memcpy(&preset, (Preset*)fp, sizeof(Preset));
			// update preset
			if (update_preset(&preset)) {
				// save updated preset
				flash_write_page(preset_id, &preset);
				presets_updated++;
			}
		}

		if (presets_updated) {
			oled_clear();
			draw_str_ctr(0, font, "updating");
			draw_str_ctr(16, font, "presets");
			if (preset_id < NUM_PRESETS / 2)
				inverted_rectangle(0, 0, 8 * (preset_id + 1), OLED_HEIGHT);
			else
				inverted_rectangle(8 * (preset_id + 1 - NUM_PRESETS / 2), 0, OLED_WIDTH, OLED_HEIGHT);
			oled_flip();
		}
	}

	// display result of updated presets
	if (presets_updated) {
		HAL_Delay(200);
		oled_clear();
		draw_str_ctr(0, font, "updated");
		char str[16];
		sprintf(str, "%d preset%s", presets_updated, presets_updated > 1 ? "s" : "");
		draw_str_ctr(16, font, str);
		oled_flip();
		HAL_Delay(2000);
	}

	// restore boot logo
	draw_logo();

	load_preset_id = sys_params.preset_id;
	// load floating preset
	FlashPage* fp = LATEST_FLASH_PTR(FLOAT_PRESET_ID);
	if (fp->footer.idx == FLOAT_PRESET_ID && fp->footer.version == FOOTER_VERSION) {
		memcpy(&cur_preset, (Preset*)fp, sizeof(Preset));
		ram_preset_id = load_preset_id;
		// load floating pattern
		load_pattern_id = param_index_unmod(P_PATTERN);
		for (u8 qtr = 0; qtr < 4; ++qtr)
			memcpy(&cur_pattern_qtr[qtr], LATEST_FLASH_PTR(FLOAT_PATTERN_ID + qtr), sizeof(PatternQuarter));
		ram_pattern_id = load_pattern_id;
	}
	// no existing floating preset
	else {
		// load & save preset slot into floating preset
		const Preset* src = init_params_ptr();
		fp = LATEST_FLASH_PTR(load_preset_id);
		if (fp->footer.idx == load_preset_id && fp->footer.version == FOOTER_VERSION)
			src = (Preset*)fp;
		memcpy(&cur_preset, src, sizeof(Preset));
		flash_write_page(FLOAT_PRESET_ID, &cur_preset);
		sys_params.preset_aligned = true;
		// load & save pattern slot into floating pattern
		load_pattern_id = param_index_unmod(P_PATTERN);
		u8 base_id = 4 * load_pattern_id + PATTERNS_START;
		for (u8 qtr = 0; qtr < 4; ++qtr) {
			memcpy(&cur_pattern_qtr[qtr], LATEST_FLASH_PTR(base_id + qtr), sizeof(PatternQuarter));
			flash_write_page(FLOAT_PATTERN_ID + qtr, &cur_pattern_qtr[qtr]);
		}
		sys_params.pattern_aligned = true;
		log_ram_edit(SEG_SYS_PARAMS);
	}
	clear_item = load_preset_id;
	ram_initialized = true;
}

// == MAIN == //

static bool need_flash_write(MemSegment seg, u32 now) {
	// segment up to date => no write
	if (last_ram_write[seg] == last_flash_write[seg])
		return false;

	// a ram item (preset, pattern, sample) being outdated means the user has requested to load a different one, but
	// that load has not happened yet because the current item hasn't finished writing to flash - we need to write it to
	// flash immediately so the new item can be loaded
	switch (seg) {
	case SEG_PRESET:
		if (preset_outdated())
			return true;
		break;
	case SEG_PAT0:
	case SEG_PAT1:
	case SEG_PAT2:
	case SEG_PAT3:
		if (pattern_outdated())
			return true;
		break;
	case SEG_SAMPLE_INFO:
		if (sample_outdated())
			return true;
		break;
	default:
		break;
	}

	// if our ram item was not outdated, that means we changed something small (a parameter, the contents of a step,
	// etc) we try to wait for at least 5 seconds after the most recent edit before we write them to flash
	if (now - last_ram_write[seg] > 5000)
		return true;
	// but if we are out of date for a minute or more, we write immediately
	return last_ram_write[seg] - last_flash_write[seg] > 60000;
}

void memory_frame(void) {
	bool message_flashed = false;
	MemItemType item_type = get_item_type(edit_item_id & 63);
	// clear requested
	if (edit_item_id != 255 && (edit_item_id & 128)) {
		// clearing the first bit also requests a save for this item
		edit_item_id &= 63;
		switch (item_type) {
		case MEM_PRESET:
			// clear floating preset
			memcpy(&cur_preset, init_params_ptr(), sizeof(Preset));
			log_ram_edit(SEG_PRESET);
			flash_message(F_16_BOLD, "Preset %d", "Initialized", edit_item_id + 1);
			message_flashed = true;
			break;
		case MEM_PATTERN:
			// clear floating pattern
			memset(&cur_pattern_qtr, 0, 4 * sizeof(PatternQuarter));
			log_ram_edit(SEG_PAT0);
			log_ram_edit(SEG_PAT1);
			log_ram_edit(SEG_PAT2);
			log_ram_edit(SEG_PAT3);
			flash_message(F_16_BOLD, "Pattern %d", "Cleared", edit_item_id - PATTERNS_START + 1);
			message_flashed = true;
			break;
		case MEM_SAMPLE:
			// clear current sample info
			memset(&cur_sample_info, 0, sizeof(SampleInfo));
			log_ram_edit(SEG_SAMPLE_INFO);
			flash_message(F_16_BOLD, "Sample %d", "Cleared", edit_item_id - SAMPLES_START + 1);
			break;
		default:
			break;
		}
	}
	// save requested
	if (edit_item_id != 255 && !(edit_item_id & 128)) {
		switch (item_type) {
		case MEM_PRESET:
			// update sys_params before writing to flash
			set_sys_param(SYS_PRESET_ALIGNED, true);
			// write floating preset to selected preset slot
			flash_write_page(edit_item_id, &cur_preset);
			// make selected preset active - the fast loop will retrieve this when necessary
			load_preset_id = edit_item_id;
			if (!message_flashed)
				flash_message(F_16_BOLD, "Preset %d", "Saved", edit_item_id + 1);
			break;
		case MEM_PATTERN: {
			// update sys_params before writing to flash
			set_sys_param(SYS_PATTERN_ALIGNED, true);
			// write floating pattern to selected pattern slot
			u8 pattern_id = edit_item_id - PATTERNS_START;
			u8 base_id = PATTERNS_START + 4 * pattern_id;
			for (u8 qtr = 0; qtr < 4; ++qtr)
				flash_write_page(base_id + qtr, &cur_pattern_qtr[qtr]);
			// make selected pattern active in preset - the fast loop will retrieve this when necessary
			save_param_index(P_PATTERN, pattern_id);
			if (!message_flashed)
				flash_message(F_16_BOLD, "Pattern %d", "Saved", pattern_id + 1);
		} break;
		default:
			// sample saving is handled elsewhere
			break;
		}
		// update clear item
		clear_item = edit_item_id;
	}
	// clear edit item
	edit_item_id = 255;

	// write ram items to flash (auto-save)

	u32 now = millis();
	for (u8 qtr = 0; qtr < 4; ++qtr) {
		if (need_flash_write(SEG_PAT0 + qtr, now)) {
			last_flash_write[SEG_SYS_PARAMS] = last_ram_write[SEG_SYS_PARAMS];
			last_flash_write[SEG_PAT0 + qtr] = last_ram_write[SEG_PAT0 + qtr];
			// write floating quarter
			flash_write_page(FLOAT_PATTERN_ID + qtr, &cur_pattern_qtr[qtr]);
		}
	}
	if (need_flash_write(SEG_SAMPLE_INFO, now)) {
		last_flash_write[SEG_SYS_PARAMS] = last_ram_write[SEG_SYS_PARAMS];
		last_flash_write[SEG_SAMPLE_INFO] = last_ram_write[SEG_SAMPLE_INFO];
		// write current sample info
		flash_write_page(F_SAMPLES_START + ram_sample_id, &cur_sample_info);
	}
	if (need_flash_write(SEG_PRESET, now) || need_flash_write(SEG_SYS_PARAMS, now)) {
		last_flash_write[SEG_SYS_PARAMS] = last_ram_write[SEG_SYS_PARAMS];
		last_flash_write[SEG_PRESET] = last_ram_write[SEG_PRESET];
		// write floating preset
		flash_write_page(FLOAT_PRESET_ID, &cur_preset);
	}
}

void revert_presets(void) {
	Font font = F_16;

	// revert system settings - only volume is relevant
	sys_params.volume_lsb = mini(((sys_params.volume_msb << 8) + sys_params.volume_lsb) >> 4, 63) - 45;
	sys_params.version = REV_SYS_PARAMS_VERSION;
	oled_clear();
	draw_str_ctr(0, font, "reverted");
	draw_str_ctr(16, font, "system settings");
	inverted_rectangle(0, 0, OLED_WIDTH, OLED_HEIGHT);
	oled_flip();
	HAL_Delay(2000);

	Preset preset;
	for (u8 preset_id = 0; preset_id < NUM_PRESETS; preset_id++) {
		// visuals
		oled_clear();
		draw_str_ctr(0, font, "reverting");
		draw_str_ctr(16, font, "presets");
		inverted_rectangle(4 * preset_id, 0, OLED_WIDTH, OLED_HEIGHT);
		oled_flip();

		// load preset
		FlashPage* fp = LATEST_FLASH_PTR(preset_id);
		if (fp->footer.idx == preset_id && fp->footer.version == FOOTER_VERSION) {
			memcpy(&preset, (Preset*)fp, sizeof(Preset));
			// revert preset
			revert_preset(&preset);
			// save reverted preset
			flash_write_page(preset_id, &preset);
		}
	}

	// finish up
	oled_clear();
	draw_str_ctr(0, font, "presets");
	draw_str_ctr(16, font, "reverted");
	oled_flip();
	HAL_Delay(2000);
	oled_clear();
	draw_str_ctr(0, font, "please turn");
	draw_str_ctr(16, font, "off plinky!");
	oled_flip();
	while (true) {}
}

// == UPDATE RAM == //

static bool segment_outdated(MemSegment segment) {
	return last_ram_write[segment] != last_flash_write[segment];
}

void log_ram_edit(MemSegment segment) {
	switch (segment) {
	case SEG_PRESET:
		set_sys_param(SYS_PRESET_ALIGNED, false);
		break;
	case SEG_PAT0:
	case SEG_PAT1:
	case SEG_PAT2:
	case SEG_PAT3:
		set_sys_param(SYS_PATTERN_ALIGNED, false);
		break;
	default:
		break;
	}
	last_ram_write[segment] = millis();
}

void update_preset_ram(void) {
	if (!ram_initialized)
		return;
	// already up to date
	if (!preset_outdated() && !force_load_preset)
		return;
	// flash is not ready
	if (flash_busy || segment_outdated(SEG_PRESET))
		return;
	clear_latch();
	// load cur_preset
	const Preset* src = init_params_ptr();
	FlashPage* fp = LATEST_FLASH_PTR(load_preset_id);
	if (fp->footer.idx == load_preset_id && fp->footer.version == FOOTER_VERSION)
		src = (Preset*)fp;
	memcpy(&cur_preset, src, sizeof(Preset));
	log_ram_edit(SEG_PRESET);
	// update state
	ram_preset_id = load_preset_id;
	set_sys_param(SYS_PRESET_ID, ram_preset_id);
	set_sys_param(SYS_PRESET_ALIGNED, true);
	force_load_preset = false;
}

void update_pattern_ram(void) {
	if (!ram_initialized)
		return;
	load_pattern_id = param_index(P_PATTERN);
	// already up to date
	if (!pattern_outdated() && !force_load_pattern)
		return;
	// flash is not ready
	if (flash_busy || segment_outdated(SEG_PAT0) || segment_outdated(SEG_PAT1) || segment_outdated(SEG_PAT2)
	    || segment_outdated(SEG_PAT3))
		return;
	// load cur_pattern_qtr
	u8 base_id = 4 * load_pattern_id + PATTERNS_START;
	for (u8 qtr = 0; qtr < 4; ++qtr) {
		u8 qtr_id = base_id + qtr;
		const PatternQuarter* src = (PatternQuarter*)zero;
		FlashPage* fp = LATEST_FLASH_PTR(qtr_id);
		if (fp->footer.idx == qtr_id && fp->footer.version == FOOTER_VERSION)
			src = (PatternQuarter*)fp;
		memcpy(&cur_pattern_qtr[qtr], src, sizeof(PatternQuarter));
		log_ram_edit(SEG_PAT0 + qtr);
	}
	// update state
	ram_pattern_id = load_pattern_id;
	set_sys_param(SYS_PATTERN_ALIGNED, true);
	force_load_pattern = false;
}

void update_sample_ram(void) {
	load_sample_id = param_index(P_SAMPLE);
	// already up to date
	if (!sample_outdated())
		return;
	// flash is not ready
	if (flash_busy || segment_outdated(SEG_SAMPLE_INFO))
		return;
	// load cur_sample_info
	const SampleInfo* src = (SampleInfo*)zero;
	if (load_sample_id != NO_SAMPLE) {
		FlashPage* fp = LATEST_FLASH_PTR(F_SAMPLES_START + load_sample_id);
		if (fp->footer.idx == F_SAMPLES_START + load_sample_id && fp->footer.version == FOOTER_VERSION)
			src = (SampleInfo*)fp;
	}
	memcpy(&cur_sample_info, src, sizeof(SampleInfo));
	ram_sample_id = load_sample_id;
}

// == SAVE / LOAD == //

void load_preset(u8 preset_id, bool show_message) {
	if (receiving_web_preset)
		return;
	load_preset_id = preset_id;
	force_load_preset = true;
	update_preset_ram();
	clear_item = preset_id;
	if (show_message)
		flash_message(F_16_BOLD, "Preset %d", "Loaded", preset_id + 1);
}

static void load_pattern(u8 pattern_id, bool show_message) {
	save_param_index(P_PATTERN, pattern_id);
	force_load_pattern = true;
	update_pattern_ram();
	clear_item = pattern_id + PATTERNS_START;
	if (show_message)
		flash_message(F_16_BOLD, "Pattern %d", "Loaded", pattern_id + 1);
}

static void load_sample(u8 sample_id, bool show_message) {
	save_param_index(P_SAMPLE, sample_id);
	update_sample_ram();
	if (sample_id == NO_SAMPLE)
		clear_item = ram_preset_id;
	else
		clear_item = sample_id + SAMPLES_START;
	if (show_message) {
		if (sample_id == NO_SAMPLE)
			flash_message(F_16_BOLD, "Sample Off", 0);
		else
			flash_message(F_16_BOLD, "Sample %d", "Loaded", sample_id + 1);
	}
}

void apply_cued_mem_items(void) {
	if (cued_preset_id != 255) {
		load_preset(cued_preset_id, false);
		cued_preset_id = 255;
	}
	if (cued_pattern_id != 255) {
		load_pattern(cued_pattern_id, false);
		cued_pattern_id = 255;
	}
	if (cued_sample_id != load_sample_id && cued_sample_id != 255) {
		load_sample(cued_sample_id, false);
		cued_sample_id = 255;
	}
}

void cue_mem_item(u8 item_id) {
	switch (get_item_type(item_id)) {
	case MEM_PRESET:
		// load immediately
		if (!seq_playing() || item_id == cued_preset_id) {
			load_preset(item_id, true);
			cued_preset_id = 255;
			return;
		}
		// want to cue identical preset => cancel cue
		if (item_id == load_preset_id && sys_params.preset_aligned) {
			cued_preset_id = 255;
			return;
		}
		// cue
		cued_preset_id = item_id;
		flash_message(F_16_BOLD, "Preset %d", "Cued", item_id + 1);
		break;
	case MEM_PATTERN: {
		u8 pattern_id = item_id - PATTERNS_START;
		// load immediately
		if (!seq_playing() || pattern_id == cued_pattern_id) {
			load_pattern(pattern_id, true);
			cued_pattern_id = 255;
			return;
		}
		// want to cue identical pattern => cancel cue
		if (pattern_id == load_pattern_id && sys_params.pattern_aligned) {
			cued_pattern_id = 255;
			return;
		}
		// cue
		cued_pattern_id = pattern_id;
		flash_message(F_16_BOLD, "Pattern %d", "Cued", pattern_id + 1);
		break;
	}
	case MEM_SAMPLE: {
		u8 sample_id = item_id - SAMPLES_START;
		// pressing the current sample cues turning it off
		if (sample_id == load_sample_id)
			sample_id = NO_SAMPLE;
		// load immediately
		if (!seq_playing() || sample_id == cued_sample_id) {
			load_sample(sample_id, true);
			cued_sample_id = 255;
			return;
		}
		// cue
		cued_sample_id = sample_id;
		if (sample_id == NO_SAMPLE)
			flash_message(F_16_BOLD, "Sample Off", "Cued");
		else
			flash_message(F_16_BOLD, "Sample %d", "Cued", sample_id + 1);
		break;
	}
	default:
		break;
	}
	clear_item = item_id;
}

// == UI == //

void long_press_load_item(u8 item_id) {
	bool is_sample = get_item_type(item_id) == MEM_SAMPLE;
	// holding shift pad
	if (shift_state == SS_LOAD)
		// sample => open sampler
		if (is_sample) {
			u8 sample_id = item_id & 7;
			open_sampler(sample_id);
			flash_message(F_16_BOLD, "Sample %d", "Editing", sample_id + 1);
		}
		// preset or pattern => cue to save
		else
			edit_item_id = item_id;
	// not holding shift pad: cue item for loading
	else
		cue_mem_item(item_id);
}

// line up cur_preset to be saved to ram_preset_id during the next tick
void save_preset(void) {
	edit_item_id = ram_preset_id;
}

// line up clear_item to be cleared during the next tick
void clear_mem_item(void) {
	edit_item_id = clear_item | 128;
}

// == CALIB == //

FlashCalibType flash_read_calib(void) {
	FlashCalibType flash_calib_type = FLASH_CALIB_NONE;
	volatile u64* flash = (volatile u64*)FLASH_PAGE_PTR(CALIB_PAGE);
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
	u64* flash = (u64*)FLASH_PAGE_PTR(CALIB_PAGE);
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

// == VISUALS == //

// we use load_*_ids to provide smoother visuals

u8 draw_cued_preset_id(void) {
	if (cued_preset_id != 255)
		return fdraw_str(0, 0, F_20_BOLD, "%c%d%s->%d", I_PRESET[0], load_preset_id + 1,
		                 sys_params.preset_aligned ? "" : ".", cued_preset_id + 1);
	else
		return 0;
}

u8 draw_preset_id(void) {
	return fdraw_str(0, 0, F_20_BOLD, I_PRESET "%d%s", load_preset_id + 1, sys_params.preset_aligned ? "" : ".");
}

void draw_preset_name(u8 xtab) {
	char preset_name[9];
	memcpy(preset_name, cur_preset.name, 8);
	preset_name[8] = 0;
	xtab += 2;
	draw_str(xtab, 0, F_8_BOLD, preset_name);
	// category
	if (cur_preset.category > 0 && cur_preset.category < NUM_PST_CATS)
		draw_str(xtab, 8, F_8, preset_category_name[cur_preset.category]);
}

u8 draw_cued_pattern_id(bool with_arp_icon) {
	if (cued_pattern_id != 255)
		return fdraw_str(0, 16, F_20_BOLD, "%c%d%s->%d", with_arp_icon ? I_NOTES[0] : I_SEQ[0], load_pattern_id + 1,
		                 sys_params.pattern_aligned ? "" : ".", cued_pattern_id + 1);
	else
		return 0;
}

void draw_pattern_id(bool with_arp_icon) {
	fdraw_str(0, 16, F_20_BOLD, "%c%d%s", with_arp_icon ? I_NOTES[0] : I_SEQ[0], load_pattern_id + 1,
	          sys_params.pattern_aligned ? "" : ".");
}

void draw_sample_id(void) {
	fdraw_str(-128, 16, F_20_BOLD, load_sample_id == NO_SAMPLE ? I_WAVE "Off" : I_WAVE "%d", load_sample_id + 1);
}

void draw_save_load_item(u8 item_id) {
	switch (get_item_type(item_id)) {
	case MEM_PRESET:
		fdraw_str_ctr(1, F_12, "%s", shift_state == SS_LOAD ? "Save" : "Load");
		fdraw_str_ctr(13, F_16_BOLD, "Preset %d?", item_id + 1);
		break;
	case MEM_PATTERN:
		fdraw_str_ctr(1, F_12, "%s", shift_state == SS_LOAD ? "Save" : "Load");
		fdraw_str_ctr(13, F_16_BOLD, "Pattern %d?", item_id - PATTERNS_START + 1);
		break;
	case MEM_SAMPLE: {
		u8 sample_id = item_id - SAMPLES_START;
		if (sample_id == ram_sample_id && shift_state != SS_LOAD) {
			draw_str_ctr(1, F_12, "Deactivate");
			draw_str_ctr(13, F_16_BOLD, "Sample?");
		}
		else {
			fdraw_str_ctr(1, F_12, "%s", shift_state == SS_LOAD ? "Edit" : "Load");
			fdraw_str_ctr(13, F_16_BOLD, "Sample %d?", sample_id + 1);
		}
		break;
	}
	default:
		break;
	}
}

void draw_clear_item(void) {
	switch (get_item_type(clear_item)) {
	case MEM_PRESET:
		fdraw_str_ctr(1, F_12, "Initialize");
		fdraw_str_ctr(13, F_16_BOLD, "Preset %d?", clear_item + 1);
		break;
	case MEM_PATTERN:
		fdraw_str_ctr(1, F_12, "Clear");
		fdraw_str_ctr(13, F_16_BOLD, "Pattern %d?", clear_item - PATTERNS_START + 1);
		break;
	case MEM_SAMPLE:
		fdraw_str_ctr(1, F_12, "Clear");
		fdraw_str_ctr(13, F_16_BOLD, "Sample %d?", clear_item - SAMPLES_START + 1);
		break;
	default:
		break;
	}
}

void draw_ui_load_label(void) {
	const u8 x0 = 101;
	const u8 x1 = OLED_WIDTH - 1;
	fdraw_str(x0 + 5, 1, F_8_BOLD, "%s", shift_state == SS_LOAD ? "SAVE" : "LOAD");
	vline(x0, 0, 9, 1);
	vline(x1, 0, 9, 1);
	hline(x0, 9, x1 + 1, 1);
}

u8 ui_load_led(u8 x, u8 y, u8 pulse) {
	u8 item_id = x * 8 + y;

	// all patterns low brightness
	u8 k = get_item_type(item_id) == MEM_PATTERN ? 64 : 0;

	// pulse cued load item
	if (item_id == cued_preset_id)
		k = pulse;
	if (item_id == PATTERNS_START + cued_pattern_id)
		k = pulse;
	if (item_id == SAMPLES_START + cued_sample_id)
		k = pulse;

	// full selected load item
	if (item_id == load_preset_id)
		k = 255;
	if (item_id == PATTERNS_START + load_pattern_id)
		k = 255;
	if (item_id == SAMPLES_START + load_sample_id)
		k = 255;

	return k;
}