/**
 * @file nro.h
 * @brief NRO headers.
 * @copyright libnx Authors
 */

#pragma once

#include "types.h"

#define NROHEADER_MAGIC 0x304f524e
#define NROMOD0_MAGIC   0x30444f4d ///< "MOD0"
#define NROOUNCE_MAGIC  0x304e554f ///< "OUN0", see \ref NroOunceInfo

#define NROASSETHEADER_MAGIC 0x54455341
#define NROASSETHEADER_VERSION 0

/// Entry for each segment in the codebin.
typedef struct {
    u32 file_off;
    u32 size;
} NroSegment;

/// Offset 0x0 in the NRO.
typedef struct {
    u32 unused;
    u32 mod_offset;
    u8  padding[8];
} NroStart;

/// This follows NroStart, the actual nro-header.
typedef struct {
    u32 magic;
    u32 unk1;
    u32 size;
    u32 unk2;
    NroSegment segments[3];
    u32 bss_size;
    u32 unk3;
    u8  build_id[0x20];
    u8  padding[0x20];
} NroHeader;

/// Custom asset section.
typedef struct {
    u64 offset;
    u64 size;
} NroAssetSection;

/// Custom asset header.
typedef struct {
    u32 magic;
    u32 version;
    NroAssetSection icon;
    NroAssetSection nacp;
    NroAssetSection romfs;
} NroAssetHeader;

/// The platform that a program's libounce was built for.
typedef enum {
    NroPlatform_Switch1 = 1, ///< libounce built as libnx, for Switch 1.
    NroPlatform_Switch2 = 2, ///< libounce for Switch 2.
} NroPlatform;

/**
 * @brief The record that libounce's startup code adds to every program it links.
 * @note It is a MOD0 extension block, after libnx's LNY blocks. A program
 * linked with libnx has none, so its absence identifies a libnx program.
 */
typedef struct {
    u32 magic;    ///< \ref NROOUNCE_MAGIC.
    u32 platform; ///< \ref NroPlatform.
    u32 version;  ///< Version of this record: 1.
} NroOunceInfo;

/**
 * @brief Finds libounce's record in the start of a program image.
 * @param[in] image The image from its first byte: an NRO file, or a loaded NRO or NSO.
 * @param[in] size Size of the bytes given. The first 0x1000 bytes are enough for libounce's startup code.
 * @param[out] out The record.
 * @return true if the record was found. false for a program that is not linked with
 * libounce (such as a libnx program), or when the bytes given end before the record.
 */
bool nroGetOunceInfo(const void* image, size_t size, NroOunceInfo* out);

