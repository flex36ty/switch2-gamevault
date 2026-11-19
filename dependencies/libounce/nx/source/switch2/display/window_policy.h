/* SPDX-License-Identifier: ISC
 * The default window's size per operation mode
 * (nwindowSetModeDimensions) and frame rate per refresh rate
 * (nwindowSetFrameRates), kept in default_window.c. native_window.c calls
 * these from the libnx functions they extend.
 * @author kurumuz
 */
#pragma once
#include "types.h"
#include "display/native_window.h"

/// Sets nw's dimensions for the current operation mode, if it has per-mode dimensions.
void _nwindowApplyModeDimensions(NWindow* nw);
/// Returns nw to one fixed size (nwindowSetDimensions).
void _nwindowClearModeDimensions(NWindow* nw);
/// Returns nw to its swap interval (nwindowSetSwapInterval).
void _nwindowClearFrameRates(NWindow* nw);
