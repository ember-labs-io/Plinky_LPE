#include "sampler.h"
#include "audio.h"
#include "audio_tools.h"
#include "data/tables.h"
#include "gfx/data/icons.h"
#include "gfx/gfx.h"
#include "hardware/adc_dac.h"
#include "hardware/flash.h"
#include "hardware/leds.h"
#include "hardware/ram.h"
#include "hardware/spi.h"
#include "params.h"
#include "strings.h"
#include "time.h"
#include "ui/oled_viz.h"
#include "ui/pad_actions.h"

SamplerMode sampler_mode = SM_PREVIEW;

int grain_pos[32];
s16 grain_buf[GRAINBUF_BUDGET];
s16 grain_buf_end[32]; // for each of the 32 grain fetches, where does it end in the grain_buf?

static u8 cur_slice_id = 0; // active slice id
static u32 record_flashaddr_base = 0;

// used while recording a new sample
static u32 buf_start_pos = 0;
static u32 buf_write_pos = 0;
static u32 buf_read_pos = 0;

// for leds drawing
static u8 peak_hist[32];
static u8 peak_hist_pos = 0;

// static float smooth_lpg(ValueSmoother* s, s32 out, float drive, float noise, float env1_lvl) {
// 	s16 n = ((s16*)rndtab)[rand() & 16383];
// 	float cutoff = 1.f - squaref(maxf(0.f, 1.f - env1_lvl * 1.1f));
// 	s->y1 += (out * drive + n * noise - s->y1) * cutoff;
// 	return s->y1;
// }

int using_sampler(void) {
	return cur_sample_info.samplelen;
}

void open_sampler(u8 with_sample_id) {
	load_sample(with_sample_id);
	save_param(P_SAMPLE, SRC_BASE, with_sample_id);
	ui_mode = UI_SAMPLE_EDIT;
	cur_slice_id = 7;
}

// == PLAY SAMPLER AUDIO == //

// start of current (slice) loop
static int calcloopstart(u8 slice_id) {
	int all = cur_sample_info.loop & 2;
	return (all) ? 0 : cur_sample_info.splitpoints[slice_id];
}

// end of current (slice) loop
static int calcloopend(u8 slice_id) {
	int all = cur_sample_info.loop & 2;
	return (all || slice_id >= 7) ? cur_sample_info.samplelen - 192 : cur_sample_info.splitpoints[slice_id + 1];
}

void sampler_recording_tick(u32* dst, u32* audioin) {
	update_sample_ram(false);
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

void apply_sample_lpg_noise(u8 voice_id, Voice* voice, float goal_lpg, float noise_diff, float drive, u32* dst) {
	// sampler parameters
	float timestretch = 1.f;
	float posjit = 0.f;
	float sizejit = 1.f;
	float gsize = 0.125f;
	float grate = 1.f;
	float gratejit = 0.f;
	int smppos = 0;
	if (ui_mode != UI_SAMPLE_EDIT) {
		timestretch = param_val_poly(P_SMP_STRETCH, voice_id) * (2.f / 65536.f);
		gsize = param_val_poly(P_SMP_GRAINSIZE, voice_id) * (1.414f / 65536.f);
		grate = param_val_poly(P_SMP_SPEED, voice_id) * (2.f / 65536.f);
		smppos = (param_val_poly(P_SMP_SCRUB, voice_id) * cur_sample_info.samplelen) >> 16;
		posjit = param_val_poly(P_SMP_SCRUB_JIT, voice_id) * (1.f / 65536.f);
		sizejit = param_val_poly(P_SMP_GRAINSIZE_JIT, voice_id) * (1.f / 65536.f);
		gratejit = param_val_poly(P_SMP_SPEED_JIT, voice_id) * (1.f / 65536.f);
	}
	int trig = env_trig_mask & (1 << voice_id);

	int prevsliceidx = voice->slice_id;
	bool gp = ui_mode == UI_SAMPLE_EDIT;
	u16 touch_pos = get_string_touch(voice_id)->pos;

	// decide on the sample for the NEXT frame
	if (trig) { // on trigger frames, we FADE out the old grains! then the next dma fetch will be the new sample and
		// we can fade in again
		goal_lpg = 0.f;
		//		DebugLog("\r\n%d", voice_id);
		int ypos = 0;
		if (cur_sample_info.pitched && !gp) {
			/// / / / ////////////////////// multisample choice
			int best = voice_id;
			int bestdist = 0x7fffffff;
			int mypitch = (voice->osc[1].pitch + voice->osc[2].pitch) / 2;
			int mysemi = (mypitch) >> 9;
			static u8 multisampletime[8];
			static u8 trig_count = 0;
			trig_count++;
			for (int i = 0; i < 8; ++i) {
				int dist = abs(mysemi - cur_sample_info.notes[i]) * 256 - (u8)(trig_count - multisampletime[i]);
				if (dist < bestdist) {
					bestdist = dist;
					best = i;
				}
			}
			multisampletime[best] = trig_count; // for round robin
			voice->slice_id = best;
			if (grate < 0.f)
				ypos = 8;
		}
		else {
			voice->slice_id = voice_id;
			ypos = (touch_pos / 256);
			if (gp)
				ypos = 0;
			if (grate < 0.f)
				ypos++;
		}
		voice->touch_pos_start = gp ? 128 : touch_pos;
		// calculate playhead position
		int pos16 = clampi(((voice->slice_id * 8) + ypos) << 10, 0, 65535);
		int i = pos16 >> 13;
		int p0 = cur_sample_info.splitpoints[i];
		int p1 = cur_sample_info.splitpoints[i + 1];
		voice->playhead8 = (p0 << 8) + (((p1 - p0) * (pos16 & 0x1fff)) >> 5);
		if (grate < 0.f) {
			voice->playhead8 -= 192 << 8;
			if (voice->playhead8 < 0)
				voice->playhead8 = 0;
		}
		set_smoother(&voice->touch_pos, 0);
	}
	else { // not trigger - just advance playhead
		float ms2 = (voice->grain_pair[0].multisample_grate
		             + voice->grain_pair[1].multisample_grate); // double multisample rate
		int delta_playhead8 = (int)(grate * ms2 * timestretch * (SAMPLES_PER_TICK * 0.5f * 256.f) + 0.5f);

		int new_playhead = voice->playhead8 + delta_playhead8;

		// if the sample loops and the new playhead has crossed the loop boundary, recalculate new playhead position
		if (cur_sample_info.loop & 1) {
			int loopstart = calcloopstart(voice->slice_id) << 8;
			int loopend = calcloopend(voice->slice_id) << 8;
			int looplen = loopend - loopstart;
			if (looplen > 0 && (new_playhead < loopstart || new_playhead >= loopstart + looplen)) {
				new_playhead = (new_playhead - loopstart) % looplen;
				if (new_playhead < 0)
					new_playhead += looplen;
				new_playhead += loopstart;
			}
		}

		voice->playhead8 = new_playhead;

		float gdeadzone = clampf(minf(1.f - posjit, timestretch * 2.f), 0.f,
		                         1.f); // if playing back normally and not jittering, add a deadzone
		float fpos = deadzone(touch_pos - voice->touch_pos_start, gdeadzone * 32.f);
		if (gp)
			fpos = 0.f;
		smooth_value(&voice->touch_pos, fpos, 2048.f);
	}

	float noise;
	for (int osc_id = 0; osc_id < OSCS_PER_VOICE / 2; osc_id++) {
		s16* osc_dst = ((s16*)dst) + (osc_id & 1);
		noise = voice->noise_lvl;
		float y1 = voice->lpg_smoother[osc_id].y1;
		float y2 = voice->lpg_smoother[osc_id].y2;
		int randtabpos = rand() & 16383;
		// mix grains
		GrainPair* g = &voice->grain_pair[osc_id];
		int grainidx = voice_id * 4 + osc_id * 2;
		int g0start = 0;
		if (grainidx)
			g0start = grain_buf_end[grainidx - 1];
		int g1start = grain_buf_end[grainidx];
		int g2start = grain_buf_end[grainidx + 1];

		int64_t posa = g->pos[0];
		int64_t posb = g->pos[1];
		int loopstart = calcloopstart(prevsliceidx);
		int loopend = calcloopend(prevsliceidx);
		bool outofrange0 = posa < loopstart || posa >= loopend;
		bool outofrange1 = posb < loopstart || posb >= loopend;
		int gvol24 = g->vol24;
		int dgvol24 = g->dvol24;
		int dpos24 = g->dpos24;
		int fpos24 = g->fpos24;
		float vol = voice->env1_lvl;
		float dvol = (goal_lpg - vol) * (1.f / SAMPLES_PER_TICK);
		outofrange0 |= g1start - g0start <= 2;
		outofrange1 |= g2start - g1start <= 2;
		g->outflags = (outofrange0 ? 1 : 0) + (outofrange1 ? 2 : 0);
		if ((g1start - g0start <= 2 && g2start - g1start <= 2)) {
			// fast mode :) emulate side effects without doing any work
			vol += dvol * SAMPLES_PER_TICK;
			noise += noise_diff * SAMPLES_PER_TICK;
			gvol24 -= dgvol24 * SAMPLES_PER_TICK;
			fpos24 += dpos24 * SAMPLES_PER_TICK;
			int id = fpos24 >> 24;
			g->pos[0] += id;
			g->pos[1] += id;
			fpos24 &= 0xffffff;
		}
		else {
			const s16* src0 = (outofrange0 ? (const s16*)zero : &grain_buf[g0start + 2]) + g->bufadjust;
			const s16* src0_backup = src0;
			const s16* src1 = (outofrange1 ? (const s16*)zero : &grain_buf[g1start + 2]) + g->bufadjust;

			spi_ready_for_sampler(grainidx);

			for (int i = 0; i < SAMPLES_PER_TICK; ++i) {
				int o0, o1;
				u32 ab0 = *(u32*)(src0); // fetch a pair of 16 bit samples to interpolate between
				u32 mix = (fpos24 << (16 - 9)) & 0x7fff0000;
				mix |= 32767 - (mix >> 16); // mix is now the weights for the linear interpolation
				SMUAD(o0, ab0, mix);        // do the interpolation, result is *32768
				o0 >>= 16;

				u32 ab1 = *(u32*)(src1); // fetch a pair for the other grain in the pair
				SMUAD(o1, ab1, mix);     // linear interp by same weights
				o1 >>= 16;

				fpos24 += dpos24; // advance fractional sample pos
				int bigdpos = (fpos24 >> 24);
				fpos24 &= 0xffffff;
				src0 += bigdpos; // advance source pointers by any whole sample increment
				src1 += bigdpos;

				mix = (gvol24 >> 9) & 0x7fff; // blend between the two grain results
				mix |= (32767 - mix) << 16;
				u32 o01 = STEREOPACK(o0, o1);
				int ofinal;
				SMUAD(ofinal, o01, mix);
				gvol24 -= dgvol24;
				if (gvol24 < 0)
					gvol24 = 0;

				s16 n = ((s16*)rndtab)[randtabpos++]; // mix in a white noise source
				noise += noise_diff;                  // volume ramp for noise

				vol += dvol;                                               // volume ramp for grain signal
				float input = (ofinal * drive + n * noise);                // input to filter
				float cutoff = 1.f - squaref(maxf(0.f, 1.f - vol * 1.1f)); // filter cutoff for low pass gate
				y1 += (input - y1) * cutoff;                               // do the lowpass

				int yy = FLOAT2FIXED(y1 * vol, 0);    // for granular, we include an element of straight VCA
				*osc_dst = SATURATE16(*osc_dst + yy); // write to output
				osc_dst += 2;
			}
			int bigposdelta = src0 - src0_backup;
			g->pos[0] += bigposdelta;
			g->pos[1] += bigposdelta;
		} // grain mix
		g->fpos24 = fpos24;
		g->vol24 = gvol24;

		if (gvol24 <= dgvol24 || trig) { // new grain trigger! this is for the *next* frame
			int ph = voice->playhead8 >> 8;
			int slicelen =
			    cur_sample_info.splitpoints[voice->slice_id + 1] - cur_sample_info.splitpoints[voice->slice_id];
			if (ui_mode != UI_SAMPLE_EDIT) {
				ph += ((int)(voice->touch_pos.y2 * slicelen)) >> (10);
				ph += smppos; // scrub input
			}
			g->vol24 = ((1 << 24) - 1);
			int grainsize = ((rand() & 127) * sizejit + 128.f) * (gsize * gsize) + 0.5f;
			grainsize *= SAMPLES_PER_TICK;
			int jitpos = (rand() & 255) * posjit;
			ph += ((grainsize + 8192) * jitpos) >> 8;
			g->dvol24 = g->vol24 / grainsize;

			float grate2 = 1.f + ((rand() & 255) * (gratejit * gratejit)) * (1.f / 256.f);
			if (timestretch < 0.f)
				grate2 = -grate2;
			g->grate_ratio = grate2;
			g->pos[0] = trig ? ph : g->pos[1];
			g->pos[1] = ph;
		}
		voice->lpg_smoother[osc_id].y1 = y1;
		voice->lpg_smoother[osc_id].y2 = y2;
	} // osc loop

	voice->env1_lvl = goal_lpg;
	voice->noise_lvl = noise;

	// update pitch (aka dpos24) for next time!
	for (int gi = 0; gi < 2; ++gi) {
		float multisample_grate;
		if (cur_sample_info.pitched && (ui_mode != UI_SAMPLE_EDIT)) {
			int relpitch = voice->osc[1 + gi].pitch - cur_sample_info.notes[voice->slice_id] * 512;
			if (relpitch < -512 * 12 * 5) {
				multisample_grate = 0.f;
			}
			else {
				multisample_grate = // exp2f(relpitch / (512.f * 12.f));
				    table_interp(pitches, relpitch + 32768);
			}
		}
		else {
			multisample_grate = 1.f;
		}
		voice->grain_pair[gi].multisample_grate = multisample_grate;
		int dpos24 = (1 << 24) * (grate * voice->grain_pair[gi].grate_ratio * multisample_grate);
		while (dpos24 > (2 << 24))
			dpos24 >>= 1;
		voice->grain_pair[gi].dpos24 = dpos24;
	}
}

void sampler_playing_tick(void) {
	// decide on a priority for 8 voices
	int gprio[8];
	u32 sampleaddr = cur_sample_id * MAX_SAMPLE_LEN;

	for (int i = 0; i < 8; ++i) {
		GrainPair* g = voices[i].grain_pair;
		int glen0 =
		    ((abs(g[0].dpos24) * (SAMPLES_PER_TICK / 2) + g[0].fpos24 / 2 + 1) >> 23) + 2; // +2 for interpolation
		int glen1 =
		    ((abs(g[1].dpos24) * (SAMPLES_PER_TICK / 2) + g[1].fpos24 / 2 + 1) >> 23) + 2; // +2 for interpolation

		// TODO - if pos at end of next fetch will be out of bounds, negate dpos24 and grate_ratio so we ping pong
		// back for the rest of the grain!
		int glen = maxi(glen0, glen1);
		glen = clampi(glen, 0, AVG_GRAINBUF_SAMPLE_SIZE * 2);
		g[0].bufadjust = (g[0].dpos24 < 0) ? maxi(glen - 2, 0) : 0;
		g[1].bufadjust = (g[1].dpos24 < 0) ? maxi(glen - 2, 0) : 0;
		grain_pos[i * 4 + 0] = (int)(g[0].pos[0]) - g[0].bufadjust + sampleaddr;
		grain_pos[i * 4 + 1] = (int)(g[0].pos[1]) - g[0].bufadjust + sampleaddr;
		grain_pos[i * 4 + 2] = (int)(g[1].pos[0]) - g[1].bufadjust + sampleaddr;
		grain_pos[i * 4 + 3] = (int)(g[1].pos[1]) - g[1].bufadjust + sampleaddr;
		glen += 2; // 2 extra 'samples' for the SPI header
		gprio[i] = ((int)(voices[i].env1_lvl * 65535.f) << 12) + i + (glen << 3);
	}
	sort8(gprio, gprio);
	u8 lengths[8];
	int pos = 0, i;
	for (i = 7; i >= 0; --i) {
		int prio = gprio[i];
		int fi = prio & 7;
		int len = (prio >> 3) & 255;
		// we only budget for MAX_SPI_STATE transfers. so after that, len goes to 0. also helps CPU load
		if (i < 8 - MAX_SAMPLE_VOICES)
			len = 0;
		else if (voices[fi].env1_lvl <= 0.01f && !(string_touched & (1 << fi)))
			len = 0; // if your finger is up and the volume is 0, we can just skip this one.
		lengths[fi] = (pos + len * 4 > GRAINBUF_BUDGET) ? 0 : len;
		pos += len * 4;
	}
	// cumulative sum
	pos = 0;
	for (int i = 0; i < 32; ++i) {
		pos += lengths[i / 4];
		grain_buf_end[i] = pos;
	}
}

// == RECORDING SAMPLES == //

static void draw_sample_erasing(u8 erase_pos) {
	oled_clear();
	draw_str(0, 0, F_32, "erasing...");
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
	record_flashaddr_base = 2 * MAX_SAMPLE_LEN * cur_sample_id;
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
	log_ram_edit(SEG_SAMPLE);
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
		log_ram_edit(SEG_SAMPLE);
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
		log_ram_edit(SEG_SAMPLE);
		// fill in the remaining split points
		int startsamp = cur_sample_info.splitpoints[cur_slice_id];
		int endsamp = cur_sample_info.samplelen;
		u8 n = 8 - cur_slice_id; // remaining slices
		for (u8 i = cur_slice_id + 1; i < 8; ++i) {
			int samp = startsamp + ((endsamp - startsamp) * (i - cur_slice_id)) / n;
			cur_sample_info.splitpoints[i] = samp;
		}
		cur_slice_id = 0;
		log_ram_edit(SEG_SAMPLE);
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
	log_ram_edit(SEG_SAMPLE); // fill in the remaining split points
	int startsamp = cur_sample_info.splitpoints[cur_slice_id];
	int endsamp = cur_sample_info.samplelen;
	int n = 8 - cur_slice_id;
	for (int i = cur_slice_id + 1; i < 8; ++i) {
		int samp = startsamp + ((endsamp - startsamp) * (i - cur_slice_id)) / n;
		cur_sample_info.splitpoints[i] = samp;
	}
	cur_slice_id = 0;
	log_ram_edit(SEG_SAMPLE);
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
		log_ram_edit(SEG_SAMPLE);
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
		log_ram_edit(SEG_SAMPLE);
	}
}

// == MODES == //

void sampler_toggle_play_mode(void) {
	cur_sample_info.pitched = !cur_sample_info.pitched;
	log_ram_edit(SEG_SAMPLE);
}

void sampler_iterate_loop_mode(void) {
	cur_sample_info.loop = (cur_sample_info.loop + 1) & 3;
	log_ram_edit(SEG_SAMPLE);
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
				draw_str(0, 32 - 12, F_12, note_name(s->notes[cur_slice_id & 7]));
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

void draw_sample_playback(SampleInfo* s) {
	static int curofscenter = 0;
	static bool jumpable = false;
	int ofs = curofscenter / 1024;
	gfx_text_color = 3;
	for (int i = 32; i < 128 - 16; ++i) {
		int x = i - 32 + ofs;
		u8 h = get_waveform4(s, x);
		vline(i, 15 - h, 16 + h, 2);
	}
	for (int si = 0; si < 8; ++si) {
		int x = (s->splitpoints[si] / 1024) - ofs + 32;
		u8 h = get_waveform4(s, s->splitpoints[si] / 1024);
		if (x >= 32 && x < 128 - 16) {
			vline(x, 0, 13 - h, 1);
			vline(x, 18 + h, 32, 1);
		}
	}
	s8 gx[8 * 4], gy[8 * 4], gd[8 * 4];
	int numtodraw = 0;
	int min_pos = MAX_SAMPLE_LEN;
	int max_pos = 0;

	min_pos = clampi(min_pos, 0, s->samplelen);
	max_pos = clampi(max_pos, 0, s->samplelen);

	for (int i = 0; i < 8; ++i) {
		GrainPair* gr = voices[i].grain_pair;
		int vvol = (int)(256.f * voices[i].env1_lvl);
		if (vvol > 8)
			for (int g = 0; g < 4; ++g) {
				if (!(gr->outflags & (1 << (g & 1)))) {
					int pos = grain_pos[i * 4 + g] & (MAX_SAMPLE_LEN - 1);
					int vol = gr->vol24 >> (24 - 4);
					if (g & 1)
						vol = 15 - vol;
					int graindir = (gr->dpos24 < 0) ? -1 : 1;
					int disppos = pos + graindir * 1024 * 32;
					min_pos = mini(min_pos, pos);
					max_pos = maxi(max_pos, pos);
					min_pos = mini(min_pos, disppos);
					max_pos = maxi(max_pos, disppos);
					int x = (pos / 1024) - ofs + 32;
					int y = (vol * vvol) >> 8;
					if (g & 2)
						y = 16 + y;
					else
						y = 16 - y;
					if (x >= 32 && x < 128 - 16 && y >= 0 && y < 32) {
						gx[numtodraw] = x;
						gy[numtodraw] = y;
						gd[numtodraw] = graindir;
						numtodraw++;
					}
				}
				if (g & 1)
					gr++;
			}
	}
	for (int i = 0; i < numtodraw; ++i) {
		int x = gx[i], y = gy[i];
		vline(x, y - 2, y + 2, 0);
		vline(x + gd[i], y - 1, y + 2, 0);
	}
	for (int i = 0; i < numtodraw; ++i) {
		int x = gx[i], y = gy[i];
		vline(x, y - 1, y + 1, 1);
		put_pixel(x + gd[i], y, 1);
	}
	if (min_pos >= max_pos)
		jumpable = true;
	else {
		if (jumpable)
			curofscenter = min_pos - 8 * 1024;
		else {
#define GRAIN_SCROLL_SHIFT 3
			if (min_pos < curofscenter)
				curofscenter += (min_pos - curofscenter) >> GRAIN_SCROLL_SHIFT;
			if (max_pos > curofscenter + (128 - 48) * 1024)
				curofscenter += (max_pos - curofscenter - (128 - 48) * 1024) >> GRAIN_SCROLL_SHIFT;
		}
		jumpable = false;
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
