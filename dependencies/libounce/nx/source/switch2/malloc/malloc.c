/* SPDX-License-Identifier: ISC
 * Original libounce-nx Switch 2 allocator: mimalloc (external/mimalloc, MIT)
 * in place of newlib's malloc. It is built as its own library,
 * libounce_malloc.a, whose objects libounce.a (-lounce, -lnx) contains with
 * the rest of libounce; a small native process that links libounce_core.a
 * (-lounce_core) alone keeps newlib's malloc.
 *
 * newlib's malloc is one heap behind one lock, so threads that allocate at
 * the same time wait for each other in the kernel. mimalloc gives each
 * thread its own pages and frees blocks of other threads without a lock.
 *
 * Horizon gives the program one fixed heap. After argvSetup takes its part,
 * _mallocInitialize (called from __libnx_init) divides the rest into 64 KiB
 * heap pages (heap_pages.c), mimalloc's "OS" memory. Most of the heap
 * becomes one arena at once: mimalloc never releases an arena, so arenas
 * reserved on demand would divide the heap for good and limit the largest
 * block to the largest arena. The rest of the pages, a pool, holds
 * mimalloc's page map, blocks aligned beyond 64 KiB, and the pages that
 * mimalloc needs once the arena is full; each returns to the pool when it is
 * freed. newlib's sbrk is left with nothing. This file defines every C and
 * newlib allocation entry point, so that newlib's allocator is not linked.
 *
 * mimalloc keeps the header of a page in front of its blocks, so a block
 * above 512 KiB (a page of its own) takes one more 64 KiB slice.
 */
#include <stddef.h>
#include <stdint.h>
#include "switch2/memory/heap.h"
#include "heap_pages.h"

// The thread id of mimalloc is the thread's kernel TLS page (TPIDRRO_EL0):
// it is unique to each live thread and 0x200-aligned. The default on AArch64
// reads TPIDR_EL0, which Horizon does not set per thread.
static inline uintptr_t _mallocThreadId(void) {
    uintptr_t tls;
    __asm__ volatile ("mrs %0, tpidrro_el0" : "=r"(tls));
    return tls;
}
#define MI_PRIM_THREAD_ID() _mallocThreadId()

// mimalloc's default keeps page information at 256 MiB-aligned boundaries,
// which needs a 256 MiB-aligned arena and limits an object to less than
// 256 MiB. free looks pages up in the page map instead, a two-level one
// that covers Horizon's 39-bit address space and grows 64 KiB per 512 MiB
// of heap.
#define MI_FREE_USE_PAGEMAP 1
#define MI_PAGE_MAP_FLAT 0
#define MI_DEFAULT_VIRTUAL_ADDRESS_BITS 39

// Upstream code is built as it is. Its profiler leaves a helper unused, and
// GCC 15 misreads the page pointer that the guarded-sampling code derives
// from a block address as a zero-size object.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#include "../../../external/mimalloc/src/static.c"
#pragma GCC diagnostic pop

#include <malloc.h>
// newlib's malloc.h may define the reentrant names as macros.
#undef _malloc_r
#undef _free_r
#undef _realloc_r
#undef _calloc_r
#undef _memalign_r
#undef _mallinfo_r
#undef _malloc_stats_r
#undef _mallopt_r
#undef _malloc_usable_size_r
#undef _valloc_r
#undef _pvalloc_r
#undef _malloc_trim_r

struct _reent;

// Heap pages left outside the main arena: 1/64 of the heap, from 8 MiB to
// 128 MiB. The page map takes about 64 KiB per 512 MiB of heap.
#define MALLOC_POOL_DIVISOR   64
#define MALLOC_POOL_MIN       (8 * 1024 * 1024)
#define MALLOC_POOL_MAX       (128 * 1024 * 1024)

//-----------------------------------------------------------------------------
// Heap
//-----------------------------------------------------------------------------

void _mallocInitialize(void) {
    extern char* fake_heap_start;
    extern char* fake_heap_end;

    _heapPagesInitialize((uintptr_t)fake_heap_start, (uintptr_t)fake_heap_end);

    // mimalloc reserves no arena of its own: one would keep its pages for
    // good. The heap is always committed, so there is nothing to purge. A
    // block may be as large as the arena.
    mi_option_set(mi_option_arena_reserve, 0);
    mi_option_set(mi_option_arena_max_object_size, MI_ARENA_MAX_SIZE / MI_KiB);
    mi_option_set(mi_option_purge_delay, -1);

    // The main arena: the free pages except the pool, at most the largest
    // arena mimalloc accepts.
    const u64 free_size = _heapPagesGetFreeSize();
    u64 pool_size = free_size / MALLOC_POOL_DIVISOR;
    if (pool_size < MALLOC_POOL_MIN)
        pool_size = MALLOC_POOL_MIN;
    if (pool_size > MALLOC_POOL_MAX)
        pool_size = MALLOC_POOL_MAX;
    if (free_size > pool_size + MI_ARENA_MIN_SIZE) {
        u64 arena_size = free_size - pool_size;
        if (arena_size > MI_ARENA_MAX_SIZE)
            arena_size = MI_ARENA_MAX_SIZE;
        void* const arena = _heapPagesAllocate(arena_size, 0);
        if (arena != NULL && !mi_manage_os_memory(arena, arena_size, true, true, false, -1))
            _heapPagesFree(arena, arena_size);
    }

    // newlib's sbrk has nothing left to give.
    fake_heap_start = fake_heap_end;
}

u64 heapGetTotalSize(void) {
    return _heapPagesGetTotalSize();
}

// Free heap pages, and the free slices of every arena, read without a lock.
u64 heapGetFreeSize(void) {
    mi_subproc_t* const subproc = _mi_subproc_main();
    const size_t arena_count = mi_arenas_get_count(subproc);
    u64 free_slices = 0;
    for (size_t index = 0; index < arena_count; index++) {
        mi_arena_t* const arena = mi_arena_from_index(subproc, index);
        if (arena == NULL)
            continue;
        mi_bbitmap_t* const slices_free = arena->slices_free;
        const size_t chunk_count = mi_bbitmap_chunk_count(slices_free);
        for (size_t chunk = 0; chunk < chunk_count; chunk++) {
            for (size_t field = 0; field < MI_BCHUNK_FIELDS; field++)
                free_slices += __builtin_popcountll(mi_atomic_load_relaxed(&slices_free->chunks[chunk].bfields[field]));
        }
    }
    return _heapPagesGetFreeSize() + free_slices * MI_ARENA_SLICE_SIZE;
}

// Called by threadExit: the thread's pages go back to the other threads.
void _mallocThreadExit(void) {
    mi_thread_done();
}

//-----------------------------------------------------------------------------
// C entry points
//
// They are weak, so that a program's own definition takes precedence: Switch
// 1 ports often define posix_memalign, which newlib lacks, and some replace
// the allocator. A program that replaces malloc replaces free, calloc and
// realloc with it, as with any C library. newlib's reentrant entry points
// below stay strong: a newlib allocator object in the same link must fail
// to link, not take over part of the allocation calls.
//-----------------------------------------------------------------------------

#define MALLOC_ENTRY __attribute__((weak))

MALLOC_ENTRY void* malloc(size_t size) { return mi_malloc(size); }
MALLOC_ENTRY void free(void* p) { mi_free(p); }
MALLOC_ENTRY void cfree(void* p) { mi_free(p); }
MALLOC_ENTRY void* calloc(size_t count, size_t size) { return mi_calloc(count, size); }
MALLOC_ENTRY void* realloc(void* p, size_t size) { return mi_realloc(p, size); }
MALLOC_ENTRY void* reallocf(void* p, size_t size) { return mi_reallocf(p, size); }
MALLOC_ENTRY void* reallocarray(void* p, size_t count, size_t size) { return mi_reallocarray(p, count, size); }
MALLOC_ENTRY void* memalign(size_t alignment, size_t size) { return mi_memalign(alignment, size); }
MALLOC_ENTRY void* aligned_alloc(size_t alignment, size_t size) { return mi_aligned_alloc(alignment, size); }
MALLOC_ENTRY void* valloc(size_t size) { return mi_valloc(size); }
MALLOC_ENTRY void* pvalloc(size_t size) { return mi_pvalloc(size); }
MALLOC_ENTRY int posix_memalign(void** p, size_t alignment, size_t size) { return mi_posix_memalign(p, alignment, size); }
MALLOC_ENTRY size_t malloc_usable_size(void* p) { return mi_usable_size(p); }

// The heap cannot shrink, and newlib's tuning options do not apply.
MALLOC_ENTRY int malloc_trim(size_t pad) { (void)pad; return 0; }
MALLOC_ENTRY int mallopt(int parameter, int value) { (void)parameter; (void)value; return 0; }
MALLOC_ENTRY void malloc_stats(void) { mi_stats_print(NULL); }

// mimalloc does not keep newlib's figures; mi_stats_print gives its own.
MALLOC_ENTRY struct mallinfo mallinfo(void) {
    struct mallinfo info = {0};
    return info;
}

//-----------------------------------------------------------------------------
// newlib's reentrant entry points, which newlib itself calls
//-----------------------------------------------------------------------------

void* _malloc_r(struct _reent* r, size_t size) { (void)r; return mi_malloc(size); }
void _free_r(struct _reent* r, void* p) { (void)r; mi_free(p); }
void* _calloc_r(struct _reent* r, size_t count, size_t size) { (void)r; return mi_calloc(count, size); }
void* _realloc_r(struct _reent* r, void* p, size_t size) { (void)r; return mi_realloc(p, size); }
void* _reallocf_r(struct _reent* r, void* p, size_t size) { (void)r; return mi_reallocf(p, size); }
void* _memalign_r(struct _reent* r, size_t alignment, size_t size) { (void)r; return mi_memalign(alignment, size); }
void* _valloc_r(struct _reent* r, size_t size) { (void)r; return mi_valloc(size); }
void* _pvalloc_r(struct _reent* r, size_t size) { (void)r; return mi_pvalloc(size); }
size_t _malloc_usable_size_r(struct _reent* r, void* p) { (void)r; return mi_usable_size(p); }
int _malloc_trim_r(struct _reent* r, size_t pad) { (void)r; return malloc_trim(pad); }
int _mallopt_r(struct _reent* r, int parameter, int value) { (void)r; return mallopt(parameter, value); }
void _malloc_stats_r(struct _reent* r) { (void)r; malloc_stats(); }
struct mallinfo _mallinfo_r(struct _reent* r) { (void)r; return mallinfo(); }
