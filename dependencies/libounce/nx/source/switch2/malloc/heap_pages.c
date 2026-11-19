/* SPDX-License-Identifier: ISC
 * Original libounce-nx Switch 2 heap pages.
 *
 * Horizon gives the program one fixed heap, always committed. The allocator
 * asks for memory in large runs (arenas, huge objects, its page map) and
 * gives them back; these come from here. One bit per 64 KiB page records
 * whether the page is in use. A request takes the lowest-addressed free run
 * that fits, which keeps the free space at the top of the heap together.
 * Requests are rare and large, so one mutex is enough.
 */
#include <string.h>
#include "switch/kernel/mutex.h"
#include "heap_pages.h"

#define HEAP_PAGES_PER_WORD 64

typedef struct {
    uintptr_t base;          ///< Address of page 0.
    size_t page_count;
    u64* used;               ///< Bit set: the page is in use.
    size_t free_page_count;
    size_t first_free_hint;  ///< No page below this index is free.
    Mutex lock;
} HeapPages;

static HeapPages g_heapPages;

//-----------------------------------------------------------------------------
// Bitmap
//-----------------------------------------------------------------------------

static bool _heapPagesIsUsed(size_t page) {
    return (g_heapPages.used[page / HEAP_PAGES_PER_WORD] >> (page % HEAP_PAGES_PER_WORD)) & 1;
}

// The mask of bits [first, first + count) of one word; count is 1 to 64.
static u64 _heapPagesWordMask(size_t first, size_t count) {
    const u64 ones = count == HEAP_PAGES_PER_WORD ? ~0ULL : (1ULL << count) - 1;
    return ones << first;
}

// Sets or clears the bits of pages [first, first + count).
static void _heapPagesMark(size_t first, size_t count, bool used) {
    while (count > 0) {
        const size_t bit = first % HEAP_PAGES_PER_WORD;
        size_t bits = HEAP_PAGES_PER_WORD - bit;
        if (bits > count)
            bits = count;
        const u64 mask = _heapPagesWordMask(bit, bits);
        u64* const word = &g_heapPages.used[first / HEAP_PAGES_PER_WORD];
        *word = used ? (*word | mask) : (*word & ~mask);
        first += bits;
        count -= bits;
    }
}

// The lowest free page at or above @p page, or page_count.
static size_t _heapPagesFindFree(size_t page) {
    while (page < g_heapPages.page_count) {
        const u64 free_bits = ~g_heapPages.used[page / HEAP_PAGES_PER_WORD] >> (page % HEAP_PAGES_PER_WORD);
        if (free_bits != 0) {
            page += __builtin_ctzll(free_bits);
            break;
        }
        page = (page / HEAP_PAGES_PER_WORD + 1) * HEAP_PAGES_PER_WORD;
    }
    return page < g_heapPages.page_count ? page : g_heapPages.page_count;
}

// The lowest used page in [page, end), or end.
static size_t _heapPagesFindUsed(size_t page, size_t end) {
    while (page < end) {
        const u64 used_bits = g_heapPages.used[page / HEAP_PAGES_PER_WORD] >> (page % HEAP_PAGES_PER_WORD);
        if (used_bits != 0) {
            page += __builtin_ctzll(used_bits);
            break;
        }
        page = (page / HEAP_PAGES_PER_WORD + 1) * HEAP_PAGES_PER_WORD;
    }
    return page < end ? page : end;
}

//-----------------------------------------------------------------------------
// Pages
//-----------------------------------------------------------------------------

void _heapPagesInitialize(uintptr_t start, uintptr_t end) {
    mutexInit(&g_heapPages.lock);
    const uintptr_t base = (start + HEAP_PAGE_SIZE - 1) & ~(uintptr_t)(HEAP_PAGE_SIZE - 1);
    if (end <= base)
        return;

    g_heapPages.base = base;
    g_heapPages.page_count = (end - base) / HEAP_PAGE_SIZE;
    g_heapPages.used = (u64*)base;

    const size_t word_count = (g_heapPages.page_count + HEAP_PAGES_PER_WORD - 1) / HEAP_PAGES_PER_WORD;
    const size_t bitmap_bytes = word_count * sizeof(u64);
    size_t bitmap_pages = (bitmap_bytes + HEAP_PAGE_SIZE - 1) / HEAP_PAGE_SIZE;
    if (bitmap_pages > g_heapPages.page_count)
        bitmap_pages = g_heapPages.page_count;
    memset(g_heapPages.used, 0, bitmap_bytes);

    // The bitmap's own pages are in use, and so are the bits past the last
    // page, so that no search runs over the end.
    _heapPagesMark(0, bitmap_pages, true);
    const size_t tail_bits = word_count * HEAP_PAGES_PER_WORD - g_heapPages.page_count;
    if (tail_bits > 0)
        _heapPagesMark(g_heapPages.page_count, tail_bits, true);

    g_heapPages.free_page_count = g_heapPages.page_count - bitmap_pages;
    g_heapPages.first_free_hint = bitmap_pages;
}

void* _heapPagesAllocate(size_t size, size_t alignment) {
    if (size == 0 || g_heapPages.page_count == 0 || (alignment & (alignment - 1)) != 0)
        return NULL;
    if (size > g_heapPages.page_count * (size_t)HEAP_PAGE_SIZE)
        return NULL;
    const size_t count = (size + HEAP_PAGE_SIZE - 1) / HEAP_PAGE_SIZE;
    if (alignment < HEAP_PAGE_SIZE)
        alignment = HEAP_PAGE_SIZE;

    void* result = NULL;
    mutexLock(&g_heapPages.lock);
    if (count <= g_heapPages.free_page_count) {
        size_t page = _heapPagesFindFree(g_heapPages.first_free_hint);
        while (page + count <= g_heapPages.page_count) {
            // Move up to the next page whose address has the alignment.
            const uintptr_t address = g_heapPages.base + page * (uintptr_t)HEAP_PAGE_SIZE;
            const uintptr_t aligned = (address + alignment - 1) & ~(uintptr_t)(alignment - 1);
            const size_t start = page + (aligned - address) / HEAP_PAGE_SIZE;
            if (start + count > g_heapPages.page_count || start + count < start)
                break;

            const size_t used = _heapPagesFindUsed(start, start + count);
            if (used == start + count) {
                _heapPagesMark(start, count, true);
                g_heapPages.free_page_count -= count;
                if (start == g_heapPages.first_free_hint)
                    g_heapPages.first_free_hint = _heapPagesFindFree(start + count);
                result = (void*)aligned;
                break;
            }
            page = _heapPagesFindFree(used + 1);
        }
    }
    mutexUnlock(&g_heapPages.lock);
    return result;
}

void _heapPagesFree(void* address, size_t size) {
    if (address == NULL || size == 0)
        return;
    const size_t first = ((uintptr_t)address - g_heapPages.base) / HEAP_PAGE_SIZE;
    const size_t count = (size + HEAP_PAGE_SIZE - 1) / HEAP_PAGE_SIZE;
    if ((uintptr_t)address < g_heapPages.base || first + count > g_heapPages.page_count)
        return;

    mutexLock(&g_heapPages.lock);
    // Only pages in use are counted back, so a repeated free cannot inflate the count.
    for (size_t page = first; page < first + count; page++) {
        if (_heapPagesIsUsed(page))
            g_heapPages.free_page_count++;
    }
    _heapPagesMark(first, count, false);
    if (first < g_heapPages.first_free_hint)
        g_heapPages.first_free_hint = first;
    mutexUnlock(&g_heapPages.lock);
}

u64 _heapPagesGetTotalSize(void) {
    return (u64)g_heapPages.page_count * HEAP_PAGE_SIZE;
}

u64 _heapPagesGetFreeSize(void) {
    return (u64)g_heapPages.free_page_count * HEAP_PAGE_SIZE;
}
