// Copyright 2018 plutoo
#include "types.h"
#include "result.h"
#include "runtime/env.h"
#include "arm/cache.h"
#include "kernel/svc.h"
#include "kernel/virtmem.h"
#include "kernel/jit.h"
#include "../runtime/alloc.h"

#if defined(__SWITCH2__)
// [Switch 2] The homebrew environment owns the process handle needed for code
// mapping. Alias hooks are optional private extensions to the existing map
// hooks; they keep RX mapped while the public Jit API writes into staging.
Result __attribute__((weak)) __nx_jit_map_code(void* rx_addr, void* src_addr, size_t size);
Result __attribute__((weak)) __nx_jit_unmap_code(void* rx_addr, void* src_addr, size_t size);
Result __attribute__((weak)) __nx_jit_map_alias(void* rx_addr, size_t size);
Result __attribute__((weak)) __nx_jit_publish_alias(void* rx_addr, const void* data, size_t size);

static bool _jitEnvironmentMapsCode(void)
{
    return &__nx_jit_map_code && &__nx_jit_unmap_code;
}

static bool _jitEnvironmentAliasesCode(void)
{
    return _jitEnvironmentMapsCode() && &__nx_jit_map_alias && &__nx_jit_publish_alias;
}

static bool _jitUsesEnvironmentAlias(const Jit* j)
{
    return j->type == JitType_SetProcessMemoryPermission && j->rw_addr != j->src_addr;
}
#endif

Result jitCreate(Jit* j, size_t size)
{
    JitType type;

    // Use new CodeMemory object introduced in [4.0.0+], if available.
    // On [5.0.0+] this is only usable with a kernel patch, as svcControlCodeMemory now errors if it's used under the same process which owns the object.
    // The homebrew loading environment is responsible for hinting the syscalls if they are available/usable for jit.
    if (envIsSyscallHinted(0x4B) && envIsSyscallHinted(0x4C)) {
        type = JitType_CodeMemory;
    }
    // Fall back to JitType_SetProcessMemoryPermission if available.
    else if (envIsSyscallHinted(0x73) && envIsSyscallHinted(0x77) && envIsSyscallHinted(0x78)
             && (envGetOwnProcessHandle() != INVALID_HANDLE)) {
        type = JitType_SetProcessMemoryPermission;
    }
#if defined(__SWITCH2__)
    // Use the environment's code mapping, with the same transitions.
    else if (_jitEnvironmentMapsCode()) {
        type = JitType_SetProcessMemoryPermission;
    }
#endif
    else {
        // Jit is unavailable. :(
        return MAKERESULT(Module_Libnx, LibnxError_JitUnavailable);
    }

    size = (size + 0xFFF) &~ 0xFFF;
    void* src_addr = __libnx_aligned_alloc(0x1000, size);

    if (src_addr == NULL)
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);

    j->type = type;
    j->size = size;
    j->src_addr = src_addr;
    j->handle = INVALID_HANDLE;
    j->is_executable = 0;

    Result rc = 0;

    switch (j->type)
    {
    case JitType_SetProcessMemoryPermission:
        virtmemLock();
        j->rx_addr = virtmemFindCodeMemory(j->size, 0x1000);
        j->rw_addr = j->src_addr;
        j->rv      = virtmemAddReservation(j->rx_addr, j->size);
        virtmemUnlock();

        if (!j->rv) {
            rc = MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
        }
#if defined(__SWITCH2__)
        else if (_jitEnvironmentAliasesCode()) {
            // [Switch 2] The locked source backs RX; writable bytes live in a
            // separate staging allocation until publication through RO.
            void* staging = __libnx_aligned_alloc(0x1000, j->size);
            if (!staging) {
                rc = MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
            }
            else {
                rc = __nx_jit_map_code(j->rx_addr, j->src_addr, j->size);
                if (R_SUCCEEDED(rc)) {
                    j->rw_addr = staging;
                    rc = __nx_jit_map_alias(j->rx_addr, j->size);
                    if (R_FAILED(rc)) {
                        Result unmap_rc = __nx_jit_unmap_code(j->rx_addr, j->src_addr, j->size);
                        if (R_FAILED(unmap_rc))
                            return unmap_rc; // Keep the object for a retry with jitClose.
                        j->rw_addr = j->src_addr;
                    }
                }
                if (R_FAILED(rc))
                    __libnx_free(staging);
            }
        }
#endif
        break;

    case JitType_CodeMemory:
        rc = svcCreateCodeMemory(&j->handle, j->src_addr, j->size);
        if (R_SUCCEEDED(rc))
        {
            virtmemLock();
            j->rw_addr = virtmemFindCodeMemory(j->size, 0x1000);
            rc = svcControlCodeMemory(j->handle, CodeMapOperation_MapOwner, j->rw_addr, j->size, Perm_Rw);
            virtmemUnlock();

            if (R_SUCCEEDED(rc))
            {
                virtmemLock();
                j->rx_addr = virtmemFindCodeMemory(j->size, 0x1000);
                rc = svcControlCodeMemory(j->handle, CodeMapOperation_MapSlave, j->rx_addr, j->size, Perm_Rx);
                virtmemUnlock();

                if (R_FAILED(rc)) {
                    svcControlCodeMemory(j->handle, CodeMapOperation_UnmapOwner, j->rw_addr, j->size, 0);
                }
            }

            if (R_FAILED(rc)) {
                svcCloseHandle(j->handle);
                j->handle = INVALID_HANDLE;
            }
        }

        if (R_FAILED(rc)) {
            j->rw_addr = NULL;
        }

        break;
    }

    if (R_FAILED(rc)) {
#if defined(__SWITCH2__)
        if (j->type == JitType_SetProcessMemoryPermission && j->rv) {
            virtmemLock();
            virtmemRemoveReservation(j->rv);
            virtmemUnlock();
        }
#endif
        __libnx_free(j->src_addr);
        j->src_addr = NULL;
    }

    return rc;
}

Result jitTransitionToWritable(Jit* j)
{
    Result rc = 0;

    switch (j->type) {
    case JitType_SetProcessMemoryPermission:
#if defined(__SWITCH2__)
        if (_jitUsesEnvironmentAlias(j))
            break;
        if (_jitEnvironmentMapsCode()) {
            if (j->is_executable) rc = __nx_jit_unmap_code(j->rx_addr, j->src_addr, j->size);
            break;
        }
#endif
        if (j->is_executable) rc = svcUnmapProcessCodeMemory(envGetOwnProcessHandle(), (u64) j->rx_addr, (u64) j->src_addr, j->size);
        break;

    case JitType_CodeMemory:
        // No need to do anything.
        break;
    }

    if (R_SUCCEEDED(rc)) j->is_executable = 0;

    return rc;
}

Result jitTransitionToExecutable(Jit* j)
{
    Result rc = 0;

    switch (j->type) {
    case JitType_SetProcessMemoryPermission:
#if defined(__SWITCH2__)
        if (_jitUsesEnvironmentAlias(j)) {
            if (!j->is_executable) {
                rc = __nx_jit_publish_alias(j->rx_addr, j->rw_addr, j->size);
                if (R_SUCCEEDED(rc)) armICacheInvalidate(j->rx_addr, j->size);
            }
            break;
        }
        if (_jitEnvironmentMapsCode()) {
            if (!j->is_executable) {
                armDCacheFlush(j->src_addr, j->size);
                rc = __nx_jit_map_code(j->rx_addr, j->src_addr, j->size);
                if (R_SUCCEEDED(rc)) armICacheInvalidate(j->rx_addr, j->size);
            }
            break;
        }
#endif
        if (!j->is_executable) {
            rc = svcMapProcessCodeMemory(envGetOwnProcessHandle(), (u64) j->rx_addr, (u64) j->src_addr, j->size);

            if (R_SUCCEEDED(rc)) {
                rc = svcSetProcessMemoryPermission(envGetOwnProcessHandle(), (u64) j->rx_addr, j->size, Perm_Rx);

                if (R_FAILED(rc)) {
                    jitTransitionToWritable(j);
                }
            }
        }
        break;

    case JitType_CodeMemory:
        armDCacheFlush(j->rw_addr, j->size);
        armICacheInvalidate(j->rx_addr, j->size);
        break;
    }

    if (R_SUCCEEDED(rc)) j->is_executable = 1;

    return rc;
}

Result jitClose(Jit* j)
{
    Result rc = 0;

    switch (j->type)
    {
    case JitType_SetProcessMemoryPermission:
#if defined(__SWITCH2__)
        if (_jitUsesEnvironmentAlias(j)) {
            rc = __nx_jit_unmap_code(j->rx_addr, j->src_addr, j->size);
            if (R_SUCCEEDED(rc)) {
                virtmemLock();
                virtmemRemoveReservation(j->rv);
                virtmemUnlock();
                __libnx_free(j->rw_addr);
                j->rw_addr = NULL;
            }
            break;
        }
#endif
        rc = jitTransitionToWritable(j);

        if (R_SUCCEEDED(rc)) {
            virtmemLock();
            virtmemRemoveReservation(j->rv);
            virtmemUnlock();
        }
        break;

    case JitType_CodeMemory:
        rc = svcControlCodeMemory(j->handle, CodeMapOperation_UnmapOwner, j->rw_addr, j->size, 0);

        if (R_SUCCEEDED(rc)) {
            rc = svcControlCodeMemory(j->handle, CodeMapOperation_UnmapSlave, j->rx_addr, j->size, 0);

            if (R_SUCCEEDED(rc)) {
                svcCloseHandle(j->handle);
            }
        }
        break;
    }

    if (R_SUCCEEDED(rc)) {
        if (j->src_addr != NULL) {
            __libnx_free(j->src_addr);
            j->src_addr = NULL;
        }
    }
    return rc;
}
