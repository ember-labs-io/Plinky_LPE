#include "sampler.h"
#include "audio.h"
#include "audio_tools.h"
#include "gfx/gfx.h"
#include "hardware/leds.h"
#include "hardware/memory.h"
#include "hardware/spi.h"
#include "params.h"
#include "synth.h"

SamplerMode sampler_mode = SM_PREVIEW;

static u8 cur_slice_id = 0; // active slice id
static u32 record_flashaddr_base = 0;

// used while recording a new sample
static u32 buf_start_pos = 0;
static u32 buf_write_pos = 0;
static u32 buf_read_pos = 0;

// for leds drawing
static u8 peak_hist[NUM_GRAINS];
static u8 peak_hist_pos = 0;

void open_sampler(u8 with_sample_id) {
	save_param_index(P_SAMPLE, with_sample_id);
	update_sample_ram();
	cur_slice_id = 7;
	ui_mode = UI_SAMPLE_EDIT;
}

void sampler_recording_tick(u32* dst, u32* audioin) {
	update_sample_ram();
	// while armed => check for incoming audio
	if ((sampler_mode == SM_ARMED) && (audio_in_peak > 1024))
		start_recording_sample();
	if (sampler_mode > SM_ERASING && sampler_mode < SM_STOPPING4) {
		s16* dldst = delay_ram_buf + (buf_write_pos & DL_SIZE_MASK);
		// stopping recording => write zeroes (why don't we just write these all at once?)
		if (sampler_mode >= SM_STOPPING1) {
			memset(dldst, 0, SAMPLES_PER_TICK * 2);
			sampler_mode++;
		}
		// armed or recording => monitor audio (write in-buffer to out-buffer)
		else {
			const s16* asrc = (const s16*)audioin;
			s16* adst = (s16*)dst;
			for (int i = 0; i < SAMPLES_PER_TICK; ++i) {
				s16 smp = *dldst++ = SATURATE16((((int)(asrc[0] + asrc[1])) * (int)(ext_gain_smoother.y2 / 2)) >> 14);
				adst[0] = adst[1] = smp;
				adst += 2;
				asrc += 2;
			}
		}
		buf_write_pos += SAMPLES_PER_TICK;
	}
}

static void draw_sample_erasing(u8 erase_pos) {
	oled_clear();
	draw_str_ctr(6, F_20_BOLD, "erasing");
	inverted_rectangle(0, 0, erase_pos * 2, 32);
	oled_flip();
	return;
}

static void setwaveform4(SampleInfo* s, int x, int v) {
	v = clampi(v, 0, 15);
	u8* b = &s->waveform4_b[(x >> 1) & 1023];
	if (x & 1) {
		v = maxi(v, (*b) >> 4);
		*b = (*b & 0x0f) | (v << 4);
	}
	else {
		v = maxi(v, (*b) & 15);
		*b = (*b & 0xf0) | v;
	}
}

u8 get_waveform4(SampleInfo* s, int x) { // x is 0-2047
	if (x < 0 || x >= 2048)
		return 0;
	return (s->waveform4_b[x >> 1] >> ((x & 1) * 4)) & 15;
}

u16 getwaveform4zoom(SampleInfo* s, int x, int zoom) { // x is 0-2048. returns average and peak!
	if (zoom <= 0)
		return get_waveform4(s, x >> zoom);
	int samplepairs = 1 << (zoom - 1);
	u8* b = &s->waveform4_b[(x >> 1) & 1023];
	int avg = 0, peak = 0;
	u8* bend = &s->waveform4_b[1024];
	for (int i = 0; i < samplepairs && b < bend; ++i, ++b) {
		int s0 = b[0] & 15;
		int s1 = b[0] >> 4;
		avg += s0 + s1;
		peak = maxi(peak, maxi(s0, s1));
	}
	avg >>= zoom;
	return avg + peak * 256;
}

// reset all sample recording variables and initiate erasing the sample flash buffer
void start_erasing_sample_buffer(void) {
	record_flashaddr_base = 2 * get_sample_address();
	cur_slice_id = 0;
	buf_start_pos = 0;
	buf_read_pos = 0;
	buf_write_pos = 0;
	init_ext_gain_for_recording();
	sampler_mode = SM_ERASING;
}

// clear the sample in flash
void clear_flash_sample(void) {
	SampleInfo* s = &cur_sample_info;
	draw_sample_erasing(0);
	// wait for spi idle and disable spi
	while (spi_state)
		;
	spi_state = 255;
	HAL_Delay(10);
	// mysteriously, sometimes page 0 wasn't erasing. maybe do it twice? WOOAHAHA
	spi_erase64k(0 + record_flashaddr_base, draw_sample_erasing, 0);
	// clear sample flash + sample info
	u8 erase_pos = 0;
	for (u32 addr = 0; addr < MAX_SAMPLE_LEN * 2; addr += 65536)
		spi_erase64k(addr + record_flashaddr_base, draw_sample_erasing, ++erase_pos);
	memset(s, 0, sizeof(SampleInfo));
	log_ram_edit(SEG_SAMPLE_INFO);
	// re-enable spi
	spi_state = 0;
	sampler_mode = SM_PRE_ARMED;
}

void start_recording_sample(void) {
	static const u16 max_leadin = 1024;
	cur_slice_id = 0;
	memset(&cur_sample_info, 0, sizeof(SampleInfo));
	int leadin = mini(buf_write_pos, max_leadin);
	buf_read_pos = buf_start_pos = buf_write_pos - leadin;
	cur_sample_info.samplelen = 0;
	cur_sample_info.splitpoints[0] = leadin;
	sampler_mode = SM_RECORDING;
}

// write blocks from delay buffer to spi flash while recording
void write_flash_sample_blocks(void) {
	static const u8 BlockSize = 256 / 2;
	SampleInfo* s = &cur_sample_info;
	u32 write_pos = buf_write_pos;
	// wait for spi idle and disable spi
	while (spi_state)
		;
	spi_state = 255;
	// when blocks available and sample not full
	while ((write_pos >= buf_read_pos + BlockSize) && (s->samplelen < MAX_SAMPLE_LEN)) {
		// set up read/write adresses
		s16* src = delay_ram_buf + (buf_read_pos & DL_SIZE_MASK);
		s16* dst = (s16*)(spi_bit_tx + 4);
		int flashaddr = (buf_read_pos - buf_start_pos) * 2;
		buf_read_pos += BlockSize;
		u16 peak = 0;
		s16* delay_ram_bufend = delay_ram_buf + DL_SIZE_MASK + 1;
		// copy a block
		for (u8 i = 0; i < BlockSize; ++i) {
			s16 smp = *src++;
			*dst++ = smp;
			// save peak value for waveform
			peak = maxi(peak, abs(smp));
			// loop buffer
			if (src == delay_ram_bufend)
				src = delay_ram_buf;
		}
		// save waveform
		setwaveform4(s, flashaddr / 2 / 1024, peak / 1024);
		// write audio to flash
		if (spi_write256(flashaddr + record_flashaddr_base) != 0) {
			DebugLog("flash write fail\n");
		}
		// recalc sample length
		s->samplelen = buf_read_pos - buf_start_pos;
		log_ram_edit(SEG_SAMPLE_INFO);
		// sample full => stop recording
		if (s->samplelen >= MAX_SAMPLE_LEN) {
			stop_recording_sample();
			break;
		}
	}

	// re-enable spi
	spi_state = 0;

	// finalize recording sample
	if (sampler_mode == SM_STOPPING4) {
		reverb_clear();
		// clear out the raw audio in the delay_ram_buf
		delay_clear();
		log_ram_edit(SEG_SAMPLE_INFO);
		// fill in the remaining split points
		int startsamp = cur_sample_info.splitpoints[cur_slice_id];
		int endsamp = cur_sample_info.samplelen;
		u8 n = 8 - cur_slice_id; // remaining slices
		for (u8 i = cur_slice_id + 1; i < 8; ++i) {
			int samp = startsamp + ((endsamp - startsamp) * (i - cur_slice_id)) / n;
			cur_sample_info.splitpoints[i] = samp;
		}
		cur_slice_id = 0;
		log_ram_edit(SEG_SAMPLE_INFO);
		sampler_mode = SM_PREVIEW;
	}
}

// register a slice point while recording
void sampler_record_slice_point(void) {
	if (cur_sample_info.samplelen >= SAMPLES_PER_TICK) {
		// add slice point if there is still room
		if (cur_slice_id < 7) {
			cur_slice_id++;
			cur_sample_info.splitpoints[cur_slice_id] = cur_sample_info.samplelen - SAMPLES_PER_TICK;
		}
		// when all slices are filled, press ends the recording
		else
			stop_recording_sample();
	}
}

void try_stop_recording_sample(void) {
	if (cur_sample_info.samplelen >= SAMPLES_PER_TICK) {
		if (cur_slice_id > 0)
			cur_slice_id--;
		stop_recording_sample();
	}
}

void finish_recording_sample(void) {
	// clear out the raw audio in the delay_ram_buf
	reverb_clear();
	delay_clear();
	log_ram_edit(SEG_SAMPLE_INFO); // fill in the remaining split points
	int startsamp = cur_sample_info.splitpoints[cur_slice_id];
	int endsamp = cur_sample_info.samplelen;
	int n = 8 - cur_slice_id;
	for (int i = cur_slice_id + 1; i < 8; ++i) {
		int samp = startsamp + ((endsamp - startsamp) * (i - cur_slice_id)) / n;
		cur_sample_info.splitpoints[i] = samp;
	}
	cur_slice_id = 0;
	log_ram_edit(SEG_SAMPLE_INFO);
	sampler_mode = SM_PREVIEW;
}

// == SLICES == //

static void set_slice_point(u8 slice_id, float slice_pos) {
	float smin = maxf(slice_id ? cur_sample_info.splitpoints[slice_id - 1] + 1024.f : 0.f, 0.f);
	float smax = minf((slice_id < 7) ? cur_sample_info.splitpoints[slice_id + 1] - 1024.f : cur_sample_info.samplelen,
	                  cur_sample_info.samplelen);
	slice_pos = clampf(slice_pos, smin, smax);
	if (cur_sample_info.splitpoints[slice_id] != slice_pos) {
		cur_sample_info.splitpoints[slice_id] = slice_pos;
		log_ram_edit(SEG_SAMPLE_INFO);
	}
}

void sampler_adjust_cur_slice_point(float diff) {
	set_slice_point(cur_slice_id, cur_sample_info.splitpoints[cur_slice_id] + diff);
}

void sampler_adjust_slice_point_from_touch(u8 slice_id, u16 touch_pos, bool init_slice) {
	static s32 start_slice_pos = 0;
	static u16 start_touch_pos = 0;
	static ValueSmoother slice_pos_smoother;

	// save start values
	if (init_slice) {
		cur_slice_id = slice_id;
		start_slice_pos = cur_sample_info.splitpoints[slice_id];
		start_touch_pos = touch_pos;
		set_smoother(&slice_pos_smoother, start_slice_pos);
	}
	// run the pos smoother and set the slice point to the result of it
	else {
		smooth_value(&slice_pos_smoother,
		             start_slice_pos - deadzone(touch_pos - start_touch_pos, 32.f) * (32000.f / 2048.f), 32000.f);
		set_slice_point(slice_id, slice_pos_smoother.y2);
	}
}

void sampler_adjust_cur_slice_pitch(s8 diff) {
	u8 newnote = clampi(cur_sample_info.notes[cur_slice_id] + diff, 0, 96);
	if (newnote != cur_sample_info.notes[cur_slice_id]) {
		cur_sample_info.notes[cur_slice_id] = newnote;
		log_ram_edit(SEG_SAMPLE_INFO);
	}
}

// == MODES == //

void sampler_toggle_play_mode(void) {
	cur_sample_info.pitched = !cur_sample_info.pitched;
	log_ram_edit(SEG_SAMPLE_INFO);
}

void sampler_iterate_loop_mode(void) {
	cur_sample_info.loop = (cur_sample_info.loop + 1) & 3;
	log_ram_edit(SEG_SAMPLE_INFO);
}

void sampler_oled_visuals(void) {
	SampleInfo* s = &cur_sample_info;
	switch (sampler_mode) {
	case SM_PREVIEW:
		// no sample => draw text
		if (!s->samplelen) {
			draw_str(0, 0, F_16, "<empty sample>");
			draw_str(0, 16, F_16, "hold " I_RECORD " to record");
		}
		// yes sample => draw current sample, slice markers, flags
		else {
			u8 slice_id = cur_slice_id & 7;
			int ofs = s->splitpoints[slice_id] / 1024;
			int maxx = s->samplelen / 1024;
			// draw slice
			for (u8 i = 0; i < 128; ++i) {
				int x = i - 64 + ofs;
				u8 h = get_waveform4(s, x);
				if (x >= 0 && x < maxx)
					vline(i, 15 - h, 16 + h, (i < 64) ? 2 : 1);
				if (i == 64) {
					vline(i, 0, 13 - h, 1);
					vline(i, 18 + h, 32, 1);
				}
			}
			gfx_text_color = 3;
			fdraw_str(64 + 2, 0, F_12, "%d", slice_id + 1);
			// draw other slices
			for (int si = 0; si < 8; ++si) {
				if (si != slice_id) {
					int x = (s->splitpoints[si] / 1024) - ofs + 64;
					char buf[2] = {'1' + si, 0};
					u8 h = get_waveform4(s, s->splitpoints[si] / 1024);
					if (x >= 0 && x < 128) {
						vline(x, 0, 13 - h, 2);
						vline(x, 18 + h, 32, 2);
					}
					drawstr_noright(x + 2, 0, F_8, buf);
				}
			}
			// draw sample flags
			gfx_text_color = 2;
			draw_str(-128 + 16, 32 - 12, F_12, (s->loop & 2) ? "all" : "slc");
			draw_icon(128 - 16, 32 - 14, ((s->loop & 1) ? I_FEEDBACK[0] : I_RIGHT[0]) - 0x80, gfx_text_color);
			if (s->pitched)
				draw_str(0, 32 - 12, F_12, note_name(s->notes[cur_slice_id & 7], true));
			else
				draw_str(0, 32 - 12, F_12, "tape");
		}
		break;
	case SM_RECORDING:
		// if we're behind on writing the recording buffer to flash, skip drawing visuals
		if (buf_write_pos - buf_read_pos >= 4096)
			return;
		// otherwise: fall thru
	default:
		// live audio visuals
		u8 peak = maxi(0, audio_in_peak / 128);
		u8 hold = maxi(0, audio_in_hold / 128);
		draw_str(-128, 0, F_12,
		         sampler_mode == SM_PRE_ARMED   ? "rec level " I_A
		         : sampler_mode == SM_ARMED     ? "armed!"
		         : sampler_mode == SM_RECORDING ? "recording"
		                                        : "");
		fdraw_str(-128, 32 - 12, F_12, (hold >= 254) ? "CLIP! %dms" : "%dms", s->samplelen / 32);
		int full = (sampler_mode == SM_RECORDING) ? s->samplelen / (MAX_SAMPLE_LEN / 128) : 0;
		// draw recorded audio waveform
		for (int i = 0; i < full; ++i) {
			u16 avg_peak = getwaveform4zoom(s, i * 16, 4);
			u8 avg = avg_peak;
			u8 peak = avg_peak >> 8;
			vline(i, 15 - avg, 16 + avg, 1);
			vline(i, 15 - peak, 15 - avg, 2);
			vline(i, 16 + avg, 16 + peak, 2);
		}
		vline(full, 0, 32, 1);
		// draw live audio waveform
		u32 srcpos = buf_write_pos;
		for (u8 i = 127; i > full; --i) {
			int pmx = 0;
			int pmn = 0;
			for (u16 j = 0; j < 256; ++j) {
				int p = -delay_ram_buf[--srcpos & DL_SIZE_MASK];
				pmx = maxi(p, pmx);
				pmn = mini(p, pmn);
			}
			vline(i, 15 + pmn / 1024, 16 + pmx / 1024, 2);
		}
		// draw vu meter
		fill_rectangle(hold - 1, 29, hold + 1, 32);
		half_rectangle(peak, 29, hold, 32);
		fill_rectangle(0, 29, peak, 32);
		break;
	}
}

void update_peak_hist(void) {
	peak_hist_pos = (peak_hist_pos + 1) & 31;
	peak_hist[peak_hist_pos] = clampi(audio_in_peak / 64, 0, 255);
}

void sampler_leds(u8 pulse_half, u8 pulse) {
	SampleInfo* s = &cur_sample_info;
	if (sampler_mode == SM_PREVIEW) {
		// map sample peaks to led brightness
		for (u8 x = 0; x < 8; ++x) {
			int sp0 = s->splitpoints[x];
			int sp1 = (x < 7) ? s->splitpoints[x + 1] : s->samplelen;
			for (u8 y = 0; y < 8; ++y) {
				int samp = sp0 + (((sp1 - sp0) * y) >> 3);
				u8 avg_peak = getwaveform4zoom(s, samp / 1024, 3) & 15;
				leds[x][y] = led_add_gamma(avg_peak * (ui_mode == UI_DEFAULT ? 6 : 32));
			}
			// pulse top pad of selected slice
			if (x == cur_slice_id)
				leds[x][0] = pulse_half;
		}
	}
	else {
		// draw live audio to leds
		for (u8 x = 0; x < 8; ++x) {
			u8 barpos0 = peak_hist[(peak_hist_pos + x * 4 + 1) & 31];
			u8 barpos1 = peak_hist[(peak_hist_pos + x * 4 + 2) & 31];
			u8 barpos2 = peak_hist[(peak_hist_pos + x * 4 + 3) & 31];
			u8 barpos3 = peak_hist[(peak_hist_pos + x * 4 + 4) & 31];
			for (u8 y = 0; y < 8; ++y) {
				u8 yy = (7 - y) * 32;
				u8 k = clampi(barpos0 - yy, 0, 31);
				k += clampi(barpos1 - yy, 0, 31);
				k += clampi(barpos2 - yy, 0, 31);
				k += clampi(barpos3 - yy, 0, 31);
				leds[x][y] = led_add_gamma(k * 2);

				// full column of currently recording slice pulses
				if (sampler_mode == SM_RECORDING && x == cur_slice_id)
					leds[x][y] = maxi(leds[x][y], pulse * 4 / (y + 4));
			}
		}
	}
}

u8 ext_audio_led(u8 x, u8 y) {
	u8 delay = 1 + (((7 - y) * (7 - y) + x * x) >> 2);
	u8 hist_pos = (peak_hist_pos + 31 - delay) & 31;
	u8 a_in_lvl = peak_hist[hist_pos];
	u8 a_in_lvl_prev = peak_hist[(hist_pos - 1) & 31];
	a_in_lvl = maxi(0, a_in_lvl - a_in_lvl_prev);                // highpass
	a_in_lvl = clampi((a_in_lvl * (32 - delay)) >> (6), 0, 255); // fade out
	return a_in_lvl;
}
