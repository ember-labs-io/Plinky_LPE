#pragma once
#include "utils.h"

typedef enum ArpOrder {
	ARP_UP,
	ARP_DOWN,
	ARP_UPDOWN,
	ARP_UPDOWN_REP,
	ARP_PEDAL_UP,
	ARP_PEDAL_DOWN,
	ARP_PEDAL_UPDOWN,
	ARP_RANDOM,
	ARP_RANDOM2,
	ARP_CHORD,
	ARP_UP8,
	ARP_DOWN8,
	ARP_UPDOWN8,
	ARP_RANDOM8,
	ARP_RANDOM28,
	NUM_ARP_ORDERS,
} ArpOrder;

extern ArpOrder arp_order;
extern s8 arp_oct_offset;

bool string_suppressed_by_arp(u8 string_id);

void arp_tick(void);
void arp_reset(void);