/**
 * @file vi2.h
 * @brief Native Switch 2 VI2 display service commands.
 * @author kurumuz
 * @copyright 2026 libounce contributors
 * @license ISC
 * @note Command layouts adapted from libounce source/display/vi2_native.c at
 *       927cb2ca. See LICENSES/libounce-ISC.txt.
 */
#pragma once
#if !defined(__SWITCH2__)
#error "VI2 is available only in a native Switch 2 build"
#endif

#include "../../switch/types.h"
#include "../../switch/sf/tipc.h"
#include "../../switch/kernel/event.h"
#include "gsv.h"

/**
 * @brief VI2 present options.
 * @note Crop edges are inclusive source-image coordinates. Fields without a
 * verified meaning retain their wire offsets as names. The metadata tail is
 * active service input, not free application scratch or padding.
 */
typedef struct Vi2PresentOptions {
    u16 crop_left;
    u16 crop_top;
    u16 crop_right_inclusive;
    u16 crop_bottom_inclusive;
    u64 flags;
    u64 system_tick;
    u16 interval_denominator;
    u16 interval_numerator;
    u32 unk_x1c;
    u8 metadata[0x20];
} Vi2PresentOptions;

/// Packs a basic full-image crop and a 1/60-second interval.
/// @note The function narrows width and height to the u16 crop extents. A
/// zero extent clears the explicit-crop bit.
void vi2MakeBasicPresentOptions(Vi2PresentOptions* out, u32 width, u32 height, u64 system_tick);

/// Packs the same options with a zero presentation interval (0/1 second).
/// @note It is not known whether this gives immediate or mailbox presentation.
void vi2MakeZeroIntervalPresentOptions(Vi2PresentOptions* out, u32 width, u32 height, u64 system_tick);

/** @brief A registered VI2 display. Close before the final vi2Exit. */
typedef struct Vi2Display {
    u64 name;
    u32 notification_index;
} Vi2Display;

/** @brief A local endpoint name associated with a live display. */
typedef struct Vi2Endpoint {
    Vi2Display* display;
    u64 name;
} Vi2Endpoint;

/** @brief A VI2 layer and its two owned notification indices. */
typedef struct Vi2Layer {
    u64 token;
    u32 destroy_notification;
    u32 availability_notification;
} Vi2Layer;

/** @brief Display state whose changes a \ref Vi2DisplayEvent reports. */
typedef enum Vi2DisplayEventType {
    Vi2DisplayEventType_RefreshRate,          ///< Reread with \ref vi2QueryDisplayRefreshRate.
    Vi2DisplayEventType_BufferMaxResolution,  ///< Reread with \ref vi2QueryDisplayBufferMaxResolution.
    Vi2DisplayEventType_ModeCapability,       ///< Reread with \ref vi2QueryDisplayModeCapability.
} Vi2DisplayEventType;

/**
 * @brief A display-change event registered on a VI2 display.
 * @note The readable handle belongs to VI2's eight-slot event pool. Later
 *       registrations of the same slot reuse it. Wait on or clear \ref event.
 *       Do not eventClose it. Use \ref vi2DestroyDisplayEvent.
 */
typedef struct Vi2DisplayEvent {
    Event event;
    u64 display_name;
    u32 slot;
    Vi2DisplayEventType type;
} Vi2DisplayEvent;

/**
 * @brief Mode capabilities from \ref vi2QueryDisplayModeCapability, in wire order.
 * @note VI2 uses refresh bits 0, 1 and 63, and HDR bits 0-1. In the
 *       current-refresh word, bit 0 is 60 Hz, bit 1 is 120 Hz and bit 63 is VRR.
 *       The difference between the two refresh-rate sets is not known.
 */
typedef struct Vi2DisplayModeCapability {
    u64 refresh_rates;
    u64 hdr_output_formats;
    u64 refresh_rates_secondary;
} Vi2DisplayModeCapability;

/** @brief A graphics producer or display release dependency (a \ref GsvNativeSync). */
typedef GsvNativeSync Vi2Dependency;

/// Initializes the game's VI2 child obtained through AM and GSV1.
/// @note Reference-counted as in libnx. The final exit releases the child.
/// Close children before the final exit.
Result vi2Initialize(void);

/// Releases the VI2 child on the final exit. Close all layers and displays first.
void vi2Exit(void);

/// Returns the owned VI2 child session. The caller must not close it.
TipcService* vi2GetServiceSession(void);

/// Registers a named display. The function uses at most the first eight bytes of the name.
/// @note Returns 0x1872 if no notification index is available.
Result vi2OpenDisplay(Vi2Display* out, const char* name);

/// Registers the default display.
Result vi2OpenDefaultDisplay(Vi2Display* out);

/// Unregisters a display and waits for its own notification without a timeout.
/// @note If the wait fails, the display can still be registered. The display
/// object keeps its notification index. Do not reuse or discard the object.
Result vi2CloseDisplay(Vi2Display* display);

/// Creates local endpoint bookkeeping. It sends no VI2 request.
/// @note The display must remain live while the endpoint refers to it.
void vi2CreateEndpoint(Vi2Endpoint* out, Vi2Display* display, const char* name);

/// Clears a local endpoint. It sends no VI2 request.
void vi2CloseEndpoint(Vi2Endpoint* endpoint);

/// Creates a layer with a unique token and two notification indices.
/// @note Returns 0x1872 if no notification index is available.
Result vi2CreateLayer(Vi2Layer* out);

/// Attaches a layer to a registered display.
Result vi2AttachLayer(Vi2Layer* layer, Vi2Display* display);

/// Selects a layer for an endpoint on a live display.
Result vi2SelectLayer(Vi2Layer* layer, Vi2Endpoint* endpoint);

/// Clears the selected layer for an endpoint.
Result vi2ClearLayerSelection(Vi2Endpoint* endpoint);

/// Reads the current available image-slot bitmask for a layer.
Result vi2GetLayerAvailability(Vi2Layer* layer, u64* out_slots);

/// Presents a registered image and its producer dependency.
/// @param[out] out_slots Available image-slot bitmask returned by VI2.
/// @param[out] out_release Dependency for the display's use of the image.
/// @note Image registration and release-qualified reuse are the caller's responsibility.
Result vi2PresentLayer(Vi2Layer* layer, u8 slot, u32 image_id,
                       const Vi2PresentOptions* options, const Vi2Dependency* producer,
                       u64* out_slots, Vi2Dependency* out_release);

/// Unregisters an image slot and returns its updated availability and release.
/// @param[out] out_slots Available image-slot bitmask returned by VI2.
/// @param[out] out_release Dependency for the display's use of the image.
Result vi2UnregisterLayerImage(Vi2Layer* layer, u8 slot,
                                const Vi2PresentOptions* options,
                                u64* out_slots, Vi2Dependency* out_release);

/// Detaches a layer from its display. Call it before you change the selection and close the layer.
Result vi2DetachLayer(Vi2Layer* layer);

/// Destroys a detached layer and waits for its first creation notification.
/// @note Explicitly replace or clear any endpoint selection after detach and
/// before this call. Retire images and their release dependencies separately.
/// The wait has no timeout. If the wait fails, the layer can still exist. The
/// layer object keeps its notification indices. Do not reuse or discard the object.
Result vi2CloseLayer(Vi2Layer* layer);

/**
 * @brief Registers an event that VI2 signals when the display's state of the given type changes.
 * @param[in] autoclear Whether waiting on the event clears it, as in \ref eventLoadRemote.
 * @note VI2 signals the event once at registration. Returns 0x1872 when all
 *       eight event slots are in use. Destroy every event of a display before
 *       \ref vi2CloseDisplay.
 */
Result vi2CreateDisplayEvent(Vi2DisplayEvent* out, Vi2Display* display,
                             Vi2DisplayEventType type, bool autoclear);

/// Unregisters the event and returns its slot. The pool keeps the readable handle.
Result vi2DestroyDisplayEvent(Vi2DisplayEvent* event);

/**
 * @brief Queries the display's nominal refresh rate.
 * @param[out] out_hz 60 or 120.
 * @param[out] out_vrr Whether the current mode reports VRR as active (bit 63).
 * @note The function writes out_vrr even when the word names neither 60 nor
 *       120 Hz. In that case, it returns 0x1472 and leaves out_hz unchanged.
 *       This is the mode's nominal rate, not a measured scanout rate.
 */
Result vi2QueryDisplayRefreshRate(Vi2Display* display, u32* out_hz, bool* out_vrr);

/**
 * @brief Queries the display's maximum buffer resolution level.
 * @param[out] out_level A level from 0 to 3. The pixel dimensions of each level are not known.
 * @note Returns 0x1472, leaving out_level unchanged, for any other wire value.
 */
Result vi2QueryDisplayBufferMaxResolution(Vi2Display* display, u32* out_level);

/// Queries the display's mode capabilities.
Result vi2QueryDisplayModeCapability(Vi2Display* display, Vi2DisplayModeCapability* out);

/**
 * @brief Declares whether the application can present at 120 Hz on the display.
 * @note This is a request, not a mode change. VI2's display manager weighs
 *       it the next time it selects a mode (normally on a presented frame).
 *       It also weighs the system's own 120 Hz permission and the display's
 *       timings. The refresh event reports the result. Presenting every
 *       120 Hz refresh also needs a 1/120 s present interval.
 */
Result vi2SetDisplay120HzCapability(Vi2Display* display, bool enable);

/**
 * @brief Declares whether the application can present with variable refresh (VRR) on the display.
 * @note This is also a request. VI2 weighs it at mode selection, with the
 *       system's VRR permission. The request can make VI2 prefer a 120 Hz
 *       mode. The current refresh word's bit 63 (\ref vi2QueryDisplayRefreshRate)
 *       reports VRR as active.
 */
Result vi2SetDisplayVrrCapability(Vi2Display* display, bool enable);

/**
 * @brief Consumes the layer's availability notification, waiting for it if it is not pending.
 * @param[in] timeout_ns Maximum wait. A value of 0 only consumes a notification that VI2 already posted.
 * @return 0 when the function consumed a notification. KERNELRESULT(TimedOut) when no notification arrived.
 * @note VI2 posts this notification when it reclaims an image slot. Consume it
 *       before each \ref vi2GetLayerAvailability, and wait for it when no slot
 *       is free.
 */
Result vi2WaitLayerAvailability(Vi2Layer* layer, u64 timeout_ns);
