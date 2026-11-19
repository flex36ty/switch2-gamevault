/* SPDX-License-Identifier: ISC
 * Original libounce-nx Switch 2 heap figures for a program that keeps
 * newlib's malloc (one linked with -lounce_core alone). The mimalloc
 * allocator (switch2/malloc/malloc.c) defines these functions for
 * libounce.a, which leaves this file out.
 */
#include <malloc.h>
#include <unistd.h>
#include "switch2/memory/heap.h"

extern char* fake_heap_start;
extern char* fake_heap_end;

// newlib's sbrk grows from fake_heap_start, which stays where __libnx_init
// and argvSetup left it.
u64 heapGetTotalSize(void) {
    return (u64)(fake_heap_end - fake_heap_start);
}

// The heap above the break, and newlib's free chunks below it.
u64 heapGetFreeSize(void) {
    const char* const top = sbrk(0);
    const struct mallinfo info = mallinfo();
    return (u64)(fake_heap_end - top) + (u64)info.fordblks;
}
