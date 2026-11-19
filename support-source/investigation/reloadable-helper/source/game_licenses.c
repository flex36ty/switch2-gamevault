#include <switch.h>
#include <stdio.h>
#include <string.h>
// Adapted from OunceCake engine/program.cjs prepareInstalledLicense.
// Existing installed rights only; no ticket import or rights fabrication.
static Service licenseService;
static Service sharedContext;
static u64 selectedApplications[256];
static unsigned selectedCount;
static Result contextFailure;
static Result traceLicense(const char* stage, Result rc, u64 title) {
    FILE* f=fopen("sdmc:/kaihen/reload-helper/license.log","a");
    if(f){fprintf(f,"v24 license %s title=%016llx result=%08x canonical=%08x\n",stage,(unsigned long long)title,rc,rc&0x3fffff);fclose(f);}
    return rc;
}
static void beforeLicense(const char* stage,u64 title) {
    FILE* f=fopen("sdmc:/kaihen/reload-helper/license.log","a");
    if(f){fprintf(f,"v24 license BEGIN %s title=%016llx\n",stage,(unsigned long long)title);fclose(f);}
}
static Result contextResult(const char* stage,Result rc,u64 title) {
    traceLicense(stage,rc,title);
    if(R_FAILED(rc))contextFailure=rc;
    return rc;
}
static Result prepare(Service* es,NcmRightsId* rights) {
    u32 generation=rights->key_generation;
    return serviceDispatch(es,503,
        .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_In,SfBufferAttr_HipcMapAlias|SfBufferAttr_In},
        .buffers={{&rights->rights_id,16},{&generation,4}});
}
Result gamePrepareInstalledLicense(NcmContentStorage* cs,const NcmContentInfo* info,const char* path,u64 title) {
    if(R_FAILED(contextFailure))return traceLicense("recovery disabled after context failure",contextFailure,title);
    beforeLicense("verify content identity",title);
    u64 program=0;Result rc=fsGetProgramId(&program,path,(FsContentAttributes)(info->attr&15));
    traceLicense("verify content identity",rc,title);if(R_FAILED(rc))return rc;
    // This standalone route handles matching base programs or their data content.
    if(program!=title){return traceLicense("content identity mismatch",MAKERESULT(Module_Libnx,LibnxError_BadInput),title);}
    beforeLicense("get installed rights",title);
    NcmRightsId rights={0};rc=ncmContentStorageGetRightsIdFromContentId(cs,&rights,&info->content_id,(FsContentAttributes)(info->attr&15));
    traceLicense("get installed rights",rc,title);if(R_FAILED(rc))return rc;
    const u8* bytes=(const u8*)&rights.rights_id;bool any=false;for(unsigned i=0;i<16;i++)any|=bytes[i]!=0;
    if(!any)return traceLicense("no rights identity",MAKERESULT(Module_Libnx,LibnxError_NotFound),title);
    if(bytes[14]&0x10)return traceLicense("requires original-content context",MAKERESULT(Module_Libnx,LibnxError_BadInput),title);
    if(!serviceIsActive(&licenseService)){beforeLicense("open es",title);rc=smGetService(&licenseService,"es");traceLicense("open es",rc,title);if(R_FAILED(rc))return rc;}
    beforeLicense("prepare installed license",title);
    rc=prepare(&licenseService,&rights);traceLicense("prepare installed license",rc,title);
    if(R_SUCCEEDED(rc)) {
        if(serviceIsActive(&sharedContext)) {
            beforeLicense("activate existing prepared rights",title);
            rc=serviceDispatch(&sharedContext,102);
            return contextResult("activate existing prepared rights",rc,title);
        }
        return rc;
    }
    if((rc&0x3fffff)!=0x1491 && (rc&0x3fffff)!=0x691)return rc;
    Service* context=&sharedContext;
    if(!serviceIsActive(context)) {
        beforeLicense("create shared rights context",title);
        rc=serviceDispatch(&licenseService,2001,.out_num_objects=1,.out_objects=context);
        if(R_FAILED(contextResult("create shared rights context",rc,title)))return rc;
    } else traceLicense("reuse shared rights context",0,title);
    u64 application=0;for(unsigned i=0;i<8;i++)application=(application<<8)|bytes[i];
    bool selected=false;
    for(unsigned i=0;i<selectedCount;i++)if(selectedApplications[i]==application)selected=true;
    if(!selected) {
        if(selectedCount>=256)return traceLicense("application tracking limit",MAKERESULT(Module_Libnx,LibnxError_OutOfMemory),title);
        beforeLicense("select application",title);
        rc=serviceDispatch(context,2,.buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_In},.buffers={{&application,8}});
        if(R_FAILED(contextResult("select application",rc,title)))return rc;
        selectedApplications[selectedCount++]=application;
    }
    u8 allAccounts=1;u64 account=0;
    beforeLicense("request installed account rights",title);
    rc=serviceDispatchIn(context,17,allAccounts,
        .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_In,SfBufferAttr_HipcMapAlias|SfBufferAttr_In},
        .buffers={{&application,8},{&account,8}});
    if(R_FAILED(contextResult("request installed account rights",rc,title)))return rc;
    beforeLicense("evaluate rights",title);
    rc=serviceDispatch(context,3);if(R_FAILED(contextResult("evaluate rights",rc,title)))return rc;
    beforeLicense("prepare after evaluation",title);
    rc=prepare(&licenseService,&rights);traceLicense("prepare after evaluation",rc,title);if(R_FAILED(rc))return rc;
    beforeLicense("activate prepared rights",title);
    rc=serviceDispatch(context,102);return contextResult("activate prepared rights",rc,title);
}
void gameCloseLicenseContexts(void){
    serviceClose(&sharedContext);serviceClose(&licenseService);
    selectedCount=0;contextFailure=0;memset(selectedApplications,0,sizeof(selectedApplications));
}



