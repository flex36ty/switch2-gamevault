#include <switch.h>
#include <kaihen/plugin.h>
#include <cstdio>
#include <cstring>
#include "plan.hpp"
#include "protocol.h"
extern "C" {
u32 __nx_applet_type=AppletType_None;
size_t __nx_heap_size=0x400000;
void __appInit(void){}
void __appExit(void){}
Result gamePrepareInstalledLicense(NcmContentStorage*,const NcmContentInfo*,const char*,u64);
void gameCloseLicenseContexts(void);
Result gameOpenRomfsPath(FsStorage*,const char*,u8,u32);
}
static Mutex lock;
static HelperInfo info{};
static unsigned pending=0;
static bool cancel=false;
static plan::Store plans;
static NcmContentStorage storage{};
static bool ncm=false,cs=false;
static Result fsResult=0;
static constexpr Result Bad=MAKERESULT(Module_Libnx,LibnxError_BadInput);
static void log(const char* message,Result rc=0){
 FILE* f=fopen("sdmc:/kaihen/reload-helper/run.log","a");
 if(f){fprintf(f,"tick=%llu generation=%u step=%u %s rc=%08x\n",(unsigned long long)armGetSystemTick(),info.generation,info.step,message,rc);fclose(f);}
}
static void closeResources(){gameCloseLicenseContexts();if(cs){ncmContentStorageClose(&storage);cs=false;}if(ncm){ncmExit();ncm=false;}}
static Result programPath(NcmContentInfo& ci,char* path){
 if(!cs)return Bad;
 const unsigned char id[]={0xc5,0xc8,0x23,0x6a,0xd2,0x97,0xd3,0xbd,0x5b,0xe5,0x80,0x30,0xe8,0xb7,0xca,0x1d};
 memcpy(ci.content_id.c,id,16);ci.content_type=NcmContentType_Program;
 Result rc=ncmContentStorageGetPath(&storage,path,FS_MAX_PATH,&ci.content_id);
 if(R_SUCCEEDED(rc)&&strncmp(path,"@SdCardContent",14))return Bad;
 return rc;
}
static Result execute(const plan::Instruction& op){
 if(op.op==plan::Log){log(op.text);return 0;}
 if(op.op==plan::Close){closeResources();return 0;}
 if(R_FAILED(fsResult))return fsResult;
 switch(op.op){
 case plan::SdProbe:{FsFileSystem sd{};Result rc=fsOpenSdCardFileSystem(&sd);if(R_SUCCEEDED(rc)){s64 size=0;rc=fsFsGetFreeSpace(&sd,"/",&size);fsFsClose(&sd);}return rc;}
 case plan::NcmOpen:{if(cs)return 0;Result rc=ncm?0:ncmInitialize();if(R_FAILED(rc))return rc;ncm=true;rc=ncmOpenContentStorage(&storage,NcmStorageId_SdCard);cs=R_SUCCEEDED(rc);return rc;}
 case plan::NcmCount:{if(!cs)return Bad;s32 count=0;Result rc=ncmContentStorageGetContentCount(&storage,&count);char line[80];snprintf(line,sizeof(line),"SD_CONTENT_COUNT %d",count);log(line,rc);return rc;}
 case plan::CelesteLicense:case plan::CelesteRomfs:{NcmContentInfo ci{};char path[FS_MAX_PATH]{};Result rc=programPath(ci,path);if(R_FAILED(rc))return rc;
  if(op.op==plan::CelesteLicense)return gamePrepareInstalledLicense(&storage,&ci,path,0x01002b30028f6000ULL);
  FsStorage rom{};rc=gameOpenRomfsPath(&rom,path,ci.attr&15,1);if(R_SUCCEEDED(rc)){s64 size=0;rc=fsStorageGetSize(&rom,&size);unsigned char header[16];if(R_SUCCEEDED(rc))rc=size>=16?fsStorageRead(&rom,0,header,sizeof(header)):Bad;fsStorageClose(&rom);}return rc;}
 default:return Bad;
 }
}
static Result command(u32 cmd,const void*,size_t size,void* out,size_t* outSize,void*){
 if(size)return Bad;
 mutexLock(&lock);
 Result rc=0;
 if(cmd==HelperStatus){memcpy(out,&info,sizeof(info));*outSize=sizeof(info);}
 else if(cmd==HelperCancel){cancel=true;}
 else if(cmd<HelperReload||cmd>HelperRollback||pending||info.state==2||info.state==1)rc=Bad;
 else if(cmd==HelperRun&&!info.loaded)rc=Bad;
 else{pending=cmd;info.control_result=0;info.state=1;}
 mutexUnlock(&lock);return rc;
}
int main(){
 Handle ready=INVALID_HANDLE,control=INVALID_HANDLE;
 Result rc=pluginRuntimeInitializeWithService(&ready,&control);if(R_FAILED(rc))return rc;
 info.version=1;svcGetProcessId(&info.pid,CUR_PROCESS_HANDLE);
 PluginService server{};rc=pluginServiceStart(&server,control,command,nullptr);if(R_FAILED(rc)){pluginRuntimeExit();return rc;}
 svcSignalEvent(ready);
 fsResult=fsInitialize();if(R_SUCCEEDED(fsResult))fsResult=fsdevMountSdmc();
 log("HOST_READY",fsResult);
 bool running=false;u32 step=0;u64 deadline=0;
 for(;;){
  mutexLock(&lock);unsigned action=pending;pending=0;bool stop=cancel;cancel=false;info.heartbeat++;mutexUnlock(&lock);
  if(stop){closeResources();running=false;deadline=0;action=0;log("CANCELLED");mutexLock(&lock);info.state=0;mutexUnlock(&lock);}
  if(action==HelperReload){char bytes[4097];size_t length=0;FILE* f=fopen("sdmc:/kaihen/reload-helper/active.plan","rb");bool valid=false;if(f){length=fread(bytes,1,sizeof(bytes),f);bool readok=!ferror(f);fclose(f);valid=readok&&plans.reload(bytes,length,false);}rc=valid?0:Bad;log("RELOAD",rc);mutexLock(&lock);info.control_result=rc;info.state=0;info.generation=plans.generation;info.loaded=plans.loaded;info.count=plans.active.count;mutexUnlock(&lock);}
  else if(action==HelperRollback){rc=plans.rollback(false)?0:Bad;log("ROLLBACK",rc);mutexLock(&lock);info.control_result=rc;info.state=0;info.generation=plans.generation;info.count=plans.active.count;mutexUnlock(&lock);}
  else if(action==HelperRun){closeResources();running=true;step=0;deadline=0;mutexLock(&lock);info.state=2;info.last_result=0;info.step=0;mutexUnlock(&lock);log("RUN_BEGIN");}
  if(running){
   if(step==plans.active.count){closeResources();running=false;log("RUN_COMPLETE");mutexLock(&lock);info.state=0;mutexUnlock(&lock);}
   else{const auto& op=plans.active.instructions[step];bool complete=true;rc=0;
    if(op.op==plan::Delay){if(!deadline)deadline=armGetSystemTick()+armNsToTicks((u64)op.value*1000000);if(armGetSystemTick()<deadline)complete=false;else deadline=0;}
    else{log("STEP_BEGIN");rc=execute(op);log("STEP_END",rc);}
    if(complete){step++;if(R_FAILED(rc)){closeResources();running=false;log("RUN_FAILED",rc);}mutexLock(&lock);info.step=step;info.last_result=rc;info.state=running?2:0;mutexUnlock(&lock);}
   }
  }
  svcSleepThread(20000000);
 }
}
