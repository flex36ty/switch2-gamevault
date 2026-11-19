/* SPDX-License-Identifier: ISC
 * Original libounce-nx Switch 2 service wrapper.
 *
 * A game opens aud:u and asks the root for a manager: command 1 for
 * AudioOut, 2 for AudioRenderer. Neither request has arguments, buffers, a
 * PID or handles. The reply moves the manager session. The service returns
 * the same manager objects it registers as audout:u and audren:u, and
 * accepts libnx's request framing. A manager stays valid after its root
 * closes, so these functions close the root.
 */
#define NX_SERVICE_ASSUME_NON_DOMAIN
#include "switch2/services/audu.h"
#include "services/sm.h"

static Result _auduOpenManager(Service* out, u32 cmd_id) {
    Service root;
    Result rc = smGetService(&root, "aud:u");
    if (R_FAILED(rc))
        return rc;

    rc = serviceDispatch(&root, cmd_id,
        .out_num_objects = 1,
        .out_objects = out,
    );
    serviceClose(&root);
    return rc;
}

Result auduOpenAudioOutManager(Service* out) {
    return _auduOpenManager(out, 1);
}

Result auduOpenAudioRendererManager(Service* out) {
    return _auduOpenManager(out, 2);
}
