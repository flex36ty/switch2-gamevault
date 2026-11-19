# mimalloc in libounce-nx

Upstream: https://github.com/microsoft/mimalloc, release tag `v3.5.4`,
MIT license (`LICENSE`). Only `include/`, `src/` and `LICENSE` are kept,
and of those only what the Horizon build compiles: the other platforms'
primitives (`src/prim/` for Windows, macOS, Unix, WASI and Emscripten) and
the C++ override headers (`mimalloc-new-delete.h`, `mimalloc-override.h`)
are left out.

Changes from upstream:

- `src/prim/horizon/prim.c` (new): primitives for Horizon. mimalloc's "OS"
  memory is runs of libounce's 64 KiB heap pages
  (`source/switch2/malloc/heap_pages.c`), honoring the requested alignment;
  freeing gives the pages back. Commit and decommit do nothing (the heap is
  always committed), randomness is the kernel's entropy, and messages go to
  the debug log.
- `src/prim/prim.c`: selects `horizon/prim.c` when `__SWITCH__` is defined.

libounce builds it in `source/switch2/malloc/malloc.c`, as the separate
library `libounce_malloc.a`, whose objects `libounce.a` (`-lounce`)
contains with the rest of libounce, `libounce_core.a`. That file defines the thread id (the thread's
kernel TLS page), the C and newlib allocation entry points, the heap setup
(one arena over most of the heap, a pool of pages for the rest), the heap
figures of `switch2/memory/heap.h`, and the thread-exit hook.
