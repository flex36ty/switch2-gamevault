// libounce.a's __libnx_init: the runtime initialization of
// source/runtime/init.c with a strong reference to _mallocInitialize. It
// takes the place of init.o in libounce.a, so that a link of the archive
// brings in the mimalloc allocator (libounce_malloc.a's objects, in the same
// archive) before anything can call newlib's malloc. libounce_core.a keeps
// the plain init.o, whose weak reference leaves newlib's malloc in place.
#define LIBOUNCE_MALLOC_LINKED 1
#include "../../runtime/init.c"
