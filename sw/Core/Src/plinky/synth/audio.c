#include "audio.h"
#include "audio_tools.h"
#include "hardware/adc_dac.h"
#include "params.h"
#include "sampler.h"
#include "time.h"
#include "ui/oled_viz.h"

// rj: this module largely has not been cleaned up yet

short* reverb_ram_buf = (short*)0x10000000; // use ram2 :)
short* delay_ram_buf = (short*)0x20008000;  // use end of ram1 :)
s16 audio_in_peak = 0;
s16 audio_in_hold = 0;
ValueSmoother ext_gain_smoother;

static s16 audioin_is_stereo = 0;
static s16 noise_gate = 0;
static u16 audio_in_hold_time = 0;
static s16 scopex = 0;
static int k_reverb_fade = 240;
static int k_reverb_shim = 240;
static float k_reverb_wob = 0.5f;
static int k_reverbsend = 0;
static int reverbpos = 0;
static int shimmerpos1 = 2000;
static int shimmerpos2 = 1000;
static int shimmerfade = 0;
static int dshimmerfade = 32768 / 4096;

// lfo

typedef struct lfo {
	float r, i, a;
} lfo;

#define LFOINIT(f) {1.f, 0.f, (f) + (f)}

static lfo aplfo = LFOINIT(1.f / 32777.f * 9.4f);
static lfo aplfo2 = LFOINIT(1.3f / 32777.f * 3.15971f);

__STATIC_FORCEINLINE
float lfo_next(lfo* l) {
	l->r -= l->a * l->i;
	l->i += l->a * l->r;
	return l->r;
}

void reverb_clear(void) {
	memset(reverb_ram_buf, 0, (RV_SIZE_MASK + 1) * 2);
}
void delay_clear(void) {
	memset(delay_ram_buf, 0, (DL_SIZE_MASK + 1) * 2);
}

void init_ext_gain_for_recording(void) {
	set_smoother(&ext_gain_smoother, 65535 - adc_get_raw(ADC_A_KNOB));
}

void audio_init(void) {
	reverb_clear(); // ram2 is not cleared by startup.s as written.
	delay_clear();
}

s32 Reverb2(s32 input, s16* buf) {
	int i = reverbpos;
	int outl = 0, outr = 0;
	float wob = lfo_next(&aplfo) * k_reverb_wob;
	int apwobpos = FLOAT2FIXED((wob + 1.f), 12 + 6);
	wob = lfo_next(&aplfo2) * k_reverb_wob;
	int delaywobpos = FLOAT2FIXED((wob + 1.f), 12 + 6);
#define RVDIV / 2
#define CHECKACC // assert(acc>=-32768 && acc<32767);
#define AP(len)                                                                                                        \
	{                                                                                                                  \
		int j = (i + len RVDIV) & RV_SIZE_MASK;                                                                        \
		s16 d = buf[j];                                                                                                \
		acc -= d >> 1;                                                                                                 \
		buf[i] = SATURATE16(acc);                                                                                      \
		acc = (acc >> 1) + d;                                                                                          \
		i = j;                                                                                                         \
		CHECKACC                                                                                                       \
	}
#define AP_WOBBLE(len, wobpos)                                                                                         \
	{                                                                                                                  \
		int j = (i + len RVDIV) & RV_SIZE_MASK;                                                                        \
		s16 d = LINEARINTERPRV(buf, j, wobpos);                                                                        \
		acc -= d >> 1;                                                                                                 \
		buf[i] = SATURATE16(acc);                                                                                      \
		acc = (acc >> 1) + d;                                                                                          \
		i = j;                                                                                                         \
		CHECKACC                                                                                                       \
	}
#define DELAY(len)                                                                                                     \
	{                                                                                                                  \
		int j = (i + len RVDIV) & RV_SIZE_MASK;                                                                        \
		buf[i] = SATURATE16(acc);                                                                                      \
		acc = buf[j];                                                                                                  \
		i = j;                                                                                                         \
		CHECKACC                                                                                                       \
	}
#define DELAY_WOBBLE(len, wobpos)                                                                                      \
	{                                                                                                                  \
		int j = (i + len RVDIV) & RV_SIZE_MASK;                                                                        \
		buf[i] = SATURATE16(acc);                                                                                      \
		acc = LINEARINTERPRV(buf, j, wobpos);                                                                          \
		i = j;                                                                                                         \
		CHECKACC                                                                                                       \
	}

	// Griesinger according to datorro does 142, 379, 107, 277 on the way in - totoal 905 (20ms)
	// then the loop does 672+excursion, delay 4453, (damp), 1800, delay 3720 - total 10,645 (241ms)
	// then decay, and feed in
	// and on the other side 908+excursion,	delay 4217, (damp), 2656, delay 3163 - total 10,944 (248 ms)

	// keith barr says:
	// I really like 2AP, delay, 2AP, delay, in a loop.
	// I try to set the delay to somewhere a bit less than the sum of the 2 preceding AP delays,
	// which are of course much longer than the initial APs(before the loop)
	// Yeah, the big loop is great; you inject input everywhere, but take it out in only two places
	// It just keeps comin� newand fresh as the thing decays away.�If you�ve got the memoryand processing!

	// lets try the 4 greisinger initial Aps, inject stereo after the first AP,

	int acc = ((s16)(input)) * k_reverbsend >> 17;
	AP(142);
	AP(379);
	acc += (input >> 16) * k_reverbsend >> 17;
	AP(107);
	AP(277);
	int reinject = acc;
	static int fb1 = 0;
	acc += fb1;
	AP_WOBBLE(672, apwobpos);
	AP(1800);
	DELAY(4453);

	if (1) {
		// shimmer - we can read from up to about 2000 samples ago

		// Brief shimmer walkthrough:
		// - We walk backwards through the reverb buffer with 2 indices: shimmerpos1 and shimmerpos2.
		//   - shimmerpos1 is the *previous* shimmer position.
		//   - shimmerpos2 is the *current* shimmer position.
		//   - Note that we add these to i (based on reverbpos), which is also walking backwards
		//     through the buffer.
		// - shimmerfade controls the crossfade between the shimmer from shimmerpos1 and shimmerpos2.
		//   - When shimmerfade == 0, shimmerpos1 (the old shimmer) is chosen.
		//   - When shimmerfade == SHIMMER_FADE_LEN - 1, shimmerpos2 (the new shimmer) is chosen.
		//   - For everything in-between, we linearly interpolate (crossfade).
		//   - When we hit the end of the fade, we reset shimmerpos2 to a random new position and set
		//     shimmerpos1 to the old shimmerpos2.
		// - dshimmerfade controls the speed at which we fade.

#define SHIMMER_FADE_LEN 32768
		shimmerfade += dshimmerfade;

		if (shimmerfade >= SHIMMER_FADE_LEN) {
			shimmerfade -= SHIMMER_FADE_LEN;

			shimmerpos1 = shimmerpos2;
			shimmerpos2 = (rand() & 4095) + 8192;
			dshimmerfade =
			    (rand() & 7) + 8; // somewhere between SHIMMER_FADE_LEN/2048 and SHIMMER_FADE_LEN/4096 ie 8 and 16
		}

		// L = shimmer from shimmerpos1, R = shimmer from shimmerpos2
		u32 shim1 = STEREOPACK(buf[(i + shimmerpos1) & RV_SIZE_MASK], buf[(i + shimmerpos2) & RV_SIZE_MASK]);
		u32 shim2 = STEREOPACK(buf[(i + shimmerpos1 + 1) & RV_SIZE_MASK], buf[(i + shimmerpos2 + 1) & RV_SIZE_MASK]);
		u32 shim = STEREOADDAVERAGE(shim1, shim2);

		// Fixed point crossfade:
		u32 a = STEREOPACK((SHIMMER_FADE_LEN - 1) - shimmerfade, shimmerfade);
		s32 shimo;
		asm("smuad %0, %1, %2" : "=r"(shimo) : "r"(a), "r"(shim));
		shimo >>= 15; // Divide by SHIMMER_FADE_LEN

		// Apply user-selected shimmer amount.
		shimo *= k_reverb_shim;
		shimo >>= 8;

		// Tone down shimmer amount.
		shimo >>= 1;

		acc += shimo;
		outl = shimo;
		outr = shimo;

		shimmerpos1--;
		shimmerpos2--;
	}

	const static float k_reverb_color = 0.95f;
	static float lpf = 0.f, dc = 0.f;
	lpf += (((acc * k_reverb_fade) >> 8) - lpf) * k_reverb_color;
	dc += (lpf - dc) * 0.005f;
	acc = (int)(lpf - dc);
	outl += acc;

	acc += reinject;
	AP_WOBBLE(908, delaywobpos);
	AP(2656);
	DELAY(3163);
	static float lpf2 = 0.f;
	lpf2 += (((acc * k_reverb_fade) >> 8) - lpf2) * k_reverb_color;
	acc = (int)(lpf2);

	outr += acc;

	reverbpos = (reverbpos - 1) & RV_SIZE_MASK;
	fb1 = (acc * k_reverb_fade) >> 8;
	return STEREOPACK(SATURATE16(outl), SATURATE16(outr));
}

void audio_pre(u32* audio_out, u32* audio_in) {
	memset(audio_out, 0, 4 * SAMPLES_PER_TICK);

	int newpeak = 0;
	int newpeakr = 0;
	static float dcl, dcr;
	int ng = mini(256, noise_gate);
	// dc remover from audio in, and peak detector while we're there.
	for (int i = 0; i < SAMPLES_PER_TICK; ++i) {
		u32 inp = audio_in[i];
		STEREOUNPACK(inp);
		dcl += (inpl - dcl) * 0.0001f;
		dcr += (inpr - dcr) * 0.0001f;
		inpl -= dcl;
		inpr -= dcr;
		newpeakr = maxi(newpeakr, abs(inpr));
		if (!audioin_is_stereo)
			inpr = inpl;
		newpeak = maxi(newpeak, abs(inpl + inpr));
		inpl = (inpl * ng) >> 8;
		inpr = (inpr * ng) >> 8;

		audio_in[i] = STEREOPACK(inpl, inpr);
	}
	if (newpeak > 400)
		noise_gate = 1000;
	else if (noise_gate > 0)
		noise_gate--;

	if (newpeakr > 300)
		audioin_is_stereo = 1000;
	else if (audioin_is_stereo > 0)
		audioin_is_stereo--;

	int audiorec_gain = (int)(ext_gain_smoother.y2) / 2;

	newpeak = SATURATE16((newpeak * audiorec_gain) >> 14);
	audio_in_peak = maxi((audio_in_peak * 220) >> 8, newpeak);
	if (audio_in_peak > audio_in_hold || audio_in_hold_time++ > 500) {
		audio_in_hold = audio_in_peak;
		audio_in_hold_time = 0;
	}

	// ext gain
	static u16 ext_gain_goal = 1 << 15;
	if (sampler_mode > SM_PREVIEW) {
		u16 gain_knob_value = 65535 - adc_get_raw(ADC_A_KNOB);
		if (abs(gain_knob_value - ext_gain_goal) > 256) // hysteresis
			ext_gain_goal = gain_knob_value;
	}
	else
		ext_gain_goal = param_val(P_IN_LVL);
	smooth_value(&ext_gain_smoother, ext_gain_goal, 65536.f);
}

void audio_post(u32* audio_out, u32* audio_in) {

	// delay params

	static u16 delaypos = 0;
	static u32 wetlr;
	const float k_target_fb = param_val_float(P_DLY_FEEDBACK) * (0.35f); // 3/4
	static float k_fb = 0.f;
	int k_target_delaytime = param_val(P_DLY_TIME);
	if (k_target_delaytime > 0) {
		// free timing
		k_target_delaytime = (((k_target_delaytime + 255) >> 8) * k_target_delaytime) >> 8;
		k_target_delaytime = (k_target_delaytime * (DL_SIZE_MASK - 64)) >> 16;
	}
	else {
		k_target_delaytime =
		    sync_divs_32nds[clampi((-k_target_delaytime * 13) >> 16, 0, 12)]; // results in a number 1-32
		// figure out how samples we can have, max, in a beat synced scenario
		int max_delay = 32000 * 600 * 4 / bpm_10x;
		while (max_delay > DL_SIZE_MASK - 64)
			max_delay >>= 1;
		k_target_delaytime = (max_delay * k_target_delaytime) >> 5;
	}
	k_target_delaytime = clampi(k_target_delaytime, SAMPLES_PER_TICK, DL_SIZE_MASK - 64) << 12;
	int k_delaysend = (param_val(P_DLY_SEND) >> 9);

	static int wobpos = 0;
	static int dwobpos = 0;
	static int wobcount = 0;
	if (wobcount <= 0) {
		const int wobamount = param_val(P_DLY_WOBBLE); // 1/2
		int newwobtarget = ((rand() & 8191) * wobamount) >> 8;
		if (newwobtarget > k_target_delaytime / 2)
			newwobtarget = k_target_delaytime / 2;
		wobcount = ((rand() & 8191) + 8192) & (~(SAMPLES_PER_TICK - 1));
		dwobpos = (newwobtarget - wobpos + wobcount / 2) / wobcount;
	}
	wobcount -= SAMPLES_PER_TICK;

	// hpf params

	static float peak = 0.f;
	peak *= 0.99f;
	static float power = 0.f;
	// at sample rate, lpf k 0.002 takes 10ms to go to half; .0006 takes 40ms; k=.0002 takes 100ms;
	// at buffer rate, k=0.13 goes to half in 10ms; 0.013 goes to half in 100ms; 0.005 is 280ms

	float g = param_val_float(P_HPF); // tanf(3.141592f * 8000.f / 32000.f); // highpass constant // TODO PARAM 0 -1
	g *= g;
	g *= g;
	g += (10.f / 32000.f);
	const static float k = 2.f;
	float a1 = 1.f / (1.f + g * (g + k));
	float a2 = g * a1;

	for (u8 i = 0; i < SAMPLES_PER_TICK; ++i) {
		u32 input = STEREOSIGMOID(audio_out[i]);
		STEREOUNPACK(input);
		static float ic1l, ic2l, ic1r, ic2r;
		float l = inputl, r = inputr;
		float v1l = a1 * ic1l + a2 * (l - ic2l);
		float v2l = ic2l + g * v1l;
		ic1l = v1l + v1l - ic1l;
		ic2l = v2l + v2l - ic2l;
		l -= k * v1l + v2l;

		float v1r = a1 * ic1r + a2 * (r - ic2r);
		float v2r = ic2r + g * v1r;
		ic1r = v1r + v1r - ic1r;
		ic2r = v2r + v2r - ic2r;
		r -= k * v1r + v2r;

		power *= 0.999f;
		power += l * l + r * r;
		peak = maxf(peak, l + r);

		s16 li = (s16)SATURATE16(l);
		s16 ri = (s16)SATURATE16(r);
		audio_out[i] = STEREOPACK(li, ri);
	}

	u32* src = (u32*)audio_out;

	// reverb params

	float f = 1.f - clampf(param_val_float(P_RVB_TIME), 0.f, 1.f);
	f *= f;
	f *= f;
	k_reverb_fade = (int)(250 * (1.f - f));
	k_reverb_shim = (param_val(P_SHIMMER) >> 9);
	k_reverb_wob = (param_val_float(P_RVB_WOBBLE));
	// k_reverb_color=(param_val_float(P_RVCOLOR));
	k_reverbsend = (param_val(P_RVB_SEND));

	// mixer params

	int synthlvl_ = param_val(P_SYN_LVL);
	int synthwidth = param_val(P_MIX_WIDTH);
	int asynthwidth = abs(synthwidth);
	int synthlvl_mid;
	int synthlvl_side;
	if (asynthwidth <= 32768) { // make more narrow
		synthlvl_mid = synthlvl_;
		synthlvl_side = (synthwidth * synthlvl_) >> 15;
	}
	else {
		synthlvl_side = (synthwidth < 0) ? -synthlvl_ : synthlvl_;
		asynthwidth = 65536 - asynthwidth;
		synthlvl_mid = (asynthwidth * synthlvl_) >> 15;
	}

	int ainwetdry = param_val(P_IN_WET_DRY);
	int wetdry = param_val(P_SYN_WET_DRY);
	int wetlvl = 65536 - maxi(-wetdry, 0);
	int drylvl = 65536 - maxi(wetdry, 0);

	int a_in_lvl = param_val(P_IN_LVL);
	int ainwetlvl = 65536 - maxi(-ainwetdry, 0);
	int aindrylvl = 65536 - maxi(ainwetdry, 0);

	ainwetlvl = ((ainwetlvl >> 4) * (a_in_lvl >> 4)) >> 8;

	a_in_lvl = ((a_in_lvl >> 4) * (drylvl >> 4)) >> 8;    // prescale by dry level
	a_in_lvl = ((a_in_lvl >> 4) * (aindrylvl >> 4)) >> 8; // prescale by fx dry level

	int delayratio = param_val(P_PING_PONG) >> 8;
	static int delaytime = SAMPLES_PER_TICK << 12;
	int scopescale = (65536 * 24) / maxi(16384, (int)peak);

	// fx processing

	for (u8 i = 0; i < SAMPLES_PER_TICK / 2; ++i) {

		// delay

		int targetdt = k_target_delaytime + 2048 - (int)wobpos;
		wobpos += dwobpos;
		delaytime += (targetdt - delaytime) >> 10;
		s16 delayreturnl = LINEARINTERPDL(delay_ram_buf, delaypos, delaytime);
		s16 delayreturnr = LINEARINTERPDL(delay_ram_buf, delaypos, ((delaytime >> 4) * delayratio) >> 4);
		// soft clipper due to drive; reduces range to half also giving headroom on tape & output
		u32 drylr0 = STEREOSIGMOID(src[0]);
		u32 drylr1 = STEREOSIGMOID(src[1]);

		// compressor

		u32 drylr01 = STEREOADDAVERAGE(drylr0, drylr1); // this is gonna have absolute max +-32768
		STEREOUNPACK(drylr01);
		static float peaktrack = 1.f;
		float peaky = (float)((1.f / 4096.f / 65536.f)
		                      * (maxi(maxi(drylr01l, -drylr01l), maxi(drylr01r, -drylr01r)) * synthlvl_));
		if (peaky > peaktrack)
			peaktrack += (peaky - peaktrack) * 0.01f;
		else {
			peaktrack += (peaky - peaktrack) * 0.0002f;
			peaktrack = maxf(peaktrack, 1.f);
		}
		float recip = (2.5f / peaktrack);
		int lvl_mid = synthlvl_mid * recip;
		int lvl_side = synthlvl_side * recip;

		drylr0 = MIDSIDESCALE(drylr0, lvl_mid, lvl_side);
		drylr1 = MIDSIDESCALE(drylr1, lvl_mid, lvl_side);

		u32 ain0 = audio_in[i * 2 + 0];
		u32 ain1 = audio_in[i * 2 + 1];

		u32 audioinwet = STEREOSCALE(STEREOADDAVERAGE(ain0, ain1), ainwetlvl);
		u32 dry2wetlr = STEREOADDAVERAGE(drylr0, drylr1);
		dry2wetlr = STEREOADDSAT(dry2wetlr, audioinwet);

		// delay

		int delaysend = (int)((delayreturnl + (delayreturnr >> 1)) * k_fb);
		delaysend += (((s16)(dry2wetlr) + (s16)(dry2wetlr >> 16)) * k_delaysend) >> 8;
		static float lpf = 0.f, dc = 0.f;
		lpf += (delaysend - lpf) * 0.75f;
		dc += (lpf - dc) * 0.05f;
		delaysend = (int)(lpf - dc);
		//- compressor in feedback of delay
		delaysend = MONOSIGMOID(delaysend);

		// adjust feedback up again
		k_fb += (k_target_fb - k_fb) * 0.001f;

		delaypos &= DL_SIZE_MASK;
		delay_ram_buf[delaypos] = delaysend;
		delaypos++;
		s16 li = dry2wetlr;
		s16 ri = dry2wetlr >> 16;
		static s16 prevli = 0;
		static s16 prevprevli = 0;
		static u16 bestedge = 0;
		static s16 antiturningpointli = 0;
		bool turningpoint = (prevli > prevprevli && prevli > li);
		bool antiturningpoint = (prevli < prevprevli && prevli < li);
		if (antiturningpoint)
			antiturningpointli = prevli; // remember the last turning point at the bottom
		if (turningpoint) {              // we are at a peak!
			int edgesize = prevli - antiturningpointli;
			if (scopex >= 256 || (scopex < 0 && edgesize > bestedge)) {
				scopex = -256;
				bestedge = edgesize;
			}
		}
		prevprevli = prevli;
		prevli = li;

		// scope generation

		if (scopex < 256 && scopex >= 0) {
			int x = scopex / 2;
			if (!(scopex & 1))
				clear_scope_pixel(x);
			put_scope_pixel(x, (li * scopescale >> 16) + 16);
			put_scope_pixel(x, (ri * scopescale >> 16) + 16);
		}
		scopex++;
		if (scopex > 1024)
			scopex = -256;

		u32 newwetlr = STEREOPACK(delayreturnl, delayreturnr);

		// reverb

		u32 reverbin = STEREOADDAVERAGE(newwetlr, dry2wetlr);
		u32 reverbout = Reverb2(reverbin, reverb_ram_buf);
		newwetlr = STEREOADDSAT(newwetlr, reverbout);

		// output upsample
		newwetlr = STEREOSCALE(newwetlr, wetlvl);
		u32 midwetlr = STEREOADDAVERAGE(newwetlr, wetlr);
		wetlr = newwetlr;

		u32 audioin0 = STEREOSIGMOID(STEREOSCALE(ain0, a_in_lvl)); // a_in_lvl already scaled by drylvl
		u32 audioin1 = STEREOSIGMOID(STEREOSCALE(ain1, a_in_lvl));

		// write to output

		src[0] = STEREOADDSAT(STEREOADDSAT(STEREOSCALE(drylr0, drylvl), audioin0), midwetlr);
		src[1] = STEREOADDSAT(STEREOADDSAT(STEREOSCALE(drylr1, drylvl), audioin1), newwetlr);

		src += 2;
	}
}