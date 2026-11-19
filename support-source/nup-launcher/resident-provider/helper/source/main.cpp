#include <switch.h>
#include <kaihen/plugin.h>
#include "cmif_server.hpp"
#include "fixture.hpp"
#include "read_contract.hpp"
#include "protocol.hpp"
#include "../../../console/source/sd_storage.hpp"
#include <atomic>
#include <memory>
extern "C" {
u32 __nx_applet_type=AppletType_None;
size_t __nx_heap_size=0x800000;
void __appInit(void){}
void __appExit(void){}
}
static constexpr Result Bad=MAKERESULT(Module_Libnx,LibnxError_BadInput);
static std::unique_ptr<nup::Storage> storage;
static std::atomic<unsigned> ready{0},reads{0},fsResult{0};
static std::atomic<uint64_t> heartbeat{0};
static uint64_t pid=0,boot=0;
static bool verify(const void*p,size_t n,const unsigned char*want){u8 h[32];sha256CalculateHash(h,p,n);return !memcmp(h,want,32);}
Result storageProviderDispatch(Handle session){
  void*message=armGetTls();auto req=ounce::parseRequest(message);
  // Read all request fields before nested FS calls overwrite this thread's TLS.
  const auto meta=req.hipc.meta;
  if(ounce::isQueryPointerBufferSize(req)){ounce::writePointerBufferSizeReply(message);return ounce::sendReply(session);}
  Result result=Bad;uint64_t output=0;size_t outputSize=0;
  bool plain=!meta.num_send_statics&&!meta.num_recv_statics&&!meta.num_send_buffers&&!meta.num_exch_buffers&&!meta.num_copy_handles&&!meta.num_move_handles;
  bool request=req.valid&&(meta.type==CmifCommandType_Request||meta.type==CmifCommandType_RequestWithContext);
  int64_t offset=0;uint64_t count=0;void*buffer=nullptr;uint64_t capacity=0;
  const auto command=req.command_id;
  if(request&&plain&&!meta.num_recv_buffers&&command==100){ProviderInfo info{pid,boot,heartbeat.load(),reads.load(),1,ready.load(),fsResult.load(),0};ounce::writeReply(message,0,&info,sizeof(info));return ounce::sendReply(session);}
  if(request&&plain&&command==0&&req.data_size>=16){
   memcpy(&offset,req.data,8);memcpy(&count,req.data+8,8);
   if(meta.num_recv_buffers==1){buffer=hipcGetBufferAddress(req.hipc.data.recv_buffers);capacity=hipcGetBufferSize(req.hipc.data.recv_buffers);}
  }
  // Transferred handles are never adopted by this private provider.
  ounce::closeRequestHandles(req);
  try{
   if(ready.load()&&request&&plain&&command==4&&!meta.num_recv_buffers){output=storage->size();outputSize=8;result=0;}
   else if(ready.load()&&request&&plain&&command==0&&req.data_size>=16&&meta.num_recv_buffers<=1&&(!count||buffer)&&provider::validRead(offset,count,capacity,storage->size())){
    storage->read(uint64_t(offset),buffer,size_t(count));reads++;result=0;
   }
  }catch(...){result=Bad;}
  // All writes, domain controls, and unsupported commands fail explicitly.
  ounce::writeReply(message,result,&output,outputSize);return ounce::sendReply(session);
 }
static Result unused(u32,const void*,size_t,void*,size_t*,void*){return Bad;}
int main(){
 Handle event=INVALID_HANDLE,control=INVALID_HANDLE;
 Result rc=pluginRuntimeInitializeWithService(&event,&control);if(R_FAILED(rc))return rc;
 svcGetProcessId(&pid,CUR_PROCESS_HANDLE);boot=armGetSystemTick();
 PluginService server{};rc=pluginServiceStart(&server,control,unused,nullptr);
 if(R_FAILED(rc)){pluginRuntimeExit();return rc;}
 svcSignalEvent(event);
 rc=fsInitialize();if(R_SUCCEEDED(rc))rc=fsdevMountSdmc();
 if(R_SUCCEEDED(rc)){
  try{storage=std::make_unique<nup::Storage>("sdmc:/kaihen/provider-test-v1",".launch/test.vfs",verify);if(storage->size()!=VirtualSize)rc=Bad;}
  catch(...){rc=Bad;}
 }
 fsResult=rc;
 if(R_SUCCEEDED(rc))ready=1;
 FILE*f=fopen("sdmc:/kaihen/provider-helper.log","a");
 if(f){fprintf(f,"PROVIDER_START pid=%llu boot=%llu fs_result=%08x ready=%u\n",(unsigned long long)pid,(unsigned long long)boot,rc,ready.load());fclose(f);}
 for(;;){heartbeat++;svcSleepThread(100000000);}
}
