#include <switch.h>
#include <string.h>
#include <stdio.h>
#include "MatchSelection.h"
extern Result gameOpenInstalledCode(FsFileSystem*,u64,u8,u8);
typedef struct {LrLocationResolver normal;LrRegisteredLocationResolver registered;u64 title;u8 storage,attr;FsFileSystem* out;} Context;
static Service* session(Context* c,int slot){return slot==2?&c->registered.s:&c->normal.s;}
static Result readLocation(void* ptr,int slot,MatchLocation* out){
 Context* c=ptr;memset(out,0,sizeof(*out));u32 cmd=slot==1?16:0;
 Result rc=serviceDispatchInOut(session(c,slot),cmd,c->title,out->attributes,
  .buffer_attrs={SfBufferAttr_Out|SfBufferAttr_HipcPointer|SfBufferAttr_FixedSize},.buffers={{out->path,sizeof(out->path)}});
 u32 code=rc&0x3fffff;
 if(code==0x408 || code==0x1408)return 0;
 if(R_SUCCEEDED(rc)){if(!memchr(out->path,0,sizeof(out->path)))return MAKERESULT(Module_Libnx,LibnxError_BadInput);out->present=1;}
 return rc;
}
static Result writeLocation(void* ptr,int slot,const MatchLocation* value){
 Context* c=ptr;struct {u64 program;u64 owner;u8 attributes[16];} in={c->title,c->title,{0}};memcpy(in.attributes,value->attributes,16);
 return serviceDispatchIn(session(c,slot),slot==2?1:slot==1?18:10,in,
  .buffer_attrs={SfBufferAttr_In|SfBufferAttr_HipcPointer},.buffers={{value->path,sizeof(value->path)}});
}
static Result eraseLocation(void* ptr,int slot){Context* c=ptr;Result rc=serviceDispatchIn(session(c,slot),slot==2?2:slot==1?19:12,c->title);u32 code=rc&0x3fffff;return code==0x408||code==0x1408?0:rc;}
static Result openCode(void* ptr){Context* c=ptr;return gameOpenInstalledCode(c->out,c->title,c->storage,c->attr);}
static void closeCode(void* ptr){fsFsClose(((Context*)ptr)->out);}
Result gameOpenMatchedBaseCode(FsFileSystem* out,u64 title,u8 storage,u8 attr,const char* path,Result* restore){
 *restore=0;if(strlen(path)>=0x300)return MAKERESULT(Module_Libnx,LibnxError_BadInput);
 Result rc=lrInitialize();if(R_FAILED(rc))return rc;
 Context c={.title=title,.storage=storage,.attr=attr,.out=out};
 rc=lrOpenLocationResolver((NcmStorageId)storage,&c.normal);
 if(R_SUCCEEDED(rc))rc=lrOpenRegisteredLocationResolver(&c.registered);
 if(R_SUCCEEDED(rc)){
  MatchLocation selected={.present=1};selected.attributes[0]=attr;strcpy(selected.path,path);
  MatchOperations op={&c,readLocation,writeLocation,eraseLocation,openCode,closeCode};
  rc=match_base_transaction(&op,&selected,MAKERESULT(Module_Libnx,LibnxError_BadInput),restore);
 }
 serviceClose(&c.registered.s);serviceClose(&c.normal.s);lrExit();
 FILE* log=fopen("sdmc:/mtp-code-export.log","a");if(log){fprintf(log,"v29 exact-base selection title=%016llx path=%s result=%08x restore=%08x\n",(unsigned long long)title,path,rc,*restore);fclose(log);}
 return rc;
}
