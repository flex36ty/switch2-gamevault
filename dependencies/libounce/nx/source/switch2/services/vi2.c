/* SPDX-License-Identifier: ISC
 * Native VI2 command layouts adapted from libounce source/display/vi2_native.c
 * at 927cb2ca. Copyright (c) 2026 libounce contributors. See
 * LICENSES/libounce-ISC.txt.
 */
#include <string.h>
#include "switch2/services/vi2.h"
#include "switch2/services/gsv.h"
#include "kernel/event.h"
#include "kernel/mutex.h"
#include "kernel/svc.h"
#include "kernel/uevent.h"
#include "arm/counter.h"
#include "../../services/service_guard.h"

_Static_assert(sizeof(Vi2PresentOptions) == 0x40, "VI2 present options size");

void vi2MakeBasicPresentOptions(Vi2PresentOptions* out, u32 width, u32 height, u64 system_tick) {
    u16 crop_width = (u16)width;
    u16 crop_height = (u16)height;
    *out = (Vi2PresentOptions){
        .crop_right_inclusive  = crop_width && crop_height ? (u16)(crop_width - 1) : 0,
        .crop_bottom_inclusive = crop_width && crop_height ? (u16)(crop_height - 1) : 0,
        .flags                 = crop_width && crop_height ? 3 : 1,
        .system_tick           = system_tick,
        .interval_denominator  = 60,
        .interval_numerator    = 1,
    };
}

void vi2MakeZeroIntervalPresentOptions(Vi2PresentOptions* out, u32 width, u32 height, u64 system_tick) {
    vi2MakeBasicPresentOptions(out, width, height, system_tick);
    out->interval_denominator = 1;
    out->interval_numerator = 0;
}

static Result _vi2CmdCreateLayer(TipcService* s, u64 token, u32 first_event, u32 second_event) {
    // VI2 CreateLayer: command 201.
    const struct {
        u64 token;
        u32 first_event;
        u32 second_event;
    } in = { token, first_event, second_event };
    _Static_assert(sizeof(in) == 0x10, "VI2 create-layer input size");
    return tipcDispatchIn(s, 201, in);
}

static Result _vi2CmdGetAvailability(TipcService* s, u64 token, u64* out_slots) {
    // VI2 GetAvailability: command 212.
    u64 slots = 0;
    Result rc = tipcDispatchInOut(s, 212, token, slots);
    if (R_SUCCEEDED(rc) && out_slots) *out_slots = slots;
    return rc;
}

static Result _vi2CmdAttachLayer(TipcService* s, u64 token, u64 display_name) {
    // VI2 AttachLayer: command 251.
    const struct {
        u64 token;
        u64 display_name;
    } in = { token, display_name };
    return tipcDispatchIn(s, 251, in);
}

static Result _vi2CmdSelectLayer(TipcService* s, u64 display_name, u64 endpoint_name, u64 token) {
    // VI2 SelectLayer: command 111.
    const struct {
        u64 display_name;
        u64 endpoint_name;
        u64 token;
    } in = { display_name, endpoint_name, token };
    return tipcDispatchIn(s, 111, in);
}

static Result _vi2CmdClearLayerSelection(TipcService* s, u64 display_name, u64 endpoint_name) {
    // VI2 ClearLayerSelection: command 112.
    const struct {
        u64 display_name;
        u64 endpoint_name;
    } in = { display_name, endpoint_name };
    return tipcDispatchIn(s, 112, in);
}

static Result _vi2CmdPresent(TipcService* s, u64 token, u8 slot, u32 image_id,
                  const Vi2PresentOptions* options, const u8 producer[16],
                  u64* out_slots, u8 out_release[16]) {
    // VI2 Present: command 229.
    struct {
        u64 token;
        u8 slot;
        u8 pad[3];
        u32 image_id;
        Vi2PresentOptions options;
        u8 producer[16];
    } in = {
        .token = token,
        .slot = slot,
        .image_id = image_id,
        .options = *options,
    };
    _Static_assert(sizeof(in) == 0x60, "VI2 present input size");
    memcpy(in.producer, producer, sizeof(in.producer));

    struct {
        u64 slots;
        u8 release[16];
    } out;
    Result rc = tipcDispatchInOut(s, 229, in, out);
    if (R_SUCCEEDED(rc)) {
        if (out_slots) *out_slots = out.slots;
        if (out_release) memcpy(out_release, out.release, sizeof(out.release));
    }
    return rc;
}

static Result _vi2CmdUnregister(TipcService* s, u64 token, u8 slot, const Vi2PresentOptions* options,
                     u64* out_slots, u8 out_release[16]) {
    // VI2 Unregister: command 228.
    struct {
        u64 token;
        u8 slot;
        u8 pad[3];
        Vi2PresentOptions options;
    } NX_PACKED in = {
        .token = token,
        .slot = slot,
        .options = *options,
    };
    _Static_assert(sizeof(in) == 0x4c, "VI2 unregister input size");

    struct {
        u64 slots;
        u8 release[16];
    } out;
    Result rc = tipcDispatchInOut(s, 228, in, out);
    if (R_SUCCEEDED(rc)) {
        if (out_slots) *out_slots = out.slots;
        if (out_release) memcpy(out_release, out.release, sizeof(out.release));
    }
    return rc;
}

static Result _vi2CmdDetachLayer(TipcService* s, u64 token) {
    // VI2 DetachLayer: command 252.
    return tipcDispatchIn(s, 252, token);
}

static Result _vi2CmdDestroyLayer(TipcService* s, u64 token) {
    // VI2 DestroyLayer: command 202.
    return tipcDispatchIn(s, 202, token);
}

static Result _vi2CmdDrainNotifications(TipcService* s, void* destination, size_t size) {
    // VI2 DrainNotifications: command 21, one B receive descriptor.
    return tipcDispatch(s, 21,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
        .buffers = { { destination, size } },
    );
}

static Result _vi2CmdGetEventSlotMax(TipcService* s, u32* out) {
    // Command 3 returns the inclusive maximum kernel-event slot index, 7:
    // eight slots are available.
    u32 value = 0;
    Result rc = tipcDispatchOut(s, 3, value);
    if (R_SUCCEEDED(rc)) *out = value;
    return rc;
}

static Result _vi2CmdGetNotificationLimit(TipcService* s, u32* out) {
    // Command 1 returns the notification limit.
    u32 value = 0;
    Result rc = tipcDispatchOut(s, 1, value);
    if (R_SUCCEEDED(rc)) *out = value;
    return rc;
}

static Result _vi2CmdGetNotificationEvent(TipcService* s, u32 limit, Handle* out) {
    // Command 11 returns a MOVE event handle.
    return tipcDispatchIn(s, 11, limit,
        .out_handle_attrs = { SfOutHandleAttr_HipcMove },
        .out_handles = out,
    );
}

static Result _vi2CmdRegisterDisplay(TipcService* s, u64 name, u32 notification_index) {
    // Command 101 registers a display.
    const struct {
        u64 name;
        u32 notification_index;
    } NX_PACKED in = { name, notification_index };
    _Static_assert(sizeof(in) == 0x0c, "VI2 display registration input size");
    return tipcDispatchIn(s, 101, in);
}

static Result _vi2CmdUnregisterDisplay(TipcService* s, u64 name) {
    // Command 102 unregisters a display.
    return tipcDispatchIn(s, 102, name);
}

// Display-change events.

static Result _vi2CmdCreateEventSlot(TipcService* s, u32 slot, Handle* out) {
    // Command 31: VI2 creates a kernel event for the slot, keeps its
    // writable side and moves the readable side. VI2 creates a slot's event
    // once per session. A second request returns 0xa72.
    return tipcDispatchIn(s, 31, slot,
        .out_handle_attrs = { SfOutHandleAttr_HipcMove },
        .out_handles = out,
    );
}

// Register and unregister commands per event type: refresh 263/264, buffer
// maximum resolution 269/270, mode capability 271/272. VI2 supplies the
// listener type itself.
static const u32 g_vi2DisplayEventCommands[][2] = {
    [Vi2DisplayEventType_RefreshRate]         = { 263, 264 },
    [Vi2DisplayEventType_BufferMaxResolution] = { 269, 270 },
    [Vi2DisplayEventType_ModeCapability]      = { 271, 272 },
};

static Result _vi2CmdDisplayEvent(TipcService* s, u32 command, u64 display_name, u32 slot) {
    // Both directions take the packed display name and the slot index, 12
    // bytes, and return no payload or handles. Registering signals the
    // slot's event once. Unregistering always returns 0.
    const struct {
        u64 display_name;
        u32 slot;
    } NX_PACKED in = { display_name, slot };
    _Static_assert(sizeof(in) == 0x0c, "VI2 display event input size");
    return tipcDispatchIn(s, command, in);
}

static Result _vi2CmdQueryRefreshWord(TipcService* s, u64 display_name, u64* out) {
    // Command 354: VI2 reads the display's locked mode word, or
    // returns 1 when the display has no mapped physical mode.
    return tipcDispatchInOut(s, 354, display_name, *out);
}

static Result _vi2CmdQueryBufferMaxResolution(TipcService* s, u64 display_name, u64* out) {
    // Command 106: the display's mode state +0x40, or 1 with no
    // mapped mode.
    return tipcDispatchInOut(s, 106, display_name, *out);
}

static Result _vi2CmdQueryModeCapability(TipcService* s, u64 display_name,
                                         Vi2DisplayModeCapability* out) {
    // Command 107: mode state +0x48/+0x50/+0x58 in wire order.
    _Static_assert(sizeof(*out) == 0x18, "VI2 mode capability output size");
    return tipcDispatchInOut(s, 107, display_name, *out);
}

static Result _vi2CmdSetRefreshRateCapability(TipcService* s, u64 display_name,
                                              u64 values, u64 mask) {
    // Command 301: the masked bits of values replace the display's
    // application capability. VI2 counts the 120 Hz requests for its mode
    // selection.
    const struct {
        u64 display_name;
        u64 values;
        u64 mask;
    } in = { display_name, values, mask };
    _Static_assert(sizeof(in) == 0x18, "VI2 refresh capability input size");
    return tipcDispatchIn(s, 301, in);
}

static TipcService g_vi2Srv;
static Handle g_vi2Event = INVALID_HANDLE;
static bool g_vi2GsvInitialized;
static Mutex g_vi2Mutex;
static Mutex g_vi2DrainMutex;
static UEvent g_vi2Done[0x400];
static u32 g_vi2NotificationLimit;
static u32 g_vi2Allocated[32];
static u32 g_vi2Pending[32];
static u64 g_vi2NextToken = 1;

// VI2's display-change kernel events, a pool separate from the
// notification indices above. Slot 0 is valid. The pool creates each slot's
// readable handle on first use and keeps it for the session. Events borrow
// the handle.
#define VI2_EVENT_SLOTS 8
static u32 g_vi2EventSlotCount;
static u32 g_vi2EventSlotsInUse;
static Handle g_vi2EventSlotHandles[VI2_EVENT_SLOTS];

// The Result when the notification bitmap has no free bit, for either
// object kind.
#define VI2_NO_NOTIFICATION_SLOT MAKERESULT(114, 12)

// The Result for a display wire value that is not recognised.
#define VI2_UNKNOWN_DISPLAY_VALUE MAKERESULT(114, 10)

NX_GENERATE_SERVICE_GUARD(vi2);

Result _vi2Initialize(void) {
    memset(g_vi2Allocated, 0, sizeof(g_vi2Allocated));
    memset(g_vi2Pending, 0, sizeof(g_vi2Pending));
    // Notification index zero is reserved.
    g_vi2Allocated[0] = 1;
    g_vi2Event = INVALID_HANDLE;
    g_vi2EventSlotsInUse = 0;
    for (u32 i = 0; i < VI2_EVENT_SLOTS; i++)
        g_vi2EventSlotHandles[i] = INVALID_HANDLE;
    mutexInit(&g_vi2Mutex);
    mutexInit(&g_vi2DrainMutex);

    Result rc = gsvInitialize();
    if (R_FAILED(rc)) return rc;
    g_vi2GsvInitialized = true;

    rc = gsvOpenVi2(&g_vi2Srv);
    if (R_FAILED(rc)) return rc;

    // Query command 3, then command 1. Clamp the latter to 0x3ff and get the
    // readable event with command 11.
    u32 event_slot_max = 0;
    rc = _vi2CmdGetEventSlotMax(&g_vi2Srv, &event_slot_max);
    if (R_FAILED(rc)) return rc;
    g_vi2EventSlotCount = event_slot_max < VI2_EVENT_SLOTS ? event_slot_max + 1 : VI2_EVENT_SLOTS;
    u32 notification_limit = 0;
    rc = _vi2CmdGetNotificationLimit(&g_vi2Srv, &notification_limit);
    if (R_FAILED(rc)) return rc;
    g_vi2NotificationLimit = notification_limit < 0x3ff ? notification_limit : 0x3ff;
    rc = _vi2CmdGetNotificationEvent(&g_vi2Srv, g_vi2NotificationLimit, &g_vi2Event);
    return rc;
}

static void _vi2Cleanup(void) {
    // Close the child before releasing its event reference.
    if (g_vi2Srv.session) {
        gsvCloseVi2(&g_vi2Srv);
        if (g_vi2Event != INVALID_HANDLE)
            svcSleepThread(100000000);
    }
    if (g_vi2Event != INVALID_HANDLE) {
        svcCloseHandle(g_vi2Event);
        g_vi2Event = INVALID_HANDLE;
    }
    for (u32 i = 0; i < VI2_EVENT_SLOTS; i++) {
        if (g_vi2EventSlotHandles[i] != INVALID_HANDLE) {
            svcCloseHandle(g_vi2EventSlotHandles[i]);
            g_vi2EventSlotHandles[i] = INVALID_HANDLE;
        }
    }
    g_vi2EventSlotsInUse = 0;
    if (g_vi2GsvInitialized) {
        gsvExit();
        g_vi2GsvInitialized = false;
    }
}

TipcService* vi2GetServiceSession(void) {
    return &g_vi2Srv;
}

static u64 _vi2PackName(const char* name) {
    // At most eight bytes, stopping at NUL.
    u64 packed = 0;
    if (name)
        for (u32 i = 0; i < 8 && name[i]; i++)
            packed |= (u64)(u8)name[i] << (8 * i);
    return packed;
}

static u32 _vi2AllocateIndex(void) {
    mutexLock(&g_vi2Mutex);
    for (u32 index = 1; index <= g_vi2NotificationLimit; index++) {
        u32 word = index >> 5;
        u32 bit = 1u << (index & 31);
        if (!(g_vi2Allocated[word] & bit)) {
            g_vi2Allocated[word] |= bit;
            ueventCreate(&g_vi2Done[index], false);
            mutexUnlock(&g_vi2Mutex);
            return index;
        }
    }
    mutexUnlock(&g_vi2Mutex);
    return 0;
}

static void _vi2ReleaseIndex(u32 index) {
    mutexLock(&g_vi2Mutex);
    u32 word = index >> 5;
    u32 bit = 1u << (index & 31);
    g_vi2Allocated[word] &= ~bit;
    g_vi2Pending[word] &= ~bit;
    mutexUnlock(&g_vi2Mutex);
}

static Result _vi2DrainSignaled(void) {
    // Clear the event before command 21. VI2 copies and clears the entire
    // 0x80-byte bitmap.
    Result rc = svcClearEvent(g_vi2Event);
    if (R_FAILED(rc)) return rc;
    u32 snapshot[32] = {0};
    rc = _vi2CmdDrainNotifications(&g_vi2Srv, snapshot, sizeof(snapshot));
    if (R_FAILED(rc)) return rc;

    mutexLock(&g_vi2Mutex);
    for (u32 i = 0; i < 32; i++) {
        g_vi2Pending[i] |= snapshot[i];
        u32 bits = snapshot[i] & g_vi2Allocated[i];
        while (bits) {
            u32 bit = __builtin_ctz(bits);
            ueventSignal(&g_vi2Done[i * 32 + bit]);
            bits &= bits - 1;
        }
    }
    mutexUnlock(&g_vi2Mutex);
    return 0;
}

static Result _vi2Collect(void) {
    // Clear the shared event and drain its bitmap before completing the
    // per-index local waiters. No long wait occurs under this mutex.
    mutexLock(&g_vi2DrainMutex);
    Result rc = svcWaitSynchronizationSingle(g_vi2Event, 0);
    if (R_VALUE(rc) == KERNELRESULT(TimedOut))
        rc = 0;
    else if (R_SUCCEEDED(rc))
        rc = _vi2DrainSignaled();

    mutexUnlock(&g_vi2DrainMutex);
    return rc;
}

static Result _vi2PrepareIndex(u32 index) {
    // Consume an already-signaled notification before registration with
    // command 101. The same preparation applies to recycled indices.
    Result rc = _vi2Collect();
    if (R_FAILED(rc)) return rc;
    mutexLock(&g_vi2Mutex);
    g_vi2Pending[index >> 5] &= ~(1u << (index & 31));
    ueventClear(&g_vi2Done[index]);
    mutexUnlock(&g_vi2Mutex);
    return 0;
}

static Result _vi2WaitIndex(u32 index) {
    for (;;) {
        mutexLock(&g_vi2Mutex);
        u32 word = index >> 5;
        u32 bit = 1u << (index & 31);
        if (g_vi2Pending[word] & bit) {
            g_vi2Pending[word] &= ~bit;
            mutexUnlock(&g_vi2Mutex);
            return 0;
        }
        mutexUnlock(&g_vi2Mutex);
        s32 signaled = -1;
        Result rc = waitMulti(&signaled, UINT64_MAX,
                              waiterForHandle(g_vi2Event),
                              waiterForUEvent(&g_vi2Done[index]));
        if (R_FAILED(rc)) return rc;
        if (signaled == 0) {
            rc = _vi2Collect();
            if (R_FAILED(rc)) return rc;
        }
    }
}

Result vi2OpenDisplay(Vi2Display* out, const char* name) {
    *out = (Vi2Display){};
    u32 index = _vi2AllocateIndex();
    if (!index) return VI2_NO_NOTIFICATION_SLOT;
    const u64 packed = _vi2PackName(name);
    Result rc = _vi2PrepareIndex(index);
    if (R_FAILED(rc)) goto fail;
    rc = _vi2CmdRegisterDisplay(&g_vi2Srv, packed, index);
    if (R_FAILED(rc)) goto fail;
    *out = (Vi2Display){ .name = packed, .notification_index = index };
    return 0;

fail:
    _vi2ReleaseIndex(index);
    return rc;
}

Result vi2OpenDefaultDisplay(Vi2Display* out) {
    return vi2OpenDisplay(out, "default");
}

Result vi2CloseDisplay(Vi2Display* display) {
    // Command 102, then the display's own index acknowledgement.
    Result rc = _vi2CmdUnregisterDisplay(&g_vi2Srv, display->name);
    if (R_FAILED(rc)) return rc;
    rc = _vi2WaitIndex(display->notification_index);
    if (R_SUCCEEDED(rc)) {
        _vi2ReleaseIndex(display->notification_index);
        *display = (Vi2Display){};
    }
    return rc;
}

void vi2CreateEndpoint(Vi2Endpoint* out, Vi2Display* display, const char* name) {
    // Endpoint creation is local bookkeeping.
    *out = (Vi2Endpoint){ .display = display, .name = _vi2PackName(name) };
}

void vi2CloseEndpoint(Vi2Endpoint* endpoint) {
    // No remote destroy command.
    *endpoint = (Vi2Endpoint){};
}

Result vi2CreateLayer(Vi2Layer* out) {
    *out = (Vi2Layer){};
    u32 destroy_index = _vi2AllocateIndex();
    if (!destroy_index) return VI2_NO_NOTIFICATION_SLOT;
    Result rc = VI2_NO_NOTIFICATION_SLOT;
    u32 availability_index = _vi2AllocateIndex();
    if (!availability_index) goto fail_destroy_index;
    rc = _vi2PrepareIndex(destroy_index);
    if (R_FAILED(rc)) goto fail_indices;
    rc = _vi2PrepareIndex(availability_index);
    if (R_FAILED(rc)) goto fail_indices;

    mutexLock(&g_vi2Mutex);
    const u64 token = g_vi2NextToken++;
    mutexUnlock(&g_vi2Mutex);
    rc = _vi2CmdCreateLayer(&g_vi2Srv, token, destroy_index, availability_index);
    if (R_FAILED(rc)) goto fail_indices;
    *out = (Vi2Layer){ .token = token, .destroy_notification = destroy_index,
                        .availability_notification = availability_index };
    return 0;

fail_indices:
    _vi2ReleaseIndex(availability_index);
fail_destroy_index:
    _vi2ReleaseIndex(destroy_index);
    return rc;
}

Result vi2AttachLayer(Vi2Layer* layer, Vi2Display* display) {
    return _vi2CmdAttachLayer(&g_vi2Srv, layer->token, display->name);
}

Result vi2SelectLayer(Vi2Layer* layer, Vi2Endpoint* endpoint) {
    return _vi2CmdSelectLayer(&g_vi2Srv, endpoint->display->name, endpoint->name, layer->token);
}

Result vi2ClearLayerSelection(Vi2Endpoint* endpoint) {
    return _vi2CmdClearLayerSelection(&g_vi2Srv, endpoint->display->name, endpoint->name);
}

Result vi2GetLayerAvailability(Vi2Layer* layer, u64* out_slots) {
    return _vi2CmdGetAvailability(&g_vi2Srv, layer->token, out_slots);
}

Result vi2PresentLayer(Vi2Layer* layer, u8 slot, u32 image_id,
                       const Vi2PresentOptions* options, const Vi2Dependency* producer,
                       u64* out_slots, Vi2Dependency* out_release) {
    return _vi2CmdPresent(&g_vi2Srv, layer->token, slot, image_id, options,
                          producer->data, out_slots, out_release ? out_release->data : NULL);
}

Result vi2UnregisterLayerImage(Vi2Layer* layer, u8 slot,
                                const Vi2PresentOptions* options,
                                u64* out_slots, Vi2Dependency* out_release) {
    return _vi2CmdUnregister(&g_vi2Srv, layer->token, slot, options,
                             out_slots, out_release ? out_release->data : NULL);
}

Result vi2DetachLayer(Vi2Layer* layer) {
    return _vi2CmdDetachLayer(&g_vi2Srv, layer->token);
}

Result vi2CloseLayer(Vi2Layer* layer) {
    // The first creation notification follows token removal, unlike the
    // command 202 reply.
    Result rc = _vi2CmdDestroyLayer(&g_vi2Srv, layer->token);
    if (R_FAILED(rc)) return rc;
    rc = _vi2WaitIndex(layer->destroy_notification);
    if (R_SUCCEEDED(rc)) {
        _vi2ReleaseIndex(layer->availability_notification);
        _vi2ReleaseIndex(layer->destroy_notification);
        *layer = (Vi2Layer){};
    }
    return rc;
}

// Layer availability: VI2 posts the layer's second notification index when
// it reclaims an image slot record. An acquire consumes it before each
// availability query and, with no free slot, waits for it. A drain clears
// the shared event, then copies the bitmap with command 21.
static bool _vi2ConsumePending(u32 index) {
    mutexLock(&g_vi2Mutex);
    u32 word = index >> 5;
    u32 bit = 1u << (index & 31);
    bool pending = (g_vi2Pending[word] & bit) != 0;
    if (pending) {
        g_vi2Pending[word] &= ~bit;
        ueventClear(&g_vi2Done[index]);
    }
    mutexUnlock(&g_vi2Mutex);
    return pending;
}

Result vi2WaitLayerAvailability(Vi2Layer* layer, u64 timeout_ns) {
    const u32 index = layer->availability_notification;
    Result rc = _vi2Collect();
    if (R_FAILED(rc)) return rc;

    const u64 start = svcGetSystemTick();
    for (;;) {
        if (_vi2ConsumePending(index))
            return 0;

        const u64 elapsed_ns = armTicksToNs(svcGetSystemTick() - start);
        if (elapsed_ns >= timeout_ns)
            return KERNELRESULT(TimedOut);

        s32 signaled = -1;
        rc = waitMulti(&signaled, timeout_ns - elapsed_ns,
                       waiterForHandle(g_vi2Event),
                       waiterForUEvent(&g_vi2Done[index]));
        if (R_VALUE(rc) == KERNELRESULT(TimedOut))
            return rc;
        if (R_FAILED(rc))
            return rc;
        if (signaled == 0) {
            rc = _vi2Collect();
            if (R_FAILED(rc)) return rc;
        }
    }
}

// Display-change events and queries. Each requires the display to be
// registered in this session.

static Result _vi2AcquireEventSlot(u32* out_slot, Handle* out_handle) {
    mutexLock(&g_vi2Mutex);
    u32 slot = 0;
    while (slot < g_vi2EventSlotCount && (g_vi2EventSlotsInUse & (1u << slot)))
        slot++;
    if (slot == g_vi2EventSlotCount) {
        mutexUnlock(&g_vi2Mutex);
        return VI2_NO_NOTIFICATION_SLOT;
    }
    g_vi2EventSlotsInUse |= 1u << slot;
    Handle handle = g_vi2EventSlotHandles[slot];
    mutexUnlock(&g_vi2Mutex);

    if (handle == INVALID_HANDLE) {
        Result rc = _vi2CmdCreateEventSlot(&g_vi2Srv, slot, &handle);
        mutexLock(&g_vi2Mutex);
        if (R_SUCCEEDED(rc))
            g_vi2EventSlotHandles[slot] = handle;
        else
            g_vi2EventSlotsInUse &= ~(1u << slot);
        mutexUnlock(&g_vi2Mutex);
        if (R_FAILED(rc)) return rc;
    }

    *out_slot = slot;
    *out_handle = handle;
    return 0;
}

static void _vi2ReleaseEventSlot(u32 slot) {
    mutexLock(&g_vi2Mutex);
    g_vi2EventSlotsInUse &= ~(1u << slot);
    mutexUnlock(&g_vi2Mutex);
}

Result vi2CreateDisplayEvent(Vi2DisplayEvent* out, Vi2Display* display,
                             Vi2DisplayEventType type, bool autoclear) {
    // The event attaches the pooled handle without owning it.
    *out = (Vi2DisplayEvent){};
    if (type > Vi2DisplayEventType_ModeCapability)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    u32 slot = 0;
    Handle handle = INVALID_HANDLE;
    Result rc = _vi2AcquireEventSlot(&slot, &handle);
    if (R_FAILED(rc)) return rc;

    Event event;
    eventLoadRemote(&event, handle, autoclear);
    eventClear(&event); // An earlier unregister of this slot signals it.

    rc = _vi2CmdDisplayEvent(&g_vi2Srv, g_vi2DisplayEventCommands[type][0], display->name, slot);
    if (R_FAILED(rc)) {
        _vi2ReleaseEventSlot(slot);
        return rc;
    }
    *out = (Vi2DisplayEvent){
        .event = event,
        .display_name = display->name,
        .slot = slot,
        .type = type,
    };
    return 0;
}

Result vi2DestroyDisplayEvent(Vi2DisplayEvent* event) {
    // Unregister and free the slot. The pool keeps the readable handle.
    Result rc = _vi2CmdDisplayEvent(&g_vi2Srv, g_vi2DisplayEventCommands[event->type][1],
                                    event->display_name, event->slot);
    if (R_FAILED(rc)) return rc;
    _vi2ReleaseEventSlot(event->slot);
    *event = (Vi2DisplayEvent){};
    return 0;
}

Result vi2QueryDisplayRefreshRate(Vi2Display* display, u32* out_hz, bool* out_vrr) {
    // Bit 63 first, then bit 0 is 60 Hz and bit 1 is 120 Hz, bit 0
    // winning.
    u64 word = 0;
    Result rc = _vi2CmdQueryRefreshWord(&g_vi2Srv, display->name, &word);
    if (R_FAILED(rc)) return rc;
    if (out_vrr) *out_vrr = (word >> 63) & 1;
    if (word & BIT(0)) {
        if (out_hz) *out_hz = 60;
    } else if (word & BIT(1)) {
        if (out_hz) *out_hz = 120;
    } else {
        return VI2_UNKNOWN_DISPLAY_VALUE;
    }
    return 0;
}

Result vi2QueryDisplayBufferMaxResolution(Vi2Display* display, u32* out_level) {
    // Exactly 1, 2, 4 or 8, mapped to levels 0-3.
    u64 code = 0;
    Result rc = _vi2CmdQueryBufferMaxResolution(&g_vi2Srv, display->name, &code);
    if (R_FAILED(rc)) return rc;
    switch (code) {
    case 1: *out_level = 0; return 0;
    case 2: *out_level = 1; return 0;
    case 4: *out_level = 2; return 0;
    case 8: *out_level = 3; return 0;
    default: return VI2_UNKNOWN_DISPLAY_VALUE;
    }
}

Result vi2QueryDisplayModeCapability(Vi2Display* display, Vi2DisplayModeCapability* out) {
    Vi2DisplayModeCapability capability = {0};
    Result rc = _vi2CmdQueryModeCapability(&g_vi2Srv, display->name, &capability);
    if (R_SUCCEEDED(rc)) *out = capability;
    return rc;
}

// The 120 Hz capability is bit 1 (values/mask 2/2 or 0/2). VRR is bit 63.
#define VI2_CAPABILITY_120HZ BITL(1)
#define VI2_CAPABILITY_VRR BITL(63)

Result vi2SetDisplay120HzCapability(Vi2Display* display, bool enable) {
    return _vi2CmdSetRefreshRateCapability(&g_vi2Srv, display->name,
                                           enable ? VI2_CAPABILITY_120HZ : 0,
                                           VI2_CAPABILITY_120HZ);
}

Result vi2SetDisplayVrrCapability(Vi2Display* display, bool enable) {
    return _vi2CmdSetRefreshRateCapability(&g_vi2Srv, display->name,
                                           enable ? VI2_CAPABILITY_VRR : 0,
                                           VI2_CAPABILITY_VRR);
}
