/* SPDX-License-Identifier: ISC
 * Original libounce-nx Switch 2 service wrapper.
 *
 * Commands are named by the TIPC command ID the code dispatches. One ID
 * names different commands on different sessions (root, sync, main, worker).
 */
#include <string.h>
#include "switch2/services/gsv.h"
#include "switch2/kernel/svc.h"
#include "services/applet.h"
#include "kernel/svc.h"
#include "arm/counter.h"
#include "../../services/service_guard.h"

// Native memory: image memory type, the SVC 0x80 attribute bit, and the
// registration flag that tells the service the attribute is set.
#define NATIVE_MEMORY_TYPE            3
#define NATIVE_MEMORY_ATTRIBUTE       0x20
#define NATIVE_MEMORY_FLAG_ATTRIBUTE  2
#define NATIVE_MEMORY_MODE_ATTRIBUTE  BIT(1)

// Interval between counter reads in gsvSyncWaitNative.
#define NATIVE_SYNC_POLL_NS  100000ULL

// AM's result for a root request while the process's previous root is still
// closing, and the interval at which gsvInitialize requests it again.
#define GSV_RESULT_ROOT_BUSY    0x280f3
#define ROOT_BUSY_POLL_NS       100000000LL

static TipcService g_gsvSrv;
static bool g_gsvAppletInitialized;
// Mode that the root permitted for frame registration.
static u64 g_gsvPermittedFrameMode;

static Result _gsvBootstrapRoot(void);

NX_GENERATE_SERVICE_GUARD(gsv);

// Root command 4 copies the current process handle and serializes the
// ARUID, the config and only the first GetInfo value.
static Result _gsvRootInitialize(u64 remote_memory_usage) {
    const struct {
        u64 aruid;
        u64 config;
        u64 reserved[3];
        u64 remote_memory_usage;
    } in = {
        .aruid = appletGetAppletResourceUserId(),
        .config = 0x8000000000000007ULL,
        .remote_memory_usage = remote_memory_usage,
    };
    return tipcDispatchIn(&g_gsvSrv, 4, in,
        .in_num_handles = 1,
        .in_handles = { CUR_PROCESS_HANDLE },
    );
}

Result _gsvInitialize(void) {
    Result rc = appletInitialize();
    if (R_FAILED(rc))
        return rc;
    g_gsvAppletInitialized = true;

    u64 remote_memory_usage = 0;
    rc = svcGetInfo(&remote_memory_usage, (InfoType)0x1f, CUR_PROCESS_HANDLE, 0);
    if (R_FAILED(rc))
        return rc;
    u64 other_memory_usage = 0;
    rc = svcGetInfo(&other_memory_usage, (InfoType)0x20, CUR_PROCESS_HANDLE, 0);
    if (R_FAILED(rc))
        return rc;

    // Right after the process's previous root closes, AM does not give a new
    // one yet; the request is repeated until it does.
    Handle h = INVALID_HANDLE;
    rc = appletGetGraphicsSupervisor(&h);
    while (rc == GSV_RESULT_ROOT_BUSY) {
        svcSleepThread(ROOT_BUSY_POLL_NS);
        rc = appletGetGraphicsSupervisor(&h);
    }
    if (R_FAILED(rc))
        return rc;
    tipcCreate(&g_gsvSrv, h);

    rc = _gsvRootInitialize(remote_memory_usage);
    if (R_SUCCEEDED(rc))
        rc = _gsvBootstrapRoot();

    return rc;
}

static void _gsvCleanup(void) {
    if (g_gsvSrv.session) {
        svcCloseHandle(g_gsvSrv.session);
        g_gsvSrv = (TipcService){};
    }
    if (g_gsvAppletInitialized) {
        appletExit();
        g_gsvAppletInitialized = false;
    }
    g_gsvPermittedFrameMode = 0;
}

TipcService* gsvGetServiceSession(void) {
    return &g_gsvSrv;
}

// Root commands 11, 101, 126, [120], 1501 and 1502. Command 11 copies the
// current process handle. Command 126 returns the mode. This function sends
// command 120 only for mode 1.
static Result _gsvBootstrapRoot(void) {
    Result rc = tipcDispatch(&g_gsvSrv, 11,
        .in_num_handles = 1,
        .in_handles = { CUR_PROCESS_HANDLE },
    );

    if (R_SUCCEEDED(rc))
        rc = tipcDispatch(&g_gsvSrv, 101);

    u32 mode = 0;
    if (R_SUCCEEDED(rc))
        rc = tipcDispatchOut(&g_gsvSrv, 126, mode);

    if (R_SUCCEEDED(rc) && mode == 1)
        rc = tipcDispatch(&g_gsvSrv, 120);

    if (R_SUCCEEDED(rc))
        rc = tipcDispatch(&g_gsvSrv, 1501);

    if (R_SUCCEEDED(rc))
        rc = tipcDispatch(&g_gsvSrv, 1502);

    return rc;
}

Result gsvOpenVi2(TipcService* out) {
    // Root command 301 probes permission. Command 305 moves the VI2 child.
    Result rc = tipcDispatch(&g_gsvSrv, 301);
    if (R_SUCCEEDED(rc)) {
        u64 selector = 0x0000000300040000ULL;
        Handle h = INVALID_HANDLE;
        rc = tipcDispatchIn(&g_gsvSrv, 305, selector,
            .out_handle_attrs = { SfOutHandleAttr_HipcMove },
            .out_handles = &h,
        );
        if (R_SUCCEEDED(rc)) tipcCreate(out, h);
        if (R_FAILED(rc))
            tipcDispatch(&g_gsvSrv, 303);
    }
    return rc;
}

Result gsvCloseVi2(TipcService* s) {
    // The child closes before root command 303.
    tipcClose(s);
    return tipcDispatch(&g_gsvSrv, 303);
}

// Root command 1402 moves the delegated nvservices session.
Result gsvOpenNvServices(Handle* out_session) {
    return tipcDispatch(&g_gsvSrv, 1402,
        .out_handle_attrs = { SfOutHandleAttr_HipcMove },
        .out_handles = out_session,
    );
}

// Root command 115 (u64 mode, returns the permitted mode), then commands
// 1101 and 1102.
Result gsvRegisterFrame(u64 mode, u64* out_permitted_mode) {
    u64 permitted_mode = 0;
    Result rc = tipcDispatchInOut(&g_gsvSrv, 115, mode, permitted_mode);

    if (R_SUCCEEDED(rc))
        g_gsvPermittedFrameMode = permitted_mode;

    if (R_SUCCEEDED(rc))
        rc = tipcDispatch(&g_gsvSrv, 1101);

    if (R_SUCCEEDED(rc))
        rc = tipcDispatch(&g_gsvSrv, 1102);

    if (R_SUCCEEDED(rc) && out_permitted_mode) *out_permitted_mode = permitted_mode;
    return rc;
}

// Root command 434 (u32 VM ID).
Result gsvRegisterVm(u32 vm_id) {
    return tipcDispatchIn(&g_gsvSrv, 434, vm_id);
}

//-----------------------------------------------------------------------------
// Native images
//-----------------------------------------------------------------------------

// Layout constructor: it truncates width minus one and height minus one to
// 16 bits, and the block-height exponent to one byte.
void gsvMakeImageLayout(GsvImageLayout* out, u32 width, u32 height, u32 block_height_log2,
                        u32 pitch_gobs, u32 pixel_format, u8 block_format, u16 luminance) {
    memset(out, 0, sizeof(*out));
    out->version = 0x100000000ULL;
    out->size = ((height - 1) & 0xFFFF) << 16 | ((width - 1) & 0xFFFF);
    out->block_height_log2 = block_height_log2;
    out->pitch_gobs = pitch_gobs;
    out->pixel_format = pixel_format;
    out->block_format = block_format;
    out->luminance = luminance;
}

// Root command 435 (layout and 1, returns the size).
Result gsvGetImageSize(const GsvImageLayout* layout, u64* out_size) {
    const struct {
        GsvImageLayout layout;
        u32 count;
    } NX_PACKED in = { *layout, 1 };
    u64 size = 0;

    Result rc = tipcDispatchInOut(&g_gsvSrv, 435, in, size);
    if (R_SUCCEEDED(rc) && out_size) *out_size = size;
    return rc;
}

// Root command 111 (type and size, returns a memory ID) and command 423
// (registers its backing). The ID is different from a worker range ID.
Result gsvReserveImageMemory(u8 type, u64 size, u32* out_memory_id) {
    const struct {
        u32 type;
        u64 size;
    } NX_PACKED in = { type, size };
    u32 memory_id = 0;

    Result rc = tipcDispatchInOut(&g_gsvSrv, 111, in, memory_id);
    if (R_SUCCEEDED(rc) && out_memory_id) *out_memory_id = memory_id;
    return rc;
}

Result gsvRegisterImageMemory(u32 memory_id, u64 cpu_address, u64 size, u32 gpu_memory_handle, u64 offset) {
    const struct {
        u32 memory_id;
        u64 cpu_address;
        u64 size;
        u32 gpu_memory_handle;
        u64 offset;
        u8 reserved[16];
    } NX_PACKED in = {
        .memory_id = memory_id,
        .cpu_address = cpu_address,
        .size = size,
        .gpu_memory_handle = gpu_memory_handle,
        .offset = offset,
    };

    return tipcDispatchIn(&g_gsvSrv, 423, in);
}

// Root command 429 (layout, 1 and memory ID, returns the image ID) and
// command 315 (image ID and alias count).
Result gsvCreateImage(const GsvImageLayout* layout, u32 memory_id, u32* out_image_id) {
    const struct {
        GsvImageLayout layout;
        u32 count;
        u32 memory_id;
    } NX_PACKED in = { *layout, 1, memory_id };
    u32 image_id = 0;

    Result rc = tipcDispatchInOut(&g_gsvSrv, 429, in, image_id);
    if (R_SUCCEEDED(rc) && out_image_id) *out_image_id = image_id;
    return rc;
}

Result gsvCreateDisplayAlias(u32 image_id, u32 count) {
    const struct {
        u32 image_id;
        u32 count;
    } in = { image_id, count };

    return tipcDispatchIn(&g_gsvSrv, 315, in);
}

// Root commands 314, 430 and 112 (one ID each). These return the Result of
// a failed request rather than aborting the process.
Result gsvDestroyDisplayAlias(u32 image_id) {
    return tipcDispatchIn(&g_gsvSrv, 314, image_id);
}

Result gsvDestroyImage(u32 image_id) {
    return tipcDispatchIn(&g_gsvSrv, 430, image_id);
}

Result gsvReleaseImageMemory(u32 memory_id) {
    return tipcDispatchIn(&g_gsvSrv, 112, memory_id);
}

// Native memory: optional SVC 0x80 attribute, then root command 111 (type 3)
// and command 114 (memory ID, CPU address, size, flag and a zero u64; no
// handles or buffers). A failed registration releases the ID and clears the
// attribute.
Result gsvCreateNativeMemory(GsvNativeMemory* out, void* address, u64 size) {
    memset(out, 0, sizeof(*out));
    const bool has_attribute = (g_gsvPermittedFrameMode & NATIVE_MEMORY_MODE_ATTRIBUTE) != 0;

    Result rc = 0;
    if (has_attribute)
        rc = svcUnknown80(address, size, NATIVE_MEMORY_ATTRIBUTE, NATIVE_MEMORY_ATTRIBUTE);
    if (R_FAILED(rc))
        return rc;

    u32 memory_id = 0;
    rc = gsvReserveImageMemory(NATIVE_MEMORY_TYPE, size, &memory_id);
    if (R_FAILED(rc))
        goto fail_attribute;

    const struct {
        u32 memory_id;
        u64 address;
        u64 size;
        u64 flags;
        u64 reserved;
    } NX_PACKED in = {
        .memory_id = memory_id,
        .address = (uintptr_t)address,
        .size = size,
        .flags = has_attribute ? NATIVE_MEMORY_FLAG_ATTRIBUTE : 0,
    };
    rc = tipcDispatchIn(&g_gsvSrv, 114, in);
    if (R_FAILED(rc))
        goto fail_id;

    out->memory_id = memory_id;
    out->address = address;
    out->size = size;
    out->has_attribute = has_attribute;
    return 0;

fail_id:
    gsvReleaseImageMemory(memory_id);
fail_attribute:
    if (has_attribute)
        svcUnknown80(address, size, NATIVE_MEMORY_ATTRIBUTE, 0);
    return rc;
}

// The attribute is cleared even when the release fails.
void gsvDestroyNativeMemory(GsvNativeMemory* m) {
    if (!m->memory_id)
        return;
    gsvReleaseImageMemory(m->memory_id);
    if (m->has_attribute)
        svcUnknown80(m->address, m->size, NATIVE_MEMORY_ATTRIBUTE, 0);
    memset(m, 0, sizeof(*m));
}

//-----------------------------------------------------------------------------
// Video decoding
//-----------------------------------------------------------------------------

// Root command 901: no input; the root's video policy bit decides the result.
Result gsvCheckVideoDecoderAccess(void) {
    return tipcDispatch(&g_gsvSrv, 901);
}

// Root command 906: a flag byte, then the u64 version at byte 4 (three data
// words). It moves the creator session.
Result gsvOpenVideoDecoderCreator(bool resolution_unlimited, u64 version, TipcService* out) {
    const struct {
        u8 resolution_unlimited;
        u8 pad[3];
        u64 version;
    } NX_PACKED in = { .resolution_unlimited = resolution_unlimited ? 1 : 0, .version = version };
    Handle h = INVALID_HANDLE;

    Result rc = tipcDispatchIn(&g_gsvSrv, 906, in,
        .out_handle_attrs = { SfOutHandleAttr_HipcMove },
        .out_handles = &h,
    );
    if (R_SUCCEEDED(rc)) tipcCreate(out, h);
    return rc;
}

// Root command 903: no input. It releases the decoder state that command 906
// created.
Result gsvReleaseVideoDecoderAccess(void) {
    return tipcDispatch(&g_gsvSrv, 903);
}

// Root command 991: preference and u32 count; returns u64 size and u64
// alignment. Command 911: preference, u32 count and u32 memory ID; returns
// the base image ID. Command 912: the base image ID. Command 961: one image ID;
// returns the 128-byte CPU descriptor.
Result gsvGetDecodeImagePoolSize(const GsvDecodeImagePreference* preference, u32 count, u64* out_size, u64* out_alignment) {
    const struct {
        GsvDecodeImagePreference preference;
        u32 count;
    } NX_PACKED in = { *preference, count };
    struct {
        u64 size;
        u64 alignment;
    } out = {};

    Result rc = tipcDispatchInOut(&g_gsvSrv, 991, in, out);
    if (R_SUCCEEDED(rc) && out_size) *out_size = out.size;
    if (R_SUCCEEDED(rc) && out_alignment) *out_alignment = out.alignment;
    return rc;
}

Result gsvCreateDecodeImagePool(const GsvDecodeImagePreference* preference, u32 count, u32 memory_id, u32* out_base_image_id) {
    const struct {
        GsvDecodeImagePreference preference;
        u32 count;
        u32 memory_id;
    } in = { *preference, count, memory_id };
    u32 base_image_id = 0;

    Result rc = tipcDispatchInOut(&g_gsvSrv, 911, in, base_image_id);
    if (R_SUCCEEDED(rc) && out_base_image_id) *out_base_image_id = base_image_id;
    return rc;
}

Result gsvDestroyDecodeImagePool(u32 base_image_id) {
    return tipcDispatchIn(&g_gsvSrv, 912, base_image_id);
}

Result gsvGetCpuImageDescriptor(u32 image_id, GsvCpuImageDescriptor* out) {
    GsvCpuImageDescriptor descriptor = {};

    Result rc = tipcDispatchInOut(&g_gsvSrv, 961, image_id, descriptor);
    if (R_SUCCEEDED(rc)) *out = descriptor;
    return rc;
}

//-----------------------------------------------------------------------------
// Sync session
//-----------------------------------------------------------------------------

// The root gives a process one sync session; a second open fails while the
// first is open. So the process's session is kept here and gsvOpenSync gives
// each caller a reference to it. Each caller's GsvSync holds a copy of the
// session that does not own the handle. The kernel can give a later session
// the same handle value, so a reference also records the generation of the
// session it belongs to; a reference to an earlier session does nothing.
typedef struct {
    Mutex mutex;
    TipcService session;
    u32 capacity;
    u32 slot_count;     // Configured event slots: min(capacity, 0x3f).
    u64 slots_in_use;   // Bit n set: slot n is allocated.
    u32 references;
    u32 generation;     // Incremented for each new session; never 0.
} GsvSharedSync;

static GsvSharedSync g_gsvSync;

// Creates and configures the sync session. Called with the mutex held.
// Root command 201 (probe) and command 202 (moves the sync session), then
// sync command 1 (query) and command 21 (configure with min(query, 0x3f)).
static Result _gsvSyncCreate(void) {
    Result rc = tipcDispatch(&g_gsvSrv, 201);

    Handle h = INVALID_HANDLE;
    if (R_SUCCEEDED(rc)) {
        rc = tipcDispatch(&g_gsvSrv, 202,
            .out_handle_attrs = { SfOutHandleAttr_HipcMove },
            .out_handles = &h,
        );
    }
    if (R_FAILED(rc))
        return rc;
    tipcCreate(&g_gsvSync.session, h);

    u32 capacity = 0;
    rc = tipcDispatchOut(&g_gsvSync.session, 1, capacity);

    const u32 configured = capacity < 0x3f ? capacity : 0x3f;
    if (R_SUCCEEDED(rc))
        rc = tipcDispatchIn(&g_gsvSync.session, 21, configured);

    if (R_FAILED(rc)) {
        tipcClose(&g_gsvSync.session);
        memset(&g_gsvSync.session, 0, sizeof(g_gsvSync.session));
        return rc;
    }

    g_gsvSync.capacity = capacity;
    g_gsvSync.slot_count = configured;
    g_gsvSync.slots_in_use = 0;
    if (++g_gsvSync.generation == 0)
        g_gsvSync.generation = 1;
    return 0;
}

// True if the reference belongs to the open session. Called with the mutex held.
static bool _gsvSyncIsCurrent(const GsvSync* s) {
    return g_gsvSync.references > 0 && s->generation == g_gsvSync.generation &&
           s->s.session == g_gsvSync.session.session;
}

// Closes the sync session. Called with the mutex held.
static void _gsvSyncDestroy(void) {
    tipcClose(&g_gsvSync.session);
    memset(&g_gsvSync.session, 0, sizeof(g_gsvSync.session));
    g_gsvSync.capacity = 0;
    g_gsvSync.slot_count = 0;
    g_gsvSync.slots_in_use = 0;
    g_gsvSync.references = 0;
}

Result gsvOpenSync(GsvSync* out) {
    memset(out, 0, sizeof(*out));

    mutexLock(&g_gsvSync.mutex);

    Result rc = 0;
    if (g_gsvSync.references == 0)
        rc = _gsvSyncCreate();

    if (R_SUCCEEDED(rc)) {
        g_gsvSync.references++;
        out->s = g_gsvSync.session;
        out->capacity = g_gsvSync.capacity;
        out->generation = g_gsvSync.generation;
    }

    mutexUnlock(&g_gsvSync.mutex);
    return rc;
}

void gsvSyncClose(GsvSync* s) {
    if (s->s.session == INVALID_HANDLE)
        return;

    mutexLock(&g_gsvSync.mutex);
    if (_gsvSyncIsCurrent(s)) {
        g_gsvSync.references--;
        if (g_gsvSync.references == 0)
            _gsvSyncDestroy();
    }
    mutexUnlock(&g_gsvSync.mutex);

    memset(s, 0, sizeof(*s));
}

Result gsvSyncAllocateSlot(GsvSync* s, u32* out_slot) {
    mutexLock(&g_gsvSync.mutex);

    Result rc = MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    if (_gsvSyncIsCurrent(s)) {
        rc = MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
        for (u32 slot = 0; slot < g_gsvSync.slot_count; slot++) {
            const u64 bit = UINT64_C(1) << slot;
            if (!(g_gsvSync.slots_in_use & bit)) {
                g_gsvSync.slots_in_use |= bit;
                *out_slot = slot;
                rc = 0;
                break;
            }
        }
    }

    mutexUnlock(&g_gsvSync.mutex);
    return rc;
}

void gsvSyncFreeSlot(GsvSync* s, u32 slot) {
    mutexLock(&g_gsvSync.mutex);

    const bool owned = _gsvSyncIsCurrent(s) && slot < g_gsvSync.slot_count &&
                       (g_gsvSync.slots_in_use & (UINT64_C(1) << slot));
    if (owned) {
        // The next owner must not see this owner's armed target. The result
        // is not used: the slot may not be armed.
        gsvSyncDisarmEvent(s, slot);
        g_gsvSync.slots_in_use &= ~(UINT64_C(1) << slot);
    }

    mutexUnlock(&g_gsvSync.mutex);
}

// Closes a sync session that a program left open at exit (see init.c).
// Handles belong to the process, so a session left open would make the next
// program that hbloader starts fail to open one.
void _gsvSyncExit(void) {
    mutexLock(&g_gsvSync.mutex);
    if (g_gsvSync.references > 0)
        _gsvSyncDestroy();
    mutexUnlock(&g_gsvSync.mutex);
}

// Sync command 103 (u32 inverted producer ID, returns a u64 counter and a u8
// mode).
Result gsvSyncReadCounter(GsvSync* s, u32 producer_id, u64* out_value, u8* out_mode) {
    const u32 in = ~producer_id;
    struct {
        u64 value;
        u8 mode;
        u8 pad[3];
    } NX_PACKED out = {};

    Result rc = tipcDispatchInOut(&s->s, 103, in, out);
    if (R_SUCCEEDED(rc) && out_value) *out_value = out.value;
    if (R_SUCCEEDED(rc) && out_mode) *out_mode = out.mode;
    return rc;
}

// Sync command 22 (u32 slot, moves a readable event), 201 (arm) and 202
// (disarm).
Result gsvSyncCreateEvent(GsvSync* s, u32 slot, Event* out_event, bool autoclear) {
    Handle h = INVALID_HANDLE;
    Result rc = tipcDispatchIn(&s->s, 22, slot,
        .out_handle_attrs = { SfOutHandleAttr_HipcMove },
        .out_handles = &h,
    );
    if (R_SUCCEEDED(rc)) eventLoadRemote(out_event, h, autoclear);
    return rc;
}

Result gsvSyncArmEvent(GsvSync* s, u32 slot, u32 producer_id, u64 target, u8 mode) {
    const struct {
        u32 slot;
        u32 inverted_producer_id;
        u64 target;
        u32 mode;
    } NX_PACKED in = { slot, ~producer_id, target, mode };

    return tipcDispatchIn(&s->s, 201, in);
}

Result gsvSyncDisarmEvent(GsvSync* s, u32 slot) {
    return tipcDispatchIn(&s->s, 202, slot);
}

// A native sync value: complemented producer ID at +0, comparison byte at +4
// and target at +8. A zero ID and comparison byte with a zero target is
// empty. Comparison 1 is reached when counter >= target. Comparison 0 is
// reached when the signed 32-bit difference counter - target is not
// negative. A counter of UINT64_MAX means that the producer is not valid,
// and the value counts as reached.
Result gsvSyncCheckNative(GsvSync* s, const GsvNativeSync* sync, bool* out_reached) {
    u32 token;
    u64 target;
    memcpy(&token, sync->data, sizeof(token));
    memcpy(&target, sync->data + 8, sizeof(target));
    const u8 comparison = sync->data[4];

    if (!token && !comparison && !target) {
        *out_reached = true;
        return 0;
    }
    if (comparison > 1)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    u64 counter = 0;
    Result rc = gsvSyncReadCounter(s, ~token, &counter, NULL);
    if (R_FAILED(rc))
        return rc;

    if (counter == UINT64_MAX)
        *out_reached = true;
    else if (comparison == 1)
        *out_reached = counter >= target;
    else
        *out_reached = (s32)((u32)counter - (u32)target) >= 0;
    return 0;
}

// This function reads the counter at intervals.
Result gsvSyncWaitNative(GsvSync* s, const GsvNativeSync* sync, u64 timeout_ns) {
    const u64 start = armGetSystemTick();
    for (;;) {
        bool reached = false;
        Result rc = gsvSyncCheckNative(s, sync, &reached);
        if (R_FAILED(rc) || reached)
            return rc;
        if (armTicksToNs(armGetSystemTick() - start) >= timeout_ns)
            return KERNELRESULT(TimedOut);
        svcSleepThread(NATIVE_SYNC_POLL_NS);
    }
}

//-----------------------------------------------------------------------------
// GPU main and worker sessions
//-----------------------------------------------------------------------------

// Root command 497 (u64 version 4, moves the main session), then root
// commands 401 and 402.
Result gsvOpenGpu(GsvGpu* out) {
    memset(out, 0, sizeof(*out));

    const u64 version = 4;
    Handle h = INVALID_HANDLE;
    Result rc = tipcDispatchIn(&g_gsvSrv, 497, version,
        .out_handle_attrs = { SfOutHandleAttr_HipcMove },
        .out_handles = &h,
    );
    if (R_FAILED(rc))
        return rc;
    tipcCreate(&out->main, h);

    rc = tipcDispatch(&g_gsvSrv, 401);

    if (R_SUCCEEDED(rc))
        rc = tipcDispatch(&g_gsvSrv, 402);

    if (R_FAILED(rc))
        gsvGpuClose(out);
    return rc;
}

void gsvGpuClose(GsvGpu* g) {
    if (g->worker.session)
        tipcClose(&g->worker);
    if (g->main.session)
        tipcClose(&g->main);
    memset(g, 0, sizeof(*g));
}

// Main command 11 (inclusive slot bounds), 12 (u32 slot, moves the slot
// event), 21 (u32 slot, enqueues) and 22 (moves the worker).
Result gsvGpuGetSlotRange(GsvGpu* g, u32* out_first, u32* out_last) {
    struct {
        u32 first;
        u32 last;
    } out = {};

    Result rc = tipcDispatchOut(&g->main, 11, out);
    if (R_SUCCEEDED(rc)) {
        if (out_first) *out_first = out.first;
        if (out_last) *out_last = out.last;
    }
    return rc;
}

Result gsvGpuCreateSlotEvent(GsvGpu* g, u32 slot, Event* out_event, bool autoclear) {
    Handle h = INVALID_HANDLE;
    Result rc = tipcDispatchIn(&g->main, 12, slot,
        .out_handle_attrs = { SfOutHandleAttr_HipcMove },
        .out_handles = &h,
    );
    if (R_SUCCEEDED(rc)) eventLoadRemote(out_event, h, autoclear);
    return rc;
}

Result gsvGpuOpenWorker(GsvGpu* g, u32 slot, Event* slot_event) {
    Result rc = tipcDispatchIn(&g->main, 21, slot);

    // Wait in 1 s periods until the slot event signals.
    while (R_SUCCEEDED(rc)) {
        rc = eventWait(slot_event, 1000000000ULL);
        if (R_VALUE(rc) != KERNELRESULT(TimedOut))
            break;
        rc = 0;
    }

    Handle h = INVALID_HANDLE;
    if (R_SUCCEEDED(rc)) {
        rc = tipcDispatch(&g->main, 22,
            .out_handle_attrs = { SfOutHandleAttr_HipcMove },
            .out_handles = &h,
        );
    }
    if (R_SUCCEEDED(rc)) tipcCreate(&g->worker, h);
    return rc;
}

// Main command 41 (packed name and two zero u64, returns the client byte)
// and 42 (release). Main command 33 (selector 0, returns the VM ID), 203
// (user ranges) and 202 (GSV range).
Result gsvGpuRegisterClient(GsvGpu* g, u64 name, u8* out_client) {
    const struct {
        u64 name;
        u64 reserved[2];
    } in = { .name = name };
    u32 client = 0;

    Result rc = tipcDispatchInOut(&g->main, 41, in, client);
    if (R_SUCCEEDED(rc) && out_client) *out_client = client & 0xFF;
    return rc;
}

Result gsvGpuReleaseClient(GsvGpu* g, u8 client) {
    const u32 in = client;
    return tipcDispatchIn(&g->main, 42, in);
}

Result gsvGpuCreateVm(GsvGpu* g, u32* out_vm_id) {
    const u32 selector = 0;
    u32 vm_id = 0;

    Result rc = tipcDispatchInOut(&g->main, 33, selector, vm_id);
    if (R_SUCCEEDED(rc) && out_vm_id) *out_vm_id = vm_id;
    return rc;
}

Result gsvGpuGetVmUserRanges(GsvGpu* g, u32 vm_id, GsvGpuVaRange out_ranges[4], u32* out_count) {
    struct {
        u64 count;
        GsvGpuVaRange ranges[4];
    } NX_PACKED out = {};

    Result rc = tipcDispatchInOut(&g->main, 203, vm_id, out);
    if (R_SUCCEEDED(rc)) {
        memcpy(out_ranges, out.ranges, sizeof(out.ranges));
        if (out_count) *out_count = (u32)out.count;
    }
    return rc;
}

Result gsvGpuGetVmGsvRange(GsvGpu* g, u32 vm_id, GsvGpuVaRange* out_range) {
    GsvGpuVaRange range = {};

    Result rc = tipcDispatchInOut(&g->main, 202, vm_id, range);
    if (R_SUCCEEDED(rc) && out_range) *out_range = range;
    return rc;
}

// Worker command 101 (inclusive CPU range, mode 0 and client, returns the
// range ID) and 102 (range ID and slot).
Result gsvGpuRegisterMemory(GsvGpu* g, u64 address, u64 size, u8 client, u32* out_range_id) {
    const struct {
        u64 first;
        u64 last;
        u32 mode;
        u32 client;
    } in = { address, address + size - 1, 0, client };
    u32 range_id = 0;

    Result rc = tipcDispatchInOut(&g->worker, 101, in, range_id);
    if (R_SUCCEEDED(rc) && out_range_id) *out_range_id = range_id;
    return rc;
}

Result gsvGpuUnregisterMemory(GsvGpu* g, u32 range_id, u32 slot) {
    const struct {
        u32 range_id;
        u32 slot;
    } in = { range_id, slot };

    return tipcDispatchIn(&g->worker, 102, in);
}

// Worker command 211 (map), 212 (change kind) and 213 (unmap). Each takes an
// inclusive GPU interval and a completion slot, and sets the synchronous
// completion byte to 1. Commands 211 and 212 return the selected kind. The
// cacheability field of command 211 is 0 or 1.
Result gsvGpuMap(GsvGpu* g, const GsvGpuMapping* mapping, u32 slot, u8* out_kind) {
    const struct {
        u32 vm_id;
        u64 first;
        u64 last;
        u32 range_id;
        u64 range_offset;
        u32 unk_x20;
        u32 gpu_cacheable;
        u32 kind;
        u32 fallback_kind;
        u32 unk_x30;
        u32 client;
        u32 synchronous;
        u32 slot;
    } NX_PACKED in = {
        .vm_id = mapping->vm_id,
        .first = mapping->va,
        .last = mapping->va + mapping->size - 1,
        .range_id = mapping->range_id,
        .range_offset = mapping->range_offset,
        .gpu_cacheable = mapping->gpu_cacheable ? 1 : 0,
        .kind = mapping->kind,
        .fallback_kind = mapping->fallback_kind,
        .client = mapping->client,
        .synchronous = 1,
        .slot = slot,
    };
    u32 kind = 0;

    Result rc = tipcDispatchInOut(&g->worker, 211, in, kind);
    if (R_SUCCEEDED(rc) && out_kind) *out_kind = kind & 0xFF;
    return rc;
}

Result gsvGpuChangeKind(GsvGpu* g, u32 vm_id, u64 va, u64 size, u8 kind, u32 slot, u8* out_kind) {
    const struct {
        u32 vm_id;
        u64 first;
        u64 last;
        u32 kind;
        u32 fallback_kind;
        u32 synchronous;
        u32 slot;
    } NX_PACKED in = { vm_id, va, va + size - 1, kind, kind, 1, slot };
    u32 selected = 0;

    Result rc = tipcDispatchInOut(&g->worker, 212, in, selected);
    if (R_SUCCEEDED(rc) && out_kind) *out_kind = selected & 0xFF;
    return rc;
}

Result gsvGpuUnmap(GsvGpu* g, u32 vm_id, u64 va, u64 size, u32 slot) {
    const struct {
        u32 vm_id;
        u64 first;
        u64 last;
        u32 synchronous;
        u32 slot;
    } NX_PACKED in = { vm_id, va, va + size - 1, 1, slot };

    return tipcDispatchIn(&g->worker, 213, in);
}
