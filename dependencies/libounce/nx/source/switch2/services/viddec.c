/* SPDX-License-Identifier: ISC
 * Original libounce-nx Switch 2 video decoder client.
 *
 * The GSV1 root admits video decoding and moves a creator session. The
 * creator moves decoder sessions. A decoder receives work memory as transfer
 * memory, the codec configuration and encoded samples, and fills registered
 * images of image pools that the client backs with CPU memory. Each queued
 * image carries a 16-byte producer dependency; a released image carries the
 * consumer's dependency back to the decoder.
 *
 * Commands are named by the TIPC command ID the code dispatches (the raw
 * HIPC type minus 16).
 */
#include <string.h>
#include "switch2/services/viddec.h"
#include "kernel/svc.h"
#include "kernel/tmem.h"
#include "arm/cache.h"
#include "../../services/service_guard.h"

// Block-linear layout: a GOB is 64 bytes wide and 8 rows high (512 bytes).
#define GOB_WIDTH_BYTES   64
#define GOB_HEIGHT_ROWS   8
#define GOB_SIZE_BYTES    512

static TipcService g_viddecCreator;
static bool g_viddecGsvInitialized;

NX_GENERATE_SERVICE_GUARD(viddec);

// Root probe, then the creator with the default flag and the library version.
Result _viddecInitialize(void) {
    Result rc = gsvInitialize();
    if (R_FAILED(rc))
        return rc;
    g_viddecGsvInitialized = true;

    rc = gsvCheckVideoDecoderAccess();
    if (R_SUCCEEDED(rc))
        rc = gsvOpenVideoDecoderCreator(false, VIDDEC_LIBRARY_VERSION, &g_viddecCreator);
    return rc;
}

// The creator closes before the root releases the decoder state.
static void _viddecCleanup(void) {
    if (g_viddecCreator.session) {
        tipcClose(&g_viddecCreator);
        gsvReleaseVideoDecoderAccess();
    }
    if (g_viddecGsvInitialized) {
        gsvExit();
        g_viddecGsvInitialized = false;
    }
}

TipcService* viddecGetServiceSession(void) {
    return &g_viddecCreator;
}

//-----------------------------------------------------------------------------
// Image pools
//-----------------------------------------------------------------------------

// The first u64 selects the output format and tiling: tables for tiling 1
// and 2, indexed by output format minus 2. The second u64 holds width - 1
// and height - 1. The rest is zero.
Result viddecMakeImagePreference(GsvDecodeImagePreference* out, u32 width, u32 height, u32 output_format, ViddecTilingMode tiling_mode) {
    static const u64 pitch_linear_formats[4] = { 0x200000000ULL, 0x100000000ULL, 0xa00000000ULL, 0xb00000000ULL };
    static const u64 block_linear_formats[4] = { 0x500000000ULL, 0x400000000ULL, 0xc00000000ULL, 0xd00000000ULL };

    if (output_format < 2 || output_format > 5)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    if (tiling_mode != ViddecTilingMode_PitchLinear && tiling_mode != ViddecTilingMode_BlockLinear)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    memset(out, 0, sizeof(*out));
    const u64* formats = tiling_mode == ViddecTilingMode_PitchLinear ? pitch_linear_formats : block_linear_formats;
    out->format = formats[output_format - 2];
    out->size = ((height - 1) & 0xFFFF) << 16 | ((width - 1) & 0xFFFF);
    return 0;
}

// The alignment is at least 0x1000, and the size is rounded up to it.
Result viddecGetImagePoolSize(const GsvDecodeImagePreference* preference, u32 count, u64* out_size, u64* out_alignment) {
    u64 size = 0, alignment = 0;
    Result rc = gsvGetDecodeImagePoolSize(preference, count, &size, &alignment);
    if (R_FAILED(rc))
        return rc;

    if (alignment <= 0x1000)
        alignment = 0x1000;
    size = (size + alignment - 1) & ~(alignment - 1);
    if (out_size) *out_size = size;
    if (out_alignment) *out_alignment = alignment;
    return 0;
}

// The size query checks the preference. Then the backing becomes native
// memory, and the images are created in it.
Result viddecImagePoolCreate(ViddecImagePool* out, const GsvDecodeImagePreference* preference, u32 count, void* backing, u64 backing_size) {
    memset(out, 0, sizeof(*out));

    Result rc = viddecGetImagePoolSize(preference, count, NULL, NULL);
    if (R_FAILED(rc))
        return rc;

    rc = gsvCreateNativeMemory(&out->memory, backing, backing_size);
    if (R_FAILED(rc))
        return rc;

    rc = gsvCreateDecodeImagePool(preference, count, out->memory.memory_id, &out->base_image_id);
    if (R_FAILED(rc)) {
        gsvDestroyNativeMemory(&out->memory);
        return rc;
    }

    out->count = count;
    out->backing = backing;
    out->backing_size = backing_size;
    return 0;
}

// The images are destroyed by their base ID, then the native memory.
Result viddecImagePoolClose(ViddecImagePool* p) {
    if (!p->memory.memory_id)
        return 0;

    Result rc = gsvDestroyDecodeImagePool(p->base_image_id);
    if (R_FAILED(rc))
        return rc;

    gsvDestroyNativeMemory(&p->memory);
    memset(p, 0, sizeof(*p));
    return 0;
}

Result viddecImagePoolReadNv12(ViddecImagePool* p, u32 index, u8* out_y, size_t y_pitch, u8* out_uv, size_t uv_pitch, GsvCpuImageDescriptor* out_descriptor) {
    if (index >= p->count)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    GsvCpuImageDescriptor descriptor;
    Result rc = gsvGetCpuImageDescriptor(viddecImagePoolGetImageId(p, index), &descriptor);
    if (R_FAILED(rc))
        return rc;
    if (out_descriptor) *out_descriptor = descriptor;

    return viddecCopyImageNv12(&descriptor, p->backing, p->backing_size, out_y, y_pitch, out_uv, uv_pitch);
}

//-----------------------------------------------------------------------------
// NV12 CPU copy
//-----------------------------------------------------------------------------

// Block-linear layout. Blocks are block[2] elements (64 bytes, one GOB) wide
// and block[3] rows high, in rows of block[0] blocks; the block index is 16
// bits. In a 512-byte GOB, the byte at (x, y) is at
// (x / 32) * 256 + ((y / 4) % 2) * 128 + ((x / 16) % 2) * 64 + (y % 4) * 16 + x % 16.

// Copies one GOB (8 rows of 64 bytes) to pitch-linear rows, in its memory
// order: 32 runs of 16 bytes. width and height clip the GOB at the edge of
// the image.
static void _viddecCopyGob(const u8* gob, u8* out, size_t out_pitch, u32 width, u32 height) {
    if (width == GOB_WIDTH_BYTES && height == GOB_HEIGHT_ROWS) {
        // A whole GOB: fixed 16-byte moves, without the edge checks. Each
        // 64-byte line holds rows y..y+3 of one 16-byte column x.
        for (u32 line = 0; line < GOB_SIZE_BYTES / 64; line++) {
            const u8* in = gob + line * 64;
            const u32 x = ((line >> 2) * 2 + (line & 1)) * 16;
            u8* row = out + ((line >> 1) & 1) * 4 * out_pitch + x;
            memcpy(row, in, 16);
            memcpy(row + out_pitch, in + 16, 16);
            memcpy(row + 2 * out_pitch, in + 32, 16);
            memcpy(row + 3 * out_pitch, in + 48, 16);
        }
        return;
    }
    for (u32 run = 0; run < GOB_SIZE_BYTES / 16; run++) {
        // The GOB offset above, inverted for each 16-byte run.
        const u32 row = ((run >> 3) & 1) * 4 + (run & 3);
        const u32 x = ((run >> 4) * 2 + ((run >> 2) & 1)) * 16;
        if (row >= height || x >= width)
            continue;
        const u32 bytes = width - x < 16 ? width - x : 16;
        memcpy(out + row * out_pitch + x, gob + run * 16, bytes);
    }
}

// Copies a block-linear plane to pitch-linear rows. A block is one GOB wide
// and block[3] rows high; the GOBs are read in memory order.
static void _viddecCopyBlockLinear(const GsvCpuImagePlane* plane, const u8* backing, u32 element_bytes,
                                   u32 elements, u32 rows, u8* out, size_t out_pitch) {
    const u32 row_bytes = elements * element_bytes;
    const u32 blocks_across = plane->block[0];
    const u32 block_height = plane->block[3];
    const size_t block_bytes = (size_t)GOB_WIDTH_BYTES * block_height;
    const u32 block_rows = (rows + block_height - 1) / block_height;
    const u32 gob_columns = (row_bytes + GOB_WIDTH_BYTES - 1) / GOB_WIDTH_BYTES;

    for (u32 block_row = 0; block_row < block_rows; block_row++) {
        for (u32 column = 0; column < gob_columns; column++) {
            const u32 block = (block_row * blocks_across + column) & 0xFFFF;
            const u8* block_data = backing + plane->offset + block * block_bytes;
            const u32 x = column * GOB_WIDTH_BYTES;
            const u32 width = row_bytes - x < GOB_WIDTH_BYTES ? row_bytes - x : GOB_WIDTH_BYTES;
            for (u32 gob = 0; gob < block_height / GOB_HEIGHT_ROWS; gob++) {
                const u32 y = block_row * block_height + gob * GOB_HEIGHT_ROWS;
                if (y >= rows)
                    break;
                const u32 height = rows - y < GOB_HEIGHT_ROWS ? rows - y : GOB_HEIGHT_ROWS;
                _viddecCopyGob(block_data + gob * GOB_SIZE_BYTES, out + (size_t)y * out_pitch + x, out_pitch,
                               width, height);
            }
        }
    }
}

static void _viddecCopyPitchLinear(const GsvCpuImagePlane* plane, const u8* backing, u32 row_bytes, u32 rows,
                                   u8* out, size_t out_pitch) {
    for (u32 y = 0; y < rows; y++)
        memcpy(out + y * out_pitch, backing + plane->offset + y * plane->pitch, row_bytes);
}

// A block-linear plane must cover the image with its blocks, and its span
// must hold all of its blocks.
static bool _viddecBlockLinearCovers(const GsvCpuImagePlane* plane, u32 element_bytes, u32 elements, u32 rows) {
    const u64 blocks_across = plane->block[0], block_rows = plane->block[1];
    const u64 block_width = plane->block[2], block_height = plane->block[3];
    if (!block_width || !block_height || block_width * element_bytes != GOB_WIDTH_BYTES || block_height % GOB_HEIGHT_ROWS)
        return false;
    if (blocks_across * block_width < elements || block_rows * block_height < rows)
        return false;
    return blocks_across * block_rows * GOB_WIDTH_BYTES * block_height <= plane->span;
}

static bool _viddecPitchLinearCovers(const GsvCpuImagePlane* plane, u32 row_bytes, u32 rows) {
    return plane->pitch >= row_bytes && (rows == 0 || plane->pitch * (rows - 1) + row_bytes <= plane->span);
}

// Luma has one byte per element. Chroma has two (U, V) per element, with
// half the width and height. The CPU reader flushes the data cache of each
// plane before it reads it.
Result viddecCopyImageNv12(const GsvCpuImageDescriptor* descriptor, const void* backing, u64 backing_size, u8* out_y, size_t y_pitch, u8* out_uv, size_t uv_pitch) {
    const u32 width = (u32)descriptor->width_minus_one + 1;
    const u32 height = (u32)descriptor->height_minus_one + 1;
    const GsvCpuImagePlane* y_plane = &descriptor->planes[0];
    const GsvCpuImagePlane* uv_plane = &descriptor->planes[1];
    const bool block_linear = descriptor->type == GsvCpuImageType_Nv12BlockLinear;

    if (descriptor->type != GsvCpuImageType_Nv12PitchLinear && descriptor->type != GsvCpuImageType_Nv12PitchLinearAlt && !block_linear)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    if ((u64)y_plane->offset + y_plane->span > backing_size || (u64)uv_plane->offset + uv_plane->span > backing_size)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    const bool covered = block_linear
        ? _viddecBlockLinearCovers(y_plane, 1, width, height) && _viddecBlockLinearCovers(uv_plane, 2, width / 2, height / 2)
        : _viddecPitchLinearCovers(y_plane, width, height) && _viddecPitchLinearCovers(uv_plane, width, height / 2);
    if (!covered)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    const u8* base = (const u8*)backing;
    armDCacheFlush((void*)(base + y_plane->offset), y_plane->span);
    armDCacheFlush((void*)(base + uv_plane->offset), uv_plane->span);

    if (block_linear) {
        _viddecCopyBlockLinear(y_plane, base, 1, width, height, out_y, y_pitch);
        _viddecCopyBlockLinear(uv_plane, base, 2, width / 2, height / 2, out_uv, uv_pitch);
    } else {
        _viddecCopyPitchLinear(y_plane, base, width, height, out_y, y_pitch);
        _viddecCopyPitchLinear(uv_plane, base, width, height / 2, out_uv, uv_pitch);
    }
    return 0;
}

//-----------------------------------------------------------------------------
// Decoder sessions
//-----------------------------------------------------------------------------

// Creator command 1: no input; moves the decoder session.
Result viddecOpenDecoder(ViddecDecoder* out) {
    memset(out, 0, sizeof(*out));
    Handle h = INVALID_HANDLE;

    Result rc = tipcDispatch(&g_viddecCreator, 1,
        .out_handle_attrs = { SfOutHandleAttr_HipcMove },
        .out_handles = &h,
    );
    if (R_SUCCEEDED(rc)) tipcCreate(&out->s, h);
    return rc;
}

// Decoder command 115 finalizes; its reply has no result. The session closes
// after it.
static void _viddecDecoderFinalize(ViddecDecoder* d) {
    tipcDispatchWithoutResult(&d->s, 115);
    d->initialized = false;
}

void viddecDecoderClose(ViddecDecoder* d) {
    if (d->initialized) {
        eventClose(&d->submission_ready);
        _viddecDecoderFinalize(d);
    }
    if (d->s.session)
        tipcClose(&d->s);
    memset(d, 0, sizeof(*d));
}

// Decoder command 101: the configuration bytes as a mapped input buffer;
// width, height, frame rate and the low 32 bits of the maximum input size,
// then 16 zero bytes. Then command 3 copies the submission-ready event,
// which clears automatically. When either fails, the decoder is finalized.
static Result _viddecDecoderConfigure(ViddecDecoder* d, const ViddecDecoderConfig* config, const void* codec_config, size_t codec_config_size) {
    const struct {
        u32 width;
        u32 height;
        u32 frame_rate;
        u32 max_input_size;
        u8 reserved[16];
    } in = {
        .width = config->width & 0xFFFF,
        .height = config->height & 0xFFFF,
        .frame_rate = config->frame_rate,
        .max_input_size = (u32)config->max_input_size,
    };
    Result rc = tipcDispatchIn(&d->s, 101, in,
        .buffer_attrs = { SfBufferAttr_In | SfBufferAttr_HipcMapAlias },
        .buffers = { { codec_config, codec_config_size } },
    );

    Handle event = INVALID_HANDLE;
    if (R_SUCCEEDED(rc)) {
        rc = tipcDispatch(&d->s, 3,
            .out_handle_attrs = { SfOutHandleAttr_HipcCopy },
            .out_handles = &event,
        );
    }

    if (R_SUCCEEDED(rc))
        eventLoadRemote(&d->submission_ready, event, true);
    else
        _viddecDecoderFinalize(d);
    return rc;
}

// Decoder command 201: the transfer memory as a COPY handle; u64 size, then
// u32 codec, input format, output format and output mode. The transfer memory
// has no CPU permission. The local handle closes after the configuration;
// the service keeps its own reference.
Result viddecDecoderInitialize(ViddecDecoder* d, const ViddecDecoderConfig* config, void* work, u64 work_size, const void* codec_config, size_t codec_config_size) {
    TransferMemory tmem;
    Result rc = tmemCreateFromMemory(&tmem, work, work_size, Perm_None);
    if (R_FAILED(rc))
        return rc;

    const struct {
        u64 work_size;
        u32 codec;
        u32 input_format;
        u32 output_format;
        u32 output_mode;
    } in = { work_size, config->codec, config->input_format, config->output_format, config->output_mode };
    rc = tipcDispatchIn(&d->s, 201, in,
        .in_num_handles = 1,
        .in_handles = { tmem.handle },
    );
    if (R_SUCCEEDED(rc)) {
        d->initialized = true;
        rc = _viddecDecoderConfigure(d, config, codec_config, codec_config_size);
    }

    tmemCloseHandle(&tmem);
    return rc;
}

static void _viddecQueueSlot(ViddecDecoder* d, u32 slot) {
    d->queue[(d->queue_head + d->queue_count) % VIDDEC_MAX_IMAGES] = slot;
    d->queue_count++;
}

// Decoder command 311: image ID, then 12 zero bytes. The reply has 32
// bytes; the first 16 are the producer dependency.
Result viddecDecoderRegisterImage(ViddecDecoder* d, u32 slot, u32 image_id) {
    if (slot >= VIDDEC_MAX_IMAGES || d->images[slot].registered)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    const struct {
        u32 image_id;
        u8 reserved[12];
    } in = { .image_id = image_id };
    struct {
        GsvNativeSync producer;
        u8 reserved[16];
    } out = {};

    Result rc = tipcDispatchInOut(&d->s, 311, in, out);
    if (R_FAILED(rc))
        return rc;

    d->images[slot].registered = true;
    d->images[slot].image_id = image_id;
    d->producers[slot] = out.producer;
    _viddecQueueSlot(d, slot);
    return 0;
}

// Decoder command 212: image ID, then 28 zero bytes.
Result viddecDecoderUnregisterImage(ViddecDecoder* d, u32 slot) {
    if (slot >= VIDDEC_MAX_IMAGES || !d->images[slot].registered)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    const struct {
        u32 image_id;
        u8 reserved[28];
    } in = { .image_id = d->images[slot].image_id };

    Result rc = tipcDispatchIn(&d->s, 212, in);
    if (R_SUCCEEDED(rc))
        d->images[slot].registered = false;
    return rc;
}

// Decoder commands 6 (start), 207 (stop) and 8 (flush): no input.
Result viddecDecoderStart(ViddecDecoder* d) {
    return tipcDispatch(&d->s, 6);
}

Result viddecDecoderStop(ViddecDecoder* d) {
    return tipcDispatch(&d->s, 207);
}

Result viddecDecoderFlush(ViddecDecoder* d) {
    return tipcDispatch(&d->s, 8);
}

// Decoder command 109: the encoded bytes as a mapped input buffer, the
// timestamp in microseconds (nanoseconds / 1000, rounded toward zero) and
// the end-of-stream byte. Returns a u64.
Result viddecDecoderSubmit(ViddecDecoder* d, const void* data, size_t size, s64 timestamp_ns, bool end_of_stream, u64* out_accepted) {
    const struct {
        s64 timestamp_us;
        u8 end_of_stream;
        u8 pad[3];
    } NX_PACKED in = { .timestamp_us = timestamp_ns / 1000, .end_of_stream = end_of_stream ? 1 : 0 };
    u64 accepted = 0;

    Result rc = tipcDispatchInOut(&d->s, 109, in, accepted,
        .buffer_attrs = { SfBufferAttr_In | SfBufferAttr_HipcMapAlias },
        .buffers = { { data, size } },
    );
    if (R_SUCCEEDED(rc) && out_accepted) *out_accepted = accepted;
    return rc;
}

Result viddecDecoderAcquireImage(ViddecDecoder* d, u32* out_slot, GsvNativeSync* out_producer) {
    if (!d->queue_count)
        return VIDDEC_RESULT_NO_IMAGE;

    const u32 slot = d->queue[d->queue_head];
    d->queue_head = (d->queue_head + 1) % VIDDEC_MAX_IMAGES;
    d->queue_count--;
    *out_slot = slot;
    *out_producer = d->producers[slot];
    return 0;
}

// Decoder command 213: no input. It takes the next decoded image from the
// service and returns 64 bytes: image ID, configured width and height, crop
// width and height, a zero u32, the timestamp in nanoseconds and the
// end-of-stream byte. Each reply is cached for the slot that has its image
// ID, until the requested slot has properties. A reply for an image that no
// slot has is not cached.
Result viddecDecoderGetOutputProperties(ViddecDecoder* d, u32 slot, ViddecOutputProperties* out) {
    if (slot >= VIDDEC_MAX_IMAGES)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    while (!d->cache[slot].valid) {
        struct {
            u32 image_id;
            u32 width;
            u32 height;
            u32 crop_width;
            u32 crop_height;
            u32 reserved_x14;
            s64 timestamp_ns;
            u8 end_of_stream;
            u8 reserved_x21[31];
        } reply = {};

        Result rc = tipcDispatchOut(&d->s, 213, reply);
        if (R_FAILED(rc))
            return rc;

        for (u32 i = 0; i < VIDDEC_MAX_IMAGES; i++) {
            if (!d->images[i].registered || d->images[i].image_id != reply.image_id)
                continue;
            d->cache[i].valid = true;
            d->cache[i].properties = (ViddecOutputProperties){
                .width = reply.width,
                .height = reply.height,
                .crop_width = reply.crop_width,
                .crop_height = reply.crop_height,
                .timestamp_ns = reply.timestamp_ns,
                .end_of_stream = reply.end_of_stream & 1,
            };
            break;
        }
    }

    *out = d->cache[slot].properties;
    return 0;
}

// Decoder command 314: image ID, a zero u32, the 16-byte consumer
// dependency and 8 zero bytes. The reply has 32 bytes; the first 16 are the
// slot's next producer dependency. The slot is queued again and its cached
// properties are dropped.
Result viddecDecoderReleaseImage(ViddecDecoder* d, u32 slot, const GsvNativeSync* consumer) {
    if (slot >= VIDDEC_MAX_IMAGES || !d->images[slot].registered)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    const struct {
        u32 image_id;
        u32 reserved_x4;
        GsvNativeSync consumer;
        u8 reserved_x18[8];
    } in = { .image_id = d->images[slot].image_id, .consumer = *consumer };
    struct {
        GsvNativeSync producer;
        u8 reserved[16];
    } out = {};

    Result rc = tipcDispatchInOut(&d->s, 314, in, out);
    if (R_FAILED(rc))
        return rc;

    d->producers[slot] = out.producer;
    _viddecQueueSlot(d, slot);
    d->cache[slot].valid = false;
    return 0;
}
