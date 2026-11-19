// The record that identifies a program built with libounce (NroOunceInfo).
#include <string.h>
#include "types.h"
#include "nro.h"

// The platform word of the record in switch_crt0.s. The startup code is the
// same for both builds of libounce, so the platform is a symbol that this
// file defines for the build it is compiled in.
#if defined(__SWITCH2__)
__asm__(".global __ounce_platform\n.set __ounce_platform, 2\n");
#else
__asm__(".global __ounce_platform\n.set __ounce_platform, 1\n");
#endif

// MOD0's header: its magic and six offsets. Extension blocks follow it, each
// a magic and two words: libnx's "LNY0".."LNY2", then libounce's "OUN0". Code
// follows the last block, so only known blocks are stepped over.
#define MOD0_HEADER_SIZE      0x1C
#define MOD0_EXTENSION_SIZE   0xC
#define MOD0_EXTENSIONS_MAX   8
#define MOD0_LIBNX_PREFIX     "LNY"

static u32 _nroReadWord(const u8* bytes, size_t offset) {
    u32 value;
    memcpy(&value, bytes + offset, sizeof(value));
    return value;
}

bool nroGetOunceInfo(const void* image, size_t size, NroOunceInfo* out) {
    const u8* bytes = (const u8*)image;
    if (size < sizeof(NroStart))
        return false;

    const size_t mod0 = _nroReadWord(bytes, offsetof(NroStart, mod_offset));
    if (mod0 > size || size - mod0 < MOD0_HEADER_SIZE || _nroReadWord(bytes, mod0) != NROMOD0_MAGIC)
        return false;

    size_t offset = mod0 + MOD0_HEADER_SIZE;
    for (int index = 0; index < MOD0_EXTENSIONS_MAX && size - offset >= MOD0_EXTENSION_SIZE; index++) {
        if (_nroReadWord(bytes, offset) == NROOUNCE_MAGIC) {
            memcpy(out, bytes + offset, sizeof(*out));
            return true;
        }
        if (memcmp(bytes + offset, MOD0_LIBNX_PREFIX, sizeof(MOD0_LIBNX_PREFIX) - 1) != 0)
            return false;
        offset += MOD0_EXTENSION_SIZE;
    }
    return false;
}
