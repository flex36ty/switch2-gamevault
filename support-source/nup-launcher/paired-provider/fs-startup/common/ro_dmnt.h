// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <switch/types.h>

// Private ro:dmnt commands, which kaiHEN uses to read and patch the FS
// process through RO's process-memory mapping capability. The caller copies
// the FS process handle in the request; RO checks that it is FS and that the
// range is FS code memory.
constexpr u32 RoProcessReadCommand = 0x524f0016;  ///< Out buffer: the bytes read, at most 4 MiB.
constexpr u32 RoProcessWriteCommand = 0x524f0017; ///< In buffer: the bytes to write, at most 256.

/// The input of RoProcessReadCommand and RoProcessWriteCommand.
struct RoProcessMemoryRequest {
    u64 process_id; ///< The FS process ID; it must match the copied handle.
    u64 address;
    u64 size;
    u64 reserved;
};

// Read-only FS RX range discovery, executed by RO, never directly by HEN.
constexpr u32 RoFsTextRangeCommand = 0x524f0019;
struct RoFsRegion { u64 address,size; u32 type,perm,attr,reserved; };
struct RoFsTextRange {
    u64 base, size;
    u64 query_address, region_address, region_size;
    u64 begin, end;
    u32 type, perm, attr, iterations;
    u32 result, reserved;
    u32 region_count, region_total;
    RoFsRegion regions[32];
};
static_assert(sizeof(RoFsTextRange)==1112);

// Checked staging-data writes only; does not authorize executable hook patches.
constexpr u32 RoCompareDataCommand=0x524f001a;
struct RoCompareDataResult { u32 result,cleanup,matched,written,verified,reserved; };

// Local RO-owned scratch self-test; no process handles or target writes.
constexpr u32 RoStagingSelfTestCommand=0x524f001b;
struct RoStagingSelfTestResult {u32 version,passed,expected,reserved;};
static_assert(sizeof(RoCompareDataResult)==24);
static_assert(sizeof(RoStagingSelfTestResult)==16);
