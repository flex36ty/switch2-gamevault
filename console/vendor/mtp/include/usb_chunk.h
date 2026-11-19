#pragma once
#include <stddef.h>
// An unaligned caller uses an aligned 4 KiB bounce buffer. Its page tail must
// not limit the USB receive: doing so discards short-packet boundary evidence.
static inline size_t usbBounceChunk(size_t remaining){return remaining<0x1000?remaining:0x1000;}
