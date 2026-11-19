#pragma once
#include <switch.h>
#include <string.h>
#include "protocol.h"

// Compatibility with the running v1 host: its callback expects exactly zero
// bytes, whereas its CMIF parser counts the client's trailing alignment slack.
// Restrict this encoder to our five scalar, no-input, non-domain commands.
static Result workbenchMakeRequest(void* message, u32 command) {
    if (command > HelperRollback)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    memset(message, 0, 0x100);
    CmifRequest request = cmifMakeRequest(message,
        (CmifRequestFormat){.request_id = command});
    size_t bytes = (u8*)request.data - (u8*)request.hipc.data_words;
    if ((bytes & 3) || bytes > 0x100)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    // Keep leading alignment padding and the complete CMIF header; remove only
    // the unused trailing bytes. No buffers, handles, PID or pointer-size table.
    ((HipcHeader*)message)->num_data_words = bytes / sizeof(u32);
    HipcParsedRequest parsed = hipcParseRequest(message);
    CmifInHeader* header = cmifGetAlignedDataStart(parsed.data.data_words, message);
    const u8* end = (u8*)(parsed.data.data_words + parsed.meta.num_data_words);
    if (end != (u8*)(header + 1) || header->magic != CMIF_IN_HEADER_MAGIC ||
        header->command_id != command || parsed.meta.type != CmifCommandType_Request)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    return 0;
}

static Result workbenchDispatch(Service* service, u32 command, HelperInfo* status) {
    if (!serviceIsActive(service) || serviceIsDomain(service) ||
        ((command == HelperStatus) != (status != NULL)))
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    void* message = armGetTls();
    Result rc = workbenchMakeRequest(message, command);
    if (R_FAILED(rc)) return rc;
    rc = svcSendSyncRequest(service->session);
    if (R_FAILED(rc)) return rc;
    CmifResponse response = {0};
    rc = cmifParseResponse(&response, message, false, status ? sizeof(*status) : 0);
    if (R_SUCCEEDED(rc) && status) memcpy(status, response.data, sizeof(*status));
    return rc;
}

// Exercise the actual libounce encoder and parser without sending any IPC.
static Result workbenchWireSelfTest(void) {
    u8 message[0x100] __attribute__((aligned(16)));
    for (u32 cmd = HelperStatus; cmd <= HelperRollback; ++cmd) {
        Result rc = workbenchMakeRequest(message, cmd);
        if (R_FAILED(rc)) return rc;
    }
    return 0;
}
