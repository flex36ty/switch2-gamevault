/**
 * @file decoder.h
 * @brief Hardware video decoding on Switch 2, as one object.
 * @author kurumuz
 * @copyright 2026 libounce contributors
 * @license ISC
 * @note A VideoDecoder owns everything that decoding needs: the GSV root and
 *       the process's sync session (both shared with other users, such as
 *       OunceVK), the decoder session, its work memory and the output images.
 *       The program gives it the codec configuration and encoded samples, and
 *       receives decoded frames as NV12 images. It can be used while OunceVK
 *       renders in the same process.
 *
 *       Pitch-linear frames are read in place. Block-linear frames (the GPU's
 *       tiled layout, which AV1 requires) are read with \ref videoDecoderCopyFrame.
 *
 *       The service limits the total decode rate of all of its decoders to
 *       \ref VIDEO_DECODER_PIXEL_RATE pixels per second, as width * height *
 *       frame rate (3840x2160 at 30 frames per second), and the frame rate of
 *       each to 60. A decoder that does not fit fails to create.
 *
 *       Typical use:
 *
 *           VideoDecoder decoder;
 *           VideoDecoderConfig config = { .codec = ViddecCodec_H264, .width = 1920, .height = 1080,
 *                                         .codec_config = sps_pps, .codec_config_size = sps_pps_size };
 *           Result rc = videoDecoderCreate(&decoder, &config);
 *           // For each sample:
 *           rc = videoDecoderSubmit(&decoder, sample, sample_size, timestamp_ns, last, UINT64_MAX);
 *           VideoFrame frame;
 *           while (R_SUCCEEDED(videoDecoderGetFrame(&decoder, &frame, 0))) {
 *               // Read frame.planes[0] (Y) and frame.planes[1] (UV).
 *               videoDecoderReleaseFrame(&decoder, &frame);
 *           }
 *           videoDecoderClose(&decoder);
 */
#pragma once
#if !defined(__SWITCH2__)
#error "The video decoder is available only in a native Switch 2 build"
#endif

#include "../../switch/types.h"
#include "../../switch/kernel/event.h"
#include "../services/gsv.h"
#include "../services/viddec.h"

/// Number of output images when \ref VideoDecoderConfig.image_count is 0.
#define VIDEO_DECODER_DEFAULT_IMAGES 4

/// Largest frame rate of a decoder.
#define VIDEO_DECODER_MAX_FRAME_RATE 60

/// Pixels per second (width * height * frame rate) that the service decodes in all of its decoders.
#define VIDEO_DECODER_PIXEL_RATE 248832000ULL

/// Decoder parameters for \ref videoDecoderCreate. Zero fields get defaults.
typedef struct {
    ViddecCodec codec;          ///< Codec.
    u32 width;                  ///< Largest coded width of the stream.
    u32 height;                 ///< Largest coded height of the stream.
    u32 image_count;            ///< Output images, 2 to \ref VIDDEC_MAX_IMAGES. 0: \ref VIDEO_DECODER_DEFAULT_IMAGES.
    u32 frame_rate;             ///< Frames per second, at most \ref VIDEO_DECODER_MAX_FRAME_RATE. 0: the largest that \ref VIDEO_DECODER_PIXEL_RATE allows for the size (60 at 1920x1080, 30 at 3840x2160).
    ViddecTilingMode tiling;    ///< Output layout. 0: block-linear for AV1, which requires it; else pitch-linear.
    u32 color_policy;           ///< Input format (color policy) 1 to 7; see \ref viddecDecoderInitialize. 0: from the size.
    u64 max_sample_size;        ///< Largest encoded sample, in bytes. 0: width * height * 3 / 2.
    const void* codec_config;   ///< Codec configuration. H.264: the SPS and PPS NAL units, each after 00 00 00 01.
    size_t codec_config_size;   ///< Size of the codec configuration.
} VideoDecoderConfig;

/// A decoded frame from \ref videoDecoderGetFrame. Return it with \ref videoDecoderReleaseFrame.
typedef struct {
    u32 width;                  ///< Visible width.
    u32 height;                 ///< Visible height.
    s64 timestamp_ns;           ///< Timestamp of the sample.
    bool end_of_stream;         ///< This is the last frame of the stream.
    ViddecTilingMode tiling;    ///< Layout of the planes.
    const u8* planes[2];        ///< Pitch-linear only: the Y plane, then the interleaved UV plane (half height). NULL when block-linear.
    u32 pitches[2];             ///< Pitch-linear only: bytes from one row of each plane to the next.
    u32 slot;                   ///< Decoder slot; used by \ref videoDecoderReleaseFrame.
} VideoFrame;

/// One output image: an NV12 image pool with one image.
typedef struct {
    ViddecImagePool pool;
    void* backing;
    u64 backing_size;
    GsvCpuImageDescriptor descriptor;
} VideoDecoderImage;

/// A hardware video decoder from \ref videoDecoderCreate.
typedef struct {
    // What videoDecoderCreate has done, for videoDecoderClose to undo.
    bool gsv_initialized;
    bool nv_initialized;
    bool sync_open;
    bool viddec_initialized;
    bool decoder_open;
    bool started;
    ViddecDecoder decoder;
    GsvSync sync;
    VideoDecoderImage images[VIDDEC_MAX_IMAGES];
    u32 image_count;            // Images with a pool.
    ViddecTilingMode tiling;
    void* work;
    u64 work_size;
    // The image that videoDecoderGetFrame acquired and waits for.
    bool pending;
    u32 pending_slot;
    GsvNativeSync pending_producer;
    bool pending_armed;
    // Completion event on a slot of the sync session, or polling without it.
    bool event_open;
    u32 event_slot;
    Event event;
} VideoDecoder;

/**
 * @brief Creates a decoder: opens the services, allocates the work memory and
 *        the output images, then initializes and starts the decoder.
 * @param[in] config Decoder parameters.
 * @note The work memory size depends on the codec, size and layout; at
 *       3840x2160 it is about 272 MiB (pitch-linear) or 308 MiB
 *       (block-linear), plus about 12 MiB for each NV12 image.
 */
Result videoDecoderCreate(VideoDecoder* d, const VideoDecoderConfig* config);

/**
 * @brief Gives the decoder one encoded sample.
 * @param[in] sample Sample. H.264: one access unit, NAL units after start codes.
 * @param[in] timestamp_ns Timestamp, returned in the sample's \ref VideoFrame.
 * @param[in] end_of_stream This is the last sample.
 * @param[in] timeout_ns Longest wait while the decoder does not take input.
 * @note The decoder takes no input while every output image is held: release
 *       frames, or get them with \ref videoDecoderGetFrame, between submissions.
 *       Gives KERNELRESULT(TimedOut) when the timeout ends first.
 */
Result videoDecoderSubmit(VideoDecoder* d, const void* sample, size_t size, s64 timestamp_ns, bool end_of_stream, u64 timeout_ns);

/**
 * @brief Gets the next decoded frame.
 * @param[out] out Frame. Its planes stay valid until \ref videoDecoderReleaseFrame.
 * @param[in] timeout_ns Longest wait. 0 checks without waiting.
 * @note Gives KERNELRESULT(TimedOut) when no frame is decoded in time. The
 *       next call continues to wait for the same frame.
 */
Result videoDecoderGetFrame(VideoDecoder* d, VideoFrame* out, u64 timeout_ns);

/**
 * @brief Copies a frame into pitch-linear NV12 planes, in either layout.
 * @param[out] out_y Y plane, frame->height rows of frame->width bytes.
 * @param[out] out_uv Interleaved UV plane, frame->height / 2 rows of frame->width bytes.
 */
Result videoDecoderCopyFrame(VideoDecoder* d, const VideoFrame* frame, u8* out_y, size_t y_pitch, u8* out_uv, size_t uv_pitch);

/// Returns a frame's image to the decoder.
void videoDecoderReleaseFrame(VideoDecoder* d, VideoFrame* frame);

/// Discards the samples and frames that the decoder holds, for example before a seek.
Result videoDecoderFlush(VideoDecoder* d);

/**
 * @brief Stops and closes the decoder and frees its memory.
 * @note The service gives the work memory back to the process asynchronously.
 *       If it has not done so after 2 seconds, that memory is not freed.
 */
void videoDecoderClose(VideoDecoder* d);
