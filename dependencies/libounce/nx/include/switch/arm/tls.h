/**
 * @file tls.h
 * @brief AArch64 thread local storage.
 * @author plutoo
 * @copyright libnx Authors
 */
#pragma once
#include "../types.h"

/**
 * @brief Gets the thread local storage buffer.
 * @return The thread local storage buffer.
 */
#ifdef LIBNX_HOST_TEST_TLS
/* Host IPC fixtures provide a TLS image without AArch64 registers. */
void* armGetTls(void);
#else
static inline void* armGetTls(void) {
    void* ret;
    __asm__ ("mrs %x[data], tpidrro_el0" : [data] "=r" (ret));
    return ret;
}
#endif
