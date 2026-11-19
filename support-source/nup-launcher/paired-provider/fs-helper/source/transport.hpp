#pragma once
#include <atomic>
#include "fs_server_core.hpp"
#include "cmif_server.hpp"
extern Result fsChildCreate(Handle*,Handle*);
extern void fsChildDiscard(Handle);
static unsigned char pointerArea[0x400];
static providerfs::Server filesystemCore;
static std::map<Handle,uint64_t> objects;
static paired::Info mountInfo;
struct LocalMount:providerfs::Mount{
 nup::Storage* source;knup_segment segment{};
 LocalMount(nup::Storage&s,const paired::Info&i):source(&s){segment={0,s.size(),0,"/stream"};storage={{this,open,read,size,release},&segment,1,s.size()};if(initialize(i.title,i.identity))throw std::runtime_error("index");}
 static uint32_t open(void*p,knup_out*out,const char*,uint32_t){*out->value=p;return 0;}
 static uint32_t read(void*p,uint64_t*n,int64_t at,const knup_buffer*b,int64_t count,uint32_t){try{static_cast<LocalMount*>(p)->source->read(at,b->data,count);*n=count;return 0;}catch(...){*n=0;return 0xd401;}}
 static uint32_t size(void*p,uint64_t*out){*out=static_cast<LocalMount*>(p)->source->size();return 0;}
 static void release(void*){}
};
static std::shared_ptr<LocalMount> mount;
static std::atomic<bool> mountReady{false};
void initializeFilesystem(paired::Pair&pair){mountInfo=pair.describe();paired::Bind b{1,0,{}};memcpy(b.identity,mountInfo.identity,32);if(!pair.bind(UINT64_MAX,b))throw std::runtime_error("bind");mount=std::make_shared<LocalMount>(pair.route(UINT64_MAX),mountInfo);mountReady=true;}
void prepareFilesystemReceive(){void*tls=armGetTls();memset(tls,0,0x100);auto r=hipcMakeRequestInline(tls,.num_recv_statics=1);r.recv_list[0]=hipcMakeRecvStatic(pointerArea,sizeof(pointerArea));}
void filesystemRelease(Handle h){auto i=objects.find(h);if(i!=objects.end()){filesystemCore.close(i->second);objects.erase(i);}}
Result filesystemDispatch(Handle session,const ounce::CmifRequest&r,bool&handled){
 auto m=r.hipc.meta;void*tls=armGetTls();handled=true;
 if(ounce::isQueryPointerBufferSize(r)){u16 size=sizeof(pointerArea);ounce::writeReply(tls,0,&size,2);return ounce::sendReply(session);}
 bool connected=objects.count(session)!=0;
 if(!connected&&r.command_id!=120){handled=false;return 0;}
 Result rc=0xd401;uint64_t output=0;size_t outSize=0;Handle server=0,client=0;
 bool base=r.valid&&(m.type==CmifCommandType_Request||m.type==CmifCommandType_RequestWithContext)&&!m.num_copy_handles&&!m.num_move_handles&&!m.num_send_buffers&&!m.num_exch_buffers;
 try{
 if(base&&mountReady.load()&&!connected&&r.command_id==120&&!m.num_send_statics&&!m.num_recv_buffers&&r.data_size>=sizeof(paired::Info)){
  paired::Info expected{};memcpy(&expected,r.data,sizeof(expected));
  if(!memcmp(&expected,&mountInfo,sizeof(expected))){uint64_t id=0;rc=filesystemCore.connect(mount,expected.title,expected.identity,&id);if(!rc){try{objects.emplace(session,id);}catch(...){filesystemCore.close(id);throw;}}}
 }else if(base&&connected){
  auto id=objects.at(session);
  if((r.command_id==7||r.command_id==8)&&m.num_send_statics==1&&!m.num_recv_buffers){
   auto ptr=(uintptr_t)hipcGetStaticAddress(r.hipc.data.send_statics);auto len=hipcGetStaticSize(r.hipc.data.send_statics);auto start=(uintptr_t)pointerArea;
   if(ptr>=start&&ptr-start<=sizeof(pointerArea)&&len<=sizeof(pointerArea)-(ptr-start)&&len&&memchr((void*)ptr,0,len)){
    char path[0x400];memcpy(path,(void*)ptr,len);
    if(r.command_id==7){u32 type=0;rc=filesystemCore.entryType(id,path,&type);output=type;outSize=4;}
    else if(r.data_size>=4){u32 mode;memcpy(&mode,r.data,4);uint64_t child=0;rc=filesystemCore.open(id,path,mode,&child);
     if(!rc){rc=fsChildCreate(&server,&client);if(rc)filesystemCore.close(child);else{try{objects.emplace(server,child);}catch(...){filesystemCore.close(child);fsChildDiscard(server);server=0;svcCloseHandle(client);client=0;throw;}}}
    }
   }
  }else if(r.command_id==4&&!m.num_send_statics&&!m.num_recv_buffers){rc=filesystemCore.size(id,&output);outSize=8;}
  else if(r.command_id==0&&!m.num_send_statics&&m.num_recv_buffers==1&&r.data_size>=24){
   struct Input{u32 option,padding;s64 offset;u64 count;} in{};memcpy(&in,r.data,sizeof(in));
   auto buffer=hipcGetBufferAddress(r.hipc.data.recv_buffers);auto capacity=hipcGetBufferSize(r.hipc.data.recv_buffers);
   rc=filesystemCore.read(id,in.option,in.padding,in.offset,in.count,buffer,capacity,&output);outSize=8;
  }
 }
 }catch(...){rc=0xd401;}
 ounce::closeRequestHandles(r);
 if(!rc&&client){
  memset(tls,0,0x100);auto reply=hipcMakeRequestInline(tls,.num_data_words=8,.num_move_handles=1);reply.move_handles[0]=client;
  auto*h=(CmifOutHeader*)cmifGetAlignedDataStart(reply.data_words,tls);h->magic=CMIF_OUT_HEADER_MAGIC;h->result=0;
 }else ounce::writeReply(tls,rc,&output,outSize);
 Result sent=ounce::sendReply(session);
 if(R_FAILED(sent)&&client){svcCloseHandle(client);fsChildDiscard(server);}
 return sent;
}
