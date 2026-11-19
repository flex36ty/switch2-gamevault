/**
 * @file heap.h
 * @brief The program's heap on Switch 2: how much memory malloc manages and how much is free.
 * @author kurumuz
 * @copyright 2026 libounce contributors
 * @license ISC
 * @note A program linked with -lounce (or -lnx) uses mimalloc
 *       (libounce_malloc.a), which manages the heap that the program
 *       receives (the loader's heap override, or svcSetHeapSize); newlib's
 *       heap variables (fake_heap_start, fake_heap_end, sbrk) and mallinfo do
 *       not describe it. A program linked with -lounce_core uses newlib's
 *       malloc, and these functions report newlib's heap.
 */
#pragma once
#if !defined(__SWITCH2__)
#error "heap.h is available only in a native Switch 2 build"
#endif

#include "../../switch/types.h"

/// Bytes of the program's heap that malloc manages.
u64 heapGetTotalSize(void);

/**
 * @brief Bytes of that heap in no allocation.
 * @note With mimalloc, counts whole free 64 KiB units: heap pages that the
 *       allocator has not taken, and free slices of the arenas it has. Free
 *       space inside partly used allocator pages is not included, so the
 *       figure is a lower bound. With newlib, counts the heap above the
 *       break (sbrk) and newlib's free chunks.
 */
u64 heapGetFreeSize(void);
