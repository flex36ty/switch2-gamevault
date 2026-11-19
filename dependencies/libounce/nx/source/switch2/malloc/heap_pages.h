/* SPDX-License-Identifier: ISC
 * Original libounce-nx Switch 2 heap pages: the program's fixed heap in
 * 64 KiB pages, which the allocator takes as its "operating system" memory.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "switch/types.h"

/// Size of one heap page.
#define HEAP_PAGE_SIZE 0x10000

/**
 * @brief Divides [start, end) into heap pages; all are free afterwards.
 * @note The page bitmap is kept in the first pages of the range.
 */
void _heapPagesInitialize(uintptr_t start, uintptr_t end);

/**
 * @brief Takes the lowest free run of pages that holds @p size bytes and
 *        starts at a multiple of @p alignment.
 * @param size Bytes; rounded up to whole pages.
 * @param alignment A power of two; values below HEAP_PAGE_SIZE mean one page.
 * @return The start of the run, or NULL if no run fits.
 */
void* _heapPagesAllocate(size_t size, size_t alignment);

/// Gives back the pages of a run from _heapPagesAllocate (same address and size).
void _heapPagesFree(void* address, size_t size);

/// Bytes in heap pages, including the bitmap's.
u64 _heapPagesGetTotalSize(void);

/// Bytes in free heap pages.
u64 _heapPagesGetFreeSize(void);
