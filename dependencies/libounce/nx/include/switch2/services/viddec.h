/**
 * @file viddec.h
 * @brief Native Switch 2 hardware video decoder sessions, through the GSV1 root.
 * @author kurumuz
 * @copyright 2026 libounce contributors
 * @license ISC
 * @note The service parses the bitstream and runs the decoder hardware. A
 *       client gives it codec configuration and encoded samples, and receives
 *       decoded images in image pools that the client backs with CPU memory.
 */
#pragma once
#if !defined(__SWITCH2__)
#error "The video decoder is available only in a native Switch 2 build"
#endif

#include "../../switch/types.h"
#include "../../switch/sf/tipc.h"
#include "../../switch/kernel/event.h"
#include "gsv.h"

/// Number of image slots in a \ref ViddecDecoder.
#define VIDDEC_MAX_IMAGES 8

/// Library version that \ref viddecInitialize sends.
#define VIDDEC_LIBRARY_VERSION 0x0000000300030000ULL

/// \ref viddecDecoderAcquireImage result when no registered image is queued.
#define VIDDEC_RESULT_NO_IMAGE MAKERESULT(111, 3604)

/// \ref viddecDecoderSubmit result when the decoder does not take more input now.
#define VIDDEC_RESULT_SUBMIT_BUSY MAKERESULT(111, 3603)

/// Codec.
typedef enum {
    ViddecCodec_H264 = 0, ///< H.264/AVC.
    ViddecCodec_Vp9  = 1, ///< VP9.
    ViddecCodec_Av1  = 3, ///< AV1.
    ViddecCodec_Vp8  = 4, ///< VP8.
} ViddecCodec;

/// Output image format.
typedef enum {
    ViddecOutputFormat_Rgba    = 2, ///< 8-bit RGBA.
    ViddecOutputFormat_Nv12    = 3, ///< 8-bit Y plane and interleaved 8-bit UV plane, 4:2:0.
    ViddecOutputFormat_RgbaHdr = 4, ///< RGBA HDR. Only VP9 and AV1.
} ViddecOutputFormat;

/// Image tiling mode of an image pool.
typedef enum {
    ViddecTilingMode_PitchLinear = 1, ///< Pitch-linear planes (NV12 gives \ref GsvCpuImageType_Nv12PitchLinear).
    ViddecTilingMode_BlockLinear = 2, ///< Block-linear planes (NV12 gives \ref GsvCpuImageType_Nv12BlockLinear).
} ViddecTilingMode;

/// Decoder parameters for \ref viddecDecoderInitialize.
typedef struct {
    ViddecCodec codec;                  ///< Codec.
    u32 input_format;                   ///< Color policy, 1 to 7. See the note at \ref viddecDecoderInitialize.
    ViddecOutputFormat output_format;   ///< Output image format.
    u32 output_mode;                    ///< Output mode. Use 1.
    u32 width;                          ///< Width. The low 16 bits are sent.
    u32 height;                         ///< Height. The low 16 bits are sent.
    u32 frame_rate;                     ///< Frames per second.
    u64 max_input_size;                 ///< Largest encoded sample, in bytes. The low 32 bits are sent.
} ViddecDecoderConfig;

/// Properties of a decoded image.
typedef struct {
    u32 width;              ///< Configured width.
    u32 height;             ///< Configured height.
    u32 crop_width;         ///< Width of the visible area.
    u32 crop_height;        ///< Height of the visible area.
    s64 timestamp_ns;       ///< Timestamp of the sample, in nanoseconds.
    bool end_of_stream;     ///< This is the last image of the stream.
} ViddecOutputProperties;

/// A decoder session from \ref viddecOpenDecoder.
typedef struct {
    TipcService s;                              ///< Decoder session.
    bool initialized;                           ///< \ref viddecDecoderInitialize succeeded.
    Event submission_ready;                     ///< Signals when the decoder can take more input.
    struct {
        bool registered;
        u32 image_id;
    } images[VIDDEC_MAX_IMAGES];                ///< Registered image of each slot.
    GsvNativeSync producers[VIDDEC_MAX_IMAGES]; ///< Producer dependency of each queued slot.
    u32 queue[VIDDEC_MAX_IMAGES];               ///< Queue of slots that \ref viddecDecoderAcquireImage returns.
    u32 queue_head;
    u32 queue_count;
    struct {
        bool valid;
        ViddecOutputProperties properties;
    } cache[VIDDEC_MAX_IMAGES];                 ///< Properties received for each slot.
} ViddecDecoder;

/// An image pool from \ref viddecImagePoolCreate.
typedef struct {
    GsvNativeMemory memory; ///< Native memory of the backing.
    u32 base_image_id;      ///< ID of image 0.
    u32 count;              ///< Number of images.
    void* backing;          ///< CPU backing.
    u64 backing_size;       ///< Size of the CPU backing.
} ViddecImagePool;

///@name Creator
///@{

/**
 * @brief Initializes video decoding on the GSV1 root and opens the creator session.
 * @note This initializes the GSV1 root (\ref gsvInitialize). Decoder image memory
 *       uses the mode that \ref gsvRegisterFrame permits, so register the frame mode first.
 */
Result viddecInitialize(void);

/// Closes the creator session and releases the root's video decoder state.
void viddecExit(void);

/// Gets the creator session.
TipcService* viddecGetServiceSession(void);

///@}

///@name Image pools
///@{

/**
 * @brief Fills an image preference.
 * @param[in] width Image width.
 * @param[in] height Image height.
 * @param[in] output_format Output format (2 to 5).
 * @param[in] tiling_mode Tiling mode.
 * @return LibnxError_BadInput for an output format or tiling mode outside the table.
 */
Result viddecMakeImagePreference(GsvDecodeImagePreference* out, u32 width, u32 height, u32 output_format, ViddecTilingMode tiling_mode);

/**
 * @brief Gets the backing size and alignment for an image pool.
 * @param[out] out_size Size, rounded up to the alignment.
 * @param[out] out_alignment Alignment, at least 0x1000.
 */
Result viddecGetImagePoolSize(const GsvDecodeImagePreference* preference, u32 count, u64* out_size, u64* out_alignment);

/**
 * @brief Creates an image pool in CPU backing.
 * @param[out] out Pool. Close it with \ref viddecImagePoolClose.
 * @param[in] backing CPU backing, aligned as \ref viddecGetImagePoolSize reports. It must
 *                    stay allocated until the pool is closed.
 * @param[in] backing_size Backing size, at least the size that \ref viddecGetImagePoolSize reports.
 */
Result viddecImagePoolCreate(ViddecImagePool* out, const GsvDecodeImagePreference* preference, u32 count, void* backing, u64 backing_size);

/**
 * @brief Destroys the pool's images, then releases its native memory.
 * @note If the image destruction fails, the pool stays open and this returns its result.
 */
Result viddecImagePoolClose(ViddecImagePool* p);

/// Gets the image ID of image \p index.
NX_CONSTEXPR u32 viddecImagePoolGetImageId(const ViddecImagePool* p, u32 index) {
    return p->base_image_id + index;
}

/**
 * @brief Copies an NV12 image of a pool into linear planes.
 * @param[in] index Image index.
 * @param[out] out_y Luma plane: height rows of width bytes.
 * @param[in] y_pitch Bytes from one luma row to the next.
 * @param[out] out_uv Interleaved chroma plane: height / 2 rows of width bytes.
 * @param[in] uv_pitch Bytes from one chroma row to the next.
 * @param[out] out_descriptor The image's CPU descriptor. Can be NULL.
 * @note Wait for the image's producer dependency first. The size of the image is in the descriptor.
 */
Result viddecImagePoolReadNv12(ViddecImagePool* p, u32 index, u8* out_y, size_t y_pitch, u8* out_uv, size_t uv_pitch, GsvCpuImageDescriptor* out_descriptor);

/**
 * @brief Copies an NV12 image that a CPU descriptor describes into linear planes.
 * @param[in] descriptor CPU descriptor from \ref gsvGetCpuImageDescriptor.
 * @param[in] backing CPU backing of the image's pool.
 * @param[in] backing_size Size of the backing.
 * @note This flushes the data cache of each plane before it reads the plane.
 * @return LibnxError_BadInput for a descriptor type other than 0x0a, 0x0d or 0x14, or for planes outside the backing.
 */
Result viddecCopyImageNv12(const GsvCpuImageDescriptor* descriptor, const void* backing, u64 backing_size, u8* out_y, size_t y_pitch, u8* out_uv, size_t uv_pitch);

///@}

///@name Decoder sessions
///@{

/**
 * @brief Opens a decoder session from the creator.
 * @param[out] out Decoder. Close it with \ref viddecDecoderClose.
 */
Result viddecOpenDecoder(ViddecDecoder* out);

/**
 * @brief Finalizes the decoder if it was initialized, then closes its session.
 * @note After this the service releases the work memory. Do not use or free
 *       that memory until the kernel restores its permission.
 */
void viddecDecoderClose(ViddecDecoder* d);

/**
 * @brief Initializes the decoder with work memory, then configures it.
 * @param[in] config Decoder parameters.
 * @param[in] work Page-aligned work memory, given to the service as transfer memory
 *                 without CPU permission. Keep it allocated until the decoder is closed.
 * @param[in] work_size Size of the work memory: a page multiple, at least 0x4000.
 * @param[in] codec_config Codec configuration. For H.264: the SPS and PPS NAL units, each after a 00 00 00 01 start code.
 * @param[in] codec_config_size Size of the codec configuration.
 * @note Input format (color policy): with color metadata (standard s, range r,
 *       transfer t): s 2-5 gives 2 if r=1 and t=3, else 1; s=1 gives 4 if r=1
 *       and t=3, else 3; s 6-7 gives 7 if r=2 and t=3, 6 if r=1 and t=6, else 5.
 *       Without metadata: 3 if the maximum width >= 1920 or height >= 1080, else 1.
 * @note If the configuration fails, the decoder is finalized.
 */
Result viddecDecoderInitialize(ViddecDecoder* d, const ViddecDecoderConfig* config, void* work, u64 work_size, const void* codec_config, size_t codec_config_size);

/**
 * @brief Registers an image in a slot as an output of the decoder.
 * @param[in] slot Slot, 0 to 7.
 * @param[in] image_id Image ID (\ref viddecImagePoolGetImageId).
 * @note The slot is queued with its producer dependency. Register images before \ref viddecDecoderStart.
 */
Result viddecDecoderRegisterImage(ViddecDecoder* d, u32 slot, u32 image_id);

/// Unregisters the image of a slot.
Result viddecDecoderUnregisterImage(ViddecDecoder* d, u32 slot);

/// Starts decoding.
Result viddecDecoderStart(ViddecDecoder* d);

/// Stops decoding.
Result viddecDecoderStop(ViddecDecoder* d);

/// Flushes the decoder's input and output.
Result viddecDecoderFlush(ViddecDecoder* d);

/**
 * @brief Submits encoded input.
 * @param[in] data Encoded bytes. For H.264: NAL units, each after a 00 00 00 01 start code.
 * @param[in] size Size of the data.
 * @param[in] timestamp_ns Timestamp in nanoseconds. It is sent in microseconds.
 * @param[in] end_of_stream This is the last input.
 * @param[out] out_accepted Value that the service returns. Can be NULL.
 * @note A successful submission is not a decoded image.
 * @note When the decoder cannot take more input, this returns \ref VIDDEC_RESULT_SUBMIT_BUSY.
 *       Wait for \ref ViddecDecoder::submission_ready, then submit the same input again.
 */
Result viddecDecoderSubmit(ViddecDecoder* d, const void* data, size_t size, s64 timestamp_ns, bool end_of_stream, u64* out_accepted);

/**
 * @brief Takes the next queued slot and its producer dependency. There is no IPC.
 * @param[out] out_slot Slot.
 * @param[out] out_producer Producer dependency. The image holds a decoded frame when it is reached.
 * @return \ref VIDDEC_RESULT_NO_IMAGE when no slot is queued.
 */
Result viddecDecoderAcquireImage(ViddecDecoder* d, u32* out_slot, GsvNativeSync* out_producer);

/**
 * @brief Gets the properties of a slot's decoded image.
 * @param[in] slot Slot from \ref viddecDecoderAcquireImage. Wait for its producer dependency first.
 * @note The service returns decoded images in order. This function caches the
 *       properties of other slots that it receives before the requested slot.
 */
Result viddecDecoderGetOutputProperties(ViddecDecoder* d, u32 slot, ViddecOutputProperties* out);

/**
 * @brief Returns a slot's image to the decoder.
 * @param[in] slot Slot.
 * @param[in] consumer Dependency that the decoder waits for before it writes the image
 *                     again. Use an empty value when the image is not in use.
 * @note The slot is queued again with a new producer dependency.
 */
Result viddecDecoderReleaseImage(ViddecDecoder* d, u32 slot, const GsvNativeSync* consumer);

///@}
