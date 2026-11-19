#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
inline bool providerPolicyAllowsIpc(const unsigned char*data,size_t size){
 if(!data||size<0x80||memcmp(data,"META",4))return false;
 auto word=[&](size_t off){uint32_t v;memcpy(&v,data+off,4);return v;};
 uint32_t start=word(0x70),length=word(0x74);
 if(start>size||length>size-start||length<0x38||memcmp(data+start,"ACI0",4))return false;
 uint32_t offset=word(start+0x30),bytes=word(start+0x34);
 if(offset<0x38||offset>length||bytes>length-offset||bytes%4)return false;
 unsigned found=0;
 for(size_t at=start+offset;at<size_t(start)+offset+bytes;at+=4){
  uint32_t cap=word(at);if((cap&31)!=15)continue;
  unsigned group=cap>>29,mask=(cap>>5)&0xffffff;
  for(unsigned j=0;j<24;j++)if(mask&(1U<<j)){
   unsigned svc=group*24+j;
   if(svc==0x40)found|=1;
   if(svc==0x43)found|=2;
   if(svc==0x45)found|=4;
  }
 }return found==7;
}
