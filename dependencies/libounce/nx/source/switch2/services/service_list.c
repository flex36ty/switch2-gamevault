// The list of open services that _serviceGuardCloseAll closes at exit
// (services/service_guard.h).
#include "../../services/service_guard.h"

static Mutex g_serviceListMutex;
static ServiceGuard* g_openServices; ///< The newest open service first.

void _serviceGuardAddOpen(ServiceGuard* g, void (*cleanupFunc)(void))
{
    mutexLock(&g_serviceListMutex);
    g->cleanupFunc = cleanupFunc;
    g->prev_next = &g_openServices;
    g->next = g_openServices;
    if (g_openServices)
        g_openServices->prev_next = &g->next;
    g_openServices = g;
    mutexUnlock(&g_serviceListMutex);
}

void _serviceGuardRemoveOpen(ServiceGuard* g)
{
    mutexLock(&g_serviceListMutex);
    if (g->prev_next) {
        *g->prev_next = g->next;
        if (g->next)
            g->next->prev_next = g->prev_next;
    }
    g->next = NULL;
    g->prev_next = NULL;
    g->cleanupFunc = NULL;
    mutexUnlock(&g_serviceListMutex);
}

// Closes one open service whatever its reference count, and takes it out of
// the list in any case, so that _serviceGuardCloseAll ends. The guard's lock
// is taken before the list's, as in serviceGuardExit.
static void _serviceGuardClose(ServiceGuard* g)
{
    mutexLock(&g->mutex);
    void (*cleanupFunc)(void) = g->cleanupFunc;
    const bool open = g->refCount != 0;
    g->refCount = 0;
    _serviceGuardRemoveOpen(g);
    if (open && cleanupFunc)
        cleanupFunc();
    mutexUnlock(&g->mutex);
}

void _serviceGuardCloseAll(void)
{
    for (;;) {
        mutexLock(&g_serviceListMutex);
        ServiceGuard* newest = g_openServices;
        mutexUnlock(&g_serviceListMutex);
        if (!newest)
            break;
        _serviceGuardClose(newest);
    }
}
