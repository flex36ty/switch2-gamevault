#define NX_SERVICE_ASSUME_NON_DOMAIN
#include "service_guard.h"
#include <string.h>
#include "kernel/shmem.h"
#include "runtime/hosversion.h"
#include "services/pl.h"

#define SHAREDMEMFONT_SIZE 0x1100000

static PlServiceType g_plServiceType;
static Service g_plSrv;
static SharedMemory g_plSharedmem;

#if !defined(__SWITCH2__)
static Result _plGetSharedMemoryNativeHandle(Handle* handle_out);
#endif

NX_GENERATE_SERVICE_GUARD_PARAMS(pl, (PlServiceType service_type), (service_type));

#if defined(__SWITCH2__)
// pl:u has new commands. It rejects legacy commands 0-6 with
// 0x003e80aa. Command 2000 copies the font shared memory handle and returns
// its size (0x02a00000 on current firmware, not SHAREDMEMFONT_SIZE). Only
// pl:u is supported.
static u32 g_plSharedmemSize;

Result _plInitialize(PlServiceType service_type) {
    g_plServiceType = service_type;
    if (g_plServiceType != PlServiceType_User)
        return MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer);

    Result rc = smGetService(&g_plSrv, "pl:u");
    if (R_FAILED(rc))
        return rc;

    Handle sharedmem_handle = INVALID_HANDLE;
    rc = serviceDispatchOut(&g_plSrv, 2000, g_plSharedmemSize,
        .out_handle_attrs = { SfOutHandleAttr_HipcCopy },
        .out_handles = &sharedmem_handle,
    );
    if (R_SUCCEEDED(rc)) {
        shmemLoadRemote(&g_plSharedmem, sharedmem_handle, g_plSharedmemSize, Perm_R);
        rc = shmemMap(&g_plSharedmem);
    }
    return rc;
}

void _plCleanup(void) {
    serviceClose(&g_plSrv);
    shmemClose(&g_plSharedmem);
}
#else
Result _plInitialize(PlServiceType service_type) {
    Result rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
    Handle sharedmem_handle=0;

    g_plServiceType = service_type;
    switch (g_plServiceType) {
        case PlServiceType_User:
            rc = smGetService(&g_plSrv, "pl:u");
            break;
        case PlServiceType_System:
            rc = smGetService(&g_plSrv, "pl:s");
            break;
    }

    if (R_SUCCEEDED(rc) && !(hosversionAtLeast(16,0,0) && g_plServiceType == PlServiceType_System)) {
        rc = _plGetSharedMemoryNativeHandle(&sharedmem_handle);

        if (R_SUCCEEDED(rc)) {
            shmemLoadRemote(&g_plSharedmem, sharedmem_handle, SHAREDMEMFONT_SIZE, Perm_R);

            rc = shmemMap(&g_plSharedmem);
        }
    }

    return rc;
}

void _plCleanup(void) {
    serviceClose(&g_plSrv);
    if (!(hosversionAtLeast(16,0,0) && g_plServiceType == PlServiceType_System))
        shmemClose(&g_plSharedmem);
}
#endif

Service* plGetServiceSession(void) {
    return &g_plSrv;
}

void* plGetSharedmemAddr(void) {
    return shmemGetAddr(&g_plSharedmem);
}

#if defined(__SWITCH2__)
// Command 2020. In: the language and variant, and the requested
// font types as an A buffer of bytes. Out: 12-byte records in a B buffer, in
// the service's priority order for the language. It returns whether fonts
// are available, and the record count. GetSharedFont requests types 0-5
// without the variant. Loading needs no request, so there is no load polling.
typedef struct {
    u8 type;
    u8 variant;
    u8 reserved[2];
    u32 offset;     ///< Offset in the font shared memory.
    u32 size;
} PlFontRecord;

static Result _plGetFontsInPriority(u64 language_code, const u8* types, size_t type_count,
                                    PlFontRecord* records, s32* total) {
    const struct {
        u64 language_code;
        u8 variant;
    } NX_PACKED in = { language_code, 0 };
    struct {
        u8 fonts_available;
        u8 reserved[3];
        s32 count;
    } out = {0};

    Result rc = serviceDispatchInOut(&g_plSrv, 2020, in, out,
        .buffer_attrs = {
            SfBufferAttr_HipcMapAlias | SfBufferAttr_In,
            SfBufferAttr_HipcMapAlias | SfBufferAttr_Out,
        },
        .buffers = {
            { types, type_count },
            { records, type_count * sizeof(PlFontRecord) },
        },
    );
    if (R_FAILED(rc))
        return rc;
    *total = out.fonts_available ? out.count : 0;
    if (*total < 0 || (size_t)*total > type_count)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    return 0;
}

static Result _plFontFromRecord(const PlFontRecord* record, PlFontData* font) {
    if (record->offset >= g_plSharedmemSize || record->size > g_plSharedmemSize - record->offset)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    u8* sharedmem_addr = (u8*)plGetSharedmemAddr();
    font->type = record->type;
    font->offset = record->offset;
    font->size = record->size;
    font->address = &sharedmem_addr[record->offset];
    return 0;
}

Result plGetSharedFontByType(PlFontData* font, PlSharedFontType SharedFontType) {
    memset(font, 0, sizeof(PlFontData));
    font->type = SharedFontType;

    // One type: the language only orders several faces, so any valid
    // language code ("en-US", as setMakeLanguageCode packs it) will do.
    u64 language_code = 0;
    memcpy(&language_code, "en-US", 5);
    const u8 type = (u8)SharedFontType;
    PlFontRecord record = {0};
    s32 total = 0;
    Result rc = _plGetFontsInPriority(language_code, &type, 1, &record, &total);
    if (R_FAILED(rc))
        return rc;
    if (total != 1 || record.type != type)
        return MAKERESULT(Module_Libnx, LibnxError_NotFound);
    return _plFontFromRecord(&record, font);
}

Result plGetSharedFont(u64 LanguageCode, PlFontData* fonts, s32 max_fonts, s32* total_fonts) {
    memset(fonts, 0, sizeof(PlFontData) * max_fonts);
    if (total_fonts) *total_fonts = 0;

    u8 types[PlSharedFontType_Total];
    for (u8 i = 0; i < PlSharedFontType_Total; i++)
        types[i] = i;
    PlFontRecord records[PlSharedFontType_Total] = {0};
    s32 total = 0;
    Result rc = _plGetFontsInPriority(LanguageCode, types, PlSharedFontType_Total, records, &total);
    if (R_FAILED(rc))
        return rc;

    s32 font_count = total < max_fonts ? total : max_fonts;
    for (s32 i = 0; i < font_count; i++) {
        rc = _plFontFromRecord(&records[i], &fonts[i]);
        if (R_FAILED(rc))
            return rc;
    }
    if (total_fonts) *total_fonts = font_count;
    return 0;
}
#else
static Result _plCmdGetHandle(Service* srv, Handle* handle_out, u32 cmd_id) {
    return serviceDispatch(srv, cmd_id,
        .out_handle_attrs = { SfOutHandleAttr_HipcCopy },
        .out_handles = handle_out,
    );
}

static Result _plCmdInU32NoOut(Service* srv, u32 inval, u32 cmd_id) {
    return serviceDispatchIn(srv, cmd_id, inval);
}

static Result _plCmdInU32OutU32(Service* srv, u32 inval, u32 *out, u32 cmd_id) {
    return serviceDispatchInOut(srv, cmd_id, inval, *out);
}

static Result _plRequestLoad(u32 SharedFontType) {
    if (hosversionAtLeast(16,0,0) && g_plServiceType == PlServiceType_System)
        return MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer);

    return _plCmdInU32NoOut(&g_plSrv, SharedFontType, 0);
}

static Result _plGetLoadState(u32 SharedFontType, u32* LoadState) {
    if (hosversionAtLeast(16,0,0) && g_plServiceType == PlServiceType_System)
        return MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer);

    return _plCmdInU32OutU32(&g_plSrv, SharedFontType, LoadState, 1);
}

static Result _plGetSize(u32 SharedFontType, u32* size) {
    if (hosversionAtLeast(16,0,0) && g_plServiceType == PlServiceType_System)
        return MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer);

    return _plCmdInU32OutU32(&g_plSrv, SharedFontType, size, 2);
}

static Result _plGetSharedMemoryAddressOffset(u32 SharedFontType, u32* offset) {
    if (hosversionAtLeast(16,0,0) && g_plServiceType == PlServiceType_System)
        return MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer);

    return _plCmdInU32OutU32(&g_plSrv, SharedFontType, offset, 3);
}

static Result _plGetSharedMemoryNativeHandle(Handle* handle_out) {
    if (hosversionAtLeast(16,0,0) && g_plServiceType == PlServiceType_System)
        return MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer);

    return _plCmdGetHandle(&g_plSrv, handle_out, 4);
}

static Result _plVerifyFontRange(u32 offset, u32 size) {
    if (offset >= SHAREDMEMFONT_SIZE || size > SHAREDMEMFONT_SIZE || ((u64)offset + (u64)size) > SHAREDMEMFONT_SIZE)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    return 0;
}

static Result _plRequestLoadWait(u32 SharedFontType) {
    Result rc=0;
    u32 LoadState=0;
    bool load_started = 0;

    while(1) {//Wait/request for the font to be loaded if it's not already loaded.
        if (load_started) svcSleepThread(1000000000ULL / 60);

        rc = _plGetLoadState(SharedFontType, &LoadState);
        if (R_FAILED(rc) || LoadState==0x1) break;

        if (load_started) continue;
        load_started = 1;
        rc = _plRequestLoad(SharedFontType);
        if (R_FAILED(rc)) break;
    }

    return rc;
}

Result plGetSharedFontByType(PlFontData* font, PlSharedFontType SharedFontType) {
    Result rc=0;
    u8* sharedmem_addr = (u8*)plGetSharedmemAddr();

    memset(font, 0, sizeof(PlFontData));

    font->type = SharedFontType;

    rc = _plRequestLoadWait(SharedFontType);
    if (R_FAILED(rc)) return rc;

    rc = _plGetSize(SharedFontType, &font->size);
    if (R_FAILED(rc)) return rc;

    rc = _plGetSharedMemoryAddressOffset(SharedFontType, &font->offset);
    if (R_FAILED(rc)) return rc;

    rc = _plVerifyFontRange(font->offset, font->size);
    if (R_FAILED(rc)) return rc;

    font->address = &sharedmem_addr[font->offset];

    return rc;
}

Result plGetSharedFont(u64 LanguageCode, PlFontData* fonts, s32 max_fonts, s32* total_fonts) {
    if (hosversionAtLeast(16,0,0) && g_plServiceType == PlServiceType_System)
        return MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer);

    Result rc=0;
    u32 types[PlSharedFontType_Total]={0};
    u32 offsets[PlSharedFontType_Total]={0};
    u32 sizes[PlSharedFontType_Total]={0};
    size_t size = PlSharedFontType_Total*sizeof(u32);
    s32 font_count=0, i;
    u8* sharedmem_addr = (u8*)plGetSharedmemAddr();

    memset(fonts, 0, sizeof(PlFontData) * max_fonts);

    if (total_fonts) *total_fonts = 0;

    for (i=0; i<PlSharedFontType_Total; i++) {
        rc = _plRequestLoadWait(i);
        if (R_FAILED(rc)) return rc;
    }

    struct {
        u8 fonts_loaded;
        s32 total_fonts;
    } out;

    rc = serviceDispatchInOut(&g_plSrv, 5, LanguageCode, out,
        .buffer_attrs = {
            SfBufferAttr_HipcMapAlias | SfBufferAttr_Out,
            SfBufferAttr_HipcMapAlias | SfBufferAttr_Out,
            SfBufferAttr_HipcMapAlias | SfBufferAttr_Out,
        },
        .buffers = {
            { types, size },
            { offsets, size },
            { sizes, size },
        },
    );
    if (R_SUCCEEDED(rc) && out.fonts_loaded) {
        font_count = out.total_fonts;
        if (font_count > PlSharedFontType_Total) font_count = PlSharedFontType_Total;
        if (font_count > max_fonts) font_count = max_fonts;
        if (font_count < 0) font_count = 0;
        if (total_fonts) *total_fonts = font_count;
        if (font_count==0) return rc;

        for (i=0; i<font_count; i++) {
            rc = _plVerifyFontRange(offsets[i], sizes[i]);
            if (R_FAILED(rc)) return rc;

            fonts[i].type = types[i];
            fonts[i].offset = offsets[i];
            fonts[i].size = sizes[i];

            fonts[i].address = &sharedmem_addr[offsets[i]];
        }
    }
    return rc;
}
#endif
