/**
 * @file audu.h
 * @brief Native Switch 2 audio root (aud:u), which gives a game its audio managers.
 * @author kurumuz
 * @copyright 2026 libounce contributors
 * @license ISC
 */
#pragma once
#if !defined(__SWITCH2__)
#error "aud:u is available only in a native Switch 2 build"
#endif

#include "../../switch/types.h"
#include "../../switch/sf/service.h"

/**
 * @brief Opens the AudioOut manager (IAudioOutManager) through the aud:u root (command 1).
 * @param[out] out The manager session. Close it with serviceClose.
 * @note A native Switch 2 game is not allowed to open audout:u directly. The manager is the
 *       same service object as audout:u's, so its commands are those of \ref audoutGetServiceSession.
 */
Result auduOpenAudioOutManager(Service* out);

/**
 * @brief Opens the AudioRenderer manager (IAudioRendererManager) through the aud:u root (command 2).
 * @param[out] out The manager session. Close it with serviceClose.
 * @note A native Switch 2 game is not allowed to open audren:u directly. The manager is the
 *       same service object as audren:u's.
 */
Result auduOpenAudioRendererManager(Service* out);
