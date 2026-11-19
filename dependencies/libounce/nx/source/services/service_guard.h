#pragma once
#include "types.h"
#include "result.h"
#include "kernel/mutex.h"
#include "sf/service.h"
#include "services/sm.h"

#if defined(__SWITCH2__)
// A program started by hbloader runs in the loader's process, and a session
// that it leaves open stays open for the next program. While its reference
// count is nonzero, each guard is also in a list of open services, so that
// _serviceGuardCloseAll can close what the program left open when it exits
// (runtime/init.c, __libnx_exit).
typedef struct ServiceGuard {
    Mutex mutex;
    u32 refCount;
    void (*cleanupFunc)(void);       ///< Set while the service is in the list.
    struct ServiceGuard* next;       ///< The service opened before this one.
    struct ServiceGuard** prev_next;
} ServiceGuard;

// switch2/services/service_list.c. Call these with g->mutex held; they take
// the list's lock.
void _serviceGuardAddOpen(ServiceGuard* g, void (*cleanupFunc)(void));
void _serviceGuardRemoveOpen(ServiceGuard* g);

/// Closes every service that is still open, the newest first, so that each
/// closes before the services that were opened before it.
void _serviceGuardCloseAll(void);
#else
typedef struct ServiceGuard {
    Mutex mutex;
    u32 refCount;
} ServiceGuard;
#endif

NX_INLINE bool serviceGuardBeginInit(ServiceGuard* g)
{
    mutexLock(&g->mutex);
    return (g->refCount++) == 0;
}

#if defined(__SWITCH2__)
NX_INLINE Result serviceGuardEndInit(ServiceGuard* g, Result rc, void (*cleanupFunc)(void))
{
    if (R_FAILED(rc)) {
        cleanupFunc();
        --g->refCount;
    } else if (g->refCount == 1) {
        _serviceGuardAddOpen(g, cleanupFunc);
    }
    mutexUnlock(&g->mutex);
    return rc;
}
#else
NX_INLINE Result serviceGuardEndInit(ServiceGuard* g, Result rc, void (*cleanupFunc)(void))
{
    if (R_FAILED(rc)) {
        cleanupFunc();
        --g->refCount;
    }
    mutexUnlock(&g->mutex);
    return rc;
}
#endif

#if defined(__SWITCH2__)
NX_INLINE void serviceGuardExit(ServiceGuard* g, void (*cleanupFunc)(void))
{
    mutexLock(&g->mutex);
    if (g->refCount && (--g->refCount) == 0) {
        _serviceGuardRemoveOpen(g);
        cleanupFunc();
    }
    mutexUnlock(&g->mutex);
}
#else
NX_INLINE void serviceGuardExit(ServiceGuard* g, void (*cleanupFunc)(void))
{
    mutexLock(&g->mutex);
    if (g->refCount && (--g->refCount) == 0)
        cleanupFunc();
    mutexUnlock(&g->mutex);
}
#endif

#define NX_GENERATE_SERVICE_GUARD_PARAMS(name, _paramdecl, _parampass) \
\
static ServiceGuard g_##name##Guard; \
NX_INLINE Result _##name##Initialize _paramdecl; \
static void _##name##Cleanup(void); \
\
Result name##Initialize _paramdecl \
{ \
    Result rc = 0; \
    if (serviceGuardBeginInit(&g_##name##Guard)) \
        rc = _##name##Initialize _parampass; \
    return serviceGuardEndInit(&g_##name##Guard, rc, _##name##Cleanup); \
} \
\
void name##Exit(void) \
{ \
    serviceGuardExit(&g_##name##Guard, _##name##Cleanup); \
}

#define NX_GENERATE_SERVICE_GUARD(name) NX_GENERATE_SERVICE_GUARD_PARAMS(name, (void), ())
