#pragma once
#include "data/tables.h"
#include "utils.h"

#define __STATIC_FORCEINLINE __attribute__((always_inline)) static inline

#define RV_SIZE_MASK 16383
#define DL_SIZE_MASK 32767

#define FLOAT2FIXED(x, bits) ((int)((x) * (1 << (bits))))
#define STEREOUNPACK(lr) int lr##l = (s16)lr, lr##r = (s16)(lr >> 16);

__STATIC_FORCEINLINE
u32 STEREOPACK(s16 l, s16 r) {
	s32 out;
	asm("pkhbt %0, %1, %2, lsl #16" : "=r"(out) : "r"(l), "r"(r));
	return out;
}

__STATIC_FORCEINLINE
s16 SATURATE16(s32 a) {
	s32 tmp;
	asm("ssat %0, %1, %2" : "=r"(tmp) : "I"(16), "r"(a));
	return tmp;
}

__STATIC_FORCEINLINE
s16 LINEARINTERPDL(const s16* buf, int basei, int wobpos) { // read buf[basei-wobpos>>12] basically
	basei -= wobpos >> 12;
	wobpos &= 0xfff;
	s16 a0 = buf[basei & DL_SIZE_MASK];
	s16 a1 = buf[(basei - 1) & DL_SIZE_MASK];
	s32 out;
	u32 a = STEREOPACK(a1, a0);
	u32 b = STEREOPACK(wobpos, 0x1000 - wobpos);
	asm("smuad %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
	return out >> 12;
}

__STATIC_FORCEINLINE
u32 MIDSIDESCALE(u32 in, int midscale, int sidescale) {
	STEREOUNPACK(in);
	s32 mid = inl + inr;
	s32 side = inl - inr;
	mid = (mid * midscale) >> 17;
	side = (side * sidescale) >> 17;
	inl = mid + side;
	inr = mid - side;
	return STEREOPACK(inl, inr);
}

__STATIC_FORCEINLINE
u32 STEREOSCALE(u32 in, int scale) {
	STEREOUNPACK(in);
	return STEREOPACK((inl * scale) >> 16, (inr * scale) >> 16);
}

__STATIC_FORCEINLINE
u32 STEREOADDSAT(u32 a, u32 b) {
	s32 out;
	asm("qadd16 %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
	return out;
}

__STATIC_FORCEINLINE
u32 STEREOADDAVERAGE(u32 a, u32 b) {
	s32 out;
	asm("shadd16 %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
	return out;
}

__STATIC_FORCEINLINE
s16 LINEARINTERPRV(const s16* buf, int basei, int wobpos) { // read buf[basei-wobpos>>12] basically
	basei -= wobpos >> 12;
	wobpos &= 0xfff;
	s16 a0 = buf[basei & RV_SIZE_MASK];
	s16 a1 = buf[(basei - 1) & RV_SIZE_MASK];
	s32 out;
	u32 a = STEREOPACK(a1, a0);
	u32 b = STEREOPACK(wobpos, 0x1000 - wobpos);
	asm("smuad %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
	return out >> 12;
}

__STATIC_FORCEINLINE
s16 MONOSIGMOID(int in) {
	in = SATURATE16(in);
	return sigmoid[(u16)in];
}

__STATIC_FORCEINLINE
u32 STEREOSIGMOID(u32 in) {
	u16 l = sigmoid[(u16)in];
	u16 r = sigmoid[in >> 16];
	return STEREOPACK(l, r);
}