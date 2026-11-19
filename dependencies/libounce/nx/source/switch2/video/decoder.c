/* SPDX-License-Identifier: ISC
 * Original libounce-nx Switch 2 video decoder object.
 *
 * Built on the GSV root (gsv.c) and the video decoder sessions (viddec.c).
 * Pitch-linear output images are read from the image memory without a copy;
 * block-linear images are copied out with viddecCopyImageNv12.
 */
#include <stdlib.h>
#include <string.h>
#include "switch2/video/decoder.h"
#include "services/nv.h"
#include "kernel/svc.h"
#include "arm/cache.h"
#include "arm/counter.h"

// The size at and above which the default color policy is 3 (else 1), as
// viddecDecoderInitialize documents.
#define LARGE_POLICY_WIDTH         1920
#define LARGE_POLICY_HEIGHT        1080
#define COLOR_POLICY_LARGE         3
#define COLOR_POLICY_SMALL         1

// Output mode 1 is the mode that every codec accepts.
#define DECODER_OUTPUT_MODE        1

// The work memory is given to the service as transfer memory.
#define WORK_ALIGNMENT             0x1000
#define WORK_SIZE_ALIGNMENT        0x10000

// Wait for the service to give the work memory back after the decoder closes.
#define WORK_RETURN_TIMEOUT_NS     2000000000ULL
#define WORK_RETURN_POLL_NS        1000000LL

// The service can list an image's properties after its producer is reached.
#define PROPERTIES_TIMEOUT_NS      500000000ULL
#define PROPERTIES_POLL_NS         1000000LL

// Completion event arm mode: 0 compares the low 32 bits of the counter.
#define EVENT_ARM_MODE             0

//-----------------------------------------------------------------------------
// Work memory size
//-----------------------------------------------------------------------------

// Size tiers: the first tier that holds both dimensions gives the work
// memory size; a larger stream uses the last tier.
#define WORK_TIER_COUNT 9

static const struct {
    u32 width;
    u32 height;
} g_workTiers[WORK_TIER_COUNT] = {
    { 256, 144 }, { 384, 216 }, { 320, 240 }, { 640, 360 }, { 854, 480 },
    { 960, 540 }, { 1280, 720 }, { 1920, 1080 }, { 3840, 2160 },
};

// Work memory in MiB for each tier, pitch-linear then block-linear; 0: the
// codec has no such tier.
static const u16 g_workMiBH264[2][WORK_TIER_COUNT] = {
    { 8, 9, 9, 15, 20, 23, 34, 68, 240 },
    { 9, 10, 10, 16, 22, 25, 40, 80, 276 },
};
static const u16 g_workMiBVp8Vp9[2][WORK_TIER_COUNT] = {
    { 14, 0, 16, 18, 24, 0, 42, 80, 274 },
    { 15, 0, 17, 20, 26, 0, 48, 92, 312 },
};
static const u16 g_workMiBAv1[2][WORK_TIER_COUNT] = {
    { 9, 0, 0, 16, 22, 0, 40, 78, 272 },
    { 10, 0, 0, 18, 24, 0, 46, 90, 308 },
};

static u64 _videoDecoderWorkSize(ViddecCodec codec, ViddecTilingMode tiling, u32 width, u32 height) {
    const u32 layout = tiling == ViddecTilingMode_BlockLinear ? 1 : 0;
    const u16* table;
    switch (codec) {
    case ViddecCodec_H264: table = g_workMiBH264[layout]; break;
    case ViddecCodec_Vp8:
    case ViddecCodec_Vp9:  table = g_workMiBVp8Vp9[layout]; break;
    case ViddecCodec_Av1:  table = g_workMiBAv1[layout]; break;
    default: return 0;
    }

    u32 mib = table[WORK_TIER_COUNT - 1];
    for (u32 i = 0; i < WORK_TIER_COUNT; i++) {
        if (table[i] && width <= g_workTiers[i].width && height <= g_workTiers[i].height) {
            mib = table[i];
            break;
        }
    }
    const u64 size = (u64)mib << 20;
    return (size + WORK_SIZE_ALIGNMENT - 1) & ~(u64)(WORK_SIZE_ALIGNMENT - 1);
}

//-----------------------------------------------------------------------------
// Output images
//-----------------------------------------------------------------------------

// The largest frame rate that the service's pixel rate allows for the size.
static u32 _videoDecoderDefaultFrameRate(u32 width, u32 height) {
    const u64 frame_rate = VIDEO_DECODER_PIXEL_RATE / ((u64)width * height);
    return frame_rate < VIDEO_DECODER_MAX_FRAME_RATE ? (u32)frame_rate : VIDEO_DECODER_MAX_FRAME_RATE;
}

// The output layout of each codec when the program does not choose one.
// AV1 writes only block-linear images: a pitch-linear image gives
// 0x000fa66f when its properties are read (kaiHEN probes/video-decoder-probe).
static ViddecTilingMode _videoDecoderDefaultTiling(ViddecCodec codec) {
    return codec == ViddecCodec_Av1 ? ViddecTilingMode_BlockLinear : ViddecTilingMode_PitchLinear;
}

static bool _videoDecoderImageHasTiling(const GsvCpuImageDescriptor* descriptor, ViddecTilingMode tiling) {
    if (tiling == ViddecTilingMode_BlockLinear)
        return descriptor->type == GsvCpuImageType_Nv12BlockLinear;
    return descriptor->type == GsvCpuImageType_Nv12PitchLinear || descriptor->type == GsvCpuImageType_Nv12PitchLinearAlt;
}

static Result _videoDecoderCreateImage(VideoDecoderImage* image, const GsvDecodeImagePreference* preference,
                                       ViddecTilingMode tiling) {
    u64 size = 0, alignment = 0;
    Result rc = viddecGetImagePoolSize(preference, 1, &size, &alignment);
    if (R_FAILED(rc))
        return rc;

    image->backing = aligned_alloc(alignment, size);
    if (!image->backing)
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    image->backing_size = size;
    memset(image->backing, 0, size);
    armDCacheFlush(image->backing, size);

    rc = viddecImagePoolCreate(&image->pool, preference, 1, image->backing, size);
    if (R_SUCCEEDED(rc))
        rc = gsvGetCpuImageDescriptor(viddecImagePoolGetImageId(&image->pool, 0), &image->descriptor);
    if (R_SUCCEEDED(rc) && !_videoDecoderImageHasTiling(&image->descriptor, tiling))
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);

    if (R_FAILED(rc)) {
        viddecImagePoolClose(&image->pool);
        free(image->backing);
        memset(image, 0, sizeof(*image));
    }
    return rc;
}

//-----------------------------------------------------------------------------
// Waiting for a decoded image
//-----------------------------------------------------------------------------

static u32 _videoDecoderNativeProducer(const GsvNativeSync* sync) {
    u32 token;
    memcpy(&token, sync->data, sizeof(token));
    return ~token;
}

static u64 _videoDecoderNativeTarget(const GsvNativeSync* sync) {
    u64 target;
    memcpy(&target, sync->data + 8, sizeof(target));
    return target;
}

static void _videoDecoderDisarm(VideoDecoder* d) {
    if (d->pending_armed) {
        gsvSyncDisarmEvent(&d->sync, d->event_slot);
        d->pending_armed = false;
    }
}

// Returns 0 when the pending image's producer is reached, or
// KERNELRESULT(TimedOut). The event stays armed across a timeout.
static Result _videoDecoderWaitPending(VideoDecoder* d, u64 timeout_ns) {
    bool reached = false;
    Result rc = gsvSyncCheckNative(&d->sync, &d->pending_producer, &reached);
    if (R_FAILED(rc))
        return rc;
    if (reached) {
        _videoDecoderDisarm(d);
        return 0;
    }
    if (timeout_ns == 0)
        return KERNELRESULT(TimedOut);

    if (!d->event_open)
        return gsvSyncWaitNative(&d->sync, &d->pending_producer, timeout_ns);

    if (!d->pending_armed) {
        eventClear(&d->event);
        rc = gsvSyncArmEvent(&d->sync, d->event_slot, _videoDecoderNativeProducer(&d->pending_producer),
                             _videoDecoderNativeTarget(&d->pending_producer), EVENT_ARM_MODE);
        if (R_FAILED(rc))
            return rc;
        d->pending_armed = true;
    }

    rc = eventWait(&d->event, timeout_ns);
    if (R_FAILED(rc) && R_VALUE(rc) != KERNELRESULT(TimedOut))
        return rc;
    if (R_SUCCEEDED(rc))
        d->pending_armed = false;

    // A wake is a hint; the counter decides. This also covers a target that
    // was reached before the event was armed.
    rc = gsvSyncCheckNative(&d->sync, &d->pending_producer, &reached);
    if (R_FAILED(rc))
        return rc;
    if (!reached)
        return KERNELRESULT(TimedOut);
    _videoDecoderDisarm(d);
    return 0;
}

// Waits until the service lists the image's properties.
static Result _videoDecoderGetProperties(VideoDecoder* d, u32 slot, ViddecOutputProperties* out) {
    const u64 start = armGetSystemTick();
    for (;;) {
        Result rc = viddecDecoderGetOutputProperties(&d->decoder, slot, out);
        if (rc != VIDDEC_RESULT_NO_IMAGE)
            return rc;
        if (armTicksToNs(armGetSystemTick() - start) >= PROPERTIES_TIMEOUT_NS)
            return rc;
        svcSleepThread(PROPERTIES_POLL_NS);
    }
}

// The kernel gives the work memory back when the service drops its transfer
// memory. Returns false if that does not happen in time.
static bool _videoDecoderWaitWorkReturned(void* work) {
    const u64 start = armGetSystemTick();
    for (;;) {
        MemoryInfo info;
        u32 page_info;
        if (R_SUCCEEDED(svcQueryMemory(&info, &page_info, (uintptr_t)work)) && (info.perm & Perm_Rw) == Perm_Rw)
            return true;
        if (armTicksToNs(armGetSystemTick() - start) >= WORK_RETURN_TIMEOUT_NS)
            return false;
        svcSleepThread(WORK_RETURN_POLL_NS);
    }
}

//-----------------------------------------------------------------------------
// Interface
//-----------------------------------------------------------------------------

Result videoDecoderCreate(VideoDecoder* d, const VideoDecoderConfig* config) {
    memset(d, 0, sizeof(*d));

    const u32 width = (config->width + 1) & ~1u;
    const u32 height = (config->height + 1) & ~1u;
    const u32 image_count = config->image_count ? config->image_count : VIDEO_DECODER_DEFAULT_IMAGES;
    if (!width || !height || image_count < 2 || image_count > VIDDEC_MAX_IMAGES)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    const u32 frame_rate = config->frame_rate ? config->frame_rate : _videoDecoderDefaultFrameRate(width, height);
    d->tiling = config->tiling ? config->tiling : _videoDecoderDefaultTiling(config->codec);
    d->work_size = _videoDecoderWorkSize(config->codec, d->tiling, width, height);
    if (!frame_rate || !d->work_size)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    // The root and nvservices are reference counted; the sync session is the
    // process's, shared with other users such as OunceVK.
    Result rc = gsvInitialize();
    if (R_SUCCEEDED(rc)) {
        d->gsv_initialized = true;
        rc = nvInitialize();
    }
    if (R_SUCCEEDED(rc)) {
        d->nv_initialized = true;
        rc = gsvOpenSync(&d->sync);
    }
    if (R_SUCCEEDED(rc)) {
        d->sync_open = true;
        rc = viddecInitialize();
    }
    if (R_SUCCEEDED(rc))
        d->viddec_initialized = true;

    GsvDecodeImagePreference preference;
    if (R_SUCCEEDED(rc))
        rc = viddecMakeImagePreference(&preference, width, height, ViddecOutputFormat_Nv12, d->tiling);
    for (u32 i = 0; R_SUCCEEDED(rc) && i < image_count; i++) {
        rc = _videoDecoderCreateImage(&d->images[i], &preference, d->tiling);
        if (R_SUCCEEDED(rc))
            d->image_count++;
    }

    if (R_SUCCEEDED(rc)) {
        d->work = aligned_alloc(WORK_ALIGNMENT, d->work_size);
        if (!d->work)
            rc = MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
        else
            armDCacheFlush(d->work, d->work_size);
    }

    if (R_SUCCEEDED(rc))
        rc = viddecOpenDecoder(&d->decoder);
    if (R_SUCCEEDED(rc)) {
        d->decoder_open = true;
        const ViddecDecoderConfig decoder_config = {
            .codec = config->codec,
            .input_format = config->color_policy ? config->color_policy
                : (width >= LARGE_POLICY_WIDTH || height >= LARGE_POLICY_HEIGHT) ? COLOR_POLICY_LARGE : COLOR_POLICY_SMALL,
            .output_format = ViddecOutputFormat_Nv12,
            .output_mode = DECODER_OUTPUT_MODE,
            .width = width,
            .height = height,
            .frame_rate = frame_rate,
            .max_input_size = config->max_sample_size ? config->max_sample_size : (u64)width * height * 3 / 2,
        };
        rc = viddecDecoderInitialize(&d->decoder, &decoder_config, d->work, d->work_size,
                                     config->codec_config, config->codec_config_size);
    }

    for (u32 slot = 0; R_SUCCEEDED(rc) && slot < d->image_count; slot++)
        rc = viddecDecoderRegisterImage(&d->decoder, slot, viddecImagePoolGetImageId(&d->images[slot].pool, 0));

    // Without an event slot, waits poll the counter instead.
    if (R_SUCCEEDED(rc) && R_SUCCEEDED(gsvSyncAllocateSlot(&d->sync, &d->event_slot))) {
        if (R_SUCCEEDED(gsvSyncCreateEvent(&d->sync, d->event_slot, &d->event, false)))
            d->event_open = true;
        else
            gsvSyncFreeSlot(&d->sync, d->event_slot);
    }

    if (R_SUCCEEDED(rc))
        rc = viddecDecoderStart(&d->decoder);
    if (R_SUCCEEDED(rc))
        d->started = true;

    if (R_FAILED(rc))
        videoDecoderClose(d);
    return rc;
}

Result videoDecoderSubmit(VideoDecoder* d, const void* sample, size_t size, s64 timestamp_ns, bool end_of_stream, u64 timeout_ns) {
    const u8* data = sample;
    const u64 start = armGetSystemTick();
    for (;;) {
        u64 accepted = 0;
        Result rc = viddecDecoderSubmit(&d->decoder, data, size, timestamp_ns, end_of_stream, &accepted);
        if (R_SUCCEEDED(rc)) {
            // The service reports the bytes it took. Give it the rest.
            if (accepted == 0 || accepted >= size)
                return 0;
            data += accepted;
            size -= accepted;
            continue;
        }
        if (rc != VIDDEC_RESULT_SUBMIT_BUSY)
            return rc;

        const u64 elapsed = armTicksToNs(armGetSystemTick() - start);
        if (elapsed >= timeout_ns)
            return KERNELRESULT(TimedOut);
        rc = eventWait(&d->decoder.submission_ready, timeout_ns - elapsed);
        if (R_FAILED(rc) && R_VALUE(rc) != KERNELRESULT(TimedOut))
            return rc;
    }
}

Result videoDecoderGetFrame(VideoDecoder* d, VideoFrame* out, u64 timeout_ns) {
    memset(out, 0, sizeof(*out));

    if (!d->pending) {
        Result rc = viddecDecoderAcquireImage(&d->decoder, &d->pending_slot, &d->pending_producer);
        // Every image is held by the program.
        if (rc == VIDDEC_RESULT_NO_IMAGE)
            return KERNELRESULT(TimedOut);
        if (R_FAILED(rc))
            return rc;
        d->pending = true;
    }

    Result rc = _videoDecoderWaitPending(d, timeout_ns);
    ViddecOutputProperties properties = {0};
    if (R_SUCCEEDED(rc))
        rc = _videoDecoderGetProperties(d, d->pending_slot, &properties);
    if (rc == KERNELRESULT(TimedOut))
        return rc;
    if (R_FAILED(rc)) {
        // The image has no frame: it goes back to the decoder, so that the
        // next call waits for the next image.
        _videoDecoderDisarm(d);
        const GsvNativeSync no_dependency = {0};
        viddecDecoderReleaseImage(&d->decoder, d->pending_slot, &no_dependency);
        d->pending = false;
        return rc;
    }

    const VideoDecoderImage* image = &d->images[d->pending_slot];
    for (u32 i = 0; d->tiling == ViddecTilingMode_PitchLinear && i < 2; i++) {
        const GsvCpuImagePlane* plane = &image->descriptor.planes[i];
        u8* data = (u8*)image->backing + plane->offset;
        // The decoder wrote the plane: drop any stale lines before reading.
        armDCacheFlush(data, plane->span);
        out->planes[i] = data;
        out->pitches[i] = (u32)plane->pitch;
    }
    out->tiling = d->tiling;
    out->width = properties.crop_width ? properties.crop_width : properties.width;
    out->height = properties.crop_height ? properties.crop_height : properties.height;
    out->timestamp_ns = properties.timestamp_ns;
    out->end_of_stream = properties.end_of_stream;
    out->slot = d->pending_slot;
    d->pending = false;
    return 0;
}

Result videoDecoderCopyFrame(VideoDecoder* d, const VideoFrame* frame, u8* out_y, size_t y_pitch, u8* out_uv, size_t uv_pitch) {
    if (!frame->width || frame->slot >= d->image_count)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    const VideoDecoderImage* image = &d->images[frame->slot];

    // Copy the frame's visible size, which is at most the image's size.
    GsvCpuImageDescriptor descriptor = image->descriptor;
    if (frame->width > (u32)descriptor.width_minus_one + 1 || frame->height > (u32)descriptor.height_minus_one + 1)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    descriptor.width_minus_one = frame->width - 1;
    descriptor.height_minus_one = frame->height - 1;
    return viddecCopyImageNv12(&descriptor, image->backing, image->backing_size, out_y, y_pitch, out_uv, uv_pitch);
}

void videoDecoderReleaseFrame(VideoDecoder* d, VideoFrame* frame) {
    if (!frame->width)
        return;
    // The CPU read the image: it goes back without a dependency.
    const GsvNativeSync no_dependency = {0};
    viddecDecoderReleaseImage(&d->decoder, frame->slot, &no_dependency);
    memset(frame, 0, sizeof(*frame));
}

Result videoDecoderFlush(VideoDecoder* d) {
    _videoDecoderDisarm(d);
    d->pending = false;
    return viddecDecoderFlush(&d->decoder);
}

void videoDecoderClose(VideoDecoder* d) {
    _videoDecoderDisarm(d);
    if (d->started)
        viddecDecoderStop(&d->decoder);

    // The image pools close before the decoder finalizes, in the movie
    // layer's order. The backings are freed after the decoder closes.
    for (u32 i = 0; i < d->image_count; i++)
        viddecImagePoolClose(&d->images[i].pool);
    if (d->decoder_open)
        viddecDecoderClose(&d->decoder);

    if (d->work) {
        if (_videoDecoderWaitWorkReturned(d->work))
            free(d->work);
    }
    for (u32 i = 0; i < d->image_count; i++)
        free(d->images[i].backing);

    if (d->event_open) {
        gsvSyncFreeSlot(&d->sync, d->event_slot);
        eventClose(&d->event);
    }
    if (d->viddec_initialized)
        viddecExit();
    if (d->sync_open)
        gsvSyncClose(&d->sync);
    if (d->nv_initialized)
        nvExit();
    if (d->gsv_initialized)
        gsvExit();

    memset(d, 0, sizeof(*d));
}
