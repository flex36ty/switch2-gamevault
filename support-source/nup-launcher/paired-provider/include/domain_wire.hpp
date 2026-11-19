#pragma once
#include <cstdint>
#include <cstring>
#include <cstddef>
namespace domainwire {
struct In {uint8_t type,objects;uint16_t size;uint32_t id,padding,token;};
struct Cmif {uint32_t magic,version,command,token;};
static_assert(sizeof(In)==16&&sizeof(Cmif)==16);
struct Request {uint32_t id=0,command=0;bool close=false;const unsigned char*data=nullptr;size_t size=0;};
// Decode only the bounded aligned domain payload, never the whole TLS buffer.
inline bool decode(const void*raw,size_t length,Request&out){
 out={};if(!raw||length<sizeof(In))return false;
 In in{};memcpy(&in,raw,sizeof(in));
 if(!in.id||in.padding||in.objects||in.token)return false;
 out.id=in.id;
 if(in.type==2){if(in.size)return false;out.close=true;return true;}
 if(in.type!=1||in.size<sizeof(Cmif)||in.size>length-sizeof(In))return false;
 Cmif h{};memcpy(&h,(const unsigned char*)raw+sizeof(In),sizeof(h));
 if(h.magic!=0x49434653||h.version>1||h.token)return false;
 out.command=h.command;out.data=(const unsigned char*)raw+sizeof(In)+sizeof(Cmif);out.size=in.size-sizeof(Cmif);return true;
}
}
