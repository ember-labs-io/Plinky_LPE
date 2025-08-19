#include "gfx.h"

u8 gfx_text_color = 1;

void gfx_init(void) {
	oled_init();
	draw_logo();
}

void draw_logo(void) {
	oled_flip_with_buffer(hw_version == HW_PLINKY ? plinky_logo : plinky_plus_logo);
}

// BASICS

void put_pixel(int x, int y, int c) {
	if (x >= OLED_WIDTH)
		return;
	if (y >= OLED_HEIGHT)
		return;
	u8* dst = oled_buffer() + x + ((y >> 3) << 7);
	u8 b = 1 << (y & 7);
	if (c)
		*dst |= b;
	else
		*dst &= ~b;
}

void vline(int x1, int y1, int y2, int c) {
	if (x1 < 0 || x1 >= OLED_WIDTH)
		return;
	y1 = clampi(y1, 0, OLED_HEIGHT);
	y2 = clampi(y2, 0, OLED_HEIGHT);
	if (y1 >= y2)
		return;
	int y1b = y1 >> 3;
	int n = (y2 >> 3) - y1b;
	u8* dst = oled_buffer() + (x1) + (y1b << 7);
	u8 b1 = 0xff << (y1 & 7), b2 = ~(0xff << (y2 & 7));
	if (c) {
		u8 mask = (c == 1) ? 255 : (x1 & 1) ? 0x55 : 0xaa;
		if (n == 0) {
			*dst |= b1 & b2 & mask;
		}
		else {
			*dst |= b1 & mask;
			dst += OLED_WIDTH;
			if (--n)
				for (; --n; dst += OLED_WIDTH)
					*dst |= mask;
			*dst |= b2 & mask;
		}
	}
	else {
		if (n == 0) {
			*dst &= ~(b1 & b2);
		}
		else {
			*dst &= ~b1;
			dst += OLED_WIDTH;
			if (--n)
				for (; --n; dst += OLED_WIDTH)
					*dst = 0;
			*dst &= ~b2;
		}
	}
}

void hline(int x1, int y1, int x2, int c) {
	if (y1 < 0 || y1 >= OLED_HEIGHT)
		return;
	x1 = clampi(x1, 0, OLED_WIDTH);
	x2 = clampi(x2, 0, OLED_WIDTH);
	int n = (x2 - x1);
	if (n <= 0)
		return;
	u8* dst = oled_buffer() + ((y1 >> 3) << 7) + (x1);
	u8 b = (1 << (y1 & 7));
	if (c) {
		for (; n--; dst++)
			*dst |= b;
	}
	else {
		b = ~b;
		for (; n--; dst++)
			*dst &= b;
	}
}

// RECTS

void fill_rectangle(int x1, int y1, int x2, int y2) {
	x1 = clampi(0, x1, 128);
	y1 = clampi(0, y1, 32);
	x2 = clampi(0, x2, 128);
	y2 = clampi(0, y2, 32);
	if (y1 >= y2 || x1 >= x2)
		return;
	u32 mask = (2 << (y2 - y1 - 1)) - 1;
	u8* dst = oled_buffer() + x1 + (y1 / 8) * 128;
	mask <<= y1 & 7;
	int w = x2 - x1;
	while (mask) {
		u8 bmask;
		bmask = mask;
		for (int i = 0; i < w; ++i)
			dst[i] |= bmask;
		mask >>= 8;
		dst += 128;
	}
}

void half_rectangle(int x1, int y1, int x2, int y2) {
	x1 = clampi(0, x1, 128);
	y1 = clampi(0, y1, 32);
	x2 = clampi(0, x2, 128);
	y2 = clampi(0, y2, 32);
	if (y1 >= y2 || x1 >= x2)
		return;
	u32 mask = (2 << (y2 - y1 - 1)) - 1;
	u8* dst = oled_buffer() + x1 + (y1 / 8) * 128;
	mask <<= y1 & 7;
	int w = x2 - x1;
	while (mask) {
		u8 bmask;
		u8 dither = (x1 & 1) ? 0x55 : 0xaa;
		bmask = mask;
		for (int i = 0; i < w; ++i, dither ^= 255)
			dst[i] |= (bmask & dither);
		mask >>= 8;
		dst += 128;
	}
}

void inverted_rectangle(int x1, int y1, int x2, int y2) {
	x1 = clampi(0, x1, 128);
	y1 = clampi(0, y1, 32);
	x2 = clampi(0, x2, 128);
	y2 = clampi(0, y2, 32);
	if (y1 >= y2 || x1 >= x2)
		return;
	u32 mask = (2 << (y2 - y1 - 1)) - 1;
	u8* dst = oled_buffer() + x1 + (y1 / 8) * 128;
	mask <<= y1 & 7;
	int w = x2 - x1;
	while (mask) {
		u8 bmask;
		bmask = mask;
		for (int i = 0; i < w; ++i)
			dst[i] ^= bmask;
		mask >>= 8;
		dst += 128;
	}
}

// ICONS

int draw_icon(int x, int y, unsigned char c, int text_color) {
	if (x <= -16 || x >= 128 || y <= -16 || y >= 32 || c >= NUM_ICONS)
		return 20;
	const u16* data = icons[c];
	u8* dst = oled_buffer() + x;
	for (int xx = 0; xx < 15; ++xx, ++dst, ++x) {
		if (x >= OLED_WIDTH)
			break;
		u32 d = *data++;
		if (!d || x < 0)
			continue;
		if (y < 0)
			d >>= -y;
		else
			d <<= y;
		if (!d)
			continue;
		if (text_color == 0) {
			if (d & 255)
				dst[0] &= ~d;
			if (!(d >>= 8))
				continue;
			if (d & 255)
				dst[OLED_WIDTH] &= ~d;
			if (!(d >>= 8))
				continue;
			if (d & 255)
				dst[OLED_WIDTH * 2] &= ~d;
			if ((d >>= 8) & 255)
				dst[OLED_WIDTH * 3] &= ~d;
		}
		else {
			if (d & 255)
				dst[0] |= d;
			if (!(d >>= 8))
				continue;
			if (d & 255)
				dst[OLED_WIDTH] |= d;
			if (!(d >>= 8))
				continue;
			if (d & 255)
				dst[OLED_WIDTH * 2] |= d;
			if ((d >>= 8) & 255)
				dst[OLED_WIDTH * 3] |= d;
		}
	}
	return 20;
}

// STRINGS

static int char_width(Font f, char c) {
	int xsize = ((int)(f) & 15) * 2 + 4;
	int ysize = xsize * 2;
	if (c & 0x80)
		return 20;
	if (c <= 32 || c > '~')
		return xsize / 2; // space
	int fo = font_offsets[f & 15][(f & BOLD) >= BOLD];
	const u8* data = (((const u8*)font_data) + fo);
	u8 datap = (ysize + 7) / 8;
	u32 mask = (2 << (ysize - 1)) - 1;
	data += datap * xsize * (c - 32);
	while (xsize > 0 && ((*(u32*)data) & mask) == 0) {
		data += datap;
		--xsize;
	} // skip blank at start?
	int lastset = 0;
	for (int xx = 0; xx < xsize; ++xx) {
		u32 d = ((*((const u32*)data)) & mask);
		data += datap;
		if (!d)
			continue;
		lastset = xx;
	}
	return lastset + 2;
}

int str_width(Font f, const char* buf) {
	if (!buf)
		return 0;
	int w = 0;
	int mw = 0;
	for (; *buf;) {
		if (*buf & 0x80)
			w += 20;
		else if (*buf == '\n') {
			mw = maxi(mw, w);
			w = 0;
		}
		else
			w += char_width(f, *buf);
		buf++;
	}
	return maxi(mw, w);
}

static int font_height(Font f) {
	return ((int)(f) & 15) * 4 + 8;
}

int str_height(Font f, const char* buf) {
	int lines = 1;
	for (; *buf; ++buf)
		if (*buf == '\n')
			lines++;
	return lines * font_height(f);
}

static int draw_char(int x, int y, Font f, char c, char text_color) {
	if (text_color == 2) {
		draw_char(x - 1, y - 1, f, c, 0);
		text_color = 1;
	}
	else if (text_color == 3) {
		draw_char(x - 1, y + 1, f, c, 0);
		text_color = 1;
	}
	int xsize = ((int)(f) & 15) * 2 + 4;
	int ysize = xsize * 2;
	if (c & 0x80)
		return draw_icon(x, mini(y, 18), c ^ 0x80, text_color);
	if (c <= 32 || c > '~')
		return xsize / 2; // space
	int fo = font_offsets[f & 15][(f & BOLD) >= BOLD];
	const u8* data = (((const u8*)font_data) + fo);
	u8 datap = (ysize + 7) / 8;
	u32 mask = (2 << (ysize - 1)) - 1;
	data += datap * xsize * (c - 32);
	while (xsize > 0 && ((*(u32*)data) & mask) == 0) {
		data += datap;
		--xsize;
	} // skip blank at start?
	int lastset = 0;
	//	u8 *vram=oled_buffer();
	u8* dst = oled_buffer() + x;
	for (int xx = 0; xx < xsize; ++xx, ++dst, ++x) {
		if (x >= OLED_WIDTH)
			break;
		u32 d = ((*((const u32*)data)) & mask);
		data += datap;
		if (!d)
			continue;
		lastset = xx;
		if (x < 0)
			continue;
		if (y < 0)
			d >>= -y;
		else
			d <<= y;
		if (!d)
			continue;
		if (text_color == 0) {
			if (d & 255)
				dst[0] &= ~d;
			if (!(d >>= 8))
				continue;
			if (d & 255)
				dst[OLED_WIDTH] &= ~d;
			if (!(d >>= 8))
				continue;
			if (d & 255)
				dst[OLED_WIDTH * 2] &= ~d;
			d >>= 8;
			if (d & 255)
				dst[OLED_WIDTH * 3] &= ~d;
		}
		else {
			if (d & 255)
				dst[0] |= d;
			if (!(d >>= 8))
				continue;
			if (d & 255)
				dst[OLED_WIDTH] |= d;
			if (!(d >>= 8))
				continue;
			if (d & 255)
				dst[OLED_WIDTH * 2] |= d;
			d >>= 8;
			if (d & 255)
				dst[OLED_WIDTH * 3] |= d;
		}
	}
	return lastset + 2;
}

int draw_str(int x, int y, Font f, const char* buf) {
	if (x < 0) {
		// right align! hohoho
		int w = str_width(f, buf);
		x = -x - w;
	}
	return drawstr_noright(x, y, f, buf);
}

int vfdraw_str(int x, int y, Font f, const char* fmt, va_list args) {
	if (!fmt)
		return 0;
	char buf[64];
	vsnprintf(buf, 63, fmt, args);
	return draw_str(x, y, f, buf);
}

int fdraw_str(int x, int y, Font f, const char* fmt, ...) {
	if (!fmt)
		return 0;
	va_list args;
	va_start(args, fmt);
	int ret = vfdraw_str(x, y, f, fmt, args);
	va_end(args);
	return ret;
}

int drawstr_noright(int x, int y, Font f, const char* buf) {
	if (!buf)
		return 0;

	int ox = x;
	int ysize = ((int)(f) & 15) * 2 + 4;
	if (x >= 128 || y <= -ysize || y >= 32)
		return 0;
	for (; *buf;) {
		if (*buf == '\n') {
			x = ox;
			y += ysize * 2 - 2;
		}
		else
			x += draw_char(x, y, f, *buf, gfx_text_color);
		buf++;
		if (x > OLED_WIDTH + 1)
			return x;
	}
	return x;
}

// DEBUG

void gfx_debug(u8 row, const char* fmt, ...) {
	static u32 ref_time[2] = {0, 0};
	row %= 2;
	// auto-throttle
	if (do_every(100, &ref_time[row])) {
		va_list args;
		va_start(args, fmt);
		oled_open_debug_buffer(row);
		gfx_text_color = 1;
		vfdraw_str(0, row * 16, F_16_BOLD, fmt, args);
		oled_close_debug_buffer();
		va_end(args);
	}
}