/**
 * @file nmouse.h
 * @brief Native Switch 2 HID Nmouse: pointer samples from mouse devices such as Joy-Con, through hid.
 * @author kurumuz
 * @copyright 2026 libounce contributors
 * @license ISC
 * @note Nmouse is separate from the ordinary mouse of \ref hidInitializeMouse and
 *       \ref HidMouseState, which keep their contract. Each Nmouse device has its own
 *       opaque handle, and its samples are in memory that the application gives the
 *       service, not in the HID shared memory.
 */
#pragma once
#if !defined(__SWITCH2__)
#error "Nmouse is available only in a native Switch 2 build"
#endif

#include "../../switch/types.h"
#include "../../switch/services/hid.h"

/// Size of the memory that \ref hidInitializeNmouse registers.
#define HID_NMOUSE_TRANSFER_MEMORY_SIZE 0xC000

/// Most handles that \ref hidGetNmouseHandles gives for one Npad.
#define HID_NMOUSE_HANDLES_PER_NPAD 2

/// Result of a call with a handle that is not active in the Nmouse memory.
#define HID_NMOUSE_RESULT_INVALID_HANDLE MAKERESULT(202, 5214)

/// Result of \ref hidGetMergedNmouseHandle and \ref hidGetStandardMouseNmouseHandle when there is no such device.
#define HID_NMOUSE_RESULT_NO_DEVICE MAKERESULT(202, 5216)

/// Bits of a type set (\ref hidGetNmouseTypeSet, \ref hidSetMergedNmouseSourceTypes).
typedef enum {
    HidNmouseType_Merged        = BIT(0),       ///< The merged pointer of the source types that \ref hidSetMergedNmouseSourceTypes selects.
    HidNmouseType_JoyLeft       = BIT(1),       ///< A left Joy-Con.
    HidNmouseType_JoyRight      = BIT(2),       ///< A right Joy-Con.
    HidNmouseType_StandardMouse = BIT(3),       ///< A standard mouse.
} HidNmouseType;

/// NmousePointerType, sent by \ref hidInitializeNmouse. Values other than the default are not established.
typedef enum {
    HidNmousePointerType_Default = 0,           ///< Default.
} HidNmousePointerType;

/// Opaque handle of one Nmouse device. It is not a kernel handle and not an index.
typedef struct HidNmouseHandle {
    u64 handle;                                 ///< Handle
} HidNmouseHandle;

/**
 * @brief NmouseState
 * @note The meaning of unk_x24 is not established.
 */
typedef struct HidNmouseState {
    u64 delta_time;                             ///< Time since the previous sample, in nanoseconds.
    u64 sampling_number;                        ///< SamplingNumber. Increases by one for each sample of the device.
    s32 x;                                      ///< Pointer X, from 0 to width-1.
    s32 y;                                      ///< Pointer Y, from 0 to height-1.
    s32 width;                                  ///< Width given to \ref hidInitializeNmouse.
    s32 height;                                 ///< Height given to \ref hidInitializeNmouse.
    s32 wheel_delta_y;                          ///< Vertical scrolling of this sample (Joy-Con: the stick), positive upward.
    u32 unk_x24;
    u32 buttons;                                ///< Held buttons: bits 0 to 4 are the output groups 1 to 5 of the button configs, read as \ref HidMouseButton. By default a Joy-Con's L/ZL or R/ZR give \ref HidMouseButton_Left.
    u32 flags;                                  ///< Flag word. The service sets bit 0 from its pointer policy; it is not a connection state. Bit 1 is clear in samples where the sensor did not track movement.
} HidNmouseState;

/**
 * @brief NmouseStateWithDelta: \ref HidNmouseState with the movement of the sample.
 */
typedef struct HidNmouseStateWithDelta {
    u64 delta_time;                             ///< Time since the previous sample, in nanoseconds.
    u64 sampling_number;                        ///< SamplingNumber.
    s32 x;                                      ///< Pointer X, from 0 to width-1.
    s32 y;                                      ///< Pointer Y, from 0 to height-1.
    s32 delta_x;                                ///< Movement in X of this sample.
    s32 delta_y;                                ///< Movement in Y of this sample.
    s32 width;                                  ///< Width of the pointer area.
    s32 height;                                 ///< Height of the pointer area.
    s32 wheel_delta_y;                          ///< As \ref HidNmouseState.wheel_delta_y.
    u32 unk_x24;                                ///< As \ref HidNmouseState.unk_x24.
    u32 buttons;                                ///< As \ref HidNmouseState.buttons.
    u32 flags;                                  ///< As \ref HidNmouseState.flags.
} HidNmouseStateWithDelta;

/**
 * @brief NmouseButtonConfigJoyLeftJcms
 * @note Each byte selects the output group (1 to 5) of one source button. Any other value selects group 0.
 *       The source button masks of bytes 0 to 7 are 0x40, 0x100, 0x1000, 0x2000, 0x4000, 0x8000, 0x10 and 0x800.
 *       The names of the buttons and groups are not established.
 */
typedef struct HidNmouseButtonConfigJoyLeftJcms {
    u8 selectors[8];                            ///< Group of each source button.
} HidNmouseButtonConfigJoyLeftJcms;

/**
 * @brief NmouseButtonConfigJoyRightJcms
 * @note Each byte selects the output group (1 to 5) of one source button. Any other value selects group 0.
 *       The source button masks of bytes 0 to 7 are 0x80, 0x200, 0x1, 0x2, 0x4, 0x8, 0x20 and 0x400.
 *       The names of the buttons and groups are not established.
 */
typedef struct HidNmouseButtonConfigJoyRightJcms {
    u8 selectors[8];                            ///< Group of each source button.
} HidNmouseButtonConfigJoyRightJcms;

/**
 * @brief NmouseButtonConfigStandardMouse
 * @note Each byte selects the output group (1 to 5) of one standard-mouse button. The buttons are not established.
 */
typedef struct HidNmouseButtonConfigStandardMouse {
    u8 selectors[5];                            ///< Group of each source button.
} HidNmouseButtonConfigStandardMouse;

/**
 * @brief InitializeNmouse
 * @note This also initializes hid (\ref hidInitialize), until \ref hidFinalizeNmouse succeeds.
 * @note This function requests revision 1 of the sample format.
 * @note Typical use: hidInitializeNmouse(NULL, 0, 1920, 1080, HidNmousePointerType_Default).
 * @warning A \p buffer that the application gives must stay allocated, and the application must not write
 *          to it, from this call until \ref hidFinalizeNmouse succeeds. The service writes the samples there.
 *          When this function fails, the buffer is not registered.
 * @param[in] buffer Memory for the samples, 0x1000-byte aligned. The service clears it. NULL: the library
 *            allocates it and \ref hidFinalizeNmouse frees it.
 * @param[in] size Size of \p buffer. The service requires \ref HID_NMOUSE_TRANSFER_MEMORY_SIZE. Not used when \p buffer is NULL.
 * @param[in] width Width of the pointer area. Sample coordinates are scaled to it.
 * @param[in] height Height of the pointer area.
 * @param[in] pointer_type \ref HidNmousePointerType. Only the low 8 bits are sent.
 */
Result hidInitializeNmouse(void *buffer, size_t size, s32 width, s32 height, HidNmousePointerType pointer_type);

/**
 * @brief FinalizeNmouse
 * @note After success the buffer of \ref hidInitializeNmouse is released and can be freed: this
 *       function waits, up to 2 seconds, until the process can write it again. A library-allocated
 *       buffer that is not given back in that time is not freed.
 *       After a failure Nmouse stays initialized.
 */
Result hidFinalizeNmouse(void);

/**
 * @brief GetNmouseHandles
 * @note Reads the Nmouse memory. It gives the left Joy-Con first (\ref HidNmouseType_JoyLeft), then the
 *       right one (\ref HidNmouseType_JoyRight).
 *       A missing device is skipped.
 * @param[out] handles Output array of \ref HidNmouseHandle.
 * @param[in] count Max number of entries in the \p handles array. At most \ref HID_NMOUSE_HANDLES_PER_NPAD are used.
 * @param[in] id \ref HidNpadIdType
 * @param[out] total_out Number of handles written.
 */
Result hidGetNmouseHandles(HidNmouseHandle *handles, s32 count, HidNpadIdType id, s32 *total_out);

/**
 * @brief Gets the handles of every Nmouse device on Npads No1 to No8 and Handheld, in that order.
 * @note Calls \ref hidGetNmouseHandles for each Npad.
 * @param[out] handles Output array of \ref HidNmouseHandle.
 * @param[in] count Max number of entries in the \p handles array. 1 gives the first device.
 * @param[out] total_out Number of handles written. 0 when no device is attached.
 */
Result hidGetAllNmouseHandles(HidNmouseHandle *handles, s32 count, s32 *total_out);

/**
 * @brief GetNmouseTypeSet
 * @note Reads the Nmouse memory.
 * @param[in] handle \ref HidNmouseHandle
 * @param[out] out Type set: \ref HidNmouseType bits.
 */
Result hidGetNmouseTypeSet(HidNmouseHandle handle, u32 *out);

/**
 * @brief GetNpadIdType
 * @note Reads the Nmouse memory. It gives \ref HID_NMOUSE_RESULT_INVALID_HANDLE also when the device has no Npad.
 * @param[in] handle \ref HidNmouseHandle
 * @param[out] out \ref HidNpadIdType of the device.
 */
Result hidGetNmouseNpadIdType(HidNmouseHandle handle, HidNpadIdType *out);

/**
 * @brief GetNmouseStates
 * @note Reads the Nmouse memory. The service keeps at most 16 samples for each device.
 * @param[in] handle \ref HidNmouseHandle
 * @param[out] states Output array of \ref HidNmouseState, newest first. Entries after \p total_out are not changed.
 * @param[in] count Max number of entries in the \p states array.
 * @param[out] total_out Number of states written. 0 when the device has no samples yet.
 */
Result hidGetNmouseStates(HidNmouseHandle handle, HidNmouseState *states, size_t count, size_t *total_out);

/**
 * @brief SetNmouseButtonConfigJoyLeftJcms
 * @param[in] config \ref HidNmouseButtonConfigJoyLeftJcms
 */
Result hidSetNmouseButtonConfigJoyLeftJcms(const HidNmouseButtonConfigJoyLeftJcms *config);

/**
 * @brief SetNmouseButtonConfigJoyRightJcms
 * @param[in] config \ref HidNmouseButtonConfigJoyRightJcms
 */
Result hidSetNmouseButtonConfigJoyRightJcms(const HidNmouseButtonConfigJoyRightJcms *config);

/**
 * @brief SetNmouseButtonConfigStandardMouse
 * @param[in] config \ref HidNmouseButtonConfigStandardMouse
 */
Result hidSetNmouseButtonConfigStandardMouse(const HidNmouseButtonConfigStandardMouse *config);

/**
 * @brief GetNmouseStatesWithDelta: as \ref hidGetNmouseStates, with the movement of each sample.
 * @param[in] handle \ref HidNmouseHandle
 * @param[out] states Output array of \ref HidNmouseStateWithDelta, newest first.
 * @param[in] count Max number of entries in the \p states array.
 * @param[out] total_out Number of states written.
 */
Result hidGetNmouseStatesWithDelta(HidNmouseHandle handle, HidNmouseStateWithDelta *states, size_t count, size_t *total_out);

/**
 * @brief SetNmousePointerRanges: changes the pointer area that \ref hidInitializeNmouse set.
 * @param[in] width Width of the pointer area.
 * @param[in] height Height of the pointer area.
 */
Result hidSetNmousePointerRanges(s32 width, s32 height);

/**
 * @brief GetNmousePointerRanges
 * @param[out] width Width of the pointer area. Can be NULL.
 * @param[out] height Height of the pointer area. Can be NULL.
 */
Result hidGetNmousePointerRanges(s32 *width, s32 *height);

/**
 * @brief SetNmousePointerMovementArea: keeps the pointers inside a rectangle of the pointer area.
 * @note The service requires left < right, top < bottom, left and top not negative, and the
 *       rectangle inside the pointer area.
 */
Result hidSetNmousePointerMovementArea(s32 left, s32 top, s32 right, s32 bottom);

/**
 * @brief RestrictNmousePointer: as \ref hidSetNmousePointerMovementArea, with the rectangle as
 *        fractions of the pointer area (0 to 1).
 */
Result hidRestrictNmousePointer(float left, float top, float right, float bottom);

/**
 * @brief SetMergedNmouseSourceTypes: selects the devices whose movement the merged pointer
 *        (\ref hidGetMergedNmouseHandle) follows.
 * @param[in] type_set \ref HidNmouseType bits. Only \ref HidNmouseType_JoyLeft, \ref HidNmouseType_JoyRight
 *            and \ref HidNmouseType_StandardMouse are sent.
 */
Result hidSetMergedNmouseSourceTypes(u32 type_set);

/**
 * @brief GetMergedNmouseHandle
 * @param[out] out Handle of the merged pointer. \ref HID_NMOUSE_RESULT_NO_DEVICE when there is none.
 */
Result hidGetMergedNmouseHandle(HidNmouseHandle *out);

/**
 * @brief GetStandardMouseNmouseHandle
 * @param[out] out Handle of the standard mouse. \ref HID_NMOUSE_RESULT_NO_DEVICE when there is none.
 */
Result hidGetStandardMouseNmouseHandle(HidNmouseHandle *out);

/**
 * @brief SetNmousePointerPosition: moves a device's pointer.
 * @param[in] handle \ref HidNmouseHandle
 * @param[in] x Pointer X.
 * @param[in] y Pointer Y.
 */
Result hidSetNmousePointerPosition(HidNmouseHandle handle, s32 x, s32 y);

/**
 * @brief EnableNmousePointer: turns a device's pointer on or off.
 * @note Sends nothing when the pointer is already in the requested state.
 * @param[in] handle \ref HidNmouseHandle
 * @param[in] enable Whether the pointer is on.
 */
Result hidEnableNmousePointer(HidNmouseHandle handle, bool enable);

/**
 * @brief IsEnableNmousePointer: reads whether a device's pointer is on.
 * @param[in] handle \ref HidNmouseHandle
 * @param[out] out Whether the pointer is on.
 */
Result hidIsNmousePointerEnabled(HidNmouseHandle handle, bool *out);
