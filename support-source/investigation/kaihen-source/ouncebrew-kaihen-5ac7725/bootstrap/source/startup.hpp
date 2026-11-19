// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "../../common/loader_gate.hpp"
#include "../../common/domain_mount.hpp"

// Host/bootstrap ABI. Inputs remain borrowed for the duration of startup.
struct BootstrapInput {
    ounce::RetainedMemoryConfig memory;
    ounce::LoaderGateConfig gate;
    ounce::DomainMountConfig mount;
    Handle shell_session;
    Handle monitor_session;
    u64 program_id;
    u32 flags;
    u8 storage_id;
    u64 (*pthread_create)(u64*, u64, void*(*)(void*), void*);
    u64 (*pthread_trywait)(u64, u64, u64);
    u64 (*pthread_join)(u64, u64*, u64);
    Handle sm_session;
    Handle ro_session;
    Handle fs_root_session;
    char package_path[96];
    Handle fs_process;
    Handle fs_notifier;
    Handle loader_root_session;
};

enum BootstrapOperation : u32 {
    BootstrapOperation_Begin = 1,
    BootstrapOperation_Park = 2,
    BootstrapOperation_Launch = 3,
    BootstrapOperation_Mount = 4,
    BootstrapOperation_Exchange = 5,
    BootstrapOperation_Restore = 6,
    BootstrapOperation_Join = 7,
    BootstrapOperation_Start = 8,
    BootstrapOperation_Connect = 9,
    BootstrapOperation_Complete = 10,
    BootstrapOperation_Package = 11,
    BootstrapOperation_GameConfig = 12,
    BootstrapOperation_GameHandles0 = 13,
    BootstrapOperation_GameHandles1 = 14,
    BootstrapOperation_GameStart = 15,
    BootstrapOperation_ReadMigrationPolicy = 16,
    BootstrapOperation_WriteMigrationPackage = 17,
    BootstrapOperation_PluginScan = 18,
    BootstrapOperation_PluginPackage = 19,
    BootstrapOperation_PluginLookup = 20,
    BootstrapOperation_PluginHandoff = 21,
    BootstrapOperation_PluginReady = 22,
};

// The primary failure and cleanup failures are independent: a failed launch
// must still restore the temporary Loader and memory state.
struct BootstrapResult {
    BootstrapOperation operation;
    Result result;
    u64 process_id;
    Result gate_restore_result;
    Result channel_restore_result;
};

struct BootstrapArgs {
    BootstrapInput input;
    BootstrapResult output;
};
static_assert(sizeof(BootstrapInput) == 408);
static_assert(__builtin_offsetof(BootstrapInput, pthread_create) == 264);
static_assert(__builtin_offsetof(BootstrapInput, ro_session) == 292);
static_assert(sizeof(BootstrapResult) == 24);
static_assert(sizeof(BootstrapArgs) == 432);
static_assert(__builtin_offsetof(BootstrapInput, gate) == 24);
static_assert(__builtin_offsetof(BootstrapInput, mount) == 176);
static_assert(__builtin_offsetof(BootstrapInput, shell_session) == 240);
static_assert(__builtin_offsetof(BootstrapInput, program_id) == 248);
static_assert(__builtin_offsetof(BootstrapArgs, output) == 408);
static_assert(__builtin_offsetof(BootstrapInput, monitor_session) == 244);
static_assert(__builtin_offsetof(BootstrapInput, flags) == 256);
static_assert(__builtin_offsetof(BootstrapInput, storage_id) == 260);
static_assert(__builtin_offsetof(BootstrapInput, sm_session) == 288);
static_assert(__builtin_offsetof(BootstrapInput, fs_root_session) == 296);
static_assert(__builtin_offsetof(BootstrapInput, package_path) == 300);
static_assert(__builtin_offsetof(BootstrapInput, fs_process) == 396);
static_assert(__builtin_offsetof(BootstrapInput, fs_notifier) == 400);
static_assert(__builtin_offsetof(BootstrapInput, loader_root_session) == 404);
