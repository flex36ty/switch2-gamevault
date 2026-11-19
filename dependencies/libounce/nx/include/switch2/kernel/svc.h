/**
 * @file svc.h
 * @brief Switch 2 system calls that the Switch 1 kernel does not have.
 * @author kurumuz
 * @copyright 2026 libounce contributors
 * @license ISC
 */
#pragma once
#if !defined(__SWITCH2__)
#error "These system calls are available only in a native Switch 2 build"
#endif

#include "../../switch/types.h"

/**
 * @brief SVC 0x80. Same argument shape as \ref svcSetMemoryAttribute.
 * @param[in] address Page-aligned address.
 * @param[in] size Page-aligned size.
 * @param[in] mask Attribute mask.
 * @param[in] value Attribute value.
 * @note Callers set mask 0x20, value 0x20 on CPU memory before they register
 *       it with the GPU. They set mask 0x20, value 0 after they unregister its range.
 *       Other effects of this call are not known. For this reason, the
 *       function has a numeric name.
 */
Result svcUnknown80(void* address, u64 size, u32 mask, u32 value);
