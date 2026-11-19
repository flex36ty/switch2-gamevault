#include <switch.h>
#include <string.h>
#include <stdio.h>
// OunceCake mounts.cjs: base program RomFS uses command 206,
// {u8 attributes, padding, u32 type=1}, plus a fixed path buffer.
// Called only during startup, before any MTP worker uses FS services.
Result gameOpenRomfsPath(FsStorage* out, const char* path, u8 attributes, u32 fsType) {
    char fixed[FS_MAX_PATH] = {0};
    if (strlen(path) >= sizeof(fixed)) return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    strcpy(fixed, path);
    struct { u8 attr; u8 pad[3]; u32 type; } in = {attributes, {0}, fsType};
    Service root = *fsGetServiceSession();
    serviceAssumeDomain(&root);
    Result rc=serviceDispatchIn(&root, 206, in,
        .buffer_attrs = { SfBufferAttr_HipcPointer | SfBufferAttr_In },
        .buffers = {{fixed, sizeof(fixed)}},
        .out_num_objects = 1, .out_objects = &out->s);
    if((rc&0x3fffff)==0x328e02) {
        FILE* log=fopen("sdmc:/kaihen/reload-helper/mount.log","a");
        if(log){fprintf(log,"v26 BEGIN legacy 206 returned 00328e02; trying batch-storage 213\n");fclose(log);}
        FsStorageForBatchRead batch={0};
        rc=fsOpenDataStorageByPathForBatchRead(&batch,fixed,(FsContentAttributes)attributes,(FsFileSystemType)fsType);
        log=fopen("sdmc:/kaihen/reload-helper/mount.log","a");
        if(log){fprintf(log,"v26 batch-storage 213 result=%08x canonical=%08x\n",rc,rc&0x3fffff);fclose(log);}
        // Both SDK interfaces use identical Service ownership, Read(0),
        // GetSize(4), and close semantics. Transfer ownership, not a second handle.
        if(R_SUCCEEDED(rc)){out->s=batch.s;memset(&batch,0,sizeof(batch));}
    }
    return rc;
}
