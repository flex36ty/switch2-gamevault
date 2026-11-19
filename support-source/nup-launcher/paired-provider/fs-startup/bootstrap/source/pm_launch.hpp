// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "compiler_tls.hpp"
extern "C" {
#include <switch/services/pm.h>
}

namespace ounce {

struct PmLaunchRequest {
    Handle shell_session;
    u64 program_id;
    u32 flags;
    u8 storage_id;
    volatile Result result;
    volatile u64 pid;
    volatile u32 completed;
    Handle monitor_session;
};

// This is the same public pmshellLaunchProgram call used by libnx. The
// bootstrap borrows a session already obtained in that process;
// this short lived mapped module has no independent SM registration.
inline void* launchFromSdkThread(void* argument) {
    auto* request = static_cast<PmLaunchRequest*>(argument);
    CompilerTlsScope tls;
    Service* shell = pmshellGetServiceSession();
    serviceCreate(shell, request->shell_session);
    NcmProgramLocation location = {};
    location.program_id = request->program_id;
    location.storageID = request->storage_id;
    u64 pid = 0;
    Result rc = pmshellLaunchProgram(request->flags, &location, &pid);
    // The host owns the borrowed handle. Do not call pmshellExit here.
    request->pid = pid;
    request->result = rc;
    request->completed = 1;
    return reinterpret_cast<void*>(static_cast<uintptr_t>(rc));
}

}