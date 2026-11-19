/**
 * @file gsv.h
 * @brief Native Switch 2 graphics-supervisor (GSV1) root and its child sessions.
 * @author kurumuz
 * @copyright 2026 libounce contributors
 * @license ISC
 */
#pragma once
#if !defined(__SWITCH2__)
#error "GSV1 is available only in a native Switch 2 build"
#endif

#include "../../switch/types.h"
#include "../../switch/sf/tipc.h"
#include "../../switch/kernel/event.h"

/// GSV sync session from \ref gsvOpenSync, which reads GPU producer counters and provides completion events.
typedef struct {
    TipcService s;      ///< Sync session. This reference does not own the handle.
    u32 capacity;       ///< Capacity that the sync session reports when it opens.
    u32 generation;     ///< Which opening of the process's session this reference belongs to.
} GsvSync;

/// GPU sessions created through the GSV1 root by \ref gsvOpenGpu: the main session and the GPU worker it provides.
typedef struct {
    TipcService main;   ///< Main session.
    TipcService worker; ///< GPU worker session, after \ref gsvGpuOpenWorker.
} GsvGpu;

/// An inclusive GPU virtual-address range.
typedef struct {
    u64 first;
    u64 last;
} GsvGpuVaRange;

/// A GPU mapping of part of a registered CPU range, for \ref gsvGpuMap.
typedef struct {
    u32 vm_id;              ///< VM from \ref gsvGpuCreateVm.
    u64 va;                 ///< GPU virtual address of the mapping (4 KiB aligned).
    u64 size;               ///< Mapping size in bytes (4 KiB aligned).
    u32 range_id;           ///< Registered CPU range from \ref gsvGpuRegisterMemory.
    u64 range_offset;       ///< Offset into the registered range.
    u8 kind;                ///< Page kind.
    u8 fallback_kind;       ///< Kind used when \ref kind cannot be selected.
    bool gpu_cacheable;     ///< Map GPU-cacheable (true) or GPU-uncached (false).
    u8 client;              ///< Client from \ref gsvGpuRegisterClient.
} GsvGpuMapping;

/// 64-byte native image layout. Use \ref gsvMakeImageLayout.
typedef struct {
    u64 version;                ///< 0x100000000.
    u32 size;                   ///< (height - 1) << 16 | (width - 1), each truncated to 16 bits.
    u32 reserved_xc;
    u8 block_height_log2;       ///< Block height exponent.
    u8 reserved_x11[3];
    u32 pitch_gobs;             ///< Pitch in GOBs.
    u32 pixel_format;           ///< Pixel format.
    u8 block_format;            ///< Block format.
    u16 luminance;              ///< Luminance value.
    u8 reserved_x1f[33];
} NX_PACKED GsvImageLayout;

/**
 * @brief A 16-byte native synchronization value: a producer dependency or a consumer release.
 * @note The u32 at +0 is the complemented producer ID. The byte at +4 selects the
 *       comparison: 0 compares the low 32 bits with wrapping, 1 compares all 64 bits.
 *       The u64 at +8 is the counter target. All zero means that there is no dependency.
 */
typedef struct GsvNativeSync {
    u8 data[16];
} GsvNativeSync;

/// 64-byte decoder image preference. Use \ref viddecMakeImagePreference.
typedef struct {
    u64 format;             ///< Output format and tiling selector, in the upper 32 bits.
    u32 size;               ///< (height - 1) << 16 | (width - 1), each truncated to 16 bits.
    u32 reserved_xc;
    u8 reserved_x10[0x30];
} GsvDecodeImagePreference;

/// Type tag of a \ref GsvCpuImageDescriptor.
typedef enum {
    GsvCpuImageType_Nv12PitchLinear    = 0x0a, ///< Two planes, pitch-linear.
    GsvCpuImageType_Nv12BlockLinear    = 0x0d, ///< Two planes, block-linear.
    GsvCpuImageType_Nv12PitchLinearAlt = 0x14, ///< Two planes, pitch-linear; same geometry as 0x0a.
    GsvCpuImageType_Nv12BlockLinearAlt = 0x15, ///< Two planes, block-linear; its plane use is not known.
} GsvCpuImageType;

/// One plane of a \ref GsvCpuImageDescriptor.
typedef struct {
    u32 offset;             ///< Offset of the plane from the start of the pool's CPU backing.
    u32 span;               ///< Bytes that the plane occupies.
    union {
        u64 pitch;          ///< Pitch-linear types: bytes from one row to the next.
        u16 block[4];       ///< Block-linear types: blocks across, block rows, block width in elements, block height in rows.
    };
} GsvCpuImagePlane;

/// 128-byte CPU image descriptor from \ref gsvGetCpuImageDescriptor.
typedef struct {
    u32 type;                   ///< \ref GsvCpuImageType.
    u16 width_minus_one;        ///< Image width minus one.
    u16 height_minus_one;       ///< Image height minus one.
    GsvCpuImagePlane planes[2]; ///< Luma, then interleaved chroma.
    u8 reserved_x28[0x58];
} GsvCpuImageDescriptor;

/// A CPU memory range registered as native memory by \ref gsvCreateNativeMemory.
typedef struct {
    u32 memory_id;          ///< Native memory ID.
    void* address;          ///< CPU address.
    u64 size;               ///< Size in bytes.
    bool has_attribute;     ///< Whether SVC 0x80 attribute 0x20 was set on the range.
} GsvNativeMemory;

/**
 * @brief Initializes the native game's AM-delegated GSV1 root.
 * @note Performs the root bootstrap: Initialize, claim process, probe,
 *       query mode, enable (mode 1 only), image init and image finish.
 */
Result gsvInitialize(void);

/// Releases the root. Close all child sessions before you call this.
void gsvExit(void);

/// Gets the borrowed GSV1 root session. The caller must not close it.
TipcService* gsvGetServiceSession(void);

/// Opens the native VI2 child session from the retained GSV1 root.
Result gsvOpenVi2(TipcService* out);

/// Closes the VI2 child, then releases the root's VI2 association.
/// @note The GSV1 root must remain initialized until this call returns.
Result gsvCloseVi2(TipcService* s);

/**
 * @brief Creates the delegated nvservices session from the root.
 * @param[out] out_session Session handle. The caller owns it.
 * @note The nv service uses this on Switch 2 (see \ref nvInitialize).
 */
Result gsvOpenNvServices(Handle* out_session);

/**
 * @brief Registers the frame mode, then sets up frame registration on the root.
 * @param[in] mode Requested mode (a game's startup uses 2).
 * @param[out] out_permitted_mode Mode value that the root reports. Can be NULL.
 * @note Send this after opening the sync session and before the GPU sessions.
 * @note \ref gsvCreateNativeMemory uses the permitted mode.
 */
Result gsvRegisterFrame(u64 mode, u64* out_permitted_mode);

/// Registers a VM created by \ref gsvGpuCreateVm with the root.
Result gsvRegisterVm(u32 vm_id);

///@name Native images (scanout)
///@{

/// Fills an image layout descriptor.
void gsvMakeImageLayout(GsvImageLayout* out, u32 width, u32 height, u32 block_height_log2,
                        u32 pitch_gobs, u32 pixel_format, u8 block_format, u16 luminance);

/// Gets the memory size an image with this layout needs.
Result gsvGetImageSize(const GsvImageLayout* layout, u64* out_size);

/**
 * @brief Reserves a native image memory ID.
 * @param[in] type Memory type.
 * @param[in] size Memory size.
 * @param[out] out_memory_id Memory ID (distinct from a worker range ID). Release with \ref gsvReleaseImageMemory.
 */
Result gsvReserveImageMemory(u8 type, u64 size, u32* out_memory_id);

/**
 * @brief Registers the backing of a reserved image memory ID.
 * @param[in] gpu_memory_handle GPU memory handle of the backing.
 * @param[in] offset Offset of the image in that GPU memory.
 */
Result gsvRegisterImageMemory(u32 memory_id, u64 cpu_address, u64 size, u32 gpu_memory_handle, u64 offset);

/// Creates a native image in registered memory. Destroy it with \ref gsvDestroyImage.
Result gsvCreateImage(const GsvImageLayout* layout, u32 memory_id, u32* out_image_id);

/// Creates display aliases for an image. Destroy them with \ref gsvDestroyDisplayAlias.
Result gsvCreateDisplayAlias(u32 image_id, u32 count);

/// Destroys an image's display aliases.
Result gsvDestroyDisplayAlias(u32 image_id);

/// Destroys a native image.
Result gsvDestroyImage(u32 image_id);

/// Releases a native image memory ID.
Result gsvReleaseImageMemory(u32 memory_id);

/**
 * @brief Registers CPU memory as native memory, for a decoder image pool.
 * @param[out] out Native memory. Destroy it with \ref gsvDestroyNativeMemory.
 * @param[in] address Page-aligned CPU address. The memory must stay allocated until it is destroyed.
 * @param[in] size Page-aligned size, 0x1000 to 0x10000000000 bytes.
 * @note When the mode that \ref gsvRegisterFrame permitted has bit 1 set, this
 *       first sets SVC 0x80 attribute 0x20 on the range, and the registration
 *       tells the service. The meaning of the attribute is not known.
 */
Result gsvCreateNativeMemory(GsvNativeMemory* out, void* address, u64 size);

/// Releases native memory, then clears the attribute that \ref gsvCreateNativeMemory set.
void gsvDestroyNativeMemory(GsvNativeMemory* m);

///@}

///@name Video decoding (root)
///@{

/// Checks that the root permits video decoding.
Result gsvCheckVideoDecoderAccess(void);

/**
 * @brief Opens the video decoder creator session.
 * @param[in] resolution_unlimited Resolution-unlimited mode flag (false by default).
 * @param[in] version Library version (0x0000000300030000).
 * @param[out] out Creator session. Close it with tipcClose, then call \ref gsvReleaseVideoDecoderAccess.
 * @warning Open the creator only once for each root initialization.
 */
Result gsvOpenVideoDecoderCreator(bool resolution_unlimited, u64 version, TipcService* out);

/// Releases the root's video decoder state. Close the creator first.
Result gsvReleaseVideoDecoderAccess(void);

/**
 * @brief Gets the size and alignment of a decoder image pool.
 * @param[in] preference Image preference.
 * @param[in] count Number of images.
 * @param[out] out_size Size that the service reports.
 * @param[out] out_alignment Alignment that the service reports.
 * @note The values are not rounded. See \ref viddecGetImagePoolSize.
 * @note Open the creator with \ref gsvOpenVideoDecoderCreator first.
 */
Result gsvGetDecodeImagePoolSize(const GsvDecodeImagePreference* preference, u32 count, u64* out_size, u64* out_alignment);

/**
 * @brief Creates decoder images in native memory.
 * @param[in] preference Image preference.
 * @param[in] count Number of images.
 * @param[in] memory_id Native memory from \ref gsvCreateNativeMemory.
 * @param[out] out_base_image_id ID of the first image. Image i has ID base + i.
 */
Result gsvCreateDecodeImagePool(const GsvDecodeImagePreference* preference, u32 count, u32 memory_id, u32* out_base_image_id);

/// Destroys the decoder images that \ref gsvCreateDecodeImagePool created.
Result gsvDestroyDecodeImagePool(u32 base_image_id);

/**
 * @brief Gets the CPU descriptor of a decoder image.
 * @param[in] image_id Image ID (base + index).
 * @param[out] out Descriptor. Offsets are relative to the pool's CPU backing.
 */
Result gsvGetCpuImageDescriptor(u32 image_id, GsvCpuImageDescriptor* out);

///@}

///@name Sync session
///@{

/**
 * @brief Opens the process's sync session.
 * @param[out] out Sync session. Close it with \ref gsvSyncClose.
 * @note A process can have one sync session: the root refuses a second one
 *       while the first is open. All callers share that session. The first
 *       call creates and configures it; later calls give a reference to it.
 *       The session closes when the last reference closes.
 * @note Calls on the session are thread-safe. Event slots are not shared:
 *       get each one from \ref gsvSyncAllocateSlot.
 */
Result gsvOpenSync(GsvSync* out);

/// Closes a reference to the process's sync session. The last close closes the session.
void gsvSyncClose(GsvSync* s);

/**
 * @brief Allocates an event slot of the process's sync session.
 * @param[out] out_slot Slot, for \ref gsvSyncCreateEvent, \ref gsvSyncArmEvent and
 *                      \ref gsvSyncDisarmEvent. Free it with \ref gsvSyncFreeSlot.
 * @note Gives LibnxError_OutOfMemory when every slot is in use.
 */
Result gsvSyncAllocateSlot(GsvSync* s, u32* out_slot);

/**
 * @brief Disarms an event slot and makes it available again.
 * @param[in] slot Slot from \ref gsvSyncAllocateSlot.
 * @note Close the slot's event (\ref gsvSyncCreateEvent) yourself.
 */
void gsvSyncFreeSlot(GsvSync* s, u32 slot);

/**
 * @brief Reads a GPU producer counter.
 * @param[in] producer_id Producer ID.
 * @param[out] out_value Counter value. UINT64_MAX means that the producer is not valid.
 * @param[out] out_mode Counter mode: 0 compares the low 32 bits with wrapping,
 *                      1 compares the full 64-bit value. May be NULL.
 */
Result gsvSyncReadCounter(GsvSync* s, u32 producer_id, u64* out_value, u8* out_mode);

/**
 * @brief Creates the completion event for an event slot.
 * @param[in] slot Event slot.
 * @param[out] out_event Event. The caller owns it.
 * @param[in] autoclear Event autoclear flag.
 */
Result gsvSyncCreateEvent(GsvSync* s, u32 slot, Event* out_event, bool autoclear);

/**
 * @brief Arms an event slot to signal when a producer counter reaches a target.
 * @param[in] slot Event slot created by \ref gsvSyncCreateEvent.
 * @param[in] producer_id Producer ID.
 * @param[in] target Counter target.
 * @param[in] mode Arm mode.
 */
Result gsvSyncArmEvent(GsvSync* s, u32 slot, u32 producer_id, u64 target, u8 mode);

/// Disarms an event slot.
Result gsvSyncDisarmEvent(GsvSync* s, u32 slot);

/**
 * @brief Checks whether a native sync value is reached.
 * @param[in] sync Native sync value.
 * @param[out] out_reached True when the counter reached the target, the value is empty,
 *                         or the producer is not valid.
 * @note A comparison byte other than 0 or 1 gives LibnxError_BadInput.
 */
Result gsvSyncCheckNative(GsvSync* s, const GsvNativeSync* sync, bool* out_reached);

/**
 * @brief Waits until a native sync value is reached.
 * @param[in] sync Native sync value.
 * @param[in] timeout_ns Maximum wait. After it, the function returns KERNELRESULT(TimedOut).
 * @note This function reads the counter at intervals with \ref gsvSyncReadCounter.
 */
Result gsvSyncWaitNative(GsvSync* s, const GsvNativeSync* sync, u64 timeout_ns);

///@}

///@name GPU main and worker sessions
///@{

/**
 * @brief Creates the GPU main session from the root.
 * @param[out] out GPU sessions. Open the worker separately. Close with \ref gsvGpuClose.
 */
Result gsvOpenGpu(GsvGpu* out);

/// Closes the worker session, if opened, then the main session.
void gsvGpuClose(GsvGpu* g);

/// Gets the inclusive range of slot indices the main session accepts.
Result gsvGpuGetSlotRange(GsvGpu* g, u32* out_first, u32* out_last);

/**
 * @brief Creates the completion event for a main-session slot.
 * @param[in] slot Slot index.
 * @param[out] out_event Event. The caller owns it.
 * @param[in] autoclear Event autoclear flag.
 * @note Worker acquisition, mapping and range retirement signal their slot's event.
 */
Result gsvGpuCreateSlotEvent(GsvGpu* g, u32 slot, Event* out_event, bool autoclear);

/**
 * @brief Requests the GPU worker session, waits for it and receives it.
 * @param[in] slot Slot. Create its event with \ref gsvGpuCreateSlotEvent first.
 * @param[in] slot_event That slot's event.
 */
Result gsvGpuOpenWorker(GsvGpu* g, u32 slot, Event* slot_event);

/**
 * @brief Registers a GPU client.
 * @param[in] name Client name, packed little-endian into 8 bytes.
 * @param[out] out_client Client ID.
 */
Result gsvGpuRegisterClient(GsvGpu* g, u64 name, u8* out_client);

/// Releases a GPU client.
Result gsvGpuReleaseClient(GsvGpu* g, u8 client);

/// Creates a GPU VM.
Result gsvGpuCreateVm(GsvGpu* g, u32* out_vm_id);

/**
 * @brief Gets a VM's user virtual-address ranges.
 * @param[out] out_ranges Up to 4 ranges.
 * @param[out] out_count Number of valid ranges.
 */
Result gsvGpuGetVmUserRanges(GsvGpu* g, u32 vm_id, GsvGpuVaRange out_ranges[4], u32* out_count);

/// Gets the VM range reserved for GSV.
Result gsvGpuGetVmGsvRange(GsvGpu* g, u32 vm_id, GsvGpuVaRange* out_range);

/**
 * @brief Registers a CPU memory range with the GPU worker.
 * @param[in] address Page-aligned CPU address.
 * @param[in] size Page-aligned size.
 * @param[in] client Client from \ref gsvGpuRegisterClient.
 * @param[out] out_range_id Range ID.
 */
Result gsvGpuRegisterMemory(GsvGpu* g, u64 address, u64 size, u8 client, u32* out_range_id);

/**
 * @brief Starts unregistering a CPU memory range.
 * @param[in] slot Slot whose event signals completion.
 */
Result gsvGpuUnregisterMemory(GsvGpu* g, u32 range_id, u32 slot);

/**
 * @brief Maps part of a registered range into a VM.
 * @param[in] slot Slot whose event signals completion.
 * @param[out] out_kind Selected page kind. Can be NULL.
 */
Result gsvGpuMap(GsvGpu* g, const GsvGpuMapping* mapping, u32 slot, u8* out_kind);

/**
 * @brief Changes the page kind of part of an existing mapping.
 * @param[in] slot Slot whose event signals completion.
 * @param[out] out_kind Selected page kind. Can be NULL.
 */
Result gsvGpuChangeKind(GsvGpu* g, u32 vm_id, u64 va, u64 size, u8 kind, u32 slot, u8* out_kind);

/**
 * @brief Unmaps a VM range.
 * @param[in] slot Slot whose event signals completion.
 */
Result gsvGpuUnmap(GsvGpu* g, u32 vm_id, u64 va, u64 size, u32 slot);

///@}
