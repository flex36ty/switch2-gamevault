/* SPDX-License-Identifier: ISC
 * Original libounce-nx Switch 2 HID Nmouse client.
 *
 * The application registers 0xC000 bytes of its memory with the hid root as
 * read-only transfer memory (command 3139). The service writes there a table
 * of 17 device handles, one sample ring for each handle and the device
 * information (type set and Npad). This client reads that memory and does
 * not map anything that the service returns. Command 3131 releases the
 * memory. Both commands send the PID and the applet resource user ID.
 *
 * A device handle is an opaque 64-bit value. It is valid while its table
 * entry is active; its table index selects the ring and the information.
 * Ring entries use a sequence counter: the service makes it odd while it
 * writes an entry and even when the entry is complete.
 */
#define NX_SERVICE_ASSUME_NON_DOMAIN
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "switch2/services/nmouse.h"
#include "services/applet.h"
#include "kernel/tmem.h"
#include "kernel/svc.h"
#include "arm/counter.h"

#define HID_NMOUSE_MAX_DEVICES      17
#define HID_NMOUSE_RING_ENTRIES     17
#define HID_NMOUSE_STATE_REVISION   1

// The kernel gives the memory back when the service drops its transfer
// memory, which can be after command 3131 replies.
#define HID_NMOUSE_MEMORY_ALIGNMENT        0x1000
#define HID_NMOUSE_RETURN_TIMEOUT_NS       2000000000ULL
#define HID_NMOUSE_RETURN_POLL_NS          1000000LL

// One entry of the handle table.
typedef struct {
    u64 handle;
    u8 active;                                  // 1 while the handle is valid.
    u8 reserved[7];
} HidNmouseHandleEntry;

// Internal sample record of the service. HidNmouseState is a projection of it.
typedef struct {
    u64 delta_time;
    u64 sampling_number;
    s32 x;
    s32 y;
    s32 width;
    s32 height;
    s32 wheel_delta_y;
    u32 unk_x24;
    u32 buttons;
    u32 flags;
    u32 unk_x30;
    s32 delta_x;
    s32 delta_y;
    u8 unk_x3c[0x34];
} HidNmouseInternalState;

typedef struct {
    u64 sequence;                               // Odd while the service writes the entry.
    HidNmouseInternalState state;
} HidNmouseRingEntry;

typedef struct {
    u64 unk_x0;
    u64 unk_x8;
    u64 latest;                                 // Index of the newest entry.
    u64 count;                                  // Number of entries written, at most 16.
    HidNmouseRingEntry entries[HID_NMOUSE_RING_ENTRIES];
} HidNmouseRing;

// Device information of one handle.
typedef struct {
    u32 type_set;
    u8 type_set_valid;                          // Bit 0 is set when type_set is valid.
    u8 pad_x5[3];
    u32 npad_id;
    u8 npad_id_valid;                           // Bit 0 is set when npad_id is valid.
    u8 pad_xd[3];
    u8 pointer_enabled;
    u8 unk_x11[0xB];
} HidNmouseInfo;

typedef struct {
    HidNmouseHandleEntry handles[HID_NMOUSE_MAX_DEVICES];
    u8 unk_x110[0x110];
    HidNmouseRing rings[HID_NMOUSE_MAX_DEVICES];
    u8 unk_x8bb8[0x88];
    HidNmouseInfo infos[HID_NMOUSE_MAX_DEVICES];
} HidNmouseMemory;

// Type-set bits that hidGetMergedNmouseHandle and hidGetStandardMouseNmouseHandle
// look for, and the source types that command 3138 can select.
#define HID_NMOUSE_TYPE_MERGED          BIT(0)
#define HID_NMOUSE_TYPE_STANDARD_MOUSE  BIT(3)
#define HID_NMOUSE_SOURCE_TYPES_MASK    0xE

_Static_assert(sizeof(HidNmouseInternalState) == 0x70, "HidNmouseInternalState size");
_Static_assert(sizeof(HidNmouseRing) == 0x818, "HidNmouseRing size");
_Static_assert(sizeof(HidNmouseInfo) == 0x1C, "HidNmouseInfo size");
_Static_assert(offsetof(HidNmouseMemory, rings) == 0x220, "HidNmouseMemory rings");
_Static_assert(offsetof(HidNmouseMemory, infos) == 0x8C40, "HidNmouseMemory infos");
_Static_assert(sizeof(HidNmouseMemory) <= HID_NMOUSE_TRANSFER_MEMORY_SIZE, "HidNmouseMemory size");
_Static_assert(sizeof(HidNmouseState) == 0x30, "HidNmouseState size");
_Static_assert(sizeof(HidNmouseStateWithDelta) == 0x38, "HidNmouseStateWithDelta size");
_Static_assert(offsetof(HidNmouseInternalState, delta_x) == 0x34, "HidNmouseInternalState delta");

// Registered memory; NULL when Nmouse is not initialized.
static const HidNmouseMemory* g_hidNmouseMemory;
// Memory that hidInitializeNmouse allocated, freed by hidFinalizeNmouse.
static void* g_hidNmouseOwnedMemory;

// Waits until the process can write the memory again. Returns false if that
// does not happen in time.
static bool _hidWaitNmouseMemoryReturned(const void* memory) {
    const u64 start = armGetSystemTick();
    for (;;) {
        MemoryInfo info;
        u32 page_info;
        if (R_SUCCEEDED(svcQueryMemory(&info, &page_info, (uintptr_t)memory)) && (info.perm & Perm_Rw) == Perm_Rw)
            return true;
        if (armTicksToNs(armGetSystemTick() - start) >= HID_NMOUSE_RETURN_TIMEOUT_NS)
            return false;
        svcSleepThread(HID_NMOUSE_RETURN_POLL_NS);
    }
}

//-----------------------------------------------------------------------------
// IHidServer
//-----------------------------------------------------------------------------

// IHidServer cmd 3139: PID, pointer type, width, height, revision, ARUID and the extent
// of the copied transfer-memory handle. No output.
static Result _hidRegisterNmouseMemory(TransferMemory* tmem, s32 width, s32 height, HidNmousePointerType pointer_type) {
    const struct {
        u8 pointer_type;
        u8 pad[3];
        s32 width;
        s32 height;
        u32 revision;
        u64 AppletResourceUserId;
        u64 size;
    } in = { (u8)pointer_type, {0}, width, height, HID_NMOUSE_STATE_REVISION, appletGetAppletResourceUserId(), tmem->size };

    return serviceDispatchIn(hidGetServiceSession(), 3139, in,
        .in_send_pid = true,
        .in_num_handles = 1,
        .in_handles = { tmem->handle },
    );
}

// The local transfer-memory handle is closed after the request, whatever its result:
// the service keeps its own copy until command 3131. hid stays initialized while
// the memory is registered. (Closing the handle again on the failure path does nothing.)
Result hidInitializeNmouse(void *buffer, size_t size, s32 width, s32 height, HidNmousePointerType pointer_type) {
    void* owned = NULL;
    if (buffer == NULL) {
        size = HID_NMOUSE_TRANSFER_MEMORY_SIZE;
        owned = aligned_alloc(HID_NMOUSE_MEMORY_ALIGNMENT, size);
        if (owned == NULL)
            return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
        buffer = owned;
    }

    TransferMemory tmem;
    Result rc = tmemCreateFromMemory(&tmem, buffer, size, Perm_R);
    if (R_FAILED(rc)) {
        free(owned);
        return rc;
    }

    rc = hidInitialize();
    if (R_FAILED(rc))
        goto fail_tmem;

    rc = _hidRegisterNmouseMemory(&tmem, width, height, pointer_type);
    tmemCloseHandle(&tmem);
    if (R_FAILED(rc))
        goto fail_hid;

    g_hidNmouseMemory = (const HidNmouseMemory*)buffer;
    g_hidNmouseOwnedMemory = owned;
    return 0;

fail_hid:
    hidExit();
fail_tmem:
    tmemCloseHandle(&tmem);
    // The service did not register the memory, so the process can use it again.
    if (owned && _hidWaitNmouseMemoryReturned(owned))
        free(owned);
    return rc;
}

// IHidServer cmd 3131: PID and ARUID. No output.
Result hidFinalizeNmouse(void) {
    if (g_hidNmouseMemory == NULL)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    u64 AppletResourceUserId = appletGetAppletResourceUserId();
    Result rc = serviceDispatchIn(hidGetServiceSession(), 3131, AppletResourceUserId,
        .in_send_pid = true,
    );
    if (R_FAILED(rc))
        return rc;

    // Memory that the service keeps is not freed: the heap would reuse it.
    const bool returned = _hidWaitNmouseMemoryReturned(g_hidNmouseMemory);
    if (g_hidNmouseOwnedMemory && returned)
        free(g_hidNmouseOwnedMemory);
    g_hidNmouseOwnedMemory = NULL;
    g_hidNmouseMemory = NULL;
    hidExit();
    return 0;
}

static Result _hidSetNmouseButtonConfig(const u8 selectors[8], u32 cmd_id) {
    struct {
        u64 AppletResourceUserId;
        u8 selectors[8];
    } in = { appletGetAppletResourceUserId(), {0} };
    memcpy(in.selectors, selectors, sizeof(in.selectors));

    return serviceDispatchIn(hidGetServiceSession(), cmd_id, in,
        .in_send_pid = true,
    );
}

// IHidServer cmd 3136: PID, ARUID and the eight selector bytes. No output.
Result hidSetNmouseButtonConfigJoyLeftJcms(const HidNmouseButtonConfigJoyLeftJcms *config) {
    return _hidSetNmouseButtonConfig(config->selectors, 3136);
}

// IHidServer cmd 3137: same input as command 3136.
Result hidSetNmouseButtonConfigJoyRightJcms(const HidNmouseButtonConfigJoyRightJcms *config) {
    return _hidSetNmouseButtonConfig(config->selectors, 3137);
}

// IHidServer cmd 3143: PID, ARUID and the five selector bytes. No output.
Result hidSetNmouseButtonConfigStandardMouse(const HidNmouseButtonConfigStandardMouse *config) {
    struct {
        u64 AppletResourceUserId;
        u8 selectors[5];
        u8 pad[3];
    } in = { appletGetAppletResourceUserId(), {0}, {0} };
    memcpy(in.selectors, config->selectors, sizeof(in.selectors));

    return serviceDispatchIn(hidGetServiceSession(), 3143, in,
        .in_send_pid = true,
    );
}

// IHidServer cmd 3132: PID, the X and Y ranges and the ARUID. No output.
Result hidSetNmousePointerRanges(s32 width, s32 height) {
    const struct {
        s32 width;
        s32 height;
        u64 AppletResourceUserId;
    } in = { width, height, appletGetAppletResourceUserId() };

    return serviceDispatchIn(hidGetServiceSession(), 3132, in,
        .in_send_pid = true,
    );
}

// IHidServer cmd 3133: PID and ARUID; returns the X and Y ranges.
Result hidGetNmousePointerRanges(s32 *width, s32 *height) {
    const u64 AppletResourceUserId = appletGetAppletResourceUserId();
    struct {
        s32 width;
        s32 height;
    } out;

    Result rc = serviceDispatchInOut(hidGetServiceSession(), 3133, AppletResourceUserId, out,
        .in_send_pid = true,
    );
    if (R_SUCCEEDED(rc)) {
        if (width) *width = out.width;
        if (height) *height = out.height;
    }
    return rc;
}

// IHidServer cmd 3134: PID, the area's left, top, right and bottom, and the ARUID. No output.
// The service requires left < right and top < bottom, left and top not negative, and the
// area within the ranges; otherwise it gives 0x28c2ca.
Result hidSetNmousePointerMovementArea(s32 left, s32 top, s32 right, s32 bottom) {
    const struct {
        s32 left;
        s32 top;
        s32 right;
        s32 bottom;
        u64 AppletResourceUserId;
    } in = { left, top, right, bottom, appletGetAppletResourceUserId() };

    return serviceDispatchIn(hidGetServiceSession(), 3134, in,
        .in_send_pid = true,
    );
}

// IHidServer cmd 3142: PID, the area as four fractions of the ranges, and the ARUID. No output.
Result hidRestrictNmousePointer(float left, float top, float right, float bottom) {
    const struct {
        float left;
        float top;
        float right;
        float bottom;
        u64 AppletResourceUserId;
    } in = { left, top, right, bottom, appletGetAppletResourceUserId() };

    return serviceDispatchIn(hidGetServiceSession(), 3142, in,
        .in_send_pid = true,
    );
}

// IHidServer cmd 3138: PID, the source type set (masked with 0xe) and the ARUID. No output.
Result hidSetMergedNmouseSourceTypes(u32 type_set) {
    const struct {
        u32 type_set;
        u32 pad;
        u64 AppletResourceUserId;
    } in = { type_set & HID_NMOUSE_SOURCE_TYPES_MASK, 0, appletGetAppletResourceUserId() };

    return serviceDispatchIn(hidGetServiceSession(), 3138, in,
        .in_send_pid = true,
    );
}

// IHidServer cmd 3140: PID, the position, the ARUID and the handle. No output.
Result hidSetNmousePointerPosition(HidNmouseHandle handle, s32 x, s32 y) {
    const struct {
        s32 x;
        s32 y;
        u64 AppletResourceUserId;
        u64 handle;
    } in = { x, y, appletGetAppletResourceUserId(), handle.handle };

    return serviceDispatchIn(hidGetServiceSession(), 3140, in,
        .in_send_pid = true,
    );
}

//-----------------------------------------------------------------------------
// Nmouse memory
//-----------------------------------------------------------------------------

// The table entry must be active (exactly 1) and hold the handle.
static Result _hidGetNmouseIndex(const HidNmouseMemory* memory, HidNmouseHandle handle, u32* out_index) {
    for (u32 i = 0; i < HID_NMOUSE_MAX_DEVICES; i++) {
        const HidNmouseHandleEntry* entry = &memory->handles[i];
        if (atomic_load_explicit(&entry->active, memory_order_acquire) == 1 &&
            atomic_load_explicit(&entry->handle, memory_order_acquire) == handle.handle) {
            *out_index = i;
            return 0;
        }
    }
    return HID_NMOUSE_RESULT_INVALID_HANDLE;
}

// Finds the active handle whose Npad and type set are both valid and equal to id and type_set.
static bool _hidFindNmouseHandle(const HidNmouseMemory* memory, HidNpadIdType id, u32 type_set, HidNmouseHandle* out) {
    for (u32 i = 0; i < HID_NMOUSE_MAX_DEVICES; i++) {
        const HidNmouseHandleEntry* entry = &memory->handles[i];
        const HidNmouseInfo* info = &memory->infos[i];
        if (!(atomic_load_explicit(&entry->active, memory_order_acquire) & 1))
            continue;
        if (!(atomic_load_explicit(&info->npad_id_valid, memory_order_acquire) & 1) ||
            atomic_load_explicit(&info->npad_id, memory_order_acquire) != id)
            continue;
        if (!(atomic_load_explicit(&info->type_set_valid, memory_order_acquire) & 1) ||
            atomic_load_explicit(&info->type_set, memory_order_acquire) != type_set)
            continue;
        out->handle = atomic_load_explicit(&entry->handle, memory_order_acquire);
        return true;
    }
    return false;
}

Result hidGetNmouseHandles(HidNmouseHandle *handles, s32 count, HidNpadIdType id, s32 *total_out) {
    static const u32 type_sets[HID_NMOUSE_HANDLES_PER_NPAD] = { HidNmouseType_JoyLeft, HidNmouseType_JoyRight };

    const HidNmouseMemory* memory = g_hidNmouseMemory;
    if (memory == NULL)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    s32 total = 0;
    for (u32 i = 0; i < HID_NMOUSE_HANDLES_PER_NPAD && total < count; i++) {
        if (_hidFindNmouseHandle(memory, id, type_sets[i], &handles[total]))
            total++;
    }

    if (total_out) *total_out = total;
    return 0;
}

Result hidGetAllNmouseHandles(HidNmouseHandle *handles, s32 count, s32 *total_out) {
    static const HidNpadIdType ids[] = {
        HidNpadIdType_No1, HidNpadIdType_No2, HidNpadIdType_No3, HidNpadIdType_No4,
        HidNpadIdType_No5, HidNpadIdType_No6, HidNpadIdType_No7, HidNpadIdType_No8,
        HidNpadIdType_Handheld,
    };

    s32 total = 0;
    for (u32 i = 0; i < sizeof(ids) / sizeof(ids[0]) && total < count; i++) {
        s32 found = 0;
        Result rc = hidGetNmouseHandles(&handles[total], count - total, ids[i], &found);
        if (R_FAILED(rc))
            return rc;
        total += found;
    }

    if (total_out) *total_out = total;
    return 0;
}

Result hidGetNmouseTypeSet(HidNmouseHandle handle, u32 *out) {
    const HidNmouseMemory* memory = g_hidNmouseMemory;
    if (memory == NULL)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    u32 index = 0;
    Result rc = _hidGetNmouseIndex(memory, handle, &index);
    if (R_FAILED(rc))
        return rc;

    const HidNmouseInfo* info = &memory->infos[index];
    if (!(atomic_load_explicit(&info->type_set_valid, memory_order_acquire) & 1))
        return MAKERESULT(Module_Libnx, LibnxError_ShouldNotHappen);

    *out = atomic_load_explicit(&info->type_set, memory_order_acquire);
    return 0;
}

Result hidGetNmouseNpadIdType(HidNmouseHandle handle, HidNpadIdType *out) {
    const HidNmouseMemory* memory = g_hidNmouseMemory;
    if (memory == NULL)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    u32 index = 0;
    Result rc = _hidGetNmouseIndex(memory, handle, &index);
    if (R_FAILED(rc))
        return rc;

    const HidNmouseInfo* info = &memory->infos[index];
    if (!(atomic_load_explicit(&info->npad_id_valid, memory_order_acquire) & 1))
        return HID_NMOUSE_RESULT_INVALID_HANDLE;

    *out = atomic_load_explicit(&info->npad_id, memory_order_acquire);
    return 0;
}

Result hidIsNmousePointerEnabled(HidNmouseHandle handle, bool *out) {
    const HidNmouseMemory* memory = g_hidNmouseMemory;
    if (memory == NULL)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    u32 index = 0;
    Result rc = _hidGetNmouseIndex(memory, handle, &index);
    if (R_FAILED(rc))
        return rc;

    *out = atomic_load_explicit(&memory->infos[index].pointer_enabled, memory_order_acquire) != 0;
    return 0;
}

// The request is sent only when the device's enable byte differs from the
// requested state. IHidServer cmd 3141: PID, the state byte, the ARUID and the
// handle. No output.
Result hidEnableNmousePointer(HidNmouseHandle handle, bool enable) {
    bool enabled = false;
    Result rc = hidIsNmousePointerEnabled(handle, &enabled);
    if (R_FAILED(rc) || enabled == enable)
        return rc;

    const struct {
        u8 enable;
        u8 pad[7];
        u64 AppletResourceUserId;
        u64 handle;
    } in = { enable, {0}, appletGetAppletResourceUserId(), handle.handle };

    return serviceDispatchIn(hidGetServiceSession(), 3141, in,
        .in_send_pid = true,
    );
}

// The first active handle whose valid type set has any of the bits.
static Result _hidFindNmouseHandleOfType(HidNmouseHandle* out, u32 type_bits) {
    const HidNmouseMemory* memory = g_hidNmouseMemory;
    if (memory == NULL)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    for (u32 i = 0; i < HID_NMOUSE_MAX_DEVICES; i++) {
        const HidNmouseInfo* info = &memory->infos[i];
        const HidNmouseHandleEntry* entry = &memory->handles[i];
        if (!(atomic_load_explicit(&info->type_set_valid, memory_order_acquire) & 1) ||
            !(atomic_load_explicit(&info->type_set, memory_order_acquire) & type_bits) ||
            !(atomic_load_explicit(&entry->active, memory_order_acquire) & 1))
            continue;
        out->handle = atomic_load_explicit(&entry->handle, memory_order_acquire);
        return 0;
    }
    return HID_NMOUSE_RESULT_NO_DEVICE;
}

Result hidGetMergedNmouseHandle(HidNmouseHandle *out) {
    return _hidFindNmouseHandleOfType(out, HID_NMOUSE_TYPE_MERGED);
}

Result hidGetStandardMouseNmouseHandle(HidNmouseHandle *out) {
    return _hidFindNmouseHandleOfType(out, HID_NMOUSE_TYPE_STANDARD_MOUSE);
}

// The public state takes the internal fields 0x00-0x23, 0x30-0x33, 0x28-0x2B and 0x2C-0x2F,
// in this order. Internal bytes 0x24-0x27 and 0x34-0x6F are not published.
static void _hidProjectNmouseState(HidNmouseState* out, const HidNmouseInternalState* in) {
    out->delta_time      = in->delta_time;
    out->sampling_number = in->sampling_number;
    out->x               = in->x;
    out->y               = in->y;
    out->width           = in->width;
    out->height          = in->height;
    out->wheel_delta_y   = in->wheel_delta_y;
    out->unk_x24         = in->unk_x30;
    out->buttons         = in->buttons;
    out->flags           = in->flags;
}

// The state with delta inserts the internal fields 0x34-0x3B after the position.
static void _hidProjectNmouseStateWithDelta(HidNmouseStateWithDelta* out, const HidNmouseInternalState* in) {
    out->delta_time      = in->delta_time;
    out->sampling_number = in->sampling_number;
    out->x               = in->x;
    out->y               = in->y;
    out->delta_x         = in->delta_x;
    out->delta_y         = in->delta_y;
    out->width           = in->width;
    out->height          = in->height;
    out->wheel_delta_y   = in->wheel_delta_y;
    out->unk_x24         = in->unk_x30;
    out->buttons         = in->buttons;
    out->flags           = in->flags;
}

// Number of samples to read: the requested count, limited by the ring's count.
static s32 _hidNmouseHistoryCount(size_t requested, s32 available) {
    if (available < 0)
        return 0;
    return (size_t)available < requested ? available : (s32)requested;
}

// Copies total samples ending at ring index latest, oldest first, into states[total-1] ... states[0].
// Returns false when an entry is odd, changes during the copy, or does not follow the previous
// sample by exactly one sampling number; the caller then reads the ring again.
// Writes one projected sample: a HidNmouseState or a HidNmouseStateWithDelta.
typedef struct {
    size_t size;
    void (*project)(void* out, const HidNmouseInternalState* in);
} HidNmouseProjection;

static void _hidProjectState(void* out, const HidNmouseInternalState* in) { _hidProjectNmouseState(out, in); }
static void _hidProjectStateWithDelta(void* out, const HidNmouseInternalState* in) { _hidProjectNmouseStateWithDelta(out, in); }

static const HidNmouseProjection g_hidNmouseStateProjection = { sizeof(HidNmouseState), _hidProjectState };
static const HidNmouseProjection g_hidNmouseStateWithDeltaProjection = { sizeof(HidNmouseStateWithDelta), _hidProjectStateWithDelta };

static bool _hidCopyNmouseHistory(const HidNmouseRing* ring, s32 latest, void* states, s32 total,
                                  const HidNmouseProjection* projection) {
    u64 older_sampling_number = 0;
    for (s32 i = 0; i < total; i++) {
        const HidNmouseRingEntry* entry = &ring->entries[(latest - total + 1 + HID_NMOUSE_RING_ENTRIES + i) % HID_NMOUSE_RING_ENTRIES];

        u64 sequence = atomic_load_explicit(&entry->sequence, memory_order_acquire);
        if (sequence & 1)
            return false;

        HidNmouseInternalState internal;
        memcpy(&internal, &entry->state, sizeof(internal));
        atomic_thread_fence(memory_order_acquire);
        if (atomic_load_explicit(&entry->sequence, memory_order_relaxed) != sequence)
            return false;

        if (i > 0 && internal.sampling_number - older_sampling_number != 1)
            return false;
        older_sampling_number = internal.sampling_number;
        projection->project((u8*)states + (size_t)(total - 1 - i) * projection->size, &internal);
    }
    return true;
}

// The ring's count is read before its latest index, at the start and on each retry. A retry
// never lowers the number of samples.
static Result _hidGetNmouseHistory(HidNmouseHandle handle, void *states, size_t count, size_t *total_out,
                                   const HidNmouseProjection* projection) {
    const HidNmouseMemory* memory = g_hidNmouseMemory;
    if (memory == NULL)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    u32 index = 0;
    Result rc = _hidGetNmouseIndex(memory, handle, &index);
    if (R_FAILED(rc))
        return rc;

    const HidNmouseRing* ring = &memory->rings[index];
    s32 total = _hidNmouseHistoryCount(count, (s32)atomic_load_explicit(&ring->count, memory_order_acquire));
    s32 latest = (s32)atomic_load_explicit(&ring->latest, memory_order_acquire);

    while (total > 0 && !_hidCopyNmouseHistory(ring, latest, states, total, projection)) {
        s32 available = (s32)atomic_load_explicit(&ring->count, memory_order_acquire);
        if (total <= available)
            total = _hidNmouseHistoryCount(count, available);
        latest = (s32)atomic_load_explicit(&ring->latest, memory_order_acquire);
    }

    if (total_out) *total_out = total;
    return 0;
}

Result hidGetNmouseStates(HidNmouseHandle handle, HidNmouseState *states, size_t count, size_t *total_out) {
    return _hidGetNmouseHistory(handle, states, count, total_out, &g_hidNmouseStateProjection);
}

Result hidGetNmouseStatesWithDelta(HidNmouseHandle handle, HidNmouseStateWithDelta *states, size_t count, size_t *total_out) {
    return _hidGetNmouseHistory(handle, states, count, total_out, &g_hidNmouseStateWithDeltaProjection);
}
