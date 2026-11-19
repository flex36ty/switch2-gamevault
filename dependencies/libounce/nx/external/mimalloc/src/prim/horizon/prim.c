/* ----------------------------------------------------------------------------
Copyright (c) 2026, libounce contributors
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

// Primitives for Horizon (Nintendo Switch 2 homebrew built with libounce).
// This file is included in `src/prim/prim.c`.
//
// Horizon gives a program one fixed heap, always committed, and no way to
// map more memory on demand. libounce divides that heap into 64 KiB pages
// (source/switch2/malloc/heap_pages.c); the "OS" memory of mimalloc -- its
// arenas, huge objects and page map -- is runs of those pages, and freeing
// it gives the pages back. Committing or decommitting does nothing. Thread
// exit is reported by libounce's threadExit (mi_thread_done).

#include "mimalloc.h"
#include "mimalloc/internal.h"
#include "mimalloc/prim.h"

#include <errno.h>
#include <string.h>
#include "result.h"
#include "kernel/svc.h"
#include "arm/counter.h"
#include "../../../../../source/switch2/malloc/heap_pages.h"

//---------------------------------------------
// Initialize
//---------------------------------------------

void _mi_prim_mem_init( mi_os_mem_config_t* config ) {
  config->page_size = 0x1000;
  config->large_page_size = 0;
  config->alloc_granularity = HEAP_PAGE_SIZE;
  config->physical_memory_in_kib = 0;
  config->virtual_address_bits = 39;
  config->has_overcommit = false;
  config->has_partial_free = false;
  config->has_virtual_reserve = false;
  config->has_transparent_huge_pages = false;
}

//---------------------------------------------
// Allocation: runs of heap pages
//---------------------------------------------

int _mi_prim_free(void* addr, size_t size ) {
  _heapPagesFree(addr, size);
  return 0;
}

int _mi_prim_alloc(void* hint_addr, size_t size, size_t try_alignment, bool commit, bool allow_large, bool* is_large, bool* is_zero, void** addr) {
  MI_UNUSED(hint_addr); MI_UNUSED(commit); MI_UNUSED(allow_large);
  *is_large = false;
  *is_zero = false;
  *addr = _heapPagesAllocate(size, try_alignment);
  return (*addr != NULL ? 0 : ENOMEM);
}

//---------------------------------------------
// Commit/Reset/Protect: the heap is always committed
//---------------------------------------------

int _mi_prim_commit(void* addr, size_t size, bool* is_zero) {
  MI_UNUSED(addr); MI_UNUSED(size);
  *is_zero = false;
  return 0;
}

int _mi_prim_decommit(void* addr, size_t size, bool* needs_recommit) {
  MI_UNUSED(addr); MI_UNUSED(size);
  *needs_recommit = false;
  return 0;
}

int _mi_prim_reset(void* addr, size_t size) {
  MI_UNUSED(addr); MI_UNUSED(size);
  return 0;
}

int _mi_prim_reuse(void* addr, size_t size) {
  MI_UNUSED(addr); MI_UNUSED(size);
  return 0;
}

int _mi_prim_protect(void* addr, size_t size, bool protect) {
  MI_UNUSED(addr); MI_UNUSED(size); MI_UNUSED(protect);
  return 0;
}

//---------------------------------------------
// Huge pages and NUMA nodes
//---------------------------------------------

int _mi_prim_alloc_huge_os_pages(void* hint_addr, size_t size, int numa_node, bool* is_zero, void** addr) {
  MI_UNUSED(hint_addr); MI_UNUSED(size); MI_UNUSED(numa_node);
  *is_zero = false;
  *addr = NULL;
  return ENOSYS;
}

size_t _mi_prim_numa_node(void) {
  return 0;
}

size_t _mi_prim_numa_node_count(void) {
  return 1;
}

//----------------------------------------------------------------
// Clock
//----------------------------------------------------------------

mi_msecs_t _mi_prim_clock_now(void) {
  return (mi_msecs_t)(armTicksToNs(armGetSystemTick()) / 1000000);
}

//----------------------------------------------------------------
// Process info
//----------------------------------------------------------------

void _mi_prim_process_info(mi_process_info_t* pinfo) {
  MI_UNUSED(pinfo);
}

//----------------------------------------------------------------
// Output: the debug log, not stdio, which can allocate
//----------------------------------------------------------------

void _mi_prim_out_stderr( const char* msg ) {
  svcOutputDebugString(msg, strlen(msg));
}

//----------------------------------------------------------------
// Environment: options are not read from it
//----------------------------------------------------------------

int _mi_prim_getenv(const char* name, char* result, size_t result_size) {
  MI_UNUSED(name); MI_UNUSED(result); MI_UNUSED(result_size);
  return 0;
}

//----------------------------------------------------------------
// Random: the kernel's entropy (svcGetInfo RandomEntropy, 4 x 64 bits),
// mixed with the system tick for buffers longer than 32 bytes
//----------------------------------------------------------------

bool _mi_prim_random_buf(void* buf, size_t buf_len) {
  uint64_t entropy[4];
  for (unsigned i = 0; i < 4; i++) {
    if (R_FAILED(svcGetInfo(&entropy[i], InfoType_RandomEntropy, INVALID_HANDLE, i)))
      return false;
  }
  uint8_t* out = (uint8_t*)buf;
  for (size_t i = 0; i < buf_len; i++) {
    if (i > 0 && i % sizeof(entropy) == 0) {
      for (unsigned k = 0; k < 4; k++)
        entropy[k] = _mi_random_shuffle(entropy[k] ^ armGetSystemTick());
    }
    out[i] = ((const uint8_t*)entropy)[i % sizeof(entropy)];
  }
  return true;
}

//----------------------------------------------------------------
// Thread init/done: libounce's threadExit calls mi_thread_done
//----------------------------------------------------------------

void _mi_prim_thread_init_auto_done(void) {
}

void _mi_prim_thread_done_auto_done(void) {
}

void _mi_prim_thread_associate_default_theap(mi_theap_t* theap) {
  MI_UNUSED(theap);
}

bool _mi_prim_thread_is_in_threadpool(void) {
  return false;
}

void _mi_prim_thread_yield(void) {
  svcSleepThread(0);
}
