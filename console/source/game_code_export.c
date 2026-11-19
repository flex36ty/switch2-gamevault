#include <switch.h>
#include "../../support-source/investigation/kaihen-source/ouncebrew-kaihen-5ac7725/libkaihen/include/kaihen.h"
// The running HEN must implement GCOD. No direct loader session is opened.
Result gameOpenInstalledCode(FsFileSystem* out,u64 program,u8 storage,u8 attributes){
    Service ro={0},hen={0};Result rc=kaihenGetRoSession(&ro);
    if(R_FAILED(rc))return rc;
    rc=kaihenOpenHomebrewSession(&ro,&hen);if(R_FAILED(rc))return rc;
    struct {u64 program;u8 storage;u8 attributes;u8 reserved[6];} in={program,storage,attributes,{0}};
    Handle handle=INVALID_HANDLE;
    rc=serviceDispatchIn(&hen,0x47434f44,in,.out_handle_attrs={SfOutHandleAttr_HipcCopy},.out_handles=&handle);
    serviceClose(&hen);
    if(R_SUCCEEDED(rc))serviceCreate(&out->s,handle);
    return rc;
}
